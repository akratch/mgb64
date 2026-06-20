#include <ultra64.h>
#include <bondgame.h>

#include "fr.h"

#if defined(NATIVE_PORT)
#define NATIVE_ZERO_INIT = {0}
#else
#define NATIVE_ZERO_INIT
#endif

/* SCREEN_HEIGHT #define changes based on version (PAL or NTSC) */
u8 cfb_16[NUM_VIDEO_FRAME_BUFFERS][SCREEN_WIDTH * SCREEN_HEIGHT * 2] NATIVE_ZERO_INIT;

#undef NATIVE_ZERO_INIT
