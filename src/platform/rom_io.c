/**
 * rom_io.c — ROM file loading and PC-specific DMA implementation.
 *
 * On N64, ROM data is accessed via the PI (Parallel Interface) using DMA.
 * On PC, we load the entire .z64 ROM into a malloc'd buffer and serve
 * all DMA requests via memcpy from that buffer.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rom_io.h"

/* ===== ROM buffer ===== */
u8  *g_romData = NULL;
u32  g_romSize = 0;

/* GoldenEye 007 is a 12 MB (96 Mbit) cartridge in every region (U/E/J). */
#define GE007_ROM_SIZE_BYTES 0xC00000

/**
 * Load the .z64 ROM file into memory.
 */
int platformInitRom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ROM] Failed to open: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "[ROM] Could not determine size of: %s\n", path);
        fclose(f);
        return -1;
    }

    /* GoldenEye 007 is a 12 MB (96 Mbit) cartridge in every region (U/E/J), so a
     * valid dump is exactly this size in any byte order. Anything else is the
     * wrong game, an over-dump with a copier header, or a truncated file — all of
     * which would otherwise sail past here and then read out of bounds when the
     * hardcoded file-table offsets (up to ~9.4 MB) are DMA'd. Fail loudly instead. */
    if (size != GE007_ROM_SIZE_BYTES) {
        fprintf(stderr,
                "[ROM] %s is %ld bytes, but a GoldenEye 007 ROM must be exactly "
                "%d bytes (12 MB).\n"
                "[ROM] This does not look like a valid ROM (wrong game, a headered/"
                "over-dumped file, or a truncated download).\n"
                "[ROM] Expected SHA-1s are in ge007.u.sha1 / ge007.e.sha1 / "
                "ge007.j.sha1.\n",
                path, size, GE007_ROM_SIZE_BYTES);
        fclose(f);
        return -1;
    }

    g_romData = (u8 *)malloc((size_t)size);
    if (!g_romData) {
        fprintf(stderr, "[ROM] Failed to allocate %ld bytes\n", size);
        fclose(f);
        return -1;
    }

    size_t read = fread(g_romData, 1, (size_t)size, f);
    fclose(f);

    if ((long)read != size) {
        fprintf(stderr, "[ROM] Short read: %zu of %ld bytes\n", read, size);
        free(g_romData);
        g_romData = NULL;
        return -1;
    }

    g_romSize = (u32)size;

    /* Detect byte order. N64 ROMs come in 3 formats:
     * .z64 (big-endian):    80 37 12 40
     * .v64 (byte-swapped):  37 80 40 12
     * .n64 (little-endian): 40 12 37 80
     * We need big-endian (.z64 format). */
    if (g_romData[0] == 0x37 && g_romData[1] == 0x80) {
        /* .v64 — swap every 2 bytes */
        fprintf(stderr, "[ROM] Detected .v64 byte-swapped format, converting...\n");
        for (u32 i = 0; i + 1 < g_romSize; i += 2) {
            u8 tmp = g_romData[i];
            g_romData[i] = g_romData[i + 1];
            g_romData[i + 1] = tmp;
        }
    } else if (g_romData[0] == 0x40 && g_romData[1] == 0x12) {
        /* .n64 — swap every 4 bytes */
        fprintf(stderr, "[ROM] Detected .n64 little-endian format, converting...\n");
        for (u32 i = 0; i + 3 < g_romSize; i += 4) {
            u8 a = g_romData[i], b = g_romData[i+1];
            u8 c = g_romData[i+2], d = g_romData[i+3];
            g_romData[i] = d; g_romData[i+1] = c;
            g_romData[i+2] = b; g_romData[i+3] = a;
        }
    } else if (g_romData[0] != 0x80 || g_romData[1] != 0x37) {
        fprintf(stderr, "[ROM] Warning: unrecognized ROM header: %02x %02x %02x %02x\n",
                g_romData[0], g_romData[1], g_romData[2], g_romData[3]);
    }

    printf("[ROM] Loaded %u bytes (%.1f MB) from %s\n",
           g_romSize, (float)g_romSize / (1024.0f * 1024.0f), path);

    return 0;
}
