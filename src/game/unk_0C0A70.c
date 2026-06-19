#include <ultra64.h>
#include "unk_0C0A70.h"

#ifdef NATIVE_PORT
#include <stdlib.h>
#include <stdio.h>
#endif

// data
s32 lastFrameCounter = -1;
s32 currentFrameCounter = 0;

/**
 * Appears to be rendered framerate, or some kind of counter since the last frame update.
 */
s32 speedgraphframes = 1;

#if defined(BUGFIX_R1)
// EU address D_8004111C
f32 jpD_800484CC = 1.0f;

// EU address D_80041120
f32 jpD_800484D0 = 1.0f;
#endif

s32 previousFrameCounter = -1;
s32 halfFrameCounter = 0; // half of currentFrameCounter
s32 isFrameCounterOdd = 0; // is currentFrameCounter Odd
s32 halfMinusPreviousCounter = 0; // half - previousFrameCounter
u32 copy_of_osgetcount_value_0 = 0;
u32 copy_of_osgetcount_value_1 = 0;
s32 frameDelay = 1; //usually 1

#ifdef NATIVE_PORT
static int pc_deterministic_speedframes_override = -1;
static int pc_frame_timing_trace_enabled = -1;
static int pc_frame_timing_trace_budget = 0;

static s32 pcGetDeterministicSpeedframesOverride(void)
{
    extern int g_deterministic;
    const char *env;
    s32 value;

    if (!g_deterministic)
    {
        return 0;
    }

    if (pc_deterministic_speedframes_override < 0)
    {
        env = getenv("GE007_DETERMINISTIC_SPEEDFRAMES");
        value = env ? atoi(env) : 0;
        if (value < 0)
        {
            value = 0;
        }
        else if (value > 8)
        {
            value = 8;
        }

        pc_deterministic_speedframes_override = value;
    }

    return pc_deterministic_speedframes_override;
}

static int pcFrameTimingTraceEnabled(void)
{
    const char *env;

    if (pc_frame_timing_trace_enabled < 0)
    {
        env = getenv("GE007_TRACE_FRAME_TIMING");
        pc_frame_timing_trace_enabled = (env != NULL && env[0] != '\0') ? 1 : 0;

        if (pc_frame_timing_trace_enabled)
        {
            env = getenv("GE007_TRACE_FRAME_TIMING_BUDGET");
            pc_frame_timing_trace_budget = (env != NULL && env[0] != '\0') ? atoi(env) : 240;
            if (pc_frame_timing_trace_budget < 0)
            {
                pc_frame_timing_trace_budget = 0;
            }
        }
    }

    return pc_frame_timing_trace_enabled && pc_frame_timing_trace_budget > 0;
}

static void pcTraceFrameTiming(const char *phase, u32 before_count, u32 after_count, s32 delta_frames)
{
    if (!pcFrameTimingTraceEnabled())
    {
        return;
    }

    pc_frame_timing_trace_budget--;
    fprintf(stderr,
            "[FRAME_TIMING] phase=%s current=%d last=%d speed=%d delay=%d "
            "copy0=%u copy1=%u before=%u after=%u elapsed=%u delta=%d\n",
            phase ? phase : "?",
            currentFrameCounter,
            lastFrameCounter,
            speedgraphframes,
            frameDelay,
            copy_of_osgetcount_value_0,
            copy_of_osgetcount_value_1,
            before_count,
            after_count,
            after_count - before_count,
            delta_frames);
    fflush(stderr);
}
#endif



/**
 * Stores the current OS count in the two global variables.
 */
void store_osgetcount(void)
{
#ifdef NATIVE_PORT
    u32 previous_count = copy_of_osgetcount_value_1;
#endif
    copy_of_osgetcount_value_1 = osGetCount();
    copy_of_osgetcount_value_0 = copy_of_osgetcount_value_1;
#ifdef NATIVE_PORT
    pcTraceFrameTiming("store", previous_count, copy_of_osgetcount_value_1, 0);
#endif
}


/**
 * Updates the timing-related counters and frame information based on the given argument.
 *
 * @param deltaFrames The number of frames to add to the current frame counter.
 */
void updateFrameCounters(s32 deltaFrames)
{
#ifdef NATIVE_PORT
    u32 previous_count = copy_of_osgetcount_value_1;
#endif
    copy_of_osgetcount_value_0 = (s32) copy_of_osgetcount_value_1;
    copy_of_osgetcount_value_1 = osGetCount();

    lastFrameCounter = currentFrameCounter;
    currentFrameCounter = (s32) (currentFrameCounter + deltaFrames);
    speedgraphframes = deltaFrames;

    #ifdef BUGFIX_R1
    jpD_800484CC = (f32) deltaFrames;
    #ifdef REFRESH_PAL
    jpD_800484D0 = (jpD_800484CC * 60.0f) / 50.0f;
    #else
    jpD_800484D0 = (f32) jpD_800484CC;
    #endif
    #endif

    previousFrameCounter = (s32) halfFrameCounter;
    halfFrameCounter = (s32) (currentFrameCounter / 2);
    isFrameCounterOdd = (s32) (currentFrameCounter & 1);
    halfMinusPreviousCounter = (s32) (halfFrameCounter - previousFrameCounter);
#ifdef NATIVE_PORT
    pcTraceFrameTiming("update", previous_count, copy_of_osgetcount_value_1, deltaFrames);
#endif
}


/**
 * Waits until the appropriate time has passed before updating the frame counters.
 * This function effectively controls the frame rate by waiting for the next tick.
 */
void waitForNextFrame(void) //maybe WaitForTick
{
  u32 nextFrameTime; //next frame time?
  u32 currentCount;
#ifdef NATIVE_PORT
  extern int pcStableDeterministicCountEnabled(void);
  extern void pcAdvanceDeterministicCountForFrame(void);
#endif

#ifdef NATIVE_PORT
  {
    s32 forcedSpeedframes = pcGetDeterministicSpeedframesOverride();

    if (forcedSpeedframes > 0)
    {
      frameDelay = 1;
      pcTraceFrameTiming("wait-forced", copy_of_osgetcount_value_1, copy_of_osgetcount_value_1, forcedSpeedframes);
      updateFrameCounters(forcedSpeedframes);
      return;
    }
  }
#endif
  
  do {
    currentCount = osGetCount();
    #ifdef REFRESH_PAL
    nextFrameTime = ((currentCount - copy_of_osgetcount_value_1) + 465525) / 931050;
    #else
    nextFrameTime = ((currentCount - copy_of_osgetcount_value_1) + 387937) / 775875; //current time + 1/5
    #endif
#ifdef NATIVE_PORT
    if (nextFrameTime < frameDelay && pcStableDeterministicCountEnabled())
    {
        pcAdvanceDeterministicCountForFrame();
    }
#endif
  } while (nextFrameTime < frameDelay);

  frameDelay = 1;
  #ifdef NATIVE_PORT
  pcTraceFrameTiming("wait", copy_of_osgetcount_value_1, currentCount, nextFrameTime);
  #endif
  updateFrameCounters(nextFrameTime);
}


void setFrameDelay(s32 arg0) {
    #ifdef LEFTOVERDEBUG
    frameDelay = arg0;
    #endif
}

#ifdef VERSION_EU
void eu_sub_7f0c00a4(void)
{
  
}
#endif
