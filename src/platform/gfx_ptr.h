/**
 * gfx_ptr.h — 64-bit pointer lookup table for GBI display list addresses.
 *
 * On 64-bit PC, GBI macros truncate pointers to 32-bit (unsigned int).
 * This table records full 64-bit pointers so the GBI translator can
 * resolve them. Game code calls gfx_ptr_store() via overridden macros;
 * the translator calls gfx_ptr_resolve() when reading addresses from
 * display list commands.
 *
 * Uses linear probing (4 slots) to handle hash collisions.
 */
#ifndef GFX_PTR_H
#define GFX_PTR_H

#include <stdint.h>

#define GFX_PTR_TABLE_BITS 16
#define GFX_PTR_TABLE_SIZE (1 << GFX_PTR_TABLE_BITS)
#define GFX_PTR_TABLE_MASK (GFX_PTR_TABLE_SIZE - 1)
#define GFX_PTR_PROBE_MAX  4

/* Segment shadow table — full 64-bit segment base addresses */
extern uintptr_t gfx_segment_table[16];

/* Pointer table — open-addressed hash of truncated→full mappings */
extern uint32_t  gfx_ptr_keys[GFX_PTR_TABLE_SIZE];
extern uintptr_t gfx_ptr_vals[GFX_PTR_TABLE_SIZE];

static inline void gfx_ptr_store(const void *ptr) {
    uintptr_t full = (uintptr_t)ptr;
    uint32_t key = (uint32_t)full;
    uint32_t base_idx = ((key >> 2) ^ (key >> 16)) & GFX_PTR_TABLE_MASK;

    /* Check if key already exists in probe window */
    for (int p = 0; p < GFX_PTR_PROBE_MAX; p++) {
        uint32_t idx = (base_idx + p) & GFX_PTR_TABLE_MASK;
        if (gfx_ptr_keys[idx] == key || gfx_ptr_keys[idx] == 0) {
            gfx_ptr_keys[idx] = key;
            gfx_ptr_vals[idx] = full;
            return;
        }
    }
    /* All probe slots occupied — evict first slot */
    gfx_ptr_keys[base_idx] = key;
    gfx_ptr_vals[base_idx] = full;
}

/* Drop every mapping whose full pointer falls in [lo, hi). Call before freeing
 * a heap region that pointers were stored from (e.g. a retired texture-arena
 * chunk) so a later resolve can never hand back freed memory. Zeroing a slot
 * mid-probe-chain can only cause a later benign miss (which re-resolves), never
 * a stale hit -- so it is safe without tombstones. */
static inline void gfx_ptr_invalidate_range(uintptr_t lo, uintptr_t hi) {
    uint32_t i;
    if (hi <= lo) {
        return;
    }
    for (i = 0; i < GFX_PTR_TABLE_SIZE; i++) {
        if (gfx_ptr_keys[i] != 0 && gfx_ptr_vals[i] >= lo && gfx_ptr_vals[i] < hi) {
            gfx_ptr_keys[i] = 0;
            gfx_ptr_vals[i] = 0;
        }
    }
}

static inline void *gfx_ptr_resolve(uint32_t key) {
    if (key == 0) return NULL;
    uint32_t base_idx = ((key >> 2) ^ (key >> 16)) & GFX_PTR_TABLE_MASK;
    for (int p = 0; p < GFX_PTR_PROBE_MAX; p++) {
        uint32_t idx = (base_idx + p) & GFX_PTR_TABLE_MASK;
        if (gfx_ptr_keys[idx] == key) {
            return (void *)gfx_ptr_vals[idx];
        }
        if (gfx_ptr_keys[idx] == 0) break; /* Empty slot = end of chain */
    }
    return NULL;
}

/**
 * Resolve a segment address: (segment << 24) | offset
 * If top nibble is 0x01-0x0F, it's a segment address.
 * Otherwise try the pointer table.
 */
static inline void *gfx_resolve_addr(uint32_t addr) {
    if (addr == 0) return NULL;
    uint32_t seg = (addr >> 24) & 0x0F;
    if (seg > 0 && gfx_segment_table[seg] != 0) {
        uint32_t offset = addr & 0x00FFFFFF;
        return (void *)(gfx_segment_table[seg] + offset);
    }
    void *resolved = gfx_ptr_resolve(addr);
    if (resolved) return resolved;
    /* Cannot resolve — return NULL instead of casting raw N64 address.
     * Callers must check for NULL. */
    return NULL;
}

#endif /* GFX_PTR_H */
