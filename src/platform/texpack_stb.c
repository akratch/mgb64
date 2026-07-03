/*
 * texpack_stb.c -- single translation unit that compiles the vendored stb_image
 * decoder used by the optional HD texture-pack loader (texture_pack.c).
 *
 * stb_image is public domain (see lib/stb/stb_image.h trailer + THIRD_PARTY.md).
 * We only need the loader (decode PNG -> RGBA); no writer is compiled here.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          /* HD packs are PNG; keep the binary lean */
/* NB: do NOT define STBI_NO_STDIO -- stb checks definedness, not value, so even
 * "#define STBI_NO_STDIO 0" would remove the path-based stbi_load() we rely on. */
#include "stb_image.h"
