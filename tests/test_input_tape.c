/*
 * ROM-free unit test for the .ge7tape reader/writer (FID-0034, Task 0.6 Step 1).
 *
 * Writes a synthetic 600-tick tape through the writer API, reads it back, and
 * asserts a byte-exact roundtrip of both the header and every per-tick record.
 * No game headers, no ROM: exercises only the pure serialisation half of
 * input_tape.c.
 */
#include "input_tape.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TICKS       600u
#define TEST_PLAYERS     4u
#define TEST_FIRST_TICK  17u   /* start g_GlobalTimer-style tick at a nonzero base */

/* Deterministic synthetic pad for (tick, player) — spans the full u16/s8 range. */
static InputTapePad synth_pad(uint32_t t, uint32_t p) {
    InputTapePad pad;
    pad.button  = (uint16_t)((t * 131u + p * 7919u) & 0xFFFFu);
    pad.stick_x = (int8_t)((int)((t * 3u + p * 11u) & 0xFF) - 128);
    pad.stick_y = (int8_t)((int)((t * 5u + p * 13u) & 0xFF) - 128);
    return pad;
}

int main(void) {
    char path[] = "test_input_tape_XXXXXX.ge7tape";
    /* mkstemp-free: a fixed name under the ctest CWD is fine for a serial test. */
    snprintf(path, sizeof path, "test_input_tape_%d.ge7tape", (int)0);

    InputTapeHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    memcpy(hdr.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN);
    hdr.version          = GE7TAPE_VERSION;
    hdr.level_id         = 0x22;        /* Dam LEVELID */
    hdr.difficulty       = 1;
    hdr.rng_seed         = GE7TAPE_DETERMINISTIC_SEED;
    hdr.tick_hz          = GE7TAPE_TICK_HZ;
    hdr.flags            = 0;
    hdr.num_players      = TEST_PLAYERS;
    hdr.controller_count = 1;
    /* tick_count intentionally left 0; the writer owns it. */

    /* ---- Write ---- */
    InputTapeWriter *w = inputTapeWriterOpen(path, &hdr);
    assert(w != NULL);
    for (uint32_t t = 0; t < TEST_TICKS; t++) {
        InputTapePad pads[TEST_PLAYERS];
        for (uint32_t p = 0; p < TEST_PLAYERS; p++) {
            pads[p] = synth_pad(t, p);
        }
        int rc = inputTapeWriterAppendTick(w, TEST_FIRST_TICK + t, pads, TEST_PLAYERS);
        assert(rc == 0);
    }
    assert(inputTapeWriterClose(w) == 0);

    /* ---- Read back ---- */
    InputTape *tape = inputTapeRead(path);
    assert(tape != NULL);

    /* Header roundtrip: every field byte-identical. */
    assert(memcmp(tape->header.magic, GE7TAPE_MAGIC, GE7TAPE_MAGIC_LEN) == 0);
    assert(tape->header.version          == GE7TAPE_VERSION);
    assert(tape->header.level_id         == 0x22u);
    assert(tape->header.difficulty       == 1u);
    assert(tape->header.rng_seed         == GE7TAPE_DETERMINISTIC_SEED);
    assert(tape->header.tick_hz          == GE7TAPE_TICK_HZ);
    assert(tape->header.flags            == 0u);
    assert(tape->header.num_players      == TEST_PLAYERS);
    assert(tape->header.controller_count == 1u);
    assert(tape->header.tick_count       == TEST_TICKS);

    /* Record roundtrip: tick indices and every pad byte-identical. */
    for (uint32_t t = 0; t < TEST_TICKS; t++) {
        assert(tape->ticks[t] == TEST_FIRST_TICK + t);
        for (uint32_t p = 0; p < TEST_PLAYERS; p++) {
            InputTapePad expect = synth_pad(t, p);
            InputTapePad got    = tape->pads[t * TEST_PLAYERS + p];
            assert(got.button  == expect.button);
            assert(got.stick_x == expect.stick_x);
            assert(got.stick_y == expect.stick_y);
        }
    }

    inputTapeFree(tape);
    remove(path);

    printf("test_input_tape: OK (%u ticks, %u players, roundtrip byte-exact)\n",
           TEST_TICKS, TEST_PLAYERS);
    return 0;
}
