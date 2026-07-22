# FID-0141 residual — low-frame-rate audio pump cushion

## Problem

The first FID-0141 fix correctly rebased the live occupancy controller from a
30 Hz audio-task frame (736 sample frames) to one 60 Hz render pump (368 sample
frames). That makes the controller's fixed point equal its configured queue
target at 60 Hz, but the base was still hard-coded to 60 Hz.

At a sustained pump rate `f`, the old live controller balanced at:

```
queue_before = target - 2 * (22050 / f - 368)
```

Thus the 50 ms target became about 33 ms at 40 fps and 17 ms at 30 fps. Audio
could remain unbroken in a perfectly regular run, but almost all of the jitter
cushion disappeared on the weak machines that need it most.

## Controller contract

`src/platform/audio_queue_controller.c` measures the real wall-clock interval
between non-deterministic `portAudioFrame` pumps and maintains a 1/4-rate EMA in
Q8 sample-frame units. Its production base is:

- never below the proven 60 Hz base (368), so 60 Hz and catch-up jitter cannot
  thin the queue or make the aligned synth size chatter;
- rounded to the nearest 16-sample libaudio quantum;
- capped at the existing synthesizer maximum (784), which supports sustained
  cadence down to about 28.1 fps; no queue controller can create enough audio
  below that rate without increasing the synth-buffer limit;
- reset after the web prefill loop, so tight prefill calls and the following
  synchronous level load are not misclassified as gameplay pump intervals.

The existing occupancy feedback, drain floor, cap, and `Audio.QueueTargetFrames`
meaning are unchanged. The deterministic branch still takes the aligned
Bresenham path and never reads the wall clock. `GE007_NO_AUDIO_PUMP_EMA=1` is the
default-off negative control that restores the fixed 368-sample base.

## Durable model gate

CTest `audio_queue_controller` calls the production helper directly and drives
the real `audi_port.c::portAudioSizeFrame` through test-only clock/queue stubs,
so a wiring revert also fails. It pins:

- 60 Hz base = 368;
- 40 Hz convergence = 544 (nearest aligned value to 551.25);
- 30 Hz convergence = 736 (nearest aligned value to 735);
- very-low-rate clamp = 784;
- recovery from 30 Hz back to the 60 Hz base;
- queue dynamics at 40 and 30 fps, including a fixed-base negative control that
  must reproduce the old thinned cushion while the adaptive path retains more
  than 90% of the configured target.

## Real native A/B

Release `build/ge007`, Dam, SDL dummy device, normal non-deterministic gameplay,
`Video.FrameCap=30`, 299 traced pumps. Statistics below use the final 120 pumps.
Both runs used `GE007_AUDIO_TRACE`; OFF additionally set
`GE007_NO_AUDIO_PUMP_EMA=1`.

| 30 fps result | EMA on (default) | EMA off (negative control) |
|---|---:|---:|
| pump base | 736 | 368 |
| mean `queue_before` | 4352.5 B / 49.35 ms | 1434.7 B / 16.27 ms |
| `queue_before` range | 37.73–62.40 ms | 5.80–27.57 ms |
| dropped buffers | 0 | 0 |
| post-prime underruns | 0 | 0 |

At `Video.FrameCap=60`, the last 120 pumps held `pump_base_samples=368` exactly,
with zero drops and zero underruns. This is the production non-regression check:
the adaptive term is inert at the shipping frame rate.

The trace now records `pump_base_samples`, making the behavior re-derivable in
future native or browser captures rather than dependent on session-local
instrumentation.
