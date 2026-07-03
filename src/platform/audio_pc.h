#ifndef GE007_AUDIO_PC_H
#define GE007_AUDIO_PC_H

#include <ultra64.h>

typedef struct PortSfxPlayParams_s {
    f32 volume;
    f32 pan;
    f32 pitch;
    s32 delaySamples;
    u8 useEnvelope;
    u8 attackVolume;
    u8 decayVolume;
    u8 fxMix;
    ALMicroTime attackTime;
    ALMicroTime decayTime;
} PortSfxPlayParams;

typedef struct PortSfxMixStats_s {
    u32 mixCalls;
    u32 voiceStarts;
    u32 voiceStops;
    u32 activeVoicesLast;
    u32 activeVoiceFrames;
    u32 sampleFramesMixed;
    u32 peakDeltaLast;
    u32 peakDeltaMax;
} PortSfxMixStats;

void portAudioRegisterConfig(void);
void portAudioInit(void);
void portAudioShutdown(void);
s32 portAudioIsReady(void);
u32 portAudioGetDeviceBufferBytes(void);

s32 portAudioPlaySfxDetailed(s16 soundIndex, const PortSfxPlayParams *params);
s32 portAudioPlaySound(s16 soundIndex, f32 volume, f32 pan, f32 pitch);
s32 portAudioPlaySoundDelayed(s16 soundIndex, f32 volume, f32 pan, f32 pitch,
                              s32 delaySamples);
s32 portAudioSubmitSfx(s16 soundIndex, f32 volume, f32 pan, f32 pitch);
s32 portAudioSubmitSfxAtPosition(s16 soundIndex, f32 volume, f32 pitch,
                                 f32 worldX, f32 worldY, f32 worldZ);
void portAudioComputeSpatialMix(f32 worldX, f32 worldY, f32 worldZ,
                                f32 *attenOut, f32 *panOut);

void portAudioStopAll(void);
void portAudioStopVoice(s32 voiceIdx);
void portAudioReleaseVoice(s32 voiceIdx, ALMicroTime releaseUsec);
s32 portAudioVoiceIsActive(s32 voiceIdx);
void portAudioUpdateVoiceMix(s32 voiceIdx, f32 volume, f32 pan);
void portAudioUpdateVoiceMixRamp(s32 voiceIdx, f32 volume, f32 pan,
                                 ALMicroTime rampUsec);
void portAudioUpdateVoicePitch(s32 voiceIdx, f32 pitch);
void portAudioUpdateVoiceFxMix(s32 voiceIdx, u8 fxMix);
void portAudioUpdateVoiceSpatial(s32 voiceIdx, f32 volume,
                                 f32 worldX, f32 worldY, f32 worldZ);
void portAudioUpdateVoiceSpatialRamp(s32 voiceIdx, f32 volume,
                                     f32 worldX, f32 worldY, f32 worldZ,
                                     ALMicroTime rampUsec);
void portAudioUpdateVoicePanPositionRamp(s32 voiceIdx, f32 volume,
                                         f32 worldX, f32 worldY, f32 worldZ,
                                         ALMicroTime rampUsec);
void portAudioMixSfxIntoBuffer(s16 *out, s32 numSamples);
void portAudioGetSfxMixStats(PortSfxMixStats *stats);
void portAudioTraceSfxJson(const char *fmt, ...);

#endif
