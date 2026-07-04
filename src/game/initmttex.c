#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "initmttex.h"
#include <token.h>

void set_mt_tex_alloc(void)
{
    g_TexCacheCount = 0;

#ifdef NATIVE_PORT
    {
        const char *tf = tokenFind(1, "-mt");
        if (tf) {
            bytes = strtol(tf, NULL, 0) * 1024;
        }
    }
    {
        u8 *alloc = mempAllocBytesInBank(bytes, MEMPOOL_STAGE);
        texInitPool(ptr_texture_alloc_start, alloc, bytes);
    }
#else
    if (tokenFind(1, "-mt"))
    {
        bytes = strtol(tokenFind(1, "-mt"), 0x0, 0) * 1024;
    }

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#endif
}
