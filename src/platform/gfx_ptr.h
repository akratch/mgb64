/**
 * gfx_ptr.h — 64-bit pointer lookup table for GBI display list addresses.
 *
 * On 64-bit PC, GBI macros truncate pointers to 32-bit (unsigned int).
 * This table records full 64-bit pointers so the GBI translator can
 * resolve them. Game code calls gfx_ptr_store() via overridden macros;
 * the translator calls gfx_ptr_resolve() when reading addresses from
 * display list commands.
 *
 * Open addressing over the whole table with a per-slot state byte
 * (EMPTY/OCCUPIED/TOMBSTONE). Insert is lossless — it never silently evicts a
 * live mapping — and range invalidation tombstones (rather than zeroes) slots so
 * a deletion mid-probe-chain can never hide a still-live mapping (AUDIT-0009).
 */
#ifndef GFX_PTR_H
#define GFX_PTR_H

#include <stddef.h>
#include <stdint.h>

#define GFX_PTR_TABLE_BITS 16
#define GFX_PTR_TABLE_SIZE (1 << GFX_PTR_TABLE_BITS)
#define GFX_PTR_TABLE_MASK (GFX_PTR_TABLE_SIZE - 1)
#define GFX_PTR_PROBE_MAX  4   /* legacy warn threshold; see gfx_ptr_max_probe */

/* Slot occupancy for gfx_ptr_state[] (open addressing with tombstones). */
#define GFX_PTR_EMPTY     0u
#define GFX_PTR_OCCUPIED  1u
#define GFX_PTR_TOMBSTONE 2u

/* Segment shadow table — full 64-bit segment base addresses */
extern uintptr_t gfx_segment_table[16];

/* Pointer table — open-addressed hash of truncated→full mappings.
 * gfx_ptr_state[] records each slot's EMPTY/OCCUPIED/TOMBSTONE status so a
 * deletion mid-probe-chain can tombstone (not zero) the slot, keeping later
 * live mappings reachable, and so store never silently evicts (AUDIT-0009). */
extern uint32_t  gfx_ptr_keys[GFX_PTR_TABLE_SIZE];
extern uintptr_t gfx_ptr_vals[GFX_PTR_TABLE_SIZE];
extern uint8_t   gfx_ptr_state[GFX_PTR_TABLE_SIZE];

/* Render-health telemetry (AUDIT-0009); read by diagnostics, never gates sim. */
extern uint32_t  gfx_ptr_ambiguous;   /* two live full ptrs shared one low-32 */
extern uint32_t  gfx_ptr_full_fails;  /* insert refused: table 100% occupied */
extern uint32_t  gfx_ptr_max_probe;   /* longest probe distance ever walked */

static inline void gfx_ptr_store(const void *ptr) {
    uintptr_t full = (uintptr_t)ptr;
    uint32_t key = (uint32_t)full;
    uint32_t base_idx = ((key >> 2) ^ (key >> 16)) & GFX_PTR_TABLE_MASK;
    uint32_t free_idx = 0;
    int have_free = 0;
    uint32_t p;

    /* Open-addressing probe over the whole table. Reuse the first EMPTY or
     * TOMBSTONE slot, but keep scanning until an EMPTY terminates the chain so
     * an already-live key is always found first (lossless -- never evicts). */
    for (p = 0; p < GFX_PTR_TABLE_SIZE; p++) {
        uint32_t idx = (base_idx + p) & GFX_PTR_TABLE_MASK;
        uint8_t st = gfx_ptr_state[idx];
        if (st == GFX_PTR_OCCUPIED) {
            if (gfx_ptr_keys[idx] == key) {
                if (gfx_ptr_vals[idx] == full) {
                    return;                 /* duplicate mapping: stable no-op */
                }
                /* Two live full pointers share one low-32 token: ambiguous.
                 * Fail closed -- keep the incumbent, never overwrite. */
                gfx_ptr_ambiguous++;
                return;
            }
            continue;                       /* different key: keep probing */
        }
        if (!have_free) {
            free_idx = idx;                 /* first reusable (EMPTY/TOMBSTONE) slot */
            have_free = 1;
        }
        if (st == GFX_PTR_EMPTY) {
            if (p > gfx_ptr_max_probe) {
                gfx_ptr_max_probe = p;
            }
            gfx_ptr_keys[free_idx] = key;
            gfx_ptr_vals[free_idx] = full;
            gfx_ptr_state[free_idx] = GFX_PTR_OCCUPIED;
            return;
        }
        /* TOMBSTONE: remembered as free_idx above; keep scanning for a live match. */
    }

    /* Scanned every slot with no EMPTY terminator. */
    if (have_free) {
        gfx_ptr_keys[free_idx] = key;
        gfx_ptr_vals[free_idx] = full;
        gfx_ptr_state[free_idx] = GFX_PTR_OCCUPIED;
        return;
    }
    gfx_ptr_full_fails++;                    /* table full: refuse, no eviction */
}

/* Drop every mapping whose full pointer falls in [lo, hi). Call before freeing
 * a heap region that pointers were stored from (e.g. a retired texture-arena
 * chunk) so a later resolve can never hand back freed memory. A deleted slot
 * becomes a TOMBSTONE, not EMPTY, so it never truncates a probe chain and hides
 * a still-live mapping stored later in the same chain (AUDIT-0009). */
static inline void gfx_ptr_invalidate_range(uintptr_t lo, uintptr_t hi) {
    uint32_t i;
    if (hi <= lo) {
        return;
    }
    for (i = 0; i < GFX_PTR_TABLE_SIZE; i++) {
        if (gfx_ptr_state[i] == GFX_PTR_OCCUPIED &&
            gfx_ptr_vals[i] >= lo && gfx_ptr_vals[i] < hi) {
            gfx_ptr_keys[i] = 0;
            gfx_ptr_vals[i] = 0;
            gfx_ptr_state[i] = GFX_PTR_TOMBSTONE;
        }
    }
}

static inline void *gfx_ptr_resolve(uint32_t key) {
    if (key == 0) return NULL;
    uint32_t base_idx = ((key >> 2) ^ (key >> 16)) & GFX_PTR_TABLE_MASK;
    uint32_t p;
    for (p = 0; p < GFX_PTR_TABLE_SIZE; p++) {
        uint32_t idx = (base_idx + p) & GFX_PTR_TABLE_MASK;
        uint8_t st = gfx_ptr_state[idx];
        if (st == GFX_PTR_EMPTY) {
            break;                          /* end of chain: genuine miss */
        }
        if (st == GFX_PTR_OCCUPIED && gfx_ptr_keys[idx] == key) {
            return (void *)gfx_ptr_vals[idx];
        }
        /* TOMBSTONE or non-matching OCCUPIED: keep probing. */
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
