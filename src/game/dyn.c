#include <ultra64.h>
#include "dyn.h"
#include <token.h>
#include <str.h>
#include <memp.h>
#include <macro.h>

#ifdef NATIVE_PORT
extern void debTryAdd(void *data, const char *name);
extern s32 getPlayerCount(void);
#endif

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx
 * argument. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

u8 *g_GfxBuffers[3];
u8 *g_VtxBuffers[3];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
s32 g_GfxRequestedDisplayList;
s32 D_800482E0 = 0;
#ifdef NATIVE_PORT
/* 64-bit: Gfx commands are 16 bytes (vs 8 on N64), so double the buffer sizes.
 * Additionally, brute-force room rendering (no portal culling) needs more space. */
s32 g_GfxSizesByPlayerCount[] = {0x40000, 0x60000, 0x80000, 0xA0000};
s32 g_VtxSizesByPlayerCount[] = {0x40000, 0x60000, 0x80000, 0xA0000};
#else
s32 g_GfxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
s32 g_VtxSizesByPlayerCount[] = {0x10000, 0x18000, 0x20000, 0x28000};
#endif

char membars_string1[] = ">>>>>>>>>>>>>>>>>>>>>>>>>";
char membars_string2[] = "=========================";
char membars_string3[] = "-------------------------";

void dynInit(void) {
    debTryAdd(&D_800482E0, "dyn_c_debug");
}

void dynInitMemory(void) {
    if (tokenFind(1, "-mgfx")) {
        g_GfxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mgfx"), NULL, 0) * 1024;
    }
    if (tokenFind(1, "-mvtx")) {
        g_VtxSizesByPlayerCount[getPlayerCount() - 1] = strtol(tokenFind(1, "-mvtx"), NULL, 0) * 1024;
    }

#ifdef NATIVE_PORT
    /* PC: Use malloc for DL buffers. Hardcode 512KB per buffer — plenty for
     * 16-byte Gfx commands + brute-force room rendering (no portal culling). */
    {
        s32 gfxSize = 512 * 1024;
        /* 512KB (was 256KB): matrices, sub-DLs and vertices all bump-allocate
         * from this one pool, and a heavy glass shatter allocates up to ~200
         * matrices late in the frame. The extra headroom keeps every shard on a
         * distinct matrix (instead of the dynAllocateMatrix overflow scratch)
         * and covers worst-case 2-player shared-pool pressure. */
        s32 vtxSize = 512 * 1024;
        g_GfxBuffers[0] = (u8 *)malloc(gfxSize * 2);
        g_GfxBuffers[1] = (g_GfxBuffers[0] + gfxSize);
        g_GfxBuffers[2] = (g_GfxBuffers[1] + gfxSize);
        g_VtxBuffers[0] = (u8 *)malloc(vtxSize * 2);
        g_VtxBuffers[1] = (g_VtxBuffers[0] + vtxSize);
        g_VtxBuffers[2] = (g_VtxBuffers[1] + vtxSize);
        memset(g_GfxBuffers[0], 0, gfxSize * 2);
        memset(g_VtxBuffers[0], 0, vtxSize * 2);
        /* Register the current PC DL and VTX pools so gfx_process_dl can
         * validate G_DL targets without relying on stale heap addresses.
         * dynAllocate stores sub-DLs, matrices, and vertices in VTX space
         * (g_GfxMemPos starts at g_VtxBuffers[0]), so the renderer treats
         * the current VTX pool as a possible native DL source after an opcode
         * plausibility check. */
        {
            extern void gfx_set_pc_dl_range(void *start, size_t size);
            extern void gfx_set_pc_vtx_range(void *start, size_t size);
            gfx_set_pc_dl_range(g_GfxBuffers[0], gfxSize * 2);
            gfx_set_pc_vtx_range(g_VtxBuffers[0], vtxSize * 2);
        }
        printf("[DYN] PC malloc'd GFX=%dKB×2 VTX=%dKB×2 gfx=[%p,%p) vtx=%p (playercount=%d)\n",
               gfxSize/1024, vtxSize/1024,
               (void*)g_GfxBuffers[0], (void*)g_GfxBuffers[2],
               (void*)g_VtxBuffers[0], getPlayerCount());
    }
#else
    g_GfxBuffers[0] = mempAllocBytesInBank(g_GfxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_GfxBuffers[1] = (g_GfxBuffers[0] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);
    g_GfxBuffers[2] = (g_GfxBuffers[1] + g_GfxSizesByPlayerCount[getPlayerCount() - 1]);

    g_VtxBuffers[0] = mempAllocBytesInBank(g_VtxSizesByPlayerCount[getPlayerCount() - 1] * 2, MEMPOOL_STAGE);
    g_VtxBuffers[1] = (g_VtxBuffers[0] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
    g_VtxBuffers[2] = (g_VtxBuffers[1] + g_VtxSizesByPlayerCount[getPlayerCount() - 1]);
#endif

    g_GfxActiveBufferIndex = 0;
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[0];
}

Gfx *dynGetMasterDisplayList(void) {
    g_GfxRequestedDisplayList = TRUE;

    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex];
}

s32 dynGetFreeGfx2(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

void/*Vtx?*/ *dynAllocate7F0BD6C4(s32 count) {
    void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * 0x10/*sizeof(Vtx)?*/;
	return ptr;
}

Mtx *dynAllocateMatrix(void)
{
#ifdef NATIVE_PORT
	/* Unbounded bump allocator: a heavy frame -- e.g. a window shattering into
	 * many glass shards, each allocating a matrix late in lvlRender -- can push
	 * g_GfxMemPos past the VTX buffer end. Hand out a reusable scratch matrix on
	 * overflow instead of an out-of-bounds pointer that the caller then writes
	 * 64 bytes into (the sub_GAME_7F0A2C44 SEGV). */
	static Mtx s_overflowScratch;
	if (g_GfxMemPos + sizeof(Mtx) > g_VtxBuffers[g_GfxActiveBufferIndex + 1]) {
		static s32 overflow_count = 0;
		if (++overflow_count <= 5) {
			printf("[DYN] MTX overflow (count=%d): VTX free=%ld bytes\n", overflow_count,
				   (long)(g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos));
		}
		return &s_overflowScratch;
	}
#endif
	{
		void *ptr = g_GfxMemPos;
		g_GfxMemPos += sizeof(Mtx);
		return ptr;
	}
}

void/*Light?*/ *dynAllocate7F0BD6F8(s32 count) {
    void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * 0x10/*sizeof(Light)?*/;
	return ptr;
}

void *dynAllocate(s32 size) {
    void *ptr = g_GfxMemPos;
	size = ALIGN16_a(size);
#ifdef NATIVE_PORT
	if (g_GfxMemPos + size > g_VtxBuffers[g_GfxActiveBufferIndex + 1]) {
		static s32 overflow_count = 0;
		if (++overflow_count <= 5) {
			printf("[DYN] VTX overflow: need %d, have %ld\n", size,
				   (long)(g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos));
		}
		return ptr; /* return current pos but don't advance past end */
	}
#endif
	g_GfxMemPos += size;
	return ptr;
}

void dynSwapBuffers(void) {
    g_GfxActiveBufferIndex = (g_GfxActiveBufferIndex ^ 1);
    g_GfxRequestedDisplayList = FALSE;
    g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
}

void dynRemovedFunc(Gfx *gdl) {
}

s32 dynGetFreeGfx(Gfx *gdl) {
    return (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

s32 dynGetFreeVtx(void) {
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Address 0x7F0BD7CC NTSC
void dynCalculateMembarLength(const char* arg0, f32 arg1, f32 arg2)
{
    s32 len;
    f32 zero = 0;
    
    len = strlen(arg0);
    
    arg1 /= arg2;
    
    if(zero);
    
    if (arg1 < zero && len > 1)
    {
        if (len > 1)
        {
            
        }
    }
}

void dynDrawMembars(Gfx *gdl) {
    dynCalculateMembarLength(membars_string2, ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl), ((Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - (Gfx*)g_GfxBuffers[g_GfxActiveBufferIndex]));
    dynCalculateMembarLength(membars_string2, (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos), (g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex]));
}
