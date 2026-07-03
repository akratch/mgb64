/**
 * gfx_pc.c — F3DEX2/Rare GBI display list interpreter.
 *
 * Walks N64 display lists built by game code and translates GBI commands
 * into OpenGL calls. Handles standard F3DEX2 commands plus Rare's custom
 * G_TRI4 and G_SETTEX extensions.
 *
 * Architecture:
 * - Game code builds Gfx[] display lists using GBI macros (unchanged)
 * - rspGfxTaskStart() calls gfx_run_dl() instead of submitting to RSP
 * - This file walks the display list, maintaining RSP/RDP shadow state
 * - gfx_opengl.c provides the actual OpenGL rendering calls
 */

#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <SDL_opengl.h>
#include "gfx_ptr.h"
#include "gfx_pc.h"
#include "rom_io.h"
#include "byteswap.h"
#include "../game/image.h"

/* ===== N64-to-GL coordinate mapping ===== */
#define GFX_N64_W 320
#define GFX_N64_H 240
static int GFX_WINDOW_W = 960;
static int GFX_WINDOW_H = 540;

/* ===== Pointer and segment table storage ===== */
uintptr_t gfx_segment_table[16];
uint32_t  gfx_ptr_keys[GFX_PTR_TABLE_SIZE];
uintptr_t gfx_ptr_vals[GFX_PTR_TABLE_SIZE];

/* ===== RSP state ===== */

#define MAX_MATRIX_STACK 32
#define MAX_VERTICES     16  /* Rare's microcode uses 16, not 32 */
#define MAX_LIGHTS       8
#define DL_STACK_SIZE    16

/* Vertex after RSP transform */
typedef struct {
    float pos[4];      /* clip-space x, y, z, w */
    float obj_pos[3];  /* object-space x, y, z (for GL hardware transform) */
    float color[4];    /* r, g, b, a (0-1) */
    float texcoord[2]; /* s, t */
    float fog;         /* fog factor (0=no fog, 1=full fog) */
} RspVertex;

/* Combine mode classification — base modes in low byte, tint flags in high bits */
enum {
    CC_MODE_MODULATE,   /* tex × shade (default) */
    CC_MODE_DECAL,      /* tex only, ignore shade */
    CC_MODE_SHADE,      /* shade only, no tex */
    CC_MODE_PRIM_ALPHA, /* color=PRIMITIVE, alpha=TEXEL0×PRIMITIVE (text) */
};
#define CC_FLAG_PRIM  0x100  /* multiply output by prim_color */
#define CC_FLAG_ENV   0x200  /* multiply output by env_color */
#define CC_BASE_MODE(m)  ((m) & 0xFF)

static struct {
    /* Matrix state */
    float modelview_stack[MAX_MATRIX_STACK][4][4];
    int   modelview_sp;
    float projection[4][4];
    float combined[4][4]; /* projection * modelview */
    int   combined_dirty;

    /* Vertex buffer */
    RspVertex vertices[MAX_VERTICES];

    /* Geometry mode */
    u32 geometry_mode;

    /* Texture state */
    int texture_on;
    u16 texture_s_scale;
    u16 texture_t_scale;
    int texture_tile;

    /* RDP state */
    u32 combine_hi, combine_lo;
    u32 other_mode_hi, other_mode_lo;
    float prim_color[4];
    float env_color[4];
    float fog_color[4];
    float blend_color[4];
    float fill_color[4];
    s16 fog_mul, fog_offset;
    u16 persp_norm;  /* from gSPPerspNormalize — scales W for fog */

    /* Tiles */
    struct {
        u32 format, size, line, tmem_addr, palette;
        u32 cms, cmt, masks, maskt, shifts, shiftt;
        u32 uls, ult, lrs, lrt;
        uintptr_t source_addr; /* texture data addr, captured at G_LOADBLOCK/G_LOADTILE */
    } tiles[8];

    /* Texture image source */
    uintptr_t texture_image_addr;
    u32 texture_image_format;
    u32 texture_image_size;
    u32 texture_image_width;

    /* TLUT (palette) for CI textures */
    u8 tlut[256 * 2]; /* up to 256 RGBA16 palette entries */
    int tlut_count;

    /* Big-endian data mode (set when processing N64-format DLs) */
    int n64_data_mode;

    /* Current texture from 0xC0 texture reference (GoldenEye-specific) */
    GLuint current_tex_id;
    float  current_tex_w;
    float  current_tex_h;

    /* Viewport */
    float vp_x, vp_y, vp_w, vp_h;
    float vp_znear, vp_zfar;
    int vp_set;

    /* Raw perspective Z parameters for depth computation.
     * The combined view×perspective matrix destroys Z/W precision.
     * These are captured from the raw perspective matrix (guPerspective)
     * and used to compute proper depth from clip-space W.
     * depth = (persp_a * (-W) + persp_b) / W  where W ≈ -z_eye */
    float persp_a;  /* projection[2][2] from raw perspective */
    float persp_b;  /* projection[3][2] from raw perspective */
    int persp_set;

    /* Scissor */
    int scissor_x0, scissor_y0, scissor_x1, scissor_y1;

    /* Lights */
    struct { float col[3]; float dir[3]; } lights[MAX_LIGHTS];
    float ambient[3];
    int num_lights;

    /* LookAt vectors for environment mapping (G_TEXTURE_GEN) */
    float lookat_x[3];
    float lookat_y[3];

    /* Display list call stack */
    Gfx *dl_stack[DL_STACK_SIZE];
    int dl_sp;

    /* Frame stats */
    int tri_count;
    int dl_cmd_count;

    /* Diagnostic counters (temporary) */
    int diag_tex_tris;      /* triangles with texture bound */
    int diag_notex_tris;    /* triangles without texture */
    int diag_shade_tris;    /* CC_MODE_SHADE tris */
    int diag_fog_tris;      /* triangles with fog applied */
    float diag_avg_bright;  /* accumulated vertex brightness */
    int diag_vert_count;    /* vertices counted */
} rsp;

/* Background clear color — set by game code before gfx_run_dl, not affected by DL commands */
static float g_clearColor[3] = {0.0f, 0.0f, 0.0f};

void gfx_set_clear_color(int r, int g, int b) {
    g_clearColor[0] = (float)r / 255.0f;
    g_clearColor[1] = (float)g / 255.0f;
    g_clearColor[2] = (float)b / 255.0f;
}

/* ===== Texture cache ===== */

#define TEX_CACHE_SIZE 1024
static struct {
    uintptr_t addr;
    GLuint gl_tex;
    float tex_w, tex_h;  /* texture dimensions for UV calculation */
} tex_cache[TEX_CACHE_SIZE];
static int tex_cache_count = 0;
static GLuint tex_bound_id = 0;
static int tex_gl_enabled = 0;

#define GE007_NUM_TEXTURES 0xBB9U

static uintptr_t gfx_resolve_loaded_texture_pointer_addr(uint32_t token) {
    struct texpool *pool = ptr_texture_alloc_start;
    uintptr_t raw = (uintptr_t)token;
    uintptr_t match = 0;

    if (pool == NULL) {
        return 0;
    }

    if (pool->start != NULL && pool->leftpos != NULL) {
        uintptr_t start = (uintptr_t)pool->start;
        uintptr_t left = (uintptr_t)pool->leftpos;

        if (raw >= start && raw < left) {
            return raw;
        }
    }

    for (struct tex *cur = pool->rightpos; cur != NULL && cur < pool->end; cur++) {
        if (cur->data != NULL && (uint32_t)(uintptr_t)cur->data == token) {
            uintptr_t data = (uintptr_t)cur->data;

            if (match != 0 && match != data) {
                return 0;
            }

            match = data;
        }
    }

    return match;
}

static uintptr_t gfx_resolve_texture_image_addr(uintptr_t raw_addr) {
    if (raw_addr == 0) {
        return 0;
    }

    if ((raw_addr >> 32) == 0) {
        void *resolved = gfx_resolve_addr((uint32_t)raw_addr);
        if (resolved != NULL) {
            return (uintptr_t)resolved;
        }

        uintptr_t loaded_texture = gfx_resolve_loaded_texture_pointer_addr((uint32_t)raw_addr);
        if (loaded_texture != 0) {
            return loaded_texture;
        }

        if ((uint32_t)raw_addr < GE007_NUM_TEXTURES) {
            texLoadFromTextureNum((s32)raw_addr, ptr_texture_alloc_start);
            struct tex *tex = texFindInPool((s32)raw_addr, ptr_texture_alloc_start);
            if (tex != NULL) {
                return (uintptr_t)tex->data;
            }
        }
    }

    return raw_addr;
}

/* RDP blender mode tracking — avoid redundant GL state changes */
static int blend_mode_cur = -1;  /* -1 = unset, 0 = OPA, 1 = XLU, 2 = TEX_EDGE, 3 = DECAL_OPA, 4 = DECAL_XLU */
static int polygon_offset_on = 0;

/* Decode N64 RGBA16 (5-5-5-1) to RGBA8888 */
static void tex_decode_rgba16(const u8 *src, u8 *dst, int count, int be) {
    for (int i = 0; i < count; i++) {
        u16 px = be ? ((src[0] << 8) | src[1]) : (src[0] | (src[1] << 8));
        dst[0] = ((px >> 11) & 0x1F) * 255 / 31;
        dst[1] = ((px >> 6)  & 0x1F) * 255 / 31;
        dst[2] = ((px >> 1)  & 0x1F) * 255 / 31;
        dst[3] = (px & 1) ? 255 : 0;
        src += 2; dst += 4;
    }
}

/* Decode N64 IA16 (8I + 8A) to RGBA8888 */
static void tex_decode_ia16(const u8 *src, u8 *dst, int count, int be) {
    for (int i = 0; i < count; i++) {
        u8 intensity = be ? src[0] : src[1];
        u8 alpha     = be ? src[1] : src[0];
        dst[0] = dst[1] = dst[2] = intensity;
        dst[3] = alpha;
        src += 2; dst += 4;
    }
}

/* Decode N64 IA8 (4I + 4A) to RGBA8888 */
static void tex_decode_ia8(const u8 *src, u8 *dst, int count) {
    for (int i = 0; i < count; i++) {
        u8 intensity = (src[0] >> 4) & 0xF;
        u8 alpha = src[0] & 0xF;
        dst[0] = dst[1] = dst[2] = (intensity << 4) | intensity;
        dst[3] = (alpha << 4) | alpha;
        src++; dst += 4;
    }
}

/* Decode N64 I8 to RGBA8888 */
static void tex_decode_i8(const u8 *src, u8 *dst, int count) {
    for (int i = 0; i < count; i++) {
        dst[0] = dst[1] = dst[2] = src[0];
        dst[3] = 255;  /* I format: alpha = opaque (blender controls transparency) */
        src++; dst += 4;
    }
}

/* Decode N64 I4 to RGBA8888 (2 texels per byte) */
static void tex_decode_i4(const u8 *src, u8 *dst, int count) {
    for (int i = 0; i < count; i += 2) {
        u8 hi = (src[0] >> 4) & 0xF;
        u8 lo = src[0] & 0xF;
        dst[0] = dst[1] = dst[2] = (hi << 4) | hi;
        dst[3] = 255;  /* I format: alpha = opaque */
        if (i + 1 < count) {
            dst[4] = dst[5] = dst[6] = (lo << 4) | lo;
            dst[7] = 255;
        }
        src++; dst += 8;
    }
}

/* Decode N64 IA4 (3I + 1A) to RGBA8888 (2 texels per byte) */
static void tex_decode_ia4(const u8 *src, u8 *dst, int count) {
    for (int i = 0; i < count; i += 2) {
        u8 hi = (src[0] >> 4) & 0xF;
        u8 lo = src[0] & 0xF;
        u8 hi_i = (hi >> 1); /* 3 bits */
        u8 lo_i = (lo >> 1);
        dst[0] = dst[1] = dst[2] = (hi_i << 5) | (hi_i << 2) | (hi_i >> 1);
        dst[3] = (hi & 1) ? 255 : 0;
        if (i + 1 < count) {
            dst[4] = dst[5] = dst[6] = (lo_i << 5) | (lo_i << 2) | (lo_i >> 1);
            dst[7] = (lo & 1) ? 255 : 0;
        }
        src++; dst += 8;
    }
}

/* Decode N64 CI4 with TLUT to RGBA8888 (2 texels per byte) */
static void tex_decode_ci4(const u8 *src, u8 *dst, int count, const u8 *tlut, int tlut_count, int be) {
    for (int i = 0; i < count; i += 2) {
        u8 hi = (src[0] >> 4) & 0xF;
        u8 lo = src[0] & 0xF;
        /* Look up in TLUT as RGBA16 */
        for (int j = 0; j < 2 && (i + j) < count; j++) {
            u8 idx = (j == 0) ? hi : lo;
            if (idx < tlut_count) {
                const u8 *pe = tlut + idx * 2;
                u16 px = be ? ((pe[0] << 8) | pe[1]) : (pe[0] | (pe[1] << 8));
                dst[0] = ((px >> 11) & 0x1F) * 255 / 31;
                dst[1] = ((px >> 6)  & 0x1F) * 255 / 31;
                dst[2] = ((px >> 1)  & 0x1F) * 255 / 31;
                dst[3] = (px & 1) ? 255 : 0;
            } else {
                dst[0] = dst[1] = dst[2] = 128; dst[3] = 255;
            }
            dst += 4;
        }
        src++;
    }
}

/* Decode N64 CI8 with TLUT to RGBA8888 */
static void tex_decode_ci8(const u8 *src, u8 *dst, int count, const u8 *tlut, int tlut_count, int be) {
    for (int i = 0; i < count; i++) {
        u8 idx = src[0];
        if (idx < tlut_count) {
            const u8 *pe = tlut + idx * 2;
            u16 px = be ? ((pe[0] << 8) | pe[1]) : (pe[0] | (pe[1] << 8));
            dst[0] = ((px >> 11) & 0x1F) * 255 / 31;
            dst[1] = ((px >> 6)  & 0x1F) * 255 / 31;
            dst[2] = ((px >> 1)  & 0x1F) * 255 / 31;
            dst[3] = (px & 1) ? 255 : 0;
        } else {
            dst[0] = dst[1] = dst[2] = 128; dst[3] = 255;
        }
        src++; dst += 4;
    }
}

/**
 * Look up or create a GL texture from N64 texture data.
 * Returns GL texture ID, or 0 if the texture can't be decoded.
 */
static GLuint tex_lookup_or_create(uintptr_t addr, u32 format, u32 size,
                                    int width, int height, int be) {
    if (!addr || width <= 0 || height <= 0 || width > 256 || height > 256)
        return 0;

    /* Check cache */
    for (int i = 0; i < tex_cache_count; i++) {
        if (tex_cache[i].addr == addr)
            return tex_cache[i].gl_tex;
    }

    int texel_count = width * height;
    u8 *rgba = (u8 *)malloc(texel_count * 4);
    if (!rgba) return 0;

    const u8 *src = (const u8 *)addr;
    int decoded = 0;

    switch (size) {
        case G_IM_SIZ_16b:
            if (format == G_IM_FMT_RGBA) {
                tex_decode_rgba16(src, rgba, texel_count, be); decoded = 1;
            } else if (format == G_IM_FMT_IA) {
                tex_decode_ia16(src, rgba, texel_count, be); decoded = 1;
            }
            break;
        case G_IM_SIZ_32b:
            /* RGBA32: bytes are already R,G,B,A in memory order */
            memcpy(rgba, src, texel_count * 4); decoded = 1;
            break;
        case G_IM_SIZ_8b:
            if (format == G_IM_FMT_IA) {
                tex_decode_ia8(src, rgba, texel_count); decoded = 1;
            } else if (format == G_IM_FMT_I) {
                tex_decode_i8(src, rgba, texel_count); decoded = 1;
            } else if (format == G_IM_FMT_CI) {
                tex_decode_ci8(src, rgba, texel_count,
                               rsp.tlut, rsp.tlut_count, be); decoded = 1;
            }
            break;
        case G_IM_SIZ_4b:
            if (format == G_IM_FMT_IA) {
                tex_decode_ia4(src, rgba, texel_count); decoded = 1;
            } else if (format == G_IM_FMT_I) {
                tex_decode_i4(src, rgba, texel_count); decoded = 1;
            } else if (format == G_IM_FMT_CI) {
                tex_decode_ci4(src, rgba, texel_count,
                               rsp.tlut, rsp.tlut_count, be); decoded = 1;
            }
            break;
    }

    if (!decoded) {
        /* Unknown format — magenta for debugging */
        for (int i = 0; i < texel_count; i++) {
            rgba[i*4+0] = 255; rgba[i*4+1] = 0;
            rgba[i*4+2] = 255; rgba[i*4+3] = 255;
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    free(rgba);

    if (tex_cache_count < TEX_CACHE_SIZE) {
        tex_cache[tex_cache_count].addr = addr;
        tex_cache[tex_cache_count].gl_tex = tex;
        tex_cache[tex_cache_count].tex_w = (float)width;
        tex_cache[tex_cache_count].tex_h = (float)height;
        tex_cache_count++;
    }

    return tex;
}

void gfx_clear_texture_cache(void) {
    for (int i = 0; i < tex_cache_count; i++) {
        glDeleteTextures(1, &tex_cache[i].gl_tex);
    }
    tex_cache_count = 0;
    tex_bound_id = 0;
    tex_gl_enabled = 0;
    blend_mode_cur = -1;
    polygon_offset_on = 0;
}

/**
 * Handle GoldenEye's 0xC0 (G_SETTEX) texture reference command.
 * Extracts texture number, loads via game's texture system, creates GL texture.
 */
static void gfx_handle_settex(u32 w0, u32 w1) {
    int texturenum = w1 & 0xFFF;

    /* Check texture cache first (by texturenum, shifted to avoid collision with addr-based keys) */
    uintptr_t cache_key = (uintptr_t)(0xDEAD0000 | texturenum);
    for (int i = 0; i < tex_cache_count; i++) {
        if (tex_cache[i].addr == cache_key) {
            rsp.current_tex_id = tex_cache[i].gl_tex;
            rsp.current_tex_w = tex_cache[i].tex_w;
            rsp.current_tex_h = tex_cache[i].tex_h;
            return;
        }
    }

    /* Load the texture via game's texture system */
    texLoadFromTextureNum(texturenum, ptr_texture_alloc_start);
    struct tex *tex = texFindInPool(texturenum, ptr_texture_alloc_start);
    if (!tex || !tex->data || tex->width == 0 || tex->height == 0) {
        rsp.current_tex_id = 0;
        return;
    }

    int w = tex->width;
    int h = tex->height;
    u32 fmt = tex->gbiformat;
    u32 sz  = tex->depth;

    /* For CI textures, decode using the cached palette from texInflateZlib */
    GLuint gl_tex = 0;
    if (fmt == G_IM_FMT_CI) {
        s32 ncolours = 0;
        u16 *pal = texGetPalette(texturenum, &ncolours);
        if (pal && ncolours > 0) {
            /* Decode CI indices + palette → RGBA directly */
            int texel_count = w * h;
            u8 *rgba = (u8 *)malloc(texel_count * 4);
            if (rgba) {
                const u8 *idx_data = tex->data;
                for (int i = 0; i < texel_count; i++) {
                    int idx;
                    if (sz == G_IM_SIZ_4b) {
                        idx = (i & 1) ? (idx_data[i/2] & 0x0F) : ((idx_data[i/2] >> 4) & 0x0F);
                    } else {
                        idx = idx_data[i];
                    }
                    if (idx >= ncolours) idx = 0;
                    u16 c = pal[idx];
                    if (tex->lutmodeindex == (G_TT_IA16 >> G_MDSFT_TEXTLUT)) {
                        /* IA88 palette: high byte = intensity, low byte = alpha */
                        rgba[i*4+0] = rgba[i*4+1] = rgba[i*4+2] = (c >> 8) & 0xFF;
                        rgba[i*4+3] = c & 0xFF;
                    } else {
                        /* RGBA5551 palette */
                        rgba[i*4+0] = ((c >> 11) & 0x1F) * 255 / 31;
                        rgba[i*4+1] = ((c >> 6) & 0x1F) * 255 / 31;
                        rgba[i*4+2] = ((c >> 1) & 0x1F) * 255 / 31;
                        rgba[i*4+3] = (c & 1) ? 255 : 0;
                    }
                }
                GLuint t;
                glGenTextures(1, &t);
                glBindTexture(GL_TEXTURE_2D, t);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                free(rgba);
                gl_tex = t;
                /* Cache it */
                if (tex_cache_count < TEX_CACHE_SIZE) {
                    tex_cache[tex_cache_count].addr = (uintptr_t)tex->data;
                    tex_cache[tex_cache_count].gl_tex = gl_tex;
                    tex_cache[tex_cache_count].tex_w = (float)w;
                    tex_cache[tex_cache_count].tex_h = (float)h;
                    tex_cache_count++;
                }
            }
        } else {
            /* No palette — fall back to grayscale */
            fmt = G_IM_FMT_I;
            gl_tex = tex_lookup_or_create(
                (uintptr_t)tex->data, fmt, sz, w, h, 0);
        }
    } else {
        /* Non-CI: decode directly */
        gl_tex = tex_lookup_or_create(
            (uintptr_t)tex->data, fmt, sz, w, h, 0 /* native byte order */);
    }

    if (gl_tex && tex_cache_count < TEX_CACHE_SIZE) {
        /* Also cache by texturenum for fast lookup */
        tex_cache[tex_cache_count].addr = cache_key;
        tex_cache[tex_cache_count].gl_tex = gl_tex;
        tex_cache[tex_cache_count].tex_w = (float)w;
        tex_cache[tex_cache_count].tex_h = (float)h;
        tex_cache_count++;
    }

    rsp.current_tex_id = gl_tex;
    rsp.current_tex_w = (float)w;
    rsp.current_tex_h = (float)h;

}

/* ===== Forward declarations ===== */
static void gfx_process_dl(Gfx *dl);

/* ===== Matrix helpers ===== */

static void mtx_identity(float m[4][4]) {
    memset(m, 0, sizeof(float) * 16);
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

static void mtx_multiply(float result[4][4], float a[4][4], float b[4][4]) {
    float tmp[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            tmp[i][j] = 0.0f;
            for (k = 0; k < 4; k++)
                tmp[i][j] += a[i][k] * b[k][j];
        }
    memcpy(result, tmp, sizeof(float) * 16);
}

static void rsp_vertex_lerp(const RspVertex *a, const RspVertex *b, float t, RspVertex *out) {
    for (int i = 0; i < 4; i++) {
        out->pos[i] = a->pos[i] + (b->pos[i] - a->pos[i]) * t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
    }
    for (int i = 0; i < 3; i++) {
        out->obj_pos[i] = a->obj_pos[i] + (b->obj_pos[i] - a->obj_pos[i]) * t;
    }
    for (int i = 0; i < 2; i++) {
        out->texcoord[i] = a->texcoord[i] + (b->texcoord[i] - a->texcoord[i]) * t;
    }
    out->fog = a->fog + (b->fog - a->fog) * t;
}

/* ===== Full 6-plane frustum clipper (Sutherland-Hodgman) ===== */

#define MAX_CLIP_VERTS 12

/* Clip polygon against one frustum plane.
 * negate=0: test W + pos[component] >= 0 (left / bottom)
 * negate=1: test W - pos[component] >= 0 (right / top / far)
 * in[] and out[] must NOT alias.  Returns new vertex count. */
static int clip_polygon_plane(const RspVertex *in, int n, RspVertex *out,
                              int component, int negate) {
    int out_n = 0;
    for (int i = 0; i < n; i++) {
        const RspVertex *cur  = &in[i];
        const RspVertex *prev = &in[(i + n - 1) % n];
        float cd = negate ? cur->pos[3]  - cur->pos[component]
                          : cur->pos[3]  + cur->pos[component];
        float pd = negate ? prev->pos[3] - prev->pos[component]
                          : prev->pos[3] + prev->pos[component];
        if (pd >= 0) {
            if (cd >= 0) {
                out[out_n++] = *cur;
            } else {
                float t = pd / (pd - cd);
                rsp_vertex_lerp(prev, cur, t, &out[out_n++]);
            }
        } else if (cd >= 0) {
            float t = pd / (pd - cd);
            rsp_vertex_lerp(prev, cur, t, &out[out_n++]);
            out[out_n++] = *cur;
        }
    }
    return out_n;
}

/* Clip polygon against near plane W >= epsilon. */
static int clip_polygon_near(const RspVertex *in, int n, RspVertex *out) {
    const float NEAR_W = 0.01f;
    int out_n = 0;
    for (int i = 0; i < n; i++) {
        const RspVertex *cur  = &in[i];
        const RspVertex *prev = &in[(i + n - 1) % n];
        float cd = cur->pos[3]  - NEAR_W;
        float pd = prev->pos[3] - NEAR_W;
        if (pd >= 0) {
            if (cd >= 0) {
                out[out_n++] = *cur;
            } else {
                float t = pd / (pd - cd);
                rsp_vertex_lerp(prev, cur, t, &out[out_n++]);
            }
        } else if (cd >= 0) {
            float t = pd / (pd - cd);
            rsp_vertex_lerp(prev, cur, t, &out[out_n++]);
            out[out_n++] = *cur;
        }
    }
    return out_n;
}

/* Clip triangle against all 6 frustum planes.  Returns 0 if fully clipped,
 * otherwise the number of output vertices (3-9) in out[]. */
static int clip_triangle_full(const RspVertex *v0, const RspVertex *v1,
                              const RspVertex *v2, RspVertex *out) {
    RspVertex a[MAX_CLIP_VERTS], b[MAX_CLIP_VERTS];
    a[0] = *v0; a[1] = *v1; a[2] = *v2;
    int n = 3;
    n = clip_polygon_near(a, n, b);              if (n < 3) return 0;
    n = clip_polygon_plane(b, n, a, 0, 0);       if (n < 3) return 0; /* W+X >= 0 left   */
    n = clip_polygon_plane(a, n, b, 0, 1);       if (n < 3) return 0; /* W-X >= 0 right  */
    n = clip_polygon_plane(b, n, a, 1, 0);       if (n < 3) return 0; /* W+Y >= 0 bottom */
    n = clip_polygon_plane(a, n, b, 1, 1);       if (n < 3) return 0; /* W-Y >= 0 top    */
    n = clip_polygon_plane(b, n, a, 2, 0);       if (n < 3) return 0; /* W+Z >= 0 z-near */
    n = clip_polygon_plane(a, n, b, 2, 1);       if (n < 3) return 0; /* W-Z >= 0 z-far  */
    for (int i = 0; i < n; i++) out[i] = b[i];
    return n;
}

/**
 * Convert N64 fixed-point Mtx to float 4x4.
 * N64 Mtx: m[0..1] = integer parts (s16 pairs packed in s32),
 *          m[2..3] = fraction parts (u16 pairs packed in s32).
 */
static void mtx_n64_to_float(const Mtx *n64, float out[4][4]) {
    const u8 *base = (const u8 *)&n64->m[0][0];
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            /* Integer part: rows 0-1 (offset 0-31), fraction: rows 2-3 (offset 32-63) */
            int idx = (i * 2 + j) * 4;
            u32 hi = *(const u32 *)(base + idx);
            u32 lo = *(const u32 *)(base + 32 + idx);
            s32 e1 = (s32)((hi & 0xFFFF0000) | (lo >> 16));
            s32 e2 = (s32)(((hi & 0xFFFF) << 16) | (lo & 0xFFFF));
            out[i][j*2]   = (float)e1 / 65536.0f;
            out[i][j*2+1] = (float)e2 / 65536.0f;
        }
    }
}

/* Big-endian version: reads matrix data stored in N64 big-endian format */
static void mtx_n64_to_float_be(const void *data, float out[4][4]) {
    const u8 *base = (const u8 *)data;
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            int idx = (i * 2 + j) * 4;
            u32 hi = read_be32u(base + idx);
            u32 lo = read_be32u(base + 32 + idx);
            s32 e1 = (s32)((hi & 0xFFFF0000) | (lo >> 16));
            s32 e2 = (s32)(((hi & 0xFFFF) << 16) | (lo & 0xFFFF));
            out[i][j*2]   = (float)e1 / 65536.0f;
            out[i][j*2+1] = (float)e2 / 65536.0f;
        }
    }
}

static void recalc_combined(void) {
    if (rsp.combined_dirty) {
        mtx_multiply(rsp.combined,
                     rsp.modelview_stack[rsp.modelview_sp],
                     rsp.projection);
        rsp.combined_dirty = 0;
    }
}

/* ===== SP command handlers ===== */

static int g_mtx_log_count = 0;
static int g_vtx_log_count = 0;
static int g_tri_log_count = 0;

static void gfx_sp_matrix(u32 w0, uintptr_t w1_addr) {
    /* GoldenEye stores the G_MTX param byte directly in the command word. */
    u32 params = (w0 >> 16) & 0xFF;
    const Mtx *n64_mtx = (const Mtx *)w1_addr;

    if (!n64_mtx || w1_addr < 0x1000) {
        return;
    }

    float mf[4][4];

    if (params & 0x08) {
        /* G_MTX_FLOAT_PORT: data is already Mtxf (float), not fixed-point Mtx */
        memcpy(mf, n64_mtx, sizeof(float) * 16);
        params &= ~0x08;
    } else {
        mtx_n64_to_float(n64_mtx, mf);
    }

    if (params & G_MTX_PROJECTION) {
        /* Detect raw perspective matrix and save Z parameters for depth.
         * Raw perspective: [2][3] ≈ -1, [3][3] ≈ 0, off-diagonals ≈ 0.
         * The combined view×perspective loaded later won't match this. */
        if (fabsf(mf[2][3] + 1.0f) < 0.01f && fabsf(mf[3][3]) < 0.01f) {
            rsp.persp_a = mf[2][2];  /* -(far+near)/(far-near) */
            rsp.persp_b = mf[3][2];  /* -2*far*near/(far-near) */
            rsp.persp_set = 1;
        }
        if (params & G_MTX_LOAD) {
            memcpy(rsp.projection, mf, sizeof(float) * 16);
        } else {
            float tmp[4][4];
            mtx_multiply(tmp, mf, rsp.projection);
            memcpy(rsp.projection, tmp, sizeof(float) * 16);
        }
        /* Widescreen: adjust horizontal FOV for non-4:3 aspect ratios.
         * Detect perspective matrices (non-zero [3][2] = -1 convention)
         * and scale [0][0] so vertical FOV stays constant (hor+ widescreen). */
        if (GFX_WINDOW_W > 0 && GFX_WINDOW_H > 0 &&
            rsp.projection[3][2] != 0.0f) {
            float game_aspect = 4.0f / 3.0f;
            float real_aspect = (float)GFX_WINDOW_W / (float)GFX_WINDOW_H;
            if (real_aspect > game_aspect * 1.01f ||
                real_aspect < game_aspect * 0.99f) {
                rsp.projection[0][0] *= game_aspect / real_aspect;
            }
        }
    } else {
        /* Modelview */
        if ((params & G_MTX_PUSH) && rsp.modelview_sp < MAX_MATRIX_STACK - 1) {
            rsp.modelview_sp++;
            memcpy(rsp.modelview_stack[rsp.modelview_sp],
                   rsp.modelview_stack[rsp.modelview_sp - 1],
                   sizeof(float) * 16);
        }
        if (params & G_MTX_LOAD) {
            memcpy(rsp.modelview_stack[rsp.modelview_sp], mf, sizeof(float) * 16);
        } else {
            float tmp[4][4];
            mtx_multiply(tmp, mf, rsp.modelview_stack[rsp.modelview_sp]);
            memcpy(rsp.modelview_stack[rsp.modelview_sp], tmp, sizeof(float) * 16);
        }
    }
    rsp.combined_dirty = 1;

    /* Math audit: dump first few matrix loads */
    if (g_mtx_log_count < 30) {
        const char *type = (params & G_MTX_PROJECTION) ? "PROJ" : "MV";
        const char *op = (params & G_MTX_LOAD) ? "LOAD" : "MUL";
        fprintf(stderr, "[MTX_AUDIT] #%d %s %s params=0x%02X\n", g_mtx_log_count, type, op, params);
        fprintf(stderr, "  loaded: [%.4f %.4f %.4f %.4f]\n", mf[0][0], mf[0][1], mf[0][2], mf[0][3]);
        fprintf(stderr, "          [%.4f %.4f %.4f %.4f]\n", mf[1][0], mf[1][1], mf[1][2], mf[1][3]);
        fprintf(stderr, "          [%.4f %.4f %.4f %.4f]\n", mf[2][0], mf[2][1], mf[2][2], mf[2][3]);
        fprintf(stderr, "          [%.4f %.4f %.4f %.4f]\n", mf[3][0], mf[3][1], mf[3][2], mf[3][3]);
        g_mtx_log_count++;
    }
}

static void gfx_sp_pop_matrix(u32 w1) {
    u32 count = w1 / 64;
    if (count == 0) count = 1;
    while (count-- > 0 && rsp.modelview_sp > 0) {
        rsp.modelview_sp--;
    }
    rsp.combined_dirty = 1;
}

static void gfx_sp_vertex(u32 w0, uintptr_t w1_addr) {
    /* Base GBI: w0 = VTX(8) | ((n-1)<<4|v0)(8) | sizeof(Vtx)*n(16)
     * n = upper nibble of param field + 1, v0 = lower nibble */
    u32 param = (w0 >> 16) & 0xFF;
    int num_verts = (param >> 4) + 1;
    int dest_idx = param & 0xF;

    /* Validate pointer before dereferencing */
    if (w1_addr == 0 || w1_addr < 0x10000) {
        return;
    }



    const Vtx *vtx_data = (const Vtx *)w1_addr;

    if (dest_idx < 0) dest_idx = 0;

    recalc_combined();

    for (int i = 0; i < num_verts && (dest_idx + i) < MAX_VERTICES; i++) {
        const Vtx_t *v = &vtx_data[i].v;
        RspVertex *rv = &rsp.vertices[dest_idx + i];

        /* Transform position by combined matrix */
        float x = (float)v->ob[0];
        float y = (float)v->ob[1];
        float z = (float)v->ob[2];

        rv->obj_pos[0] = x;
        rv->obj_pos[1] = y;
        rv->obj_pos[2] = z;
        rv->pos[0] = rsp.combined[0][0]*x + rsp.combined[1][0]*y + rsp.combined[2][0]*z + rsp.combined[3][0];
        rv->pos[1] = rsp.combined[0][1]*x + rsp.combined[1][1]*y + rsp.combined[2][1]*z + rsp.combined[3][1];
        rv->pos[2] = rsp.combined[0][2]*x + rsp.combined[1][2]*y + rsp.combined[2][2]*z + rsp.combined[3][2];
        rv->pos[3] = rsp.combined[0][3]*x + rsp.combined[1][3]*y + rsp.combined[2][3]*z + rsp.combined[3][3];

        /* Math audit: dump first few vertex transforms */
        if (g_vtx_log_count < 5) {
            float ndc_x = rv->pos[3] != 0 ? rv->pos[0]/rv->pos[3] : 99;
            float ndc_y = rv->pos[3] != 0 ? rv->pos[1]/rv->pos[3] : 99;
            float ndc_z = rv->pos[3] != 0 ? rv->pos[2]/rv->pos[3] : 99;
            fprintf(stderr, "[VTX_AUDIT] #%d obj=(%d,%d,%d) clip=(%.1f,%.1f,%.1f,W=%.1f) ndc=(%.3f,%.3f,%.3f)\n",
                    g_vtx_log_count, v->ob[0], v->ob[1], v->ob[2],
                    rv->pos[0], rv->pos[1], rv->pos[2], rv->pos[3], ndc_x, ndc_y, ndc_z);
            if (g_vtx_log_count == 0) {
                fprintf(stderr, "[VTX_AUDIT] combined matrix:\n");
                for (int mi = 0; mi < 4; mi++)
                    fprintf(stderr, "  [%.6f %.6f %.6f %.6f]\n",
                            rsp.combined[mi][0], rsp.combined[mi][1], rsp.combined[mi][2], rsp.combined[mi][3]);
            }
            g_vtx_log_count++;
        }

        /* Vertex color or normal */
        if (rsp.geometry_mode & G_LIGHTING) {
            /* cn[0..2] are vertex normal (signed bytes) when lighting is on */
            float nx = (float)(s8)v->cn[0] / 127.0f;
            float ny = (float)(s8)v->cn[1] / 127.0f;
            float nz = (float)(s8)v->cn[2] / 127.0f;

            float r = rsp.ambient[0];
            float g = rsp.ambient[1];
            float b = rsp.ambient[2];

            for (int li = 0; li < rsp.num_lights && li < MAX_LIGHTS; li++) {
                float dot = nx * rsp.lights[li].dir[0]
                          + ny * rsp.lights[li].dir[1]
                          + nz * rsp.lights[li].dir[2];
                if (dot > 0.0f) {
                    r += dot * rsp.lights[li].col[0];
                    g += dot * rsp.lights[li].col[1];
                    b += dot * rsp.lights[li].col[2];
                }
            }
            rv->color[0] = fminf(r, 1.0f);
            rv->color[1] = fminf(g, 1.0f);
            rv->color[2] = fminf(b, 1.0f);
            rv->color[3] = (float)v->cn[3] / 255.0f;
        } else {
            /* Vertex colors */
            rv->color[0] = (float)v->cn[0] / 255.0f;
            rv->color[1] = (float)v->cn[1] / 255.0f;
            rv->color[2] = (float)v->cn[2] / 255.0f;
            rv->color[3] = (float)v->cn[3] / 255.0f;
        }

        /* Texture coordinates */
        /* Environment mapping temporarily disabled for crash investigation */
        rv->texcoord[0] = (float)v->tc[0];
        rv->texcoord[1] = (float)v->tc[1];
    }
}

/* Classify the current combine mode for GL rendering.
 * N64 color combiner: result = (a - b) * c + d, with 2 cycles.
 * Check BOTH cycles — GoldenEye commonly uses cycle 0 for LOD blending
 * and cycle 1 for the actual material formula (texture × shade). */
static int classify_combine_mode(void) {
    /* Cycle 0 */
    int a0 = (rsp.combine_hi >> 20) & 0xF;    /* saRGB0 */
    int c0 = (rsp.combine_hi >> 15) & 0x1F;   /* mRGB0 */
    int b0 = (rsp.combine_lo >> 28) & 0xF;    /* sbRGB0 */
    int d0 = (rsp.combine_lo >> 15) & 0x7;    /* aRGB0 */

    /* Cycle 1 */
    int a1 = (rsp.combine_hi >> 5) & 0xF;     /* saRGB1 */
    int c1 = (rsp.combine_hi >> 0) & 0x1F;    /* mRGB1 */
    int b1 = (rsp.combine_lo >> 24) & 0xF;    /* sbRGB1 */
    int d1 = (rsp.combine_lo >> 0) & 0x7;     /* aRGB1 */

    /* Detect PRIMITIVE-only color with texture-modulated alpha (text rendering):
     * RGB cycle 0: (0-0)*0 + PRIMITIVE  → a=0, b=0, c=0, d=PRIMITIVE(3)
     * Alpha cycle 0: (TEXEL0-0)*PRIMITIVE+0 → saA0=1, sbA0=0, mA0=3, aA0=0
     * Must validate both RGB and alpha parts to avoid misclassifying
     * non-text primitive-tinted quads. */
    int saA0 = (rsp.combine_hi >> 12) & 0x7;
    int mA0  = (rsp.combine_hi >> 9) & 0x7;
    int sbA0 = (rsp.combine_lo >> 12) & 0x7;
    int aA0  = (rsp.combine_lo >> 9) & 0x7;

    if (a0 == 0 && b0 == 0 && c0 == 0 && d0 == 3 &&
        saA0 == 1 && sbA0 == 0 && mA0 == 3 && aA0 == 0) {
        return CC_MODE_PRIM_ALPHA;
    }

    /* Check both cycles for texel, shade, primitive, and environment usage.
     * N64 CC input indices: 0=COMBINED 1=TEXEL0 2=TEXEL1 3=PRIMITIVE
     * 4=SHADE 5=ENVIRONMENT.  For mRGB (c field, 5-bit): 8=TEXEL0_ALPHA
     * 9=TEXEL1_ALPHA 10=PRIM_ALPHA 11=SHADE_ALPHA 12=ENV_ALPHA */
    int uses_texel = (a0 == 1 || a0 == 2 || b0 == 1 || b0 == 2 || d0 == 1 ||
                      c0 == 8 || c0 == 9 ||
                      a1 == 1 || a1 == 2 || b1 == 1 || b1 == 2 || d1 == 1 ||
                      c1 == 8 || c1 == 9);
    int uses_shade = (a0 == 4 || b0 == 4 || c0 == 4 || d0 == 4 || c0 == 11 ||
                      a1 == 4 || b1 == 4 || c1 == 4 || d1 == 4 || c1 == 11);
    int uses_prim  = (a0 == 3 || b0 == 3 || c0 == 3 || d0 == 3 || c0 == 10 ||
                      a1 == 3 || b1 == 3 || c1 == 3 || d1 == 3 || c1 == 10);
    int uses_env   = (a0 == 5 || b0 == 5 || c0 == 5 || d0 == 5 || c0 == 12 ||
                      a1 == 5 || b1 == 5 || c1 == 5 || d1 == 5 || c1 == 12);

    /* Also treat COMBINED (0) in cycle 1 as "uses texel" since cycle 0 output
     * is typically texture-derived */
    if (a1 == 0 || d1 == 0) uses_texel = 1;

    int base;
    if (!uses_texel && uses_shade) base = CC_MODE_SHADE;
    else if (uses_texel && !uses_shade) base = CC_MODE_DECAL;
    else base = CC_MODE_MODULATE;

    return base | (uses_prim ? CC_FLAG_PRIM : 0) | (uses_env ? CC_FLAG_ENV : 0);
}

/* Emit one vertex to GL using software-computed clip-space coordinates.
 * GL matrices are identity, so glVertex4f passes clip coords directly
 * through perspective divide → viewport → rasterization. */
static void gfx_emit_vertex(const RspVertex *rv, int use_tex, float s_scale_f,
                             float t_scale_f, float tex_w, float tex_h,
                             int apply_fog) {
    if (use_tex) {
        float u = rv->texcoord[0] * s_scale_f / (32.0f * tex_w);
        float v = rv->texcoord[1] * t_scale_f / (32.0f * tex_h);
        glTexCoord2f(u, v);
    }
    float r = rv->color[0], g = rv->color[1], b = rv->color[2];
    float a = rv->color[3];
    if (apply_fog) {
        float f = rv->fog;
        r = r * (1.0f - f) + rsp.fog_color[0] * f;
        g = g * (1.0f - f) + rsp.fog_color[1] * f;
        b = b * (1.0f - f) + rsp.fog_color[2] * f;
    }
    rsp.diag_avg_bright += (r + g + b) / 3.0f;
    rsp.diag_vert_count++;
    glColor4f(r, g, b, a);
    glVertex4f(rv->pos[0], rv->pos[1], rv->pos[2], rv->pos[3]);
}

static void gfx_sp_tri1(int v0, int v1, int v2) {
    if (v0 >= MAX_VERTICES || v1 >= MAX_VERTICES || v2 >= MAX_VERTICES) return;
    static int cull_mode = -1;

    RspVertex *rv0 = &rsp.vertices[v0];
    RspVertex *rv1 = &rsp.vertices[v1];
    RspVertex *rv2 = &rsp.vertices[v2];

    if (cull_mode < 0) {
        if (getenv("GFX_NO_CULL") != NULL) {
            cull_mode = 0;
        } else if (getenv("GFX_FLIP_CULL") != NULL) {
            cull_mode = 2;
        } else {
            cull_mode = 1;
        }
    }

    int apply_fog = 0; /* TODO: per-vertex fog from RSP fog_mul/fog_offset */

    /* --- Trivial reject: fast all-outside-one-plane check --- */
    {
        float *p0 = rv0->pos, *p1 = rv1->pos, *p2 = rv2->pos;
        if (p0[3] <= 0 && p1[3] <= 0 && p2[3] <= 0) return;
        if (p0[0]+p0[3] < 0 && p1[0]+p1[3] < 0 && p2[0]+p2[3] < 0) return;
        if (p0[3]-p0[0] < 0 && p1[3]-p1[0] < 0 && p2[3]-p2[0] < 0) return;
        if (p0[1]+p0[3] < 0 && p1[1]+p1[3] < 0 && p2[1]+p2[3] < 0) return;
        if (p0[3]-p0[1] < 0 && p1[3]-p1[1] < 0 && p2[3]-p2[1] < 0) return;
        if (p0[3]-p0[2] < 0 && p1[3]-p1[2] < 0 && p2[3]-p2[2] < 0) return;
    }

    /* Classify combine mode (base mode + tint flags) */
    int cc_mode = classify_combine_mode();
    int cc_base = CC_BASE_MODE(cc_mode);

    /* Texture binding — check if we should enable texturing for this triangle */
    int use_tex = 0;
    float tex_w = 1.0f, tex_h = 1.0f;
    float s_scale_f = 1.0f, t_scale_f = 1.0f;

    /* SHADE mode: no texture, just vertex color */
    if (cc_base == CC_MODE_SHADE) {
        if (tex_gl_enabled) {
            glDisable(GL_TEXTURE_2D);
            tex_gl_enabled = 0;
            tex_bound_id = 0;
        }
    } else if (rsp.texture_on) {
        GLuint tex_id = 0;

        /* Primary path: 0xC0 (G_SETTEX) texture reference from GoldenEye room DLs */
        if (rsp.current_tex_id) {
            tex_id = rsp.current_tex_id;
            tex_w = rsp.current_tex_w;
            tex_h = rsp.current_tex_h;
        }
        /* Fallback path: tile-based texture from standard GBI G_LOADBLOCK flow */
        else {
            int tile_idx = rsp.texture_tile;
            uintptr_t src_addr = rsp.tiles[tile_idx].source_addr;
            if (src_addr) {
                int tw = ((rsp.tiles[tile_idx].lrs - rsp.tiles[tile_idx].uls) >> 2) + 1;
                int th = ((rsp.tiles[tile_idx].lrt - rsp.tiles[tile_idx].ult) >> 2) + 1;
                if (tw > 0 && th > 0 && tw <= 256 && th <= 256) {
                    tex_id = tex_lookup_or_create(
                        src_addr, rsp.tiles[tile_idx].format,
                        rsp.tiles[tile_idx].size, tw, th, rsp.n64_data_mode);
                    tex_w = (float)tw;
                    tex_h = (float)th;
                }
            }
        }

        if (tex_id) {
            if (!tex_gl_enabled) {
                glEnable(GL_TEXTURE_2D);
                tex_gl_enabled = 1;
            }
            /* Set GL tex env based on combine mode */
            if (cc_base == CC_MODE_DECAL && !(cc_mode & (CC_FLAG_PRIM | CC_FLAG_ENV))) {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            } else {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            }
            if (tex_id != tex_bound_id) {
                glBindTexture(GL_TEXTURE_2D, tex_id);
                tex_bound_id = tex_id;
            }
            use_tex = 1;
            s_scale_f = (float)rsp.texture_s_scale / 65536.0f;
            t_scale_f = (float)rsp.texture_t_scale / 65536.0f;
        }
    }

    if (!use_tex && tex_gl_enabled && cc_base != CC_MODE_SHADE) {
        glDisable(GL_TEXTURE_2D);
        tex_gl_enabled = 0;
        tex_bound_id = 0;
    }

    /* RDP blender mode dispatch — decode other_mode_lo to set GL blend/alpha/depth state */
    {
        u32 mode = rsp.other_mode_lo;
        int force_bl   = (mode >> 14) & 1;
        int zmode      = (mode >> 10) & 3;  /* 0=OPA 1=INTER 2=XLU 3=DECAL */
        int cvg_x_alpha    = (mode >> 12) & 1;
        int alpha_cvg_sel  = (mode >> 13) & 1;

        int want;
        if (zmode == 3) {
            /* DECAL — coplanar geometry (blood, bullet holes) */
            want = force_bl ? 4 : 3;  /* 4=DECAL_XLU, 3=DECAL_OPA */
        } else if (cvg_x_alpha && alpha_cvg_sel) {
            /* TEX_EDGE — alpha-tested cutout (fences, railings, trees) */
            want = 2;
        } else if (force_bl || zmode == 2) {
            /* XLU_SURF / CLD_SURF — translucent */
            want = 1;
        } else {
            /* OPA_SURF — opaque */
            want = 0;
        }

        if (want != blend_mode_cur) {
            switch (want) {
            case 0: /* OPA */
                glDisable(GL_BLEND);
                glDisable(GL_ALPHA_TEST);
                glDepthMask(GL_TRUE);
                break;
            case 1: /* XLU */
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_ALPHA_TEST);
                glDepthMask(GL_FALSE);
                break;
            case 2: /* TEX_EDGE */
                glDisable(GL_BLEND);
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, 0.5f);
                glDepthMask(GL_TRUE);
                break;
            case 3: /* DECAL_OPA */
                glDisable(GL_BLEND);
                glDisable(GL_ALPHA_TEST);
                glDepthMask(GL_TRUE);
                break;
            case 4: /* DECAL_XLU */
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_ALPHA_TEST);
                glDepthMask(GL_FALSE);
                break;
            }
            blend_mode_cur = want;
        }

        /* Polygon offset for DECAL modes */
        int want_offset = (want == 3 || want == 4);
        if (want_offset != polygon_offset_on) {
            if (want_offset) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            polygon_offset_on = want_offset;
        }
    }

    (void)cc_mode;

    /* Software backface culling (SM64/libultraship pattern).
     * Compute cross product in NDC space. The (w<0) XOR flips the sign
     * when an odd number of vertices have negative W (behind camera). */
    if (rsp.geometry_mode & (G_CULL_FRONT | G_CULL_BACK)) {
        float w1 = rv0->pos[3], w2 = rv1->pos[3], w3 = rv2->pos[3];
        /* Avoid division by zero for behind-camera vertices */
        if (w1 == 0.0f) w1 = 0.001f;
        if (w2 == 0.0f) w2 = 0.001f;
        if (w3 == 0.0f) w3 = 0.001f;
        float dx1 = rv0->pos[0]/w1 - rv1->pos[0]/w2;
        float dy1 = rv0->pos[1]/w1 - rv1->pos[1]/w2;
        float dx2 = rv2->pos[0]/w3 - rv1->pos[0]/w2;
        float dy2 = rv2->pos[1]/w3 - rv1->pos[1]/w2;
        float cross = dx1 * dy2 - dy1 * dx2;
        /* Flip sign if odd number of negative W values */
        if ((rv0->pos[3] < 0) ^ (rv1->pos[3] < 0) ^ (rv2->pos[3] < 0))
            cross = -cross;
        if ((rsp.geometry_mode & G_CULL_BACK) && cross <= 0.0f) return;
        if ((rsp.geometry_mode & G_CULL_FRONT) && cross >= 0.0f) return;
    }

    if (use_tex) rsp.diag_tex_tris++; else rsp.diag_notex_tris++;
    if (cc_base == CC_MODE_SHADE) rsp.diag_shade_tris++;

    /* Math audit: dump first few drawn triangles with their clip coords */
    if (g_tri_log_count < 3) {
        fprintf(stderr, "[TRI_AUDIT] #%d v(%d,%d,%d)\n", g_tri_log_count, v0, v1, v2);
        const RspVertex *verts[3] = {rv0, rv1, rv2};
        for (int vi = 0; vi < 3; vi++) {
            float w = verts[vi]->pos[3];
            fprintf(stderr, "  v%d: clip=(%.1f,%.1f,%.1f,W=%.1f) ndc=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f)\n",
                    vi, verts[vi]->pos[0], verts[vi]->pos[1], verts[vi]->pos[2], w,
                    w!=0 ? verts[vi]->pos[0]/w : 99, w!=0 ? verts[vi]->pos[1]/w : 99,
                    w!=0 ? verts[vi]->pos[2]/w : 99,
                    verts[vi]->color[0], verts[vi]->color[1], verts[vi]->color[2]);
        }
        g_tri_log_count++;
    }

    glBegin(GL_TRIANGLES);
    {
        const RspVertex *tri[3] = { rv0, rv1, rv2 };
        for (int i = 0; i < 3; i++) {
            const RspVertex *rv = tri[i];
            if (use_tex) {
                float u = rv->texcoord[0] * s_scale_f / (32.0f * tex_w);
                float v = rv->texcoord[1] * t_scale_f / (32.0f * tex_h);
                glTexCoord2f(u, v);
            }
            float r = rv->color[0], g = rv->color[1], b = rv->color[2];
            float a = rv->color[3];
            if (apply_fog) {
                float f = rv->fog;
                r = r*(1-f) + rsp.fog_color[0]*f;
                g = g*(1-f) + rsp.fog_color[1]*f;
                b = b*(1-f) + rsp.fog_color[2]*f;
            }
            rsp.diag_avg_bright += (r+g+b)/3.0f;
            rsp.diag_vert_count++;
            glColor4f(r, g, b, a);
            glVertex4f(rv->pos[0], rv->pos[1], rv->pos[2], rv->pos[3]);
        }
    }
    glEnd();
    rsp.tri_count++;
}

static void gfx_sp_set_geometry_mode(u32 w1) {
    rsp.geometry_mode |= w1;
}

static void gfx_sp_clear_geometry_mode(u32 w1) {
    rsp.geometry_mode &= ~w1;
}

static void gfx_sp_texture(u32 w0, u32 w1) {
    rsp.texture_s_scale = (u16)(w1 >> 16);
    rsp.texture_t_scale = (u16)(w1 & 0xFFFF);
    rsp.texture_tile = (w0 >> 8) & 7;
    /* Base GBI: 'on' is 8 bits at bits 0-7 (gSPTexture passes _SHIFTL(on,0,8)) */
    rsp.texture_on = (w0 & 0xFF) ? 1 : 0;
}

static void gfx_sp_moveword(u32 w0, uintptr_t w1_full) {
    /* Base GBI: gImmp21 encodes (offset << 8) | index in low 24 bits of w0 */
    u32 index = w0 & 0xFF;
    u32 offset = (w0 >> 8) & 0xFFFF;
    u32 w1 = (u32)w1_full;

    switch (index) {
        case G_MW_SEGMENT:
            /* Segment table — w1 is the segment base address (full uintptr_t). */
            {
                int seg = offset / 4;
                if (seg < 16) {
                    gfx_segment_table[seg] = w1_full;
                }
            }
            break;
        case G_MW_FOG:
            rsp.fog_mul = (s16)(w1 >> 16);
            rsp.fog_offset = (s16)(w1 & 0xFFFF);
            break;
        case G_MW_NUMLIGHT:
            /* Base GBI: NUML(n) = ((n+1)*32 + 0x80000000) */
            rsp.num_lights = (int)(((w1 - 0x80000000U) / 32) - 1);
            if (rsp.num_lights < 0) rsp.num_lights = 0;
            if (rsp.num_lights > MAX_LIGHTS) rsp.num_lights = MAX_LIGHTS;
            break;
        case G_MW_PERSPNORM:
            rsp.persp_norm = (u16)(w1 & 0xFFFF);
            break;
        default:
            break;
    }
}

static void gfx_sp_movemem(u32 w0, uintptr_t w1_addr) {
    /* Base GBI gDma1p: w0 = (c << 24) | (p << 16) | (l & 0xFFFF)
     * p is at bits 23:16 (8 bits wide).
     * For lights, p = ((n)-1)*2 + G_MV_L0 (0x86..0x94).
     * For viewport, p = G_MV_VIEWPORT (0x80). */
    u32 type = (w0 >> 16) & 0xFF;

    switch (type) {
        case G_MV_VIEWPORT: {
            const Vp_t *vp = &((const Vp *)w1_addr)->vp;
            rsp.vp_x = (float)vp->vtrans[0] / 4.0f;
            rsp.vp_y = (float)vp->vtrans[1] / 4.0f;
            rsp.vp_w = (float)vp->vscale[0] / 4.0f;
            rsp.vp_h = (float)vp->vscale[1] / 4.0f;
            rsp.vp_znear = 0.0f;
            rsp.vp_zfar = 1.0f;
            rsp.vp_set = 1;
            break;
        }
        /* Base GBI light constants: G_MV_L0..G_MV_L7 (0x86..0x94) */
        case 0x86: case 0x88: case 0x8A: case 0x8C:
        case 0x8E: case 0x90: case 0x92: case 0x94: {
            /* N64 Light_t: col[3](u8), pad, colc[3](u8), pad, dir[3](s8), pad
             * Byte fields don't need endian swapping. */
            int light_idx = (type - 0x86) / 2;  /* 0-based: 0=L0, 1=L1, ... */
            const u8 *ldata = (const u8 *)w1_addr;
            if (light_idx < rsp.num_lights && light_idx < MAX_LIGHTS) {
                /* Directional light */
                rsp.lights[light_idx].col[0] = (float)ldata[0] / 255.0f;
                rsp.lights[light_idx].col[1] = (float)ldata[1] / 255.0f;
                rsp.lights[light_idx].col[2] = (float)ldata[2] / 255.0f;
                rsp.lights[light_idx].dir[0] = (float)(s8)ldata[8] / 127.0f;
                rsp.lights[light_idx].dir[1] = (float)(s8)ldata[9] / 127.0f;
                rsp.lights[light_idx].dir[2] = (float)(s8)ldata[10] / 127.0f;
            } else {
                /* Ambient light (slot after last directional) */
                rsp.ambient[0] = (float)ldata[0] / 255.0f;
                rsp.ambient[1] = (float)ldata[1] / 255.0f;
                rsp.ambient[2] = (float)ldata[2] / 255.0f;
            }
            break;
        }
        case G_MV_LOOKATX: {
            const u8 *ldata = (const u8 *)w1_addr;
            rsp.lookat_x[0] = (float)(s8)ldata[8] / 127.0f;
            rsp.lookat_x[1] = (float)(s8)ldata[9] / 127.0f;
            rsp.lookat_x[2] = (float)(s8)ldata[10] / 127.0f;
            break;
        }
        case G_MV_LOOKATY: {
            const u8 *ldata = (const u8 *)w1_addr;
            rsp.lookat_y[0] = (float)(s8)ldata[8] / 127.0f;
            rsp.lookat_y[1] = (float)(s8)ldata[9] / 127.0f;
            rsp.lookat_y[2] = (float)(s8)ldata[10] / 127.0f;
            break;
        }
        default:
            break;
    }
}

/* ===== DP command handlers ===== */

static void gfx_dp_set_combine_mode(u32 w0, u32 w1) {
    rsp.combine_hi = w0 & 0x00FFFFFF;
    rsp.combine_lo = w1;
}

static void gfx_dp_set_other_mode(int hi, u32 w0, u32 w1) {
    if (hi) {
        rsp.other_mode_hi = w0 & 0x00FFFFFF;
    } else {
        rsp.other_mode_lo = w1;
    }
}

static void gfx_dp_set_prim_color(u32 w0, u32 w1) {
    rsp.prim_color[0] = (float)((w1 >> 24) & 0xFF) / 255.0f;
    rsp.prim_color[1] = (float)((w1 >> 16) & 0xFF) / 255.0f;
    rsp.prim_color[2] = (float)((w1 >> 8)  & 0xFF) / 255.0f;
    rsp.prim_color[3] = (float)((w1 >> 0)  & 0xFF) / 255.0f;
}

static void gfx_dp_set_env_color(u32 w0, u32 w1) {
    rsp.env_color[0] = (float)((w1 >> 24) & 0xFF) / 255.0f;
    rsp.env_color[1] = (float)((w1 >> 16) & 0xFF) / 255.0f;
    rsp.env_color[2] = (float)((w1 >> 8)  & 0xFF) / 255.0f;
    rsp.env_color[3] = (float)((w1 >> 0)  & 0xFF) / 255.0f;
}

static void gfx_dp_set_fog_color(u32 w0, u32 w1) {
    rsp.fog_color[0] = (float)((w1 >> 24) & 0xFF) / 255.0f;
    rsp.fog_color[1] = (float)((w1 >> 16) & 0xFF) / 255.0f;
    rsp.fog_color[2] = (float)((w1 >> 8)  & 0xFF) / 255.0f;
    rsp.fog_color[3] = (float)((w1 >> 0)  & 0xFF) / 255.0f;
}

static void gfx_dp_set_fill_color(u32 w1) {
    /* RGBA16 packed: RRRRRGGGGGGBBBBA repeated twice */
    u16 c = (u16)(w1 >> 16);
    rsp.fill_color[0] = (float)((c >> 11) & 0x1F) / 31.0f;
    rsp.fill_color[1] = (float)((c >> 6)  & 0x1F) / 31.0f;
    rsp.fill_color[2] = (float)((c >> 1)  & 0x1F) / 31.0f;
    rsp.fill_color[3] = (float)(c & 1);
}

static void gfx_dp_fill_rectangle(u32 w0, u32 w1) {
    int x0 = ((w1 >> 12) & 0xFFF) >> 2;
    int y0 = ((w1 >> 0)  & 0xFFF) >> 2;
    int x1 = ((w0 >> 12) & 0xFFF) >> 2;
    int y1 = ((w0 >> 0)  & 0xFFF) >> 2;

    /* Skip full-screen fill rects that are likely Z-buffer clears or screen wipes.
     * The N64 uses gDPSetColorImage to redirect fill operations to the Z-buffer,
     * but our GL path doesn't track color image state. Full-screen fills with
     * black or Z-fill values would overwrite the rendered scene. */
    if (x0 == 0 && y0 == 0 && x1 >= 319 && y1 >= 239) {
        return;
    }

    /* Convert N64 screen coords (320x240) to GL NDC (-1..1) */
    float gl_x0 = (float)x0 * 2.0f / (float)GFX_N64_W - 1.0f;
    float gl_y0 = 1.0f - (float)y0 * 2.0f / (float)GFX_N64_H;
    float gl_x1 = (float)(x1 + 1) * 2.0f / (float)GFX_N64_W - 1.0f;
    float gl_y1 = 1.0f - (float)(y1 + 1) * 2.0f / (float)GFX_N64_H;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glColor4f(rsp.fill_color[0], rsp.fill_color[1], rsp.fill_color[2], rsp.fill_color[3]);
    glBegin(GL_QUADS);
    glVertex2f(gl_x0, gl_y0);
    glVertex2f(gl_x1, gl_y0);
    glVertex2f(gl_x1, gl_y1);
    glVertex2f(gl_x0, gl_y1);
    glEnd();
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/**
 * Draw a textured rectangle (G_TEXRECT / G_TEXRECTFLIP).
 * This is the core 2D rendering primitive — used for font glyphs, HUD elements, menus.
 *
 * Coordinates are 10.2 fixed-point N64 screen coordinates.
 * S/T are 10.5 fixed-point, DsDx/DtDy are 5.10 fixed-point.
 */
static void gfx_dp_texture_rectangle(u32 w0, u32 w1, u32 w2, u32 w3, int flip) {
    /* Parse coordinates (10.2 fixed-point → float pixels) */
    float xh = (float)((w0 >> 12) & 0xFFF) / 4.0f;  /* right edge */
    float yh = (float)((w0 >>  0) & 0xFFF) / 4.0f;  /* bottom edge */
    int tile  = (w1 >> 24) & 7;
    float xl = (float)((w1 >> 12) & 0xFFF) / 4.0f;   /* left edge */
    float yl = (float)((w1 >>  0) & 0xFFF) / 4.0f;   /* top edge */

    /* Parse texture coordinates from RDPHALF words */
    float s = (float)(s16)(w2 >> 16) / 32.0f;       /* 10.5 fixed-point */
    float t = (float)(s16)(w2 & 0xFFFF) / 32.0f;    /* 10.5 fixed-point */
    float dsdx = (float)(s16)(w3 >> 16) / 1024.0f;  /* 5.10 fixed-point */
    float dtdy = (float)(s16)(w3 & 0xFFFF) / 1024.0f;

    /* Compute texture coordinates at the four corners */
    float width = xh - xl;
    float height = yh - yl;
    float s0, t0, s1, t1;

    if (!flip) {
        /* Normal: S varies with X, T varies with Y */
        s0 = s;
        t0 = t;
        s1 = s + width * dsdx;
        t1 = t + height * dtdy;
    } else {
        /* Flipped: S varies with Y, T varies with X */
        s0 = s;
        t0 = t;
        s1 = s + height * dsdx;  /* S varies with height */
        t1 = t + width * dtdy;   /* T varies with width */
    }

    /* Look up or create GL texture from the tile's source data */
    uintptr_t src_addr = rsp.tiles[tile].source_addr;
    GLuint tex_id = 0;
    float tex_w = 1.0f, tex_h = 1.0f;

    if (src_addr) {
        int tw = ((rsp.tiles[tile].lrs - rsp.tiles[tile].uls) >> 2) + 1;
        int th = ((rsp.tiles[tile].lrt - rsp.tiles[tile].ult) >> 2) + 1;
        if (tw > 0 && th > 0 && tw <= 256 && th <= 256) {
            tex_id = tex_lookup_or_create(
                src_addr, rsp.tiles[tile].format,
                rsp.tiles[tile].size, tw, th, rsp.n64_data_mode);
            tex_w = (float)tw;
            tex_h = (float)th;
        }
    }

    /* Convert N64 screen coords to GL NDC (-1..1) */
    /* N64: (0,0) = top-left, (320,240) = bottom-right */
    /* GL NDC: (-1,-1) = bottom-left, (1,1) = top-right */
    float ndc_x0 = xl * 2.0f / (float)GFX_N64_W - 1.0f;
    float ndc_y0 = 1.0f - yl * 2.0f / (float)GFX_N64_H;
    float ndc_x1 = xh * 2.0f / (float)GFX_N64_W - 1.0f;
    float ndc_y1 = 1.0f - yh * 2.0f / (float)GFX_N64_H;

    /* Normalize texture coords to [0,1] range for GL */
    float u0 = s0 / tex_w;
    float v0 = t0 / tex_h;
    float u1 = s1 / tex_w;
    float v1 = t1 / tex_h;

    /* Save and set up GL state for 2D rendering */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    int cc_mode = classify_combine_mode();
    int cc_base_r = CC_BASE_MODE(cc_mode);

    if (tex_id) {
        if (!tex_gl_enabled) {
            glEnable(GL_TEXTURE_2D);
            tex_gl_enabled = 1;
        }
        if (tex_id != tex_bound_id) {
            glBindTexture(GL_TEXTURE_2D, tex_id);
            tex_bound_id = tex_id;
        }
        if (cc_base_r == CC_MODE_PRIM_ALPHA) {
            /* Text mode: Color = PRIMITIVE (flat), Alpha = TEXEL0 × PRIMITIVE.
             * Use GL_COMBINE to separate RGB (from primary color) and alpha
             * (texture × primary color). */
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            /* RGB: just output primary color (prim_color via glColor4f) */
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            /* Alpha: texture alpha × primary alpha */
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        } else if (cc_base_r == CC_MODE_DECAL && !(cc_mode & (CC_FLAG_PRIM | CC_FLAG_ENV))) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        } else {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
    }

    /* Use prim_color as the vertex color for text/HUD rendering.
     * Apply env_color tinting when ENVIRONMENT is part of the combine mode. */
    {
        float rc = rsp.prim_color[0], gc = rsp.prim_color[1];
        float bc = rsp.prim_color[2], ac = rsp.prim_color[3];
        if (cc_mode & CC_FLAG_ENV) {
            rc *= rsp.env_color[0]; gc *= rsp.env_color[1];
            bc *= rsp.env_color[2]; ac *= rsp.env_color[3];
        }
        glColor4f(rc, gc, bc, ac);
    }

    glBegin(GL_QUADS);
    if (!flip) {
        glTexCoord2f(u0, v0); glVertex2f(ndc_x0, ndc_y0);
        glTexCoord2f(u1, v0); glVertex2f(ndc_x1, ndc_y0);
        glTexCoord2f(u1, v1); glVertex2f(ndc_x1, ndc_y1);
        glTexCoord2f(u0, v1); glVertex2f(ndc_x0, ndc_y1);
    } else {
        /* TEXRECTFLIP: S and T axes are swapped */
        glTexCoord2f(u0, v0); glVertex2f(ndc_x0, ndc_y0);
        glTexCoord2f(u0, v1); glVertex2f(ndc_x1, ndc_y0);
        glTexCoord2f(u1, v1); glVertex2f(ndc_x1, ndc_y1);
        glTexCoord2f(u1, v0); glVertex2f(ndc_x0, ndc_y1);
    }
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static void gfx_dp_set_scissor(u32 w0, u32 w1) {
    rsp.scissor_x0 = ((w0 >> 12) & 0xFFF) >> 2;
    rsp.scissor_y0 = ((w0 >> 0)  & 0xFFF) >> 2;
    rsp.scissor_x1 = ((w1 >> 12) & 0xFFF) >> 2;
    rsp.scissor_y1 = ((w1 >> 0)  & 0xFFF) >> 2;

    /* Scale N64 screen coordinates (320x240) to GL window coordinates (640x480) */
    int sx0 = rsp.scissor_x0 * GFX_WINDOW_W / GFX_N64_W;
    int sy0 = rsp.scissor_y0 * GFX_WINDOW_H / GFX_N64_H;
    int sx1 = rsp.scissor_x1 * GFX_WINDOW_W / GFX_N64_W;
    int sy1 = rsp.scissor_y1 * GFX_WINDOW_H / GFX_N64_H;

    glScissor(sx0, GFX_WINDOW_H - sy1, sx1 - sx0, sy1 - sy0);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_dp_set_color_image(u32 w0, u32 w1) {
    /* Track the framebuffer target — on PC we always render to backbuffer */
    (void)w0; (void)w1;
}

static void gfx_dp_set_depth_image(u32 w1) {
    (void)w1;
}

static void gfx_dp_set_texture_image(u32 w0, uintptr_t w1_addr) {
    rsp.texture_image_format = (w0 >> 21) & 7;
    rsp.texture_image_size = (w0 >> 19) & 3;
    rsp.texture_image_width = (w0 & 0xFFF) + 1;
    rsp.texture_image_addr = gfx_resolve_texture_image_addr(w1_addr);
}

static void gfx_dp_set_tile(u32 w0, u32 w1) {
    int tile = (w1 >> 24) & 7;
    rsp.tiles[tile].format = (w0 >> 21) & 7;
    rsp.tiles[tile].size = (w0 >> 19) & 3;
    rsp.tiles[tile].line = (w0 >> 9) & 0x1FF;
    rsp.tiles[tile].tmem_addr = (w0 >> 0) & 0x1FF;
    rsp.tiles[tile].palette = (w1 >> 20) & 0xF;
    rsp.tiles[tile].cmt = (w1 >> 18) & 3;
    rsp.tiles[tile].maskt = (w1 >> 14) & 0xF;
    rsp.tiles[tile].shiftt = (w1 >> 10) & 0xF;
    rsp.tiles[tile].cms = (w1 >> 8) & 3;
    rsp.tiles[tile].masks = (w1 >> 4) & 0xF;
    rsp.tiles[tile].shifts = (w1 >> 0) & 0xF;

    /* Propagate source_addr from load tile sharing the same TMEM address.
     * On N64, TMEM bridges load tile (7) and render tile (0). On PC we skip
     * TMEM, so we copy the source_addr when TMEM addresses match. */
    if (!rsp.tiles[tile].source_addr) {
        u32 tmem = rsp.tiles[tile].tmem_addr;
        for (int t = 0; t < 8; t++) {
            if (t != tile && rsp.tiles[t].tmem_addr == tmem && rsp.tiles[t].source_addr) {
                rsp.tiles[tile].source_addr = rsp.tiles[t].source_addr;
                break;
            }
        }
    }
}

static void gfx_dp_set_tile_size(u32 w0, u32 w1) {
    int tile = (w1 >> 24) & 7;
    rsp.tiles[tile].uls = (w0 >> 12) & 0xFFF;
    rsp.tiles[tile].ult = (w0 >> 0) & 0xFFF;
    rsp.tiles[tile].lrs = (w1 >> 12) & 0xFFF;
    rsp.tiles[tile].lrt = (w1 >> 0) & 0xFFF;
}

/* ===== Weapon rendering diagnostic ===== */
static int g_weapon_dl_marker = 0;
static int g_weapon_tris_start = 0;
void gfx_weapon_dl_begin(void) {
    g_weapon_dl_marker = 1;
}
void gfx_weapon_dl_end(void) {
    g_weapon_dl_marker = 0;
}

/* ===== N64-format DL region tracking ===== */

#define MAX_N64_DL_REGIONS 512
static struct { uintptr_t start, end; } n64_dl_regions[MAX_N64_DL_REGIONS];
static int n64_dl_region_count = 0;

void gfx_register_n64_dl_region(void *addr, size_t size) {
    uintptr_t s = (uintptr_t)addr, e = s + size;
    /* Dedup: skip if this exact range is already registered */
    for (int i = 0; i < n64_dl_region_count; i++) {
        if (n64_dl_regions[i].start == s && n64_dl_regions[i].end == e)
            return;
    }
    if (n64_dl_region_count < MAX_N64_DL_REGIONS) {
        n64_dl_regions[n64_dl_region_count].start = s;
        n64_dl_regions[n64_dl_region_count].end = e;
        n64_dl_region_count++;
    }
}

void gfx_clear_n64_dl_regions(void) {
    n64_dl_region_count = 0;
}

static int gfx_is_n64_region(uintptr_t addr) {
    for (int i = 0; i < n64_dl_region_count; i++) {
        if (addr >= n64_dl_regions[i].start && addr < n64_dl_regions[i].end)
            return 1;
    }
    return 0;
}

/* Float matrix registration — PC code that passes raw float[4][4] matrices
 * through gSPMatrix (instead of guMtxF2L-produced N64 Mtx) must register the
 * memory range so the G_MTX handler can distinguish float from fixed-point. */
#define MAX_FLOAT_MTX_RANGES 8
static struct { uintptr_t start, end; } g_float_mtx_ranges[MAX_FLOAT_MTX_RANGES];
static int g_float_mtx_range_count = 0;

void gfx_register_float_matrix_range(void *start, size_t size) {
    if (g_float_mtx_range_count < MAX_FLOAT_MTX_RANGES) {
        g_float_mtx_ranges[g_float_mtx_range_count].start = (uintptr_t)start;
        g_float_mtx_ranges[g_float_mtx_range_count].end = (uintptr_t)start + size;
        g_float_mtx_range_count++;
    }
}

static int gfx_is_float_matrix(uintptr_t addr) {
    for (int i = 0; i < g_float_mtx_range_count; i++) {
        if (addr >= g_float_mtx_ranges[i].start && addr < g_float_mtx_ranges[i].end)
            return 1;
    }
    return 0;
}

/* PC Gfx buffer range — set once when DL buffers are allocated.
 * Any G_DL target address must be in a registered N64 region OR
 * within this PC range. Otherwise it's garbage. */
static uintptr_t pc_gfx_range_start = 0;
static uintptr_t pc_gfx_range_end = 0;

void gfx_set_pc_dl_range(void *start, size_t size) {
    pc_gfx_range_start = (uintptr_t)start;
    pc_gfx_range_end = (uintptr_t)start + size;
}

static int gfx_is_valid_pc_dl(uintptr_t addr) {
    return (pc_gfx_range_start && addr >= pc_gfx_range_start && addr < pc_gfx_range_end);
}

/* Check if an opcode's w1 field contains an address (needs segment resolution in N64 DLs) */
static int opcode_has_address(u32 opcode) {
    switch (opcode) {
        case 0x01: /* G_MTX */
        case 0x04: /* G_VTX */
        case 0x06: /* G_DL */
        case 0x03: /* G_MOVEMEM */
        case 0xFD: /* G_SETTIMG */
        case 0xFF: /* G_SETCIMG */
        case 0xFE: /* G_SETZIMG */
            return 1;
        default:
            return 0;
    }
}

/* Resolve an N64 segment address to a real pointer */
static uintptr_t gfx_resolve_segment(u32 segaddr) {
    int seg = (segaddr >> 24) & 0xF;
    u32 offset = segaddr & 0x00FFFFFF;
    uintptr_t resolved = gfx_segment_table[seg] + offset;

    /* WS-01D: Warn on NULL/suspiciously low resolved addresses */
    if (resolved == 0 || resolved < 0x1000) {
        static int seg_fail_count = 0;
        if (seg_fail_count < 20) {
            fprintf(stderr, "[GFX_DIAG] segment resolution failed: seg=%d offset=0x%06X "
                    "base=0x%lx resolved=0x%lx segaddr=0x%08X\n",
                    seg, offset, (unsigned long)gfx_segment_table[seg],
                    (unsigned long)resolved, segaddr);
        }
        seg_fail_count++;
    }

    return resolved;
}

/* ===== Main display list processor ===== */

/*
 * Opcode helper: base GBI uses negative values for immediate commands.
 * The opcode is stored as a u8 in the top byte of w0, so we compare
 * with (u8) casts to get the unsigned 0-255 value.
 */
#define OP(cmd) ((u32)(u8)(cmd))

/* WS-01D: Max unique unknown opcodes to track per frame / globally */
#define DIAG_MAX_UNIQUE_OPCODES 20

/* Forward declaration */
static void gfx_process_n64_dl(const u8 *data);

/* Per-frame G_DL dispatch counters (reset in gfx_run_dl) */
static int g_pc_gdl_followed = 0;
static int g_pc_gdl_skipped = 0;
static int g_pc_gdl_null = 0;
static int g_pc_gdl_to_n64 = 0;

static void gfx_process_dl(Gfx *dl) {
    if (!dl) return;

    for (;;) {
        uintptr_t w0_full = dl->words.w0;
        uintptr_t w1_full = dl->words.w1;
        u32 w0 = (u32)w0_full;
        u32 w1 = (u32)w1_full;
        u32 opcode = w0 >> 24;

        rsp.dl_cmd_count++;

        /* Safety: PC Gfx commands always have upper 32 bits of w0 = 0.
         * If non-zero, we've walked past valid DL data into garbage.
         * Pop the DL stack to recover (like G_ENDDL) instead of aborting
         * the entire frame's DL processing. */
        if (w0_full >> 32) {
            static int garble_count = 0;
            if (garble_count++ < 10) {
                printf("[GFX] DL garbled at cmd#%d: w0_full=0x%llx w1_full=0x%llx dl=%p sp=%d\n",
                       rsp.dl_cmd_count, (unsigned long long)w0_full,
                       (unsigned long long)w1_full, (void*)dl, rsp.dl_sp);
                fflush(stdout);
            }
            if (rsp.dl_sp > 0) {
                dl = rsp.dl_stack[--rsp.dl_sp];
                continue; /* Return to caller DL */
            }
            return; /* Top-level — nothing left to process */
        }

        /* Safety limit: prevent infinite loops.
         * Gameplay rendering processes ~200K+ commands across all rooms,
         * so allow up to 1M before aborting. */
        if (rsp.dl_cmd_count > 1000000) {
            static int limit_msg = 0;
            if (limit_msg++ < 3)
                printf("[GFX] DL command limit exceeded (%d commands)\n", rsp.dl_cmd_count);
            return;
        }

        switch (opcode) {

        /* ===== SP DMA commands (opcodes 0-9) ===== */

        case OP(G_MTX):
            gfx_sp_matrix(w0, w1_full);
            break;

        case OP(G_VTX):
            if (w1_full >= 0x1000) {
                gfx_sp_vertex(w0, w1_full);
            }
            break;

        case OP(G_DL): {
            /* Base GBI: push/nopush flag at bits 16-23 (gDma1p 'p' field) */
            int is_branch = ((w0 >> 16) & 0xFF) == G_DL_NOPUSH;
            uintptr_t dl_addr = w1_full;
            /* Check if target is N64-format binary DL data */
            if (gfx_is_n64_region(dl_addr)) {
                g_pc_gdl_to_n64++;
                gfx_process_n64_dl((const u8 *)dl_addr);
                break;
            }
            if (dl_addr < 0x1000) {
                g_pc_gdl_null++;
                break; /* NULL-like address */
            }
            /* Only follow G_DL to addresses in known PC Gfx buffers.
             * Do NOT probe unknown memory — garbage addresses cause BUS errors. */
            if (!gfx_is_valid_pc_dl(dl_addr)) {
                g_pc_gdl_skipped++;
                break;
            }
            g_pc_gdl_followed++;
            if (!is_branch) {
                if (rsp.dl_sp < DL_STACK_SIZE - 1) {
                    rsp.dl_stack[rsp.dl_sp++] = dl + 1;
                }
            }
            dl = (Gfx *)dl_addr;
            continue;
        }

        case OP(G_MOVEMEM):
            gfx_sp_movemem(w0, w1_full);
            break;

        /* ===== SP Immediate commands (opcodes 0xB0-0xBF range) ===== */

        case OP(G_TRI1): {
            /* Base GBI: vertex indices in w1 */
            int v0 = ((w1 >> 16) & 0xFF) / 10;
            int v1 = ((w1 >> 8) & 0xFF) / 10;
            int v2 = ((w1 >> 0) & 0xFF) / 10;
            gfx_sp_tri1(v0, v1, v2);
            break;
        }

        case 0xB1: {
            /* Rare's G_TRI4: 4 triangles with 4-bit vertex indices
             * w0: [0xB1(8)] [z4(4)|z3(4)|z2(4)|z1(4)] [0(8)]
             * w1: [y4(4)|x4(4)|y3(4)|x3(4)|y2(4)|x2(4)|y1(4)|x1(4)] */
            int v[4][3];
            v[0][0] = (w1 >> 0)  & 0xF;
            v[0][1] = (w1 >> 4)  & 0xF;
            v[0][2] = (w0 >> 0)  & 0xF;
            v[1][0] = (w1 >> 8)  & 0xF;
            v[1][1] = (w1 >> 12) & 0xF;
            v[1][2] = (w0 >> 4)  & 0xF;
            v[2][0] = (w1 >> 16) & 0xF;
            v[2][1] = (w1 >> 20) & 0xF;
            v[2][2] = (w0 >> 8)  & 0xF;
            v[3][0] = (w1 >> 24) & 0xF;
            v[3][1] = (w1 >> 28) & 0xF;
            v[3][2] = (w0 >> 12) & 0xF;
            for (int i = 0; i < 4; i++) {
                if (v[i][0] != v[i][1] || v[i][0] != v[i][2]) {
                    gfx_sp_tri1(v[i][0], v[i][1], v[i][2]);
                }
            }
            break;
        }

        case OP(G_ENDDL):
            if (rsp.dl_sp > 0) {
                dl = rsp.dl_stack[--rsp.dl_sp];
                continue;
            }
            return; /* Top-level end */

        case OP(G_POPMTX):
            gfx_sp_pop_matrix(w1);
            break;

        case OP(G_SETGEOMETRYMODE):
            gfx_sp_set_geometry_mode(w1);
            break;

        case OP(G_CLEARGEOMETRYMODE):
            gfx_sp_clear_geometry_mode(w1);
            break;

        /* Base GBI combined geometry mode (F3DEX2 only — 0xD9).
         * Not used by GoldenEye. Kept for completeness. */
        case 0xD9:
            rsp.geometry_mode &= (w0 & 0x00FFFFFF);
            rsp.geometry_mode |= w1;
            break;

        case OP(G_TEXTURE):
            gfx_sp_texture(w0, w1);
            break;

        case OP(G_MOVEWORD):
            gfx_sp_moveword(w0, w1_full);
            break;

        case 0xC0:
            /* 0xC0 = G_NOOP in base GBI, repurposed as G_SETTEX by Rare.
             * If w0/w1 are both 0, treat as noop. Otherwise treat as SETTEX. */
            if (w0 != 0 || w1_full != 0) {
                gfx_handle_settex(w0, w1);
            }
            break;

        /* ===== DP commands (RDP pass-through, opcodes 0xE0-0xFF) ===== */

        case OP(G_SETCOMBINE):
            gfx_dp_set_combine_mode(w0, w1);
            break;

        case OP(G_SETOTHERMODE_H):
            gfx_dp_set_other_mode(1, w0, w1);
            break;

        case OP(G_SETOTHERMODE_L):
            gfx_dp_set_other_mode(0, w0, w1);
            break;

        case OP(G_RDPSETOTHERMODE):
            rsp.other_mode_hi = w0 & 0x00FFFFFF;
            rsp.other_mode_lo = w1;
            break;

        case OP(G_SETPRIMCOLOR):
            gfx_dp_set_prim_color(w0, w1);
            break;

        case OP(G_SETENVCOLOR):
            gfx_dp_set_env_color(w0, w1);
            break;

        case OP(G_SETFOGCOLOR):
            gfx_dp_set_fog_color(w0, w1);
            break;

        case OP(G_SETBLENDCOLOR):
            rsp.blend_color[0] = (float)((w1 >> 24) & 0xFF) / 255.0f;
            rsp.blend_color[1] = (float)((w1 >> 16) & 0xFF) / 255.0f;
            rsp.blend_color[2] = (float)((w1 >> 8)  & 0xFF) / 255.0f;
            rsp.blend_color[3] = (float)((w1 >> 0)  & 0xFF) / 255.0f;
            break;

        case OP(G_SETFILLCOLOR):
            gfx_dp_set_fill_color(w1);
            break;

        case OP(G_FILLRECT):
            gfx_dp_fill_rectangle(w0, w1);
            break;

        case OP(G_SETSCISSOR):
            gfx_dp_set_scissor(w0, w1);
            break;

        case OP(G_SETCIMG):
            gfx_dp_set_color_image(w0, w1);
            break;

        case OP(G_SETZIMG):
            gfx_dp_set_depth_image(w1);
            break;

        case OP(G_SETTIMG):
            gfx_dp_set_texture_image(w0, w1_full);
            break;

        case OP(G_SETTILE):
            gfx_dp_set_tile(w0, w1);
            break;

        case OP(G_SETTILESIZE):
            gfx_dp_set_tile_size(w0, w1);
            break;

        case OP(G_LOADBLOCK):
        case OP(G_LOADTILE): {
            /* Capture texture source addr for the target tile */
            int load_tile = (w1 >> 24) & 7;
            rsp.tiles[load_tile].source_addr = rsp.texture_image_addr;
            break;
        }

        case OP(G_LOADTLUT): {
            /* Load palette (TLUT) from texture_image_addr */
            int tlut_count = ((w1 >> 14) & 0x3FF) + 1;
            if (tlut_count > 256) tlut_count = 256;
            if (rsp.texture_image_addr) {
                memcpy(rsp.tlut, (const u8 *)rsp.texture_image_addr, tlut_count * 2);
            }
            rsp.tlut_count = tlut_count;
            break;
        }

        case OP(G_RDPLOADSYNC):
        case OP(G_RDPPIPESYNC):
        case OP(G_RDPTILESYNC):
        case OP(G_RDPFULLSYNC):
            /* Sync commands — no action needed on PC */
            break;

        case OP(G_TEXRECT):
        case OP(G_TEXRECTFLIP): {
            /* Texture rectangle: 3 Gfx words total.
             * Word 0 (current): coords xh, yh, tile, xl, yl
             * Word 1 (RDPHALF_1): S, T starting texture coords
             * Word 2 (RDPHALF_2): DsDx, DtDy texture deltas */
            int is_flip = (opcode == G_TEXRECTFLIP);
            u32 texrect_w2 = (u32)dl[1].words.w1;  /* RDPHALF_1 */
            u32 texrect_w3 = (u32)dl[2].words.w1;  /* RDPHALF_2 */
            gfx_dp_texture_rectangle(w0, w1, texrect_w2, texrect_w3, is_flip);
            dl += 2; /* skip the 2 extra words */
            break;
        }

        case OP(G_RDPHALF_1):
        case OP(G_RDPHALF_2):
        case OP(G_SPNOOP):
            /* No action needed (consumed inline by TEXRECT handler) */
            break;

        default:
            /* Unknown command — skip */
            /* WS-01D: Log unknown PC DL opcodes (first 20 unique, throttled) */
            {
                static u32 pc_unknown_ops[DIAG_MAX_UNIQUE_OPCODES];
                static int pc_unknown_count = 0;
                if (pc_unknown_count < DIAG_MAX_UNIQUE_OPCODES) {
                    int found = 0;
                    for (int ui = 0; ui < pc_unknown_count; ui++) {
                        if (pc_unknown_ops[ui] == opcode) { found = 1; break; }
                    }
                    if (!found) {
                        pc_unknown_ops[pc_unknown_count++] = opcode;
                        fprintf(stderr, "[GFX_DIAG] PC DL unknown opcode 0x%02X "
                                "(w0=0x%08X w1=0x%08X)\n",
                                opcode, w0, w1);
                    }
                }
            }
            break;
        }

        dl++;
    }
}

/* ===== N64-format display list interpreter ===== */

/**
 * Process an N64-format display list stored in memory as big-endian
 * 8-byte command pairs. Resolves segment addresses and handles the
 * same GBI commands as the PC DL interpreter.
 *
 * N64 DL format: each command is two big-endian u32 words (8 bytes total).
 * Address fields use segment:offset format (top nibble of w1 = segment index).
 *
 * The N64 vertex data (Vtx structs) pointed to by these DLs is also
 * big-endian and needs byte-swapping during vertex loading.
 */
static int g_n64dl_call_count = 0;  /* per-frame N64 DL invocations */
static int g_n64dl_total_cmds = 0;
static int g_n64dl_total_tris = 0;

/* ===== WS-01D: Diagnostic instrumentation ===== */
static int g_diag_frame_count = 0;          /* global frame counter */

/* Unknown opcode tracking (N64 DL) */
static u32 g_diag_unknown_opcodes[DIAG_MAX_UNIQUE_OPCODES];
static int g_diag_unknown_opcode_counts[DIAG_MAX_UNIQUE_OPCODES];
static int g_diag_num_unique_unknowns = 0;
static int g_diag_unknown_total = 0;

/* Bad matrix tracking */
static int g_diag_bad_matrix_count = 0;
#define DIAG_MAX_BAD_MATRIX_LOGS 20

/* Vertex buffer overflow tracking */
static int g_diag_vtx_overflow_count = 0;
#define DIAG_MAX_VTX_OVERFLOW_LOGS 20

/* DL nesting depth tracking */
static int g_diag_n64dl_depth = 0;
static int g_diag_max_depth_warned = 0;
#define DIAG_MAX_DL_DEPTH 10
#define DIAG_MAX_DEPTH_WARNS 10

static void gfx_process_n64_dl(const u8 *data) {
    #define N64_DL_STACK_SIZE 16
    const u8 *n64_stack[N64_DL_STACK_SIZE];
    int n64_sp = 0;
    int prev_n64_mode = rsp.n64_data_mode;

    if (!data) return;

    /* WS-01D: Track DL call nesting depth */
    g_diag_n64dl_depth++;
    if (g_diag_n64dl_depth > DIAG_MAX_DL_DEPTH && g_diag_max_depth_warned < DIAG_MAX_DEPTH_WARNS) {
        fprintf(stderr, "[GFX_DIAG] N64 DL nesting depth=%d exceeds %d (data=%p)\n",
                g_diag_n64dl_depth, DIAG_MAX_DL_DEPTH, (const void *)data);
        g_diag_max_depth_warned++;
    }

    g_n64dl_call_count++;
    int tris_before = rsp.tri_count;

    rsp.n64_data_mode = 1;

    int n64_cmd_count = 0;
    int n64_zero_run = 0;  /* consecutive all-zero commands (detect past-end-of-DL) */

    for (;;) {
        u32 w0 = read_be32u(data);
        u32 w1 = read_be32u(data + 4);
        u32 opcode = w0 >> 24;
        uintptr_t w1_addr;

        rsp.dl_cmd_count++;
        n64_cmd_count++;

        /* Detect running past end of DL into zero-filled memory.
         * G_SPNOOP (0x00) is valid but >4 consecutive zeros = past end. */
        if (w0 == 0 && w1 == 0) {
            n64_zero_run++;
            if (n64_zero_run > 4) {
                /* Walked past end of DL into zero-fill */
                break;
            }
        } else {
            n64_zero_run = 0;
        }

        /* Per-DL safety limit: prevent infinite loops in malformed N64 DLs */
        if (n64_cmd_count > 50000) {
            break;
        }

        /* Resolve segment address for commands that have address arguments */
        if (opcode_has_address(opcode)) {
            w1_addr = gfx_resolve_segment(w1);
        } else {
            w1_addr = (uintptr_t)w1;
        }

        switch (opcode) {

        /* ===== SP DMA commands ===== */
        case OP(G_MTX): {
            /* We are inside gfx_process_n64_dl — processing big-endian N64
             * binary DL data loaded from ROM.  Matrix data referenced by
             * these DLs is almost always also big-endian ROM data, so
             * default to big-endian conversion.  Fall back to native-endian
             * only for addresses explicitly registered as float matrices
             * (from PC renderer code) or that are clearly outside any
             * N64 data region (game-allocated Mtx from guMtxF2L). */
            u32 params = (w0 >> 16) & 0xFF;
            float mf[4][4];
            if (w1_addr == 0 || w1_addr < 0x1000) {
                if (g_diag_bad_matrix_count < DIAG_MAX_BAD_MATRIX_LOGS) {
                    fprintf(stderr, "[GFX_DIAG] N64 G_MTX NULL/invalid ptr: "
                            "w1_addr=0x%lx params=0x%02X\n",
                            (unsigned long)w1_addr, params);
                }
                g_diag_bad_matrix_count++;
                break;
            }
            if (gfx_is_float_matrix(w1_addr)) {
                /* Explicitly registered as raw float[4][4] by PC renderer */
                memcpy(mf, (const void *)w1_addr, sizeof(float) * 16);
            } else if (gfx_is_n64_region(w1_addr)) {
                /* Address is in a registered N64 data region — big-endian */
                mtx_n64_to_float_be((const void *)w1_addr, mf);
            } else {
                /* Address is in PC memory (e.g. game-allocated Mtx from
                 * guMtxF2L / matrix_4x4_f32_to_s32).  Use native-endian. */
                mtx_n64_to_float((const Mtx *)w1_addr, mf);
            }
            /* WS-01D: Check converted matrix for NaN/Inf */
            {
                int bad_elem = 0;
                for (int mi = 0; mi < 4 && !bad_elem; mi++) {
                    for (int mj = 0; mj < 4; mj++) {
                        if (isnan(mf[mi][mj]) || isinf(mf[mi][mj])) {
                            bad_elem = 1;
                            break;
                        }
                    }
                }
                if (bad_elem && g_diag_bad_matrix_count < DIAG_MAX_BAD_MATRIX_LOGS) {
                    fprintf(stderr, "[GFX_DIAG] N64 G_MTX has NaN/Inf after conversion: "
                            "w1_addr=0x%lx params=0x%02X\n"
                            "  row0=[%g %g %g %g]\n  row1=[%g %g %g %g]\n"
                            "  row2=[%g %g %g %g]\n  row3=[%g %g %g %g]\n",
                            (unsigned long)w1_addr, params,
                            mf[0][0], mf[0][1], mf[0][2], mf[0][3],
                            mf[1][0], mf[1][1], mf[1][2], mf[1][3],
                            mf[2][0], mf[2][1], mf[2][2], mf[2][3],
                            mf[3][0], mf[3][1], mf[3][2], mf[3][3]);
                    g_diag_bad_matrix_count++;
                }
            }

            if (params & G_MTX_PROJECTION) {
                if (params & G_MTX_LOAD) {
                    memcpy(rsp.projection, mf, sizeof(float) * 16);
                } else {
                    float tmp[4][4];
                    mtx_multiply(tmp, mf, rsp.projection);
                    memcpy(rsp.projection, tmp, sizeof(float) * 16);
                }
            } else {
                if ((params & G_MTX_PUSH) && rsp.modelview_sp < MAX_MATRIX_STACK - 1) {
                    rsp.modelview_sp++;
                    memcpy(rsp.modelview_stack[rsp.modelview_sp],
                           rsp.modelview_stack[rsp.modelview_sp - 1],
                           sizeof(float) * 16);
                }
                if (params & G_MTX_LOAD) {
                    memcpy(rsp.modelview_stack[rsp.modelview_sp], mf, sizeof(float) * 16);
                } else {
                    float tmp[4][4];
                    mtx_multiply(tmp, mf, rsp.modelview_stack[rsp.modelview_sp]);
                    memcpy(rsp.modelview_stack[rsp.modelview_sp], tmp, sizeof(float) * 16);
                }
            }
            rsp.combined_dirty = 1;
            break;
        }

        case OP(G_VTX):
            /* N64 vertices are big-endian. We need to byte-swap them.
             * Create a temporary swapped copy. */
            {
                u32 param = (w0 >> 16) & 0xFF;
                u32 len = w0 & 0xFFFF;
                int num_verts = 0;
                int dest_idx = 0;
                const u8 *src = (const u8 *)w1_addr;
                if (w1_addr == 0 || w1_addr < 0x1000) break;

                /* GoldenEye's ROM-baked DLs use the F3DEX/F3DLP G_VTX layout:
                 *   w0 = cmd(8) | v0*2(8) | (n<<10 | sizeof(Vtx)*n-1)(16)
                 * Fall back to the older base-GBI interpretation if the packed
                 * length bits don't describe a sensible vertex DMA. */
                {
                    int f3dex_num = (len >> 10) & 0x3F;
                    int f3dex_dst = param / 2;
                    int dma_len = (len & 0x3FF) + 1;

                    if (f3dex_num > 0 && f3dex_num <= MAX_VERTICES
                        && f3dex_dst >= 0 && f3dex_dst < MAX_VERTICES
                        && dma_len == f3dex_num * (int)sizeof(Vtx)) {
                        num_verts = f3dex_num;
                        dest_idx = f3dex_dst;
                    } else {
                        num_verts = (param >> 4) + 1;
                        dest_idx = param & 0xF;
                    }
                }

                /* WS-01D: Validate vertex count + dest doesn't overflow buffer */
                if (num_verts + dest_idx > MAX_VERTICES) {
                    if (g_diag_vtx_overflow_count < DIAG_MAX_VTX_OVERFLOW_LOGS) {
                        fprintf(stderr, "[GFX_DIAG] N64 G_VTX overflow: num_verts=%d dest_idx=%d "
                                "sum=%d max=%d (w0=0x%08X)\n",
                                num_verts, dest_idx, num_verts + dest_idx,
                                MAX_VERTICES, w0);
                    }
                    g_diag_vtx_overflow_count++;
                }

                recalc_combined();

                /* Math audit: dump combined matrix and first N64 vertex */
                if (g_vtx_log_count < 5) {
                    fprintf(stderr, "[N64_VTX_AUDIT] #%d num=%d dest=%d\n", g_vtx_log_count, num_verts, dest_idx);
                    fprintf(stderr, "  PROJ: [%.4f %.4f %.4f %.4f] [%.4f %.4f %.4f %.4f]\n",
                            rsp.projection[0][0], rsp.projection[0][1], rsp.projection[0][2], rsp.projection[0][3],
                            rsp.projection[1][0], rsp.projection[1][1], rsp.projection[1][2], rsp.projection[1][3]);
                    fprintf(stderr, "  PROJ: [%.4f %.4f %.4f %.4f] [%.4f %.4f %.4f %.4f]\n",
                            rsp.projection[2][0], rsp.projection[2][1], rsp.projection[2][2], rsp.projection[2][3],
                            rsp.projection[3][0], rsp.projection[3][1], rsp.projection[3][2], rsp.projection[3][3]);
                    fprintf(stderr, "  MV:   [%.4f %.4f %.4f %.4f] [%.1f %.1f %.1f %.1f]\n",
                            rsp.modelview_stack[rsp.modelview_sp][0][0], rsp.modelview_stack[rsp.modelview_sp][0][1],
                            rsp.modelview_stack[rsp.modelview_sp][0][2], rsp.modelview_stack[rsp.modelview_sp][0][3],
                            rsp.modelview_stack[rsp.modelview_sp][3][0], rsp.modelview_stack[rsp.modelview_sp][3][1],
                            rsp.modelview_stack[rsp.modelview_sp][3][2], rsp.modelview_stack[rsp.modelview_sp][3][3]);
                    fprintf(stderr, "  combined: [%.4f %.4f %.4f %.4f]\n",
                            rsp.combined[0][0], rsp.combined[0][1], rsp.combined[0][2], rsp.combined[0][3]);
                    fprintf(stderr, "            [%.4f %.4f %.4f %.4f]\n",
                            rsp.combined[3][0], rsp.combined[3][1], rsp.combined[3][2], rsp.combined[3][3]);
                    /* First vertex raw + transformed */
                    const u8 *v0raw = src;
                    s16 ob0_t = (s16)((v0raw[0] << 8) | v0raw[1]);
                    s16 ob1_t = (s16)((v0raw[2] << 8) | v0raw[3]);
                    s16 ob2_t = (s16)((v0raw[4] << 8) | v0raw[5]);
                    float xx = (float)ob0_t, yy = (float)ob1_t, zz = (float)ob2_t;
                    float cx = rsp.combined[0][0]*xx + rsp.combined[1][0]*yy + rsp.combined[2][0]*zz + rsp.combined[3][0];
                    float cw = rsp.combined[0][3]*xx + rsp.combined[1][3]*yy + rsp.combined[2][3]*zz + rsp.combined[3][3];
                    fprintf(stderr, "  v0: obj=(%d,%d,%d) clip_x=%.1f clip_w=%.1f ndc_x=%.3f\n",
                            ob0_t, ob1_t, ob2_t, cx, cw, cw != 0 ? cx/cw : 99);
                    g_vtx_log_count++;
                }

                for (int i = 0; i < num_verts && (dest_idx + i) < MAX_VERTICES; i++) {
                    RspVertex *rv = &rsp.vertices[dest_idx + i];
                    /* Vtx_t: ob[3](s16), pad(u16), tc[2](s16), cn[4](u8) = 16 bytes */
                    const u8 *v = src + i * 16;
                    s16 ob0 = (s16)((v[0] << 8) | v[1]);
                    s16 ob1 = (s16)((v[2] << 8) | v[3]);
                    s16 ob2 = (s16)((v[4] << 8) | v[5]);
                    /* v[6..7] = padding */
                    s16 tc0 = (s16)((v[8] << 8) | v[9]);
                    s16 tc1 = (s16)((v[10] << 8) | v[11]);
                    u8 cn0 = v[12], cn1 = v[13], cn2 = v[14], cn3 = v[15];

                    float x = (float)ob0, y = (float)ob1, z = (float)ob2;
                    rv->obj_pos[0] = x;
                    rv->obj_pos[1] = y;
                    rv->obj_pos[2] = z;
                    rv->pos[0] = rsp.combined[0][0]*x + rsp.combined[1][0]*y + rsp.combined[2][0]*z + rsp.combined[3][0];
                    rv->pos[1] = rsp.combined[0][1]*x + rsp.combined[1][1]*y + rsp.combined[2][1]*z + rsp.combined[3][1];
                    rv->pos[2] = rsp.combined[0][2]*x + rsp.combined[1][2]*y + rsp.combined[2][2]*z + rsp.combined[3][2];
                    rv->pos[3] = rsp.combined[0][3]*x + rsp.combined[1][3]*y + rsp.combined[2][3]*z + rsp.combined[3][3];


                    if (rsp.geometry_mode & G_LIGHTING) {
                        /* cn0..cn2 are vertex normal (signed bytes) */
                        float nx = (float)(s8)cn0 / 127.0f;
                        float ny = (float)(s8)cn1 / 127.0f;
                        float nz = (float)(s8)cn2 / 127.0f;

                        float r = rsp.ambient[0];
                        float g = rsp.ambient[1];
                        float b = rsp.ambient[2];

                        for (int li = 0; li < rsp.num_lights && li < MAX_LIGHTS; li++) {
                            float dot = nx * rsp.lights[li].dir[0]
                                      + ny * rsp.lights[li].dir[1]
                                      + nz * rsp.lights[li].dir[2];
                            if (dot > 0.0f) {
                                r += dot * rsp.lights[li].col[0];
                                g += dot * rsp.lights[li].col[1];
                                b += dot * rsp.lights[li].col[2];
                            }
                        }
                        rv->color[0] = fminf(r, 1.0f);
                        rv->color[1] = fminf(g, 1.0f);
                        rv->color[2] = fminf(b, 1.0f);
                        rv->color[3] = (float)cn3 / 255.0f;
                    } else {
                        rv->color[0] = (float)cn0 / 255.0f;
                        rv->color[1] = (float)cn1 / 255.0f;
                        rv->color[2] = (float)cn2 / 255.0f;
                        rv->color[3] = (float)cn3 / 255.0f;
                    }
                    if ((rsp.geometry_mode & G_TEXTURE_GEN) && (rsp.geometry_mode & G_LIGHTING)) {
                        float nx2 = (float)(s8)cn0 / 127.0f;
                        float ny2 = (float)(s8)cn1 / 127.0f;
                        float nz2 = (float)(s8)cn2 / 127.0f;
                        float (*mv)[4] = rsp.modelview_stack[rsp.modelview_sp];
                        float tnx = mv[0][0]*nx2 + mv[1][0]*ny2 + mv[2][0]*nz2;
                        float tny = mv[0][1]*nx2 + mv[1][1]*ny2 + mv[2][1]*nz2;
                        float tnz = mv[0][2]*nx2 + mv[1][2]*ny2 + mv[2][2]*nz2;
                        float nlen = sqrtf(tnx*tnx + tny*tny + tnz*tnz);
                        if (nlen > 0.0001f) { tnx /= nlen; tny /= nlen; tnz /= nlen; }
                        float dx = tnx*rsp.lookat_x[0] + tny*rsp.lookat_x[1] + tnz*rsp.lookat_x[2];
                        float dy = tnx*rsp.lookat_y[0] + tny*rsp.lookat_y[1] + tnz*rsp.lookat_y[2];
                        if (rsp.geometry_mode & G_TEXTURE_GEN_LINEAR) {
                            dx = fmaxf(-1.0f, fminf(1.0f, dx));
                            dy = fmaxf(-1.0f, fminf(1.0f, dy));
                            rv->texcoord[0] = acosf(dx) * (32768.0f / 3.14159265f);
                            rv->texcoord[1] = acosf(dy) * (32768.0f / 3.14159265f);
                        } else {
                            rv->texcoord[0] = dx * 16384.0f + 16384.0f;
                            rv->texcoord[1] = dy * 16384.0f + 16384.0f;
                        }
                    } else {
                        rv->texcoord[0] = (float)tc0;
                        rv->texcoord[1] = (float)tc1;
                    }

                }
            }
            break;

        case OP(G_DL): {
            int is_branch = ((w0 >> 16) & 0xFF) == G_DL_NOPUSH;
            if (gfx_is_n64_region(w1_addr)) {
                if (!is_branch && n64_sp < N64_DL_STACK_SIZE - 1) {
                    n64_stack[n64_sp++] = data + 8;
                }
                data = (const u8 *)w1_addr;
                continue;
            } else {
                /* Target is a PC-format DL (unusual from N64 data) — process it */
                gfx_process_dl((Gfx *)w1_addr);
            }
            break;
        }

        case OP(G_MOVEMEM):
            gfx_sp_movemem(w0, w1_addr);
            break;

        /* ===== SP Immediate commands ===== */
        case OP(G_TRI1): {
            int v0 = ((w1 >> 16) & 0xFF) / 10;
            int v1 = ((w1 >> 8) & 0xFF) / 10;
            int v2 = ((w1 >> 0) & 0xFF) / 10;
            gfx_sp_tri1(v0, v1, v2);
            break;
        }

        case 0xB1: {
            /* Rare's G_TRI4 */
            int v[4][3];
            v[0][0] = (w1 >> 0)  & 0xF;
            v[0][1] = (w1 >> 4)  & 0xF;
            v[0][2] = (w0 >> 0)  & 0xF;
            v[1][0] = (w1 >> 8)  & 0xF;
            v[1][1] = (w1 >> 12) & 0xF;
            v[1][2] = (w0 >> 4)  & 0xF;
            v[2][0] = (w1 >> 16) & 0xF;
            v[2][1] = (w1 >> 20) & 0xF;
            v[2][2] = (w0 >> 8)  & 0xF;
            v[3][0] = (w1 >> 24) & 0xF;
            v[3][1] = (w1 >> 28) & 0xF;
            v[3][2] = (w0 >> 12) & 0xF;

            for (int i = 0; i < 4; i++) {
                if (v[i][0] != v[i][1] || v[i][0] != v[i][2]) {
                    gfx_sp_tri1(v[i][0], v[i][1], v[i][2]);
                }
            }
            break;
        }

        case OP(G_ENDDL):
            if (n64_sp > 0) {
                data = n64_stack[--n64_sp];
                continue;
            }
            g_n64dl_total_cmds += n64_cmd_count;
            g_n64dl_total_tris += (rsp.tri_count - tris_before);
            rsp.n64_data_mode = prev_n64_mode;
            g_diag_n64dl_depth--;  /* WS-01D: unwind depth tracking */
            return;

        case OP(G_POPMTX):
            gfx_sp_pop_matrix(w1);
            break;

        case OP(G_SETGEOMETRYMODE):
            gfx_sp_set_geometry_mode(w1);
            break;

        case OP(G_CLEARGEOMETRYMODE):
            gfx_sp_clear_geometry_mode(w1);
            break;

        case 0xD9:
            rsp.geometry_mode &= (w0 & 0x00FFFFFF);
            rsp.geometry_mode |= w1;
            break;

        case OP(G_TEXTURE):
            gfx_sp_texture(w0, w1);
            break;

        case OP(G_MOVEWORD):
            gfx_sp_moveword(w0, w1_addr);
            break;

        case 0xC0:
            /* Rare's G_SETTEX: texture reference by number, NOT a texture enable. */
            if (w0 != 0 || w1 != 0) {
                gfx_handle_settex(w0, w1);
            }
            break;

        /* ===== DP commands ===== */
        case OP(G_SETCOMBINE):
            gfx_dp_set_combine_mode(w0, w1);
            break;
        case OP(G_SETOTHERMODE_H):
            gfx_dp_set_other_mode(1, w0, w1);
            break;
        case OP(G_SETOTHERMODE_L):
            gfx_dp_set_other_mode(0, w0, w1);
            break;
        case OP(G_RDPSETOTHERMODE):
            rsp.other_mode_hi = w0 & 0x00FFFFFF;
            rsp.other_mode_lo = w1;
            break;
        case OP(G_SETPRIMCOLOR):
            gfx_dp_set_prim_color(w0, w1);
            break;
        case OP(G_SETENVCOLOR):
            gfx_dp_set_env_color(w0, w1);
            break;
        case OP(G_SETFOGCOLOR):
            gfx_dp_set_fog_color(w0, w1);
            break;
        case OP(G_SETBLENDCOLOR):
            rsp.blend_color[0] = (float)((w1 >> 24) & 0xFF) / 255.0f;
            rsp.blend_color[1] = (float)((w1 >> 16) & 0xFF) / 255.0f;
            rsp.blend_color[2] = (float)((w1 >> 8)  & 0xFF) / 255.0f;
            rsp.blend_color[3] = (float)((w1 >> 0)  & 0xFF) / 255.0f;
            break;
        case OP(G_SETFILLCOLOR):
            gfx_dp_set_fill_color(w1);
            break;
        case OP(G_FILLRECT):
            gfx_dp_fill_rectangle(w0, w1);
            break;
        case OP(G_SETSCISSOR):
            gfx_dp_set_scissor(w0, w1);
            break;
        case OP(G_SETCIMG):
            gfx_dp_set_color_image(w0, w1);
            break;
        case OP(G_SETZIMG):
            gfx_dp_set_depth_image(w1);
            break;
        case OP(G_SETTIMG):
            gfx_dp_set_texture_image(w0, w1_addr);
            break;
        case OP(G_SETTILE):
            gfx_dp_set_tile(w0, w1);
            break;
        case OP(G_SETTILESIZE):
            gfx_dp_set_tile_size(w0, w1);
            break;
        case OP(G_LOADBLOCK):
        case OP(G_LOADTILE): {
            int load_tile = (w1 >> 24) & 7;
            rsp.tiles[load_tile].source_addr = rsp.texture_image_addr;
            break;
        }
        case OP(G_LOADTLUT): {
            int tlut_cnt = ((w1 >> 14) & 0x3FF) + 1;
            if (tlut_cnt > 256) tlut_cnt = 256;
            if (rsp.texture_image_addr) {
                memcpy(rsp.tlut, (const u8 *)rsp.texture_image_addr, tlut_cnt * 2);
            }
            rsp.tlut_count = tlut_cnt;
            break;
        }
        case OP(G_RDPLOADSYNC):
        case OP(G_RDPPIPESYNC):
        case OP(G_RDPTILESYNC):
        case OP(G_RDPFULLSYNC):
            break;
        case OP(G_TEXRECT):
        case OP(G_TEXRECTFLIP):
            data += 16; /* skip 2 extra 8-byte words for texrect */
            break;
        case OP(G_RDPHALF_1):
        case OP(G_RDPHALF_2):
        case OP(G_SPNOOP):
            break;
        default:
            /* WS-01D: Track unknown N64 DL opcodes */
            g_diag_unknown_total++;
            if (g_diag_num_unique_unknowns < DIAG_MAX_UNIQUE_OPCODES) {
                int found = 0;
                for (int ui = 0; ui < g_diag_num_unique_unknowns; ui++) {
                    if (g_diag_unknown_opcodes[ui] == opcode) {
                        g_diag_unknown_opcode_counts[ui]++;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    int idx = g_diag_num_unique_unknowns++;
                    g_diag_unknown_opcodes[idx] = opcode;
                    g_diag_unknown_opcode_counts[idx] = 1;
                    fprintf(stderr, "[GFX_DIAG] N64 DL unknown opcode 0x%02X "
                            "(w0=0x%08X w1=0x%08X) first seen at cmd #%d\n",
                            opcode, w0, w1, n64_cmd_count);
                }
            }
            break;
        }

        data += 8;
    }
    /* Fall-through exit (zero-run or cmd limit) */
    g_n64dl_total_cmds += n64_cmd_count;
    g_n64dl_total_tris += (rsp.tri_count - tris_before);
    rsp.n64_data_mode = prev_n64_mode;
    g_diag_n64dl_depth--;  /* WS-01D: unwind depth tracking */
    #undef N64_DL_STACK_SIZE
}

/* ===== Public API ===== */

void gfx_init(void) {
    memset(&rsp, 0, sizeof(rsp));
    memset(gfx_segment_table, 0, sizeof(gfx_segment_table));
    memset(gfx_ptr_keys, 0, sizeof(gfx_ptr_keys));
    memset(gfx_ptr_vals, 0, sizeof(gfx_ptr_vals));

    mtx_identity(rsp.projection);
    mtx_identity(rsp.modelview_stack[0]);
    mtx_identity(rsp.combined);

    rsp.prim_color[0] = rsp.prim_color[1] = rsp.prim_color[2] = rsp.prim_color[3] = 1.0f;
    rsp.env_color[0] = rsp.env_color[1] = rsp.env_color[2] = rsp.env_color[3] = 1.0f;
    rsp.fill_color[0] = rsp.fill_color[1] = rsp.fill_color[2] = rsp.fill_color[3] = 1.0f;

}

void gfx_set_window_size(int w, int h) {
    GFX_WINDOW_W = w;
    GFX_WINDOW_H = h;
}

void gfx_run_dl(Gfx *dl) {
    /* Reset per-frame state (N64 RSP resets all state at task start) */
    rsp.dl_sp = 0;
    rsp.tri_count = 0;
    rsp.dl_cmd_count = 0;
    rsp.modelview_sp = 0;
    rsp.combined_dirty = 1;
    rsp.vp_set = 0;
    rsp.persp_set = 0;
    rsp.n64_data_mode = 0;
    rsp.current_tex_id = 0;
    rsp.current_tex_w = 1.0f;
    rsp.current_tex_h = 1.0f;
    rsp.geometry_mode = 0;
    rsp.texture_on = 0;
    rsp.diag_tex_tris = 0;
    rsp.diag_notex_tris = 0;
    rsp.diag_shade_tris = 0;
    rsp.diag_fog_tris = 0;

    rsp.diag_avg_bright = 0.0f;
    rsp.diag_vert_count = 0;

    /* Default ambient so geometry is visible even before light commands arrive */
    rsp.ambient[0] = 0.7f;
    rsp.ambient[1] = 0.7f;
    rsp.ambient[2] = 0.7f;
    rsp.num_lights = 0;
    rsp.persp_norm = 0; /* will be set by gSPPerspNormalize */

    /* Reset N64 DL counters */
    g_n64dl_call_count = 0;
    g_n64dl_total_cmds = 0;
    g_n64dl_total_tris = 0;
    g_pc_gdl_followed = 0;
    g_pc_gdl_skipped = 0;
    g_pc_gdl_null = 0;
    g_pc_gdl_to_n64 = 0;

    /* WS-01D: Reset per-frame diagnostic counters */
    g_diag_frame_count++;
    g_diag_num_unique_unknowns = 0;
    g_diag_unknown_total = 0;
    g_diag_n64dl_depth = 0;
    memset(g_diag_unknown_opcodes, 0, sizeof(g_diag_unknown_opcodes));
    memset(g_diag_unknown_opcode_counts, 0, sizeof(g_diag_unknown_opcode_counts));

    mtx_identity(rsp.projection);
    mtx_identity(rsp.modelview_stack[0]);

    /* Reset per-frame texture binding state */
    tex_bound_id = 0;
    if (tex_gl_enabled) {
        glDisable(GL_TEXTURE_2D);
        tex_gl_enabled = 0;
    }
    blend_mode_cur = -1;
    polygon_offset_on = 0;

    /* Set up GL state for rendering */
    glViewport(0, 0, GFX_WINDOW_W, GFX_WINDOW_H);
    glDepthMask(GL_TRUE);  /* ensure depth clear works after XLU frames */
    /* Clear to background color — set by gfx_set_clear_color before DL submission */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(g_clearColor[0], g_clearColor[1], g_clearColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(0x864F);  /* GL_DEPTH_CLAMP — prevents near-plane artifacts (libultraship pattern) */
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_CULL_FACE);  /* Software culling in Phase 2 — GL cull doesn't work with clip-space input */

    /* Process the display list */
    gfx_process_dl(dl);

    /* WS-01D: Per-frame diagnostic summary (every 60 frames for first 600, then quiet) */
    if (g_diag_frame_count <= 600 && (g_diag_frame_count % 60) == 0) {
        if (g_diag_unknown_total > 0) {
            fprintf(stderr, "[GFX_DIAG] frame %d: %d unknown N64 opcodes (%d unique):",
                    g_diag_frame_count, g_diag_unknown_total, g_diag_num_unique_unknowns);
            for (int ui = 0; ui < g_diag_num_unique_unknowns; ui++) {
                fprintf(stderr, " 0x%02X(%d)", g_diag_unknown_opcodes[ui],
                        g_diag_unknown_opcode_counts[ui]);
            }
            fprintf(stderr, "\n");
        }
        if (g_diag_bad_matrix_count > 0) {
            fprintf(stderr, "[GFX_DIAG] frame %d: %d bad matrix events (cumulative)\n",
                    g_diag_frame_count, g_diag_bad_matrix_count);
        }
        if (g_diag_vtx_overflow_count > 0) {
            fprintf(stderr, "[GFX_DIAG] frame %d: %d vertex buffer overflows (cumulative)\n",
                    g_diag_frame_count, g_diag_vtx_overflow_count);
        }
    }
    /* Print once on frame 1 unconditionally as a startup marker */
    if (g_diag_frame_count == 1) {
        fprintf(stderr, "[GFX_DIAG] diagnostic instrumentation active (WS-01D)\n");
    }

    /* Clean up */
    if (tex_gl_enabled) {
        glDisable(GL_TEXTURE_2D);
        tex_gl_enabled = 0;
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
}

void gfx_end_frame(void) {
}
