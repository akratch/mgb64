/* ROM-free unit test for the sim-state invariance hash (remaster P0.2 rail). */
#include "sim_state_hash.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    /* 1. Determinism: identical bytes -> identical hash across calls. */
    unsigned char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (unsigned char)(i * 7);
    SimHashRegion r1[1] = {{"buf", buf, sizeof buf}};
    uint64_t h1 = sim_state_hash_compute(r1, 1);
    uint64_t h2 = sim_state_hash_compute(r1, 1);
    assert(h1 == h2);

    /* 2. Sensitivity: a single-byte change flips the hash. */
    buf[100] ^= 0xFF;
    assert(sim_state_hash_compute(r1, 1) != h1);
    buf[100] ^= 0xFF;
    assert(sim_state_hash_compute(r1, 1) == h1);

    /* 3. Pointer normalization: the SAME logical layout at a DIFFERENT base
     *    address hashes identically. Regions share a name so only the pointer
     *    canonicalization is under test. */
    unsigned char a[64] = {0}, b[64] = {0};
    *(void **)(a + 8) = (void *)(a + 40);   /* self-pointer offset 8 -> offset 40 */
    *(void **)(b + 8) = (void *)(b + 40);   /* identical layout, different base    */
    SimHashRegion ra[1] = {{"r", a, sizeof a}};
    SimHashRegion rb[1] = {{"r", b, sizeof b}};
    assert(sim_state_hash_compute(ra, 1) == sim_state_hash_compute(rb, 1));

    /* 4. Pointer sensitivity: a pointer to a DIFFERENT in-region offset diverges. */
    unsigned char c[64] = {0};
    *(void **)(c + 8) = (void *)(c + 16);   /* -> offset 16, not 40 */
    SimHashRegion rc[1] = {{"r", c, sizeof c}};
    assert(sim_state_hash_compute(rc, 1) != sim_state_hash_compute(ra, 1));

    /* 5. Region identity: renaming a region changes the hash (region-set guard). */
    SimHashRegion rn[1] = {{"renamed", a, sizeof a}};
    assert(sim_state_hash_compute(rn, 1) != sim_state_hash_compute(ra, 1));

    printf("test_sim_state_hash: OK\n");
    return 0;
}
