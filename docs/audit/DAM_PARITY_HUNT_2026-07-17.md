# Dam renderer defect hunt — WebGPU backend (feat/webgpu-backend @ d820ff1)

Status: **INVESTIGATION HALTED mid-hunt** (owner deferred the Dam parity program to
post-release). This brief captures everything reproduced, partial/unverified
hypotheses, what was NOT reached, and exact commands to resume.

Binary: `<repo>/build-glyph/ge007` (left as-is, unmodified).
ROM: `<repo>/baserom.u.z64` (local, gitignored). All source READ-ONLY (no edits).
Evidence PNGs: `…/scratchpad/evidence/`. Helpers: `…/scratchpad/cap.sh`, `captape.sh`.

---

## TL;DR ranked list

| id | sev | site | GL-vs-WebGPU | class | stock verdict |
|----|-----|------|--------------|-------|---------------|
| DAM-R1 | P1 | intro swirl f190 **and** gameplay walkway | **GL-clean, WebGPU-WRONG** | WebGPU backend (sky/backdrop over-bright) | UNVERIFIED (stock≈GL=clean expected → real port defect) |
| DAM-R2 | P2 | intro establishing cam (reservoir) | **SAME-WRONG** | gfx_pc room admission (T13b/M3.4/T25) | UNVERIFIED (T25 open) |
| DAM-R3 | P3 | reservoir far-horizon gaps + sky-thru-rock specks | SAME-WRONG | genuine geometry gap (train-sky-leak class) | UNVERIFIED — **likely FAITHFUL** per prior art |
| (neg) | — | reservoir water surface itself | SAME (both clean) | — | **no bleed reproduced** |

**Headline caveat:** the owner's reported "background water bleeding through" is stated
to be long-standing + same-wrong on both backends. I did **NOT** reproduce teal-water
bleeding through geometry — the reservoir water renders correctly occluded on both
backends everywhere I looked. The most prominent background anomaly I DID find (DAM-R1)
is a **WebGPU-only sky seam** (GL clean), so by the owner's own same-wrong criterion it
is a *different* defect from the one they're describing. Either (a) the owner is
perceiving DAM-R1's blue rectangular sky patch as "water," or (b) the true water-bleed
site is a Dam location/angle I did not reach (outro/bungee side and deep interior were
not fully swept). **Resume by getting a stock ares capture — see "Resume" below.**

---

## Tooling / repro harness (verified working)

Deterministic Dam boot + single-frame screenshot (BMP in CWD → I convert with `sips`):
```
cd <evidence-dir>
SDL_AUDIODRIVER=dummy GE007_MUTE=1 <extra-env> \
  build-glyph/ge007 --rom baserom.u.z64 --level dam --no-ui --deterministic \
  --savedir <sd> --screenshot-frame N --screenshot-exit --screenshot-label LBL
```
Key facts learned:
- `--level dam` == LEVELID_DAM (mission 1). `cli_stage_tables.c:27`.
- **Level intro is OFF by default** on direct boot → frame ~0 is already gameplay.
  Enable with `GE007_ENABLE_LEVEL_INTRO=1` (oracle route `native_env`). Intro spans
  rendered frames ~0–300; establishing shot → swirl around Bond → FP handoff.
- `GE007_INTRO_CAMERA_INDEX=N` pins the establishing camera (Dam has ≥7: 0-6).
- Camera survey without input: `GE007_AUTO_LOOK_LEFT/RIGHT/UP/DOWN="start:duration"`
  (frame windows, comma-sep) + `GE007_AUTO_LOOK_STEP=<1-64>` (px/frame yaw).
  `stubs.c:6075,6232`.
- Teleport survey: `GE007_AUTO_WARP_FRAME=<f> GE007_AUTO_WARP_PAD=<0-94>` (+ optional
  `_RIGHT/_FORWARD/_Y_OFFSET`). `stubs.c:1455`. Pad faces its authored heading.
- Tape replay: `--play-tape baselines/tapes/dam_forward_30s.ge7tape` (1802 ticks,
  fwd+strafe — **walks INTO the tunnel and jams on walls/guards; useless for vistas**).
- Discriminators: `GE007_TINT_SKY=1` (sky→magenta); `GE007_RENDERER=gl` (GL fallback
  A/B); `GE007_WEBGPU_DUMP_FRAME=N`/`_DUMP_SURFACE=N` (PPM). Room-admission root-cause:
  `GE007_TRACE_ROOM_CLASSIFY=1` (classifies unrendered frustum rooms dropped/far).
- Outro: `GE007_AUTO_CAMERA_MODE=posend GE007_AUTO_CAMERA_MODE_FRAME=<f>
  GE007_AUTO_CAMERA_POSEND_PAD=<pad>`. `stubs.c:4628`. **Needs the real Dam outro pad —
  my guess (pad 10) landed a road-level shot, not the bungee. Not resolved.**

---

## DAM-R1 — WebGPU sky/backdrop seam (P1, backend, GL-clean) — CONFIRMED

**What:** at camera angles where a room portal splits a large sky area, WebGPU draws an
**over-bright, higher-contrast rectangular sky region** on one side with a hard vertical
seam vs the duller adjacent sky. GL renders the sky uniform/continuous (correct). The
distant-mountain silhouette continues unbroken across the seam → it is the **sky/distant
backdrop**, not sky-vs-geometry.

**Repro (strongest):** intro swirl, frame 190.
```
SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_ENABLE_LEVEL_INTRO=1 build-glyph/ge007 \
  --rom baserom.u.z64 --level dam --no-ui --deterministic --savedir sd \
  --screenshot-frame 190 --screenshot-exit --screenshot-label x
```
Evidence:
- `dam_swirl_f190.png` (WebGPU, seam present) vs `dam_swirl_f190_gl.png` (GL, clean).
- `cmp_f190_sky.png` — 3-panel: WebGPU / GL / diff×4. **Decisive.**
- `dam_swirl_f190_tintsky.png` — both regions tint magenta → both are sky.
- **Also in GAMEPLAY:** `contact_p72pan.png` top tile (`dam_p72_lookL.png`) shows the
  same rectangular sky seam from the dam walkway → **not intro-only.**

**A/B / gating results (all CONFIRMED):**
- `GE007_RENDERER=gl` → seam GONE (backend bug, not shared gfx_pc).
- `GE007_NO_CAMERA_SEED_WALK=1` (T13b off) → seam PRESENT (`dam_swirl_f190_nowalk.png`).
  Not the camera-seed draw-only rooms.
- `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` → seam PRESENT (`dam_f190_noxlucvg.png`).
- `GE007_DISABLE_SKY_BACKDROP_DEPTH=1` → seam PRESENT (`dam_f190_nobackdropdepth.png`).
- Pixel sample (frame 190): left sky WG=(69,104,155) vs GL=(66,87,124) — WebGPU left
  region is brighter/more saturated; right region matches GL. So WebGPU **adds
  brightness to one region only.**

**Mechanism — UNVERIFIED hypotheses (ranked):**
1. Double-drawn / additively-composited sky where two rooms' sky quads overlap in screen
   space, WebGPU stacking them (GL replaces/depth-rejects the 2nd). Rectangular bound =
   portal aperture.
2. Fog/combiner divergence: near-room sky fogged (dull), far-through-portal sky
   unfogged (bright), WebGPU applies fog per-region differently.
   Note TINT_SKY (`dam_estab_i4_f045_tintsky.png`) shows the sky is layered (magenta
   cloud band + an UN-tinted flat-blue horizon fill) — two sky primitives, consistent
   with a per-primitive/per-region divergence.

**Code anchors to start:** sky classification `g_sky_tri_mode` and the sky/backdrop
material path `gfx_pc.c:5337-5535` (`gfx_room_water_alpha_suppress_needed`,
`gfx_room_xlu_cvg_memory_gate_reason` returns "sky" at :5447, `gfx_sky_backdrop_depth_enabled`
:5515); WebGPU fog shader `gfx_webgpu.c:2779-2801,2990` and blend/depth pipeline keying.

**Fix-shape sketch (UNVERIFIED):** compare the sky primitive's per-draw fog/blend/depth
state as WebGPU sees it vs GL for the two seam regions (add a per-draw trace keyed on
`g_sky_tri_mode`); the fix is almost certainly in the WebGPU pipeline/blend translation,
NOT in gfx_pc. **Regression risk LOW** (backend-local, sky-only) but MUST A/B every level's
sky (Surface/Frigate/Caverns have big skies) and keep GL byte-identical.

---

## DAM-R2 — distant reservoir shore/mountains absent from establishing cam (P2, admission, same-wrong) — CONFIRMED, stock UNVERIFIED

**What:** the intro establishing camera over the reservoir (`INTRO_CAMERA_INDEX=4`) shows
the water extending to a **bare water-to-sky horizon on the right — no distant shore /
mountains** (`dam_estab_i4_f045.png`, `zoom_i4_horizon.png`). The **same mountains DO
render in gameplay** from the walkway (`dam_warp2_p70.png` / `dam_warp2_p72.png`) → the
geometry exists; it is simply not admitted from the detached establishing camera.

**A/B:** WebGPU vs GL identical here — `dam_estab_i4_f045_gl.png`, diff mean 5/px. Water
surface renders the same on both. This is a **shared gfx_pc room-admission** effect, the
**T25 / T13b-M3.4 camera-seed residual** (memory: "distant reservoir mountains not
portal-reachable from room 53, still absent — needs stock pixel comparison"). This is the
strongest candidate for "background differs in certain parts": from the establishing
camera the water fills the far background where the shore should be.

**Stock verdict UNVERIFIED** — this is exactly the T25 open item; needs stock ares.
**Fix-shape:** the camera-seed walk (T13b, DEFAULT ON) reaches the camera room + rescue
hops but not the multi-hop distant-shore room; extending its frontier (`GE007_CAMERA_SEED_WALK_HOPS`
exists as an opt-in deeper reach) as **draw-only** could admit it. **Regression risk:
MEDIUM** — touches the room-admission draw-only machinery that other levels' intros
depend on; must keep sim byte-identical (draw-only invariant) and re-run intro oracles.

---

## DAM-R3 — far-horizon gaps + sky-through-rock specks (P3, geometry gap, likely FAITHFUL)

- Reservoir far-horizon between mountain masses = hazy fogged background, no distinct
  distant terrain (`zoom_p72_junction.png`). Waterline itself is CLEAN (water correctly
  occluded, no teal creeping up the mountains).
- Small sky pixels through rock gaps during swirl (`dam_swirl_f230.png` top-center speck;
  TINT confirms sky, `dam_swirl_f230_tintsky.png`).
- **Class = the "train sky leak" (genuine geometry gaps; stock shows them too →
  FAITHFUL, close as not-a-bug).** Stock UNVERIFIED but prior art strongly suggests
  faithful. Do NOT "fix" without a stock capture proving stock occludes it.

**Negative result (important):** the reservoir **water surface does NOT bleed through the
dam wall or mountains** on either backend. Checked: `zoom_i4_wallbase.png` (wall cleanly
occludes water, clean waterline w/ algae band), pads 65/68/70/72/75/78/80
(`contact_warp2.png`), grazing pans left/right/down at pad 72 (`contact_p72pan.png`).
Downstream drop side (pad 80) is a flat-blue void — faithful.

---

## Intro / outro D-item audit (partial)

Confirmed HEALTHY at HEAD:
- **Bond body render** — visible, no red-shards / invisible / spiky, in swirl
  (`dam_swirl_f230.png`) and end-pose (`dam_swirl_f250.png`). D12/Bond-body-fix good.
- **Establishing shots render full scene** (not blank-blue) across indices 0-6
  (`dam_estab_i0..i6_f045.png`) → D42/T13b working; location text "Byelomorye Dam,
  Arkangelsk, USSR" renders.
- Establishing shots (indices 2/3/4) WebGPU≈GL (diff ~5/px, texture-filter noise only;
  `cmp_i3.png`).

Open / not re-verified this pass:
- **DAM-R1 sky seam is a NEW finding NOT in `docs/design/INTRO_OUTRO_LEDGER.md`.** Add it.
- Ledger D32/D35/D36/D37/D38/D39/D41 (camera-anchor / anim-phase / static-shot
  cam_floor/cam_delta divergences) — NOT re-tested (pixel-peeping, needs oracle).
- **OUTRO fully UNTOUCHED** — could not trigger the bungee outro (need the real Dam
  POSEND pad; pad 10 guess gave a road-level shot `dam_outro_try.png`). The bungee side
  is the most likely place the owner sees "water" that I never inspected.

---

## What I did NOT get to (resume here)

1. **STOCK ares parity capture — THE decisive missing piece.** Ares binary EXISTS:
   `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`.
   Run the ready-made Dam intro oracle (native+stock, camera path over intro/swirl):
   ```
   ctest --test-dir build-glyph -R intro_oracle_dam_route -V     # ~ up to 900s
   # or directly:
   tools/movement_oracle_capture.sh --route dam_intro_camera_path \
     --build-dir build-glyph --rom baserom.u.z64 --no-build \
     --ares-bin <ares path above>
   ```
   NOTE: `dam_intro_camera_path.json` compares camera-path VECTORS, not pixels — confirm
   whether it also dumps stock frames before relying on it for the seam. For a true
   stock *pixel* reference of the reservoir you likely need a new route or the
   `tools/startup_visual_parity_capture.sh` pattern pointed at Dam.
2. **Root-cause DAM-R1** — per-draw fog/blend/depth trace of `g_sky_tri_mode` sky quads,
   WebGPU vs GL, at frame 190. Identify which room's sky is over-bright.
3. **Outro** — find the Dam POSEND pad (dump setup intro list / `bondview.c`
   `stage_intro_anim_table` @1481) and inspect the bungee cinematic for water bleed.
4. **Deep interior sweep** — I only warped a handful of pads; rooms 30-60 not fully swept.
5. Confirm DAM-R3 faithful via stock (train-sky-leak precedent).

## Evidence index (…/scratchpad/evidence/)
- Sky seam: `dam_swirl_f190.png`, `_gl.png`, `_nowalk.png`, `_tintsky.png`,
  `cmp_f190_sky.png`, `dam_f190_noxlucvg.png`, `dam_f190_nobackdropdepth.png`,
  `contact_p72pan.png` (gameplay).
- Intro swirl arc: `dam_swirl_f110..290.png`, `dam_intro_f030..400.png`.
- Establishing cams: `dam_estab_i0..i6_f045.png` (+ `_gl`/`_tintsky` for i2/i3/i4),
  `cmp_i3.png`, `zoom_i4_wallbase.png`, `zoom_i4_horizon.png`.
- Reservoir gameplay: `dam_warp_p70.png`(+`_gl`,`_tintsky`), `cmp_p70.png`,
  `zoom_p70_waterline.png`, `contact_warp2.png` (pads 25/50/60/65/68/72/75/78/80/90),
  `zoom_p72_junction.png`, `contact_p72pan.png`.
- Interior/other: `dam_warp_p40.png`, `p55`, `p85`; `dam_outro_try.png`.
- Forward tape (jams on walls): `dam_fwd_f0200..1700.png`.
