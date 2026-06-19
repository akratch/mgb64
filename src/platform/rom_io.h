/**
 * rom_io.h — ROM file I/O for the PC port.
 *
 * Loads the N64 .z64 ROM into memory and provides the infrastructure
 * for romCopy() and osPiStartDma() to read from it.
 */
#ifndef ROM_IO_H
#define ROM_IO_H

#include <ultra64.h>

/* The loaded ROM image */
extern u8  *g_romData;
extern u32  g_romSize;

/**
 * Open and load the .z64 ROM file into memory.
 * Returns 0 on success, -1 on failure.
 */
int platformInitRom(const char *path);

/**
 * Patch file_resource_table hw_address pointers to reference
 * correct offsets within the loaded ROM buffer.
 * (Implemented in rom_offsets.c, generated from N64 ELF.)
 */
void platformPatchFileTable(u8 *romData);

/**
 * Initialize segment symbol values to real ROM offsets
 * extracted from the N64 ELF.
 */
void platformInitSegmentOffsets(void);

#endif /* ROM_IO_H */
