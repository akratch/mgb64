# FID-0145 — WebGPU live draw-boundary readback

## Problem

The native `[TRI-PIXEL]` and `[SETTEX-PIXEL]` probes sample the framebuffer
immediately before and after selected draws. WebGPU's `wgpu_flush()` only
recorded commands; the scene command encoder was not submitted until
`end_frame`. `wgpu_read_framebuffer_rgb()` also preferred the retained
presentation texture, which contained the previous completed frame during an
open frame. A 2,156-row all-triangle capture consequently reported zero changed
rows and could not identify the draw that owned a disputed pixel.

Metal already implemented the required diagnostic contract by committing and
waiting for the live encoder, then resuming the render pass with Load semantics.

## Fix

When and only when framebuffer readback occurs during an open WebGPU frame:

1. end the current scene pass;
2. upload WEB-023's deferred vertex shadow before submitting draws that refer to
   those buffer offsets;
3. finish and submit the current encoder;
4. begin a new encoder and scene pass with color/depth Load+Store;
5. invalidate dynamic pass state so the next draw rebinds it; and
6. read the live `s_scene_tex` rather than the prior presentation target.

End-of-frame screenshot behavior is unchanged and still reads the post-FX or
minimap presentation target. With no diagnostic readback, gameplay retains the
ordinary single-upload, single-submit frame path.

## Evidence

The focused Dam frame-122 texnum-654 probe at logical pixel `[94,95]` changed
from the previous-frame false negative to real per-draw transitions:

- triangle 788: `[15,15,15] -> [18,18,18]`; predicted alpha output `18`;
- triangle 791: `[18,18,18] -> [14,14,14]`; predicted output `15` (one luma
  from the observed result).

The complete focused owner chain also recovered the sky transition
`[16,48,96] -> [42,71,113]`, the texnum-949 edge owner to `[15,15,15]`, and the
two texnum-654 transitions above. The probe-armed and unarmed `640x480`
screenshots were byte-identical: `0 / 307200` pixels changed. The stock/native
route and screenshot-health gates remained green.

CTest `port_webgpu_live_readback_guard` is the ROM-free ratchet. It fails if the
readback slow path stops selecting the live scene, omits the partial submit or
deferred vertex upload, fails to resume both attachments with Load/Store, or
spreads partial submission into the normal gameplay path.
