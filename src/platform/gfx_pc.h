/**
 * gfx_pc.h — GBI display list translator public API.
 *
 * This header provides the interface between the game code and the
 * fast3d display list interpreter. The implementation is in fast3d/gfx_pc.c.
 */
#ifndef GFX_PC_H
#define GFX_PC_H

#include <stddef.h>
#include <stdint.h>
#include <ultra64.h>

struct GfxDimensions {
    uint32_t width, height;
    float aspect_ratio;
};

extern struct GfxDimensions gfx_current_dimensions;

void gfx_init(void);
void gfx_run_dl(Gfx *dl);
void gfx_end_frame(void);

/**
 * Register a memory region containing N64-format display list data
 * (8-byte big-endian commands). The GFX translator will automatically
 * use the N64 DL interpreter when jumping to addresses in these regions.
 */
void gfx_register_n64_dl_region(void *addr, size_t size);
void gfx_clear_n64_dl_regions(void);
void gfx_clear_texture_cache(void);
void gfx_register_extra_pc_dl(void *addr, size_t size);
void gfx_set_pc_vtx_range(void *start, size_t size);
void gfx_register_pc_vertex_region(void *addr, size_t size);

/* Set the background clear color for gfx_run_dl (0-255 range). */
void gfx_set_clear_color(int r, int g, int b);

/**
 * Register a memory range containing raw float[4][4] matrices that will be
 * passed to gSPMatrix with G_MTX_FLOAT_PORT flag.
 * (Currently a no-op — float matrices detected via param byte flag.)
 */
void gfx_register_float_matrix_range(void *start, size_t size);

/**
 * Register the PC Gfx display list buffer range (from dyn.c).
 * Any G_DL target within this range is treated as native PC Gfx.
 * Anything outside this range (and outside the executable image) is N64 binary.
 */
void gfx_set_pc_dl_range(void *start, size_t size);

/* Legacy: called by platform_sdl.c on window resize (no-op) */
void gfx_set_window_size(int w, int h);

/* Process an N64 binary display list (big-endian, from ROM) */
void gfx_process_n64_dl(const uint8_t *data);

/* Register a guard/chrprop model DL region for execution-time P×MV detection.
 * Mirrors gfx_register_weapon_dl_region — called from model loading code. */
void gfx_register_guard_dl_region(void *addr, size_t size);
void gfx_register_visibility_scaled_matrix_region(void *addr, size_t size);

/* Drawing classification for diagnostics.
 * Top-level renderers tag which subsystem is active so that per-triangle
 * and per-matrix logging can identify the source of pathological geometry. */
enum DrawClass {
    DRAWCLASS_UNKNOWN = 0,
    DRAWCLASS_ROOM,
    DRAWCLASS_WEAPON,
    DRAWCLASS_CHRPROP,
    DRAWCLASS_EFFECT,
    DRAWCLASS_HUD,
};
void gfx_set_draw_class(enum DrawClass cls);
void gfx_set_prop_context(const void *prop,
                          int prop_type,
                          int obj_type,
                          int pad,
                          uint32_t flags,
                          uint32_t flags2,
                          const void *model,
                          int stan_room,
                          int roomid,
                          int renderpass,
                          int withalpha);
void gfx_clear_prop_context(void);
void gfx_register_effect_dl_range(const char *label, const void *start, const void *end);

/* Synchronize rendering state for sky triangles.  Must be called BEFORE
 * gfx_draw_sky_triangle() to ensure the correct texture, combiner, and
 * blend state — because the game's GBI commands (texSelect, SetCombine)
 * are written to the display list buffer and haven't been processed yet. */
void gfx_prepare_sky_rendering(uint32_t texture_num, uint8_t env_r, uint8_t env_g, uint8_t env_b);

/* Submit a sky triangle directly from game code (NATIVE_PORT only).
 * The original RDP path rasterizes using SkyRelated38's screen-space
 * coordinates after the game has already clamped them to the current
 * player viewport, so the native path takes screen-space x/y while
 * preserving the original clip-space z/w for depth and perspective.
 *
 * Used by player.c skyRender() to replace raw RDP triangle commands
 * that the DL interpreter cannot process. */
void gfx_draw_sky_triangle(
    float sx0, float sy0, float z0, float w0,
    uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
    float u0, float v0,
    float sx1, float sy1, float z1, float w1,
    uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1,
    float u1, float v1,
    float sx2, float sy2, float z2, float w2,
    uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2,
    float u2, float v2);

#endif /* GFX_PC_H */
