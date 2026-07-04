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

/* Audio volume buses (W6.E3.T1, docs/remaster-aaa/06-audio-remaster.md §4.3).
 * Q15 fixed point: 32768 == unity (callers identity-bypass at >= 32768 so the
 * default mix stays byte-identical). Storage + registration live in audio_pc.c;
 * consumed by portAudioApplyMasterVolume (audi_port.c), musicTrackNApplySeqpVol
 * (music.c), and alSynSetVol (audio_compat.c) respectively. */
s32 portAudioGetMasterVolumeQ15(void);
s32 portAudioGetMusicBusVolumeQ15(void);
s32 portAudioGetSfxBusVolumeQ15(void);
#endif

#endif
