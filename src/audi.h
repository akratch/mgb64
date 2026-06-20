#ifndef _AUDI_H_
#define _AUDI_H_

#include <ultra64.h>

void amCreateAudioManager(ALSynConfig* alconf);
void amStartAudioThread(void);

#ifdef NATIVE_PORT
typedef struct PortAiStats {
    u32 queue_before_bytes;
    u32 queue_after_bytes;
    u32 queue_limit_bytes;
    u32 requested_bytes;
    u32 accepted_bytes;
    u32 dropped_buffers;
    u32 dropped_bytes;
} PortAiStats;

void portAudioFrame(void);
u32 portAudioGetFrameSize(void);
u32 portAiGetDroppedBufferCount(void);
void portAiGetStats(PortAiStats *stats);
u32 portAudioGetDeviceBufferBytes(void);
#endif

#endif
