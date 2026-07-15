/**
 * audi_port.c — Port-specific audio manager for GoldenEye PC.
 *
 * Replaces the N64 audio thread model with synchronous per-frame
 * audio synthesis. amCreateAudioManager initializes the libultra
 * synthesizer. portAudioFrame() is called each frame to drive
 * the synthesis chain via alAudioFrame() + mixer.c.
 */
#ifdef NATIVE_PORT

#include <ultra64.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audi.h"
#include "audio_pc.h"
#include "mixer.h"
#include "snd.h"

/* libaudio.h is now ungated, all types available via ultra64.h */
extern int g_deterministic;

#define OUTPUT_RATE                    0x5622
#ifdef REFRESH_PAL
#define MAYBE_FRAME_RATE               50
#else
#define MAYBE_FRAME_RATE               60
#endif
#define FRAMES_PER_FIELD_AS_POW2       1
#define EXTRA_SAMPLES                  0x25
#define NUMBER_OUTPUT_BUFFERS          3
#define NUMBER_ACMD_LISTS              2
#define MAX_ACMD_SIZE                  3000
#define NUMBER_DMA_BUFFERS             64
#define AUDIO_DMA_MAX_BUFFER_LENGTH    0x400  /* 1024: must cover MAX_RATIO * AL_MAX_RSP_SAMPLES * 2 + padding */
#define AUDIO_DMA_ALLOC_PADDING        64    /* extra bytes so alignment overreads land in zeroed memory */
#define AUDIO_DMA_IO_QUEUE_SIZE        64
#define AUDIO_DMA_QUEUE_SIZE           66
#define PORT_AUDIO_CUSTOM_FX_SECTION_COUNT 6
#define PORT_AUDIO_FX_MS               *(((s32)((f32)44.1f)) & ~0x7)
/* H1 output low-pass (§2.4/§4.2): default-OFF one-pole DAC-coloration filter.
 * Now driven by two LIVE settings (Audio.OutputFilter / Audio.OutputFilterAlpha,
 * registered in audio_pc.c) instead of a latched env + a hardcoded alpha. The
 * ares boot capture (W6.E1.T2) matched the direct libaudio path within budget
 * with the filter OFF, so it stays default-OFF and is NOT in --remaster; the knob
 * is retained. Legacy GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS still honored below. */

static s32 s_portAudioCustomFxParams[PORT_AUDIO_CUSTOM_FX_SECTION_COUNT * 8 + 2] = {
    6,     160 PORT_AUDIO_FX_MS,
    0,       4 PORT_AUDIO_FX_MS,   9830,  -9830,      0,        0,     0,       0,
    4 PORT_AUDIO_FX_MS,     8 PORT_AUDIO_FX_MS,   9830,  -9830, 0x2B84,        0,     0,  0x2500,
    20 PORT_AUDIO_FX_MS,   64 PORT_AUDIO_FX_MS,  16384, -16384, 0x11EB,        0,     0,  0x3000,
    80 PORT_AUDIO_FX_MS,  140 PORT_AUDIO_FX_MS,  16384, -16384, 0x11EB,        0,     0,  0x3500,
    84 PORT_AUDIO_FX_MS,  120 PORT_AUDIO_FX_MS,   8192,  -8192,      0,        0,     0,  0x4000,
    0,     148 PORT_AUDIO_FX_MS,  13000, -13000,      0,   0x017C,   0xA,  0x4500
};

typedef struct {
    ALLink node;
    int    startAddr;
    u32    lastFrame;
    u8    *ptr;
} DMABuffer;

typedef struct {
    union { u8 initialized; s32 _align; } u;
    DMABuffer *firstUsed;
    DMABuffer *firstFree;
} DMAState;

typedef struct {
    s16 *data;
    s16  frameSamples;
} AudioInfo;

static struct {
    Acmd      *cmdList[NUMBER_ACMD_LISTS];
    AudioInfo *audioInfo[NUMBER_OUTPUT_BUFFERS];
    ALGlobals  g;
} g_PortAudioMgr;

static DMAState  g_DmaState;
static DMABuffer g_DmaBuffers[NUMBER_DMA_BUFFERS];
static u32       g_FrameSize;
static u32       g_NominalFrameSizeBase;
static u32       g_NominalFrameSizeRemainder;
static u32       g_NominalFrameSizeQuantum;
static u32       g_NominalFrameSizeAccumulator;
static u32       g_MaxFrameSize;
static s32       g_CommandLength;
static u32       g_CurrentAcmdList;
static u32       g_AudioFrameCount;
static u32       g_NextDMa;
static OSIoMesg  g_DmaIOMessageBuffer[AUDIO_DMA_IO_QUEUE_SIZE];
static OSMesgQueue g_DmaMessageQueue;
static OSMesg    g_DmaMessageBuffer[AUDIO_DMA_QUEUE_SIZE];
static s32       g_portAudioReady;
static AudioInfo *g_lastInfo;
static ALFxId    g_portAudioFxType;
static u8        g_portAudioFxCustom;
static s32       g_LibaudioLowPassState[2];
static s32       g_LibaudioLowPassInitialized;
static s32       g_LibaudioLowPassEnabled;      /* legacy env cache (-1 = unread)   */

/* H1 LIVE settings (registered in audio_pc.c::portAudioRegisterConfig). Re-read
 * each frame in portAudioApplyLibaudioLowPass — no latching. */
s32 g_portAudioOutputFilter      = 0;
s32 g_portAudioOutputFilterAlpha = PORT_AUDIO_OUTPUT_FILTER_ALPHA_DEFAULT;

static u32 portAudioAlign16(u32 samples) {
    return samples & ~0xfU;
}

static s16 portAudioClampS16(s32 value) {
    if (value < -0x8000) return -0x8000;
    if (value > 0x7fff) return 0x7fff;
    return (s16)value;
}

static u32 portAudioNextNominalFrameSize(void) {
    u32 samples = g_NominalFrameSizeBase;

    if (samples == 0) {
        return g_FrameSize;
    }

    if (g_NominalFrameSizeRemainder != 0 && g_NominalFrameSizeQuantum != 0) {
        g_NominalFrameSizeAccumulator += g_NominalFrameSizeRemainder;
        if (g_NominalFrameSizeAccumulator >= g_NominalFrameSizeQuantum) {
            samples += 0x10;
            g_NominalFrameSizeAccumulator -= g_NominalFrameSizeQuantum;
        }
    }

    return samples;
}

static void portAudioApplyLibaudioLowPass(s16 *samples, s32 sampleFrames) {
    s32 i;
    s32 alpha;
    int enabled;

    if (samples == NULL || sampleFrames <= 0) {
        return;
    }

    /* Legacy env alias for pre-existing A/B scripts. Env is immutable per run, so
     * cache it once (-1 = unread). Honored IN ADDITION to Audio.OutputFilter so
     * old GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS invocations keep working. */
    if (g_LibaudioLowPassEnabled < 0) {
        const char *enabled_env = getenv("GE007_ENABLE_LIBAUDIO_LOWPASS");
        const char *disabled_env = getenv("GE007_DISABLE_LIBAUDIO_LOWPASS");
        g_LibaudioLowPassEnabled =
            (enabled_env != NULL && *enabled_env != '\0' && *enabled_env != '0' &&
             (disabled_env == NULL || *disabled_env == '\0' || *disabled_env == '0'))
                ? 1
                : 0;
    }

    /* LIVE: re-read the setting every frame (lock-free s32 read). */
    enabled = (g_portAudioOutputFilter != 0) || g_LibaudioLowPassEnabled;
    if (!enabled) {
        g_LibaudioLowPassInitialized = 0; /* re-seed cleanly on the next enable */
        return;
    }

    alpha = g_portAudioOutputFilterAlpha;
    if (alpha < 1) {
        alpha = 1;
    } else if (alpha > 32767) {
        alpha = 32767;
    }

    if (!g_LibaudioLowPassInitialized) {
        g_LibaudioLowPassState[0] = (s32)samples[0] << 15;
        g_LibaudioLowPassState[1] = (s32)samples[1] << 15;
        g_LibaudioLowPassInitialized = 1;
    }

    for (i = 0; i < sampleFrames; i++) {
        s32 ch;

        for (ch = 0; ch < 2; ch++) {
            s32 *state = &g_LibaudioLowPassState[ch];
            s32 target = (s32)samples[(i << 1) + ch] << 15;
            int64_t delta = (int64_t)target - (int64_t)*state;

            *state += (s32)((delta * alpha) >> 15);
            samples[(i << 1) + ch] = portAudioClampS16(*state >> 15);
        }
    }
}

/* W6.E3.T1 §4.3: final-mix master-volume scaler. Applied to the fully-mixed
 * stereo buffer (music + SFX) at the tail of portAudioFrame, just before the
 * queue write, so Audio.MasterVolume governs the real libaudio output (the dead
 * master fix). Q15 integer multiply with identity-bypass at unity (32768) => the
 * default (Audio.MasterVolume=1.0) is byte-identical. LIVE: re-reads the setting
 * every frame, so a menu change is audible without restart. */
static void portAudioApplyMasterVolume(s16 *samples, s32 sampleFrames) {
    const s32 q15 = portAudioGetMasterVolumeQ15();
    s32 i;
    s32 n;

    if (samples == NULL || sampleFrames <= 0) {
        return;
    }
    if (q15 >= 32768) {
        return; /* unity: byte-identical bypass */
    }

    n = sampleFrames << 1; /* stereo interleaved samples */
    for (i = 0; i < n; i++) {
        s32 s = ((s32)samples[i] * q15) >> 15;
        samples[i] = portAudioClampS16(s);
    }
}

static intptr_t amDmaCallback(s32 addr, s32 len, void *state) {
    void *freeBuffer;
    s32 delta;
    DMABuffer *dmaPtr, *lastDmaPtr;
    s32 addrEnd, buffEnd;
    (void)state;

    /* DMA from ROM — addr is a ROM offset */

    lastDmaPtr = NULL;
    dmaPtr = g_DmaState.firstUsed;
    delta = addr & 0x1;
    addrEnd = addr + len;

    while (dmaPtr) {
        buffEnd = dmaPtr->startAddr + AUDIO_DMA_MAX_BUFFER_LENGTH;
        if ((u32)dmaPtr->startAddr > (u32)addr) break;
        if (addrEnd <= buffEnd) {
            dmaPtr->lastFrame = g_AudioFrameCount;
            freeBuffer = (dmaPtr->ptr + addr) - dmaPtr->startAddr;
            return (intptr_t)freeBuffer;
        }
        lastDmaPtr = dmaPtr;
        dmaPtr = (DMABuffer *)dmaPtr->node.next;
    }

    dmaPtr = g_DmaState.firstFree;
    if (!dmaPtr) {
        if (!lastDmaPtr) lastDmaPtr = g_DmaState.firstUsed;
        if (!lastDmaPtr) return 0;
        return (intptr_t)(lastDmaPtr->ptr) + delta;
    }

    g_DmaState.firstFree = (DMABuffer *)dmaPtr->node.next;
    alUnlink((ALLink *)dmaPtr);

    if (lastDmaPtr)
        alLink((ALLink *)dmaPtr, (ALLink *)lastDmaPtr);
    else if (g_DmaState.firstUsed) {
        lastDmaPtr = g_DmaState.firstUsed;
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = (ALLink *)lastDmaPtr;
        dmaPtr->node.prev = 0;
        lastDmaPtr->node.prev = (ALLink *)dmaPtr;
    } else {
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = 0;
        dmaPtr->node.prev = 0;
    }

    freeBuffer = dmaPtr->ptr;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = g_AudioFrameCount;

    osPiStartDma(&g_DmaIOMessageBuffer[g_NextDMa++], OS_MESG_PRI_HIGH,
                 OS_READ, (u32)addr, freeBuffer,
                 AUDIO_DMA_MAX_BUFFER_LENGTH, &g_DmaMessageQueue);

    return (intptr_t)freeBuffer + delta;
}

static ALDMAproc amDmaNew(DMAState **state) {
    if (!g_DmaState.u.initialized) {
        g_DmaState.firstUsed = NULL;
        g_DmaState.firstFree = g_DmaBuffers;
        g_DmaState.u.initialized = 1;
    }
    *state = &g_DmaState;
    return (ALDMAproc)&amDmaCallback;
}

static void amClearDmaBuffers(void) {
    u32 i;
    OSMesg m = 0;
    DMABuffer *dmaPtr, *nextPtr;
    for (i = 0; i < g_NextDMa; i++)
        osRecvMesg(&g_DmaMessageQueue, &m, OS_MESG_NOBLOCK);
    dmaPtr = g_DmaState.firstUsed;
    while (dmaPtr) {
        nextPtr = (DMABuffer *)dmaPtr->node.next;
        if (dmaPtr->lastFrame + 1 < g_AudioFrameCount) {
            if (g_DmaState.firstUsed == dmaPtr)
                g_DmaState.firstUsed = (DMABuffer *)dmaPtr->node.next;
            alUnlink((ALLink *)dmaPtr);
            if (g_DmaState.firstFree)
                alLink((ALLink *)dmaPtr, (ALLink *)g_DmaState.firstFree);
            else {
                g_DmaState.firstFree = dmaPtr;
                dmaPtr->node.next = 0;
                dmaPtr->node.prev = 0;
            }
        }
        dmaPtr = nextPtr;
    }
    g_NextDMa = 0;
    g_AudioFrameCount++;
}

static void portAudioMeasureOutput(const s16 *samples, s32 sampleFrames,
                                   u32 *peakOut, u32 *railHitsOut) {
    s32 i;
    s32 totalSamples = sampleFrames * 2;
    u32 peak = 0;
    u32 railHits = 0;

    for (i = 0; i < totalSamples; i++) {
        s32 v = samples[i];
        u32 amp;

        if (v == -32768 || v == 32767) {
            railHits++;
        }

        amp = (v < 0) ? (u32)-v : (u32)v;
        if (amp > peak) {
            peak = amp;
        }
    }

    *peakOut = peak;
    *railHitsOut = railHits;
}

void amCreateAudioManager(ALSynConfig *alconf) {
    u32 j;
    f32 fsize;
    u32 exactFrameNumerator;

    printf("[AUDIO-PORT] Initializing audio synthesizer...\n");
    mixerInit();

    alconf->dmaproc = (ALDMANew)&amDmaNew;
    alconf->outputRate = OUTPUT_RATE;

    fsize = (f32)((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);
    g_FrameSize = (u32)fsize;
    if (g_FrameSize < fsize) g_FrameSize++;
    if (g_FrameSize & 0xf) g_FrameSize = (g_FrameSize & ~0xf) + 0x10;
    g_MaxFrameSize = g_FrameSize + EXTRA_SAMPLES + 0x10;
    exactFrameNumerator = alconf->outputRate << FRAMES_PER_FIELD_AS_POW2;
    g_NominalFrameSizeBase =
        portAudioAlign16(exactFrameNumerator / MAYBE_FRAME_RATE);
    g_NominalFrameSizeRemainder =
        exactFrameNumerator - (g_NominalFrameSizeBase * MAYBE_FRAME_RATE);
    g_NominalFrameSizeQuantum = 0x10 * MAYBE_FRAME_RATE;
    g_NominalFrameSizeAccumulator = 0;
    g_LibaudioLowPassState[0] = 0;
    g_LibaudioLowPassState[1] = 0;
    g_LibaudioLowPassInitialized = 0;
    g_LibaudioLowPassEnabled = -1;

    if (getenv("GE007_DISABLE_NATIVE_REVERB") != NULL) {
        alconf->fxType = AL_FX_NONE;
        alconf->params = NULL;
    } else if (alconf->fxType == AL_FX_CUSTOM) {
        /* The sole caller (musicSeqPlayerInit, music.c) never assigns
         * ALSynConfig.params — decomp-faithful, as retail relied on a zeroed
         * stack for that slot. The port owns the CUSTOM FX table, so bind it
         * unconditionally rather than trusting the caller's uninitialized field.
         *
         * Native (LP64): the slot reads NULL, so the old `params == NULL` guard
         * already substituted s_portAudioCustomFxParams — this change is inert.
         * wasm32 (ILP32): the narrower ALSynConfig layout leaves the params slot
         * holding stack garbage (observed 0x1). The old `== NULL` guard skipped
         * substitution, so native_fx_params_for_config() later returned 0x1 and
         * alFxNew dereferenced address 1 (params[0]) -> first-frame segfault.
         * Binding unconditionally fixes wasm and preserves the native result. */
        alconf->params = s_portAudioCustomFxParams;
    }
    g_portAudioFxType = alconf->fxType;
    g_portAudioFxCustom = (alconf->fxType == AL_FX_CUSTOM && alconf->params != NULL) ? 1 : 0;
    alInit(&g_PortAudioMgr.g, alconf);

    for (j = 0; j < NUMBER_OUTPUT_BUFFERS; j++) {
        g_PortAudioMgr.audioInfo[j] = alHeapAlloc(alconf->heap, 1, sizeof(AudioInfo));
        g_PortAudioMgr.audioInfo[j]->data = alHeapAlloc(alconf->heap, 1, g_MaxFrameSize * 4);
    }

    osCreateMesgQueue(&g_DmaMessageQueue, g_DmaMessageBuffer, AUDIO_DMA_IO_QUEUE_SIZE);

    g_DmaBuffers[0].node.prev = NULL;
    g_DmaBuffers[0].node.next = NULL;
    for (j = 0; (s32)j < NUMBER_DMA_BUFFERS - 1; j++) {
        alLink((ALLink *)&g_DmaBuffers[j+1], (ALLink *)&g_DmaBuffers[j]);
        /* Use malloc for DMA buffers to avoid heap overflow into other allocations */
        g_DmaBuffers[j].ptr = (u8 *)calloc(1, AUDIO_DMA_MAX_BUFFER_LENGTH + AUDIO_DMA_ALLOC_PADDING);
    }
    g_DmaBuffers[j].ptr = (u8 *)calloc(1, AUDIO_DMA_MAX_BUFFER_LENGTH + AUDIO_DMA_ALLOC_PADDING);

    for (j = 0; j < NUMBER_ACMD_LISTS; j++)
        g_PortAudioMgr.cmdList[j] = alHeapAlloc(alconf->heap, 1, MAX_ACMD_SIZE * sizeof(Acmd));

    g_CurrentAcmdList = 0;
    g_AudioFrameCount = 0;
    g_NextDMa = 0;
    g_portAudioReady = 1;
    g_lastInfo = NULL;

    printf("[AUDIO-PORT] Synthesizer ready: rate=%d, frameSize=%d fxType=%u custom=%u\n",
           alconf->outputRate, g_FrameSize,
           (unsigned int)g_portAudioFxType,
           (unsigned int)g_portAudioFxCustom);
}

void amStartAudioThread(void) {
    /* No thread on port — audio runs synchronously via portAudioFrame() */
}

void portAudioFrame(void) {
    AudioInfo *info;
    u32 target_bytes;

    if (!g_portAudioReady) return;

    amClearDmaBuffers();
    info = g_PortAudioMgr.audioInfo[g_AudioFrameCount % NUMBER_OUTPUT_BUFFERS];

    /* Occupancy-targeted frame sizing (replaces N64 subtraction formula).
     *
     * Treat the SDL queue as explicit occupancy, not a fake DMA remainder.
     * Keep the queue near a modest target and scale production linearly:
     * - below target: nudge upward toward g_MaxFrameSize
     * - above target: reduce toward a true half-frame drain mode
     *
     * The previous version only dipped from g_FrameSize to g_MinFrameSize
     * (736 -> 720 samples on NTSC), which was too weak to drain backlog and
     * ended up leaning on the hard drop cap. */
    {
        if (g_deterministic) {
            /* Deterministic mode still needs the exact average output rate.
             * Use an aligned Bresenham cadence: NTSC alternates 720/736
             * samples to average 735 samples per 30 Hz game frame. */
            info->frameSamples = (s16)portAudioNextNominalFrameSize();
        } else {
            const u32 queued_bytes = osAiGetLength();
            const s32 queued_samples = (s32)(queued_bytes >> 2);  /* stereo s16 -> sample frames */
            const s32 target_samples = (s32)(g_FrameSize + (g_FrameSize / 2));
            const s32 full_samples = (s32)g_FrameSize;
            const s32 min_samples = (s32)portAudioAlign16(g_FrameSize / 2);
            const s32 max_samples = (s32)portAudioAlign16(g_MaxFrameSize);
            s32 chosen = full_samples + ((target_samples - queued_samples) / 2);

            if (chosen < min_samples) chosen = min_samples;
            if (chosen > max_samples) chosen = max_samples;
            info->frameSamples = (s16)chosen;
        }

        /* Always keep the synth input aligned. */
        info->frameSamples = (s16)portAudioAlign16((u32)info->frameSamples);
    }

    target_bytes = (g_FrameSize + (g_FrameSize / 2)) * 4;

    alAudioFrame(g_PortAudioMgr.cmdList[g_CurrentAcmdList],
                 &g_CommandLength, info->data, info->frameSamples);
    portAudioApplyLibaudioLowPass(info->data, info->frameSamples);

    {
        static int s_musicDumpEnabled = -1;
        if (s_musicDumpEnabled < 0) {
            s_musicDumpEnabled = (getenv("GE007_MUSIC_AUDIO_DUMP") != NULL) ? 1 : 0;
        }
        if (s_musicDumpEnabled) {
            extern void portMusicAudioDump(const void *buf, unsigned int size);
            portMusicAudioDump(info->data, info->frameSamples * 4);
        }
    }

    portAudioMixSfxIntoBuffer(info->data, info->frameSamples);

    /* W6.E3.T1 §4.3: master-volume final stage (identity at unity). */
    portAudioApplyMasterVolume(info->data, info->frameSamples);

    /* Queue audio immediately (no 1-frame delay like N64 double-buffer) */
    osAiSetNextBuffer(info->data, info->frameSamples * 4);

    /* Debug: audio health telemetry (gated behind GE007_VERBOSE) */
    {
        static int s_frameCount = 0;
        static int s_verbose = -1;
        static int s_trace_init = 0;
        static FILE *s_trace_fp = NULL;
        static u32 s_minQueue = 0xFFFFFFFF, s_maxQueue = 0;
        static u32 s_underruns = 0, s_overTarget = 0;
        static u32 s_queuePrimed = 0;
        static PortMixerStats s_prevMixerStats = {0};
        static int s_prevMixerStatsValid = 0;
        if (s_verbose < 0) s_verbose = (getenv("GE007_VERBOSE") != NULL) ? 1 : 0;
        if (!s_trace_init) {
            const char *trace_path = getenv("GE007_AUDIO_TRACE");
            s_trace_init = 1;
            if (trace_path && *trace_path) {
                s_trace_fp = fopen(trace_path, "w");
            }
        }
        s_frameCount++;

        if (s_verbose || s_trace_fp) {
            PortAiStats ai_stats = {0};
            PortSfxMixStats sfx_stats = {0};
            PortMixerStats mixer_stats = {0};
            PortMixerStats mixer_delta = {0};
            PortSndPlayerStats sndp_stats = {0};
            u32 qb = 0;
            u32 post_queue = 0;
            u32 output_peak = 0;
            u32 output_rail_hits = 0;
            u32 controller_target_bytes = target_bytes;
            u32 soft_target_bytes = controller_target_bytes;
            u32 queue_limit = g_FrameSize * 4 * 4;
            u32 drop_count = portAiGetDroppedBufferCount();
            u32 device_buffer_bytes = portAudioGetDeviceBufferBytes();

            portAiGetStats(&ai_stats);
            portAudioGetSfxMixStats(&sfx_stats);
            mixerGetStats(&mixer_stats);
            sndGetPlayerStats(&sndp_stats);
            portAudioMeasureOutput(info->data, info->frameSamples,
                                   &output_peak, &output_rail_hits);

            if (s_prevMixerStatsValid) {
                mixer_delta.adpcmClampHits =
                    mixer_stats.adpcmClampHits - s_prevMixerStats.adpcmClampHits;
                mixer_delta.resampleClampHits =
                    mixer_stats.resampleClampHits - s_prevMixerStats.resampleClampHits;
                mixer_delta.envMixerClampHits =
                    mixer_stats.envMixerClampHits - s_prevMixerStats.envMixerClampHits;
                mixer_delta.mixClampHits =
                    mixer_stats.mixClampHits - s_prevMixerStats.mixClampHits;
                mixer_delta.poleFilterClampHits =
                    mixer_stats.poleFilterClampHits - s_prevMixerStats.poleFilterClampHits;
            } else {
                mixer_delta.adpcmClampHits = mixer_stats.adpcmClampHits;
                mixer_delta.resampleClampHits = mixer_stats.resampleClampHits;
                mixer_delta.envMixerClampHits = mixer_stats.envMixerClampHits;
                mixer_delta.mixClampHits = mixer_stats.mixClampHits;
                mixer_delta.poleFilterClampHits = mixer_stats.poleFilterClampHits;
            }
            s_prevMixerStats = mixer_stats;
            s_prevMixerStatsValid = 1;

            if (!g_deterministic) {
                qb = ai_stats.queue_before_bytes;
                post_queue = ai_stats.queue_after_bytes;
                queue_limit = ai_stats.queue_limit_bytes;
            }

            if (device_buffer_bytes != 0) {
                u32 host_jitter_headroom = device_buffer_bytes * 2;
                if (soft_target_bytes < controller_target_bytes + host_jitter_headroom) {
                    soft_target_bytes = controller_target_bytes + host_jitter_headroom;
                }
            }

            if (qb < s_minQueue) s_minQueue = qb;
            if (qb > s_maxQueue) s_maxQueue = qb;
            if (!s_queuePrimed && s_frameCount > 30) {
                s_queuePrimed = 1;
            }
            if (s_queuePrimed && qb == 0) s_underruns++;
            if (qb > soft_target_bytes) s_overTarget++;

            if (s_trace_fp) {
                fprintf(s_trace_fp,
                        "{\"frame\":%d,\"samples\":%d,\"queue_before\":%u,\"queue_after\":%u,"
                        "\"target_bytes\":%u,\"soft_target_bytes\":%u,\"limit_bytes\":%u,\"primed\":%u,"
                        "\"fx_type\":%u,\"fx_custom\":%u,"
                        "\"output_peak\":%u,\"output_rail_hits\":%u,"
                        "\"adpcm_dec_calls\":%u,\"adpcm_clamp_hits\":%u,"
                        "\"adpcm_clamp_delta\":%u,"
                        "\"resample_calls\":%u,\"resample_clamp_hits\":%u,"
                        "\"resample_clamp_delta\":%u,"
                        "\"env_mixer_calls\":%u,\"env_mixer_sample_frames\":%u,"
                        "\"env_mixer_clamp_hits\":%u,\"env_sample_xor\":%u,"
                        "\"env_mixer_clamp_delta\":%u,"
                        "\"mix_calls\":%u,\"mix_clamp_hits\":%u,"
                        "\"mix_clamp_delta\":%u,"
                        "\"pole_filter_calls\":%u,\"pole_filter_sample_frames\":%u,"
                        "\"pole_filter_clamp_hits\":%u,"
                        "\"pole_filter_clamp_delta\":%u,"
                        "\"pole_sample_xor\":%u,\"pole_filter_peak\":%u,"
                        "\"save_buffer_calls\":%u,\"save_buffer_bytes\":%u,"
                        "\"save_buffer_dmemout_calls\":%u,"
                        "\"requested_bytes\":%u,\"accepted_bytes\":%u,\"dropped_buffers\":%u,"
                        "\"dropped_bytes\":%u,\"underruns\":%u,\"over_soft_target\":%u,"
                        "\"sfx_mix_calls\":%u,\"sfx_voice_starts\":%u,\"sfx_voice_stops\":%u,"
                        "\"sfx_active_voices\":%u,\"sfx_active_voice_frames\":%u,"
                        "\"sfx_sample_frames\":%u,\"sfx_peak_delta\":%u,\"sfx_peak_delta_max\":%u,"
                        "\"sndp_real_path\":%u,\"sndp_stub_path\":%u,"
                        "\"sndp_player_inits\":%u,\"sndp_submit_events\":%u,"
                        "\"sndp_play_events\":%u,\"sndp_voice_starts\":%u,"
                        "\"sndp_voice_stops\":%u,\"sndp_active_voices\":%u,"
                        "\"sndp_volume_updates\":%u,\"sndp_pan_updates\":%u,"
                        "\"sndp_pitch_updates\":%u,\"sndp_fx_updates\":%u,"
                        "\"sndp_release_events\":%u,\"sndp_decay_events\":%u}\n",
                        s_frameCount, info->frameSamples, qb, post_queue,
                        controller_target_bytes, soft_target_bytes, queue_limit, s_queuePrimed,
                        (unsigned int)g_portAudioFxType,
                        (unsigned int)g_portAudioFxCustom,
                        output_peak,
                        output_rail_hits,
                        mixer_stats.adpcmDecCalls,
                        mixer_stats.adpcmClampHits,
                        mixer_delta.adpcmClampHits,
                        mixer_stats.resampleCalls,
                        mixer_stats.resampleClampHits,
                        mixer_delta.resampleClampHits,
                        mixer_stats.envMixerCalls,
                        mixer_stats.envMixerSampleFrames,
                        mixer_stats.envMixerClampHits,
                        mixer_stats.envSampleXor,
                        mixer_delta.envMixerClampHits,
                        mixer_stats.mixCalls,
                        mixer_stats.mixClampHits,
                        mixer_delta.mixClampHits,
                        mixer_stats.poleFilterCalls,
                        mixer_stats.poleFilterSampleFrames,
                        mixer_stats.poleFilterClampHits,
                        mixer_delta.poleFilterClampHits,
                        mixer_stats.poleSampleXor,
                        mixer_stats.poleFilterPeak,
                        mixer_stats.saveBufferCalls,
                        mixer_stats.saveBufferBytes,
                        mixer_stats.saveBufferDmemoutCalls,
                        ai_stats.requested_bytes, ai_stats.accepted_bytes,
                        drop_count, ai_stats.dropped_bytes, s_underruns, s_overTarget,
                        sfx_stats.mixCalls, sfx_stats.voiceStarts, sfx_stats.voiceStops,
                        sfx_stats.activeVoicesLast, sfx_stats.activeVoiceFrames,
                        sfx_stats.sampleFramesMixed, sfx_stats.peakDeltaLast,
                        sfx_stats.peakDeltaMax,
                        sndp_stats.realPath,
                        sndp_stats.stubPath,
                        sndp_stats.playerInits,
                        sndp_stats.submitEvents,
                        sndp_stats.playEvents,
                        sndp_stats.voiceStarts,
                        sndp_stats.voiceStops,
                        sndp_stats.activeVoices,
                        sndp_stats.volumeUpdates,
                        sndp_stats.panUpdates,
                        sndp_stats.pitchUpdates,
                        sndp_stats.fxUpdates,
                        sndp_stats.releaseEvents,
                        sndp_stats.decayEvents);
                fflush(s_trace_fp);
            }

            if (s_frameCount % 120 == 1) {
                printf("[AUDIO] f=%d samp=%d q=%uB [%u-%u] target=%uB soft=%uB limit=%uB under=%u over=%u drop=%u post=%uB req=%uB enq=%uB dropB=%uB peak=%u rails=%u sfxStart=%u sfxActive=%u sfxPeak=%u\n",
                       s_frameCount, info->frameSamples, qb,
                       s_minQueue, s_maxQueue, controller_target_bytes, soft_target_bytes, queue_limit,
                       s_underruns, s_overTarget, drop_count, post_queue,
                       ai_stats.requested_bytes, ai_stats.accepted_bytes,
                       ai_stats.dropped_bytes, output_peak, output_rail_hits,
                       sfx_stats.voiceStarts, sfx_stats.activeVoicesLast,
                       sfx_stats.peakDeltaLast);
                s_minQueue = 0xFFFFFFFF;
                s_maxQueue = 0;
            }
        }
    }

    g_CurrentAcmdList ^= 1;
    g_lastInfo = info;
}

/* Export the nominal per-game-frame sample count so the AI stub can derive
 * a queue-limit safety net without duplicating PAL/NTSC math. */
u32 portAudioGetFrameSize(void) {
    return g_FrameSize;
}

#endif /* NATIVE_PORT */
