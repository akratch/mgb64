/**
 * platform.h — Master platform abstraction for the GoldenEye native port.
 *
 * This header replaces the N64 SDK headers (os.h, gu.h, mbi.h, libaudio.h,
 * sptask.h, etc.) when building for PC. It provides stub types, macros,
 * and function declarations that let the original game code compile on
 * modern platforms.
 */
#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#include <PR/ultratypes.h>
#include <platform_info.h>
#include <platform_stdio.h>

/* Pull in sub-headers.
 * Order matters: GBI first (defines Mtx, Gfx, etc. used by platform_os.h) */
#include "platform_gbi.h"
#include "platform_os.h"
#include "platform_audio.h"

#endif /* _PLATFORM_H_ */
