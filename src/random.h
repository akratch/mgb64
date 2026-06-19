#ifndef _RANDOM_H_
#define _RANDOM_H_
#include <ultra64.h>

void randomSetSeed(u32 param_1);
u32 randomGetNext(void);
u32 randomGetNextFrom(u64 *param_1);
#ifdef NATIVE_PORT
u64 pcRandomGetNextCallCount(void);
void pcRandomTraceSetRamromActive(s32 active);
#endif

// 4294967295 == UINT_MAX
#define RANDOMGETNEXT_F32() ((f32) (u32)randomGetNext() * (1.0f / 4294967295))

#endif
