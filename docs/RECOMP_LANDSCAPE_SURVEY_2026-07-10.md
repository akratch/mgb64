# Recomp Landscape Survey — 2026-07-10

**What this is.** A deep-dive on what mgb64 can learn and adopt from the N64
recompilation/port ecosystem: **GoldenRecomp** (kholdfuzion/sonicdcer's
N64Recomp+RT64 recompilation of GoldenEye), **RT64** (the renderer behind the
recomp projects), **Zelda64Recomp**, the **sm64ex** 60fps lineage, and
adjacent projects. Findings are ranked by adoption effort: **Tier 1 drop-in**,
**Tier 2 moderate engineering**, **Tier 3 aspirational**.

**Method.** Three parallel tracks on 2026-07-10:
1. A fan-out research pass (103 agents) with 3-vote adversarial verification
   per claim; 15 findings survived, 0 refuted. Verifiers cross-checked claims
   against *our own tree* where applicable.
2. A full source-level read of a GoldenRecomp clone — every file in
   `patches/` plus runtime (`src/main`, `src/game`), build pipeline, and
   commit history. Their patch directory is effectively a documented list of
   GoldenEye-on-PC landmines.
3. A grounding inventory of mgb64 (renderer, timing, texpack, widescreen,
   input, sky, audio, MP, distribution) so effort tiers reflect what we
   already have.

**Top-line verdict.** GoldenRecomp does not leapfrog mgb64 — it *corroborates*
us. Its two headline weaknesses (framerate-coupled gameplay, failed
interpolation and sky recodes) are exactly the areas our F5 plan and native
sky queue address at source level. The most valuable imports are (a) a likely
**live fidelity bug in mgb64 today** (F1), (b) three concrete correctness
patterns for the F5 uncapped-FPS interpolators (F14), and (c) RT64's
field-proven texture-pack model (F7/F15/F16).

---

## Tier 1 — Drop-in ready (audit items and free wins)

### F1. 🔥 Weapon fire rate is coupled to rendered-frame count — likely live divergence in mgb64

**Finding.** GoldenEye gates full-auto fire on a **per-rendered-frame counter,
not a tick-scaled one**. In our tree: `src/game/gun.c` (~17927) increments
`hand_ptr->field_88C++` once per frame, *unscaled* — while the adjacent
`field_890 += g_ClockTimer` is properly tick-scaled — and fire is gated on
`field_88C % fire_rate`. On N64, frames cost 2–4 fields (counter ran at
15–30Hz). mgb64 runs a locked 60Hz loop with `g_ClockTimer == 1` essentially
always, so the counter runs at 60Hz: **automatics likely fire 2–4× faster
than on hardware**. GoldenRecomp confirms empirically — README known-issue:
*"Certain guns shoot too fast because the game is running at native 60 all
the time, their fire rate needs to be adjusted."* (Unfixed in their tree.)
Verified 3-0 with the mechanism independently confirmed in our source.

**Adoption concept.** Open a FID ledger entry. Oracle lane: ares reproduces
real N64 frame costs, so sustained-fire cadence A/B (input tape holding
fire; count shots per wall-second in oracle vs port) settles it objectively.
Then a fidelity-policy call: "retail code at 60Hz" vs "retail experience at
15–30Hz." If remediated, scale the per-frame counters by `g_ClockTimer`
behind a setting, with the oracle adjudicating target cadence (see F18).

**Sources.** [GoldenRecomp README](https://github.com/kholdfuzion/GoldenRecomp);
our `src/game/gun.c` ~17927–17928, ~18360.

### F2. The locked-60 casualty checklist: intro pacing, menu timers, attract demos

**Finding.** GoldenRecomp's patches enumerate everything else that broke when
the sim locked to 60 — a ready-made audit list for us:
- **Gunbarrel/title intro** ran too fast; they re-timed it with a
  `waitForNextFrame2()` clone that sets `frameDelay = speedgraphframes`
  (`patches/workbench_theboy.c:7`, `patches/fps.c:35`).
- **Menu timers** are frame-count based (`MENU_LEGALSCREEN_MENU_TIMER_MAX`).
- **Attract demos (ramrom recordings)** were recorded against real
  per-frame `speedgraphframes` and desync at constant 60. Their fix: a
  `demoMode` global plus a retrace gate in their reimplemented
  `bossMainloop` that only ticks after
  `MAIN_LOOP_TICK_INTERVAL * (speedgraphframes * 2)` COUNT ticks
  (`patches/workbench_theboy.c:476,633-638`).

**Adoption concept.** Three cheap fidelity lanes: (1) time the gunbarrel/
legal-screen sequence against the ares oracle; (2) grep menu code for
frame-count timers and compare durations; (3) verify attract-demo playback
timing — this matters doubly because our ramrom replay is *itself* oracle
infrastructure, and a cadence mismatch would silently bias any lane built
on it.

**Sources.** GoldenRecomp `patches/workbench_theboy.c`, `patches/fps.c`
([repo](https://github.com/kholdfuzion/GoldenRecomp)).

### F3. Widened culling has sim-visible side effects — corroborates our render→sim coupling

**Finding.** GoldenRecomp discovered their widescreen culling-window widening
(`bgUpdateCurrentPlayerScreenMinMax` fudged to ±4 screens) and prop draw-in
extension (`getinstsize` ×4) **desync attract demos**; they revert to stock
values while `demoMode` and during the Jungle intro camera
(`patches/widescreen.c:216,243,fixups`). This independently confirms the
coupling we found at `chr.c:5205` (`room_rendered` → auto-aim): GoldenEye's
visibility computation feeds back into non-render state.

**Adoption concept.** Our own `widenCullHorizontal()` fix
(`src/game/bondview.c:2064-2090`) may make **auto-aim aspect-ratio-dependent**.
Fidelity lane: run identical input tapes at 4:3 vs widescreen and diff
sim-state hashes; if they diverge, decide whether to (a) accept and document,
(b) decouple the sim-visible read from the widened render cull. Also feeds
F14: render-only interpolated frames must not run any culling that mutates
sim state — extend the 0-tick purity fuzz (FID-0033) to cover widescreen
cull state.

**Sources.** GoldenRecomp `patches/widescreen.c`; our
`src/game/bondview.c:2064-2090`, `src/game/chr.c:5205`.

### F4. `musicTrackNPlay` busy-wait — latent hang landmine

**Finding.** GE busy-waits `while (alCSPGetState(seqPlayer));` for the
sequence player to stop before loading the next track, in **three copy-pasted
functions** (`musicTrack1Play/2Play/3Play`). This hung GoldenRecomp two ways:
clang collapsed the "empty" spin (they added `__attribute__((optnone))`), and
the spin starved their cooperative scheduler (they inject a host
`yield_self_1ms()` — `patches/audio.c:22-25`, commits `6eac063`/`e2dc7e8`/
`7057218`).

**Adoption concept.** 5-minute audit: grep our tree for the `alCSPGetState`
spin. Our synth runs synchronously per frame (`portAudioFrame()`,
`src/platform/audi_port.c:417`), so if the spin exists on our main thread it
either passes immediately (state already stopped) or would deadlock — since
music works, likely the former, but that makes it a latent landmine for rapid
track transitions. Classify, comment, and (if present) bound it.

**Sources.** GoldenRecomp `patches/audio.c`; our `src/platform/audi_port.c:417`.

### F5. Rare's dormant in-game debug menu is re-enableable

**Finding.** GoldenRecomp's HEAD commit ("enable debug mode") force-enables a
leftover Rare debug menu via the PD-beta **C-Up+C-Down chord** inside
`bossMainloop`. Retail GE also ships the `rmon` debug-monitor thread (they
stub it) and a built-in CLI-ish token parser (`-level_XX`, `-hard`,
`-ma <kb>` via `tokenFind`).

**Adoption concept.** Check whether the debug menu path survives in the
decomp and gate it behind a `GE007_DEBUG_MENU` env flag — free dev/fidelity
tooling (their disabled `speedgraphDisplayMetrics` patch also shows how to
read GE's own utz/rsp/tex % counters). The token parser may duplicate or
compose with our `--level`/AUTO_* direct-boot flags.

**Sources.** GoldenRecomp `patches/workbench_theboy.c:476+` (bossMainloop
recode), commit `f31b5d1`.

### F6. LOD selection shifts at high output resolution

**Finding.** GoldenRecomp needed `getPlayer_c_lodscalez / 8`
(`patches/widescreen.c:243`, "Proper LOD fix?") plus RT64-side
`f3dex.forceBranch = true` and `textureLOD.scale = true` — at high output
res, GE's model-LOD selection kicks to low-detail meshes too eagerly.

**Adoption concept.** Audit whether our remaster resolutions (RenderScale,
SSAA) shift LOD/model selection vs stock. If yes, one-line aspect: scale
`c_lodscalez` by resolution factor behind a default-on remaster flag with a
`GE007_*` opt-out, per house style.

**Sources.** GoldenRecomp `patches/widescreen.c:243`,
`src/main/rt64_render_context.cpp:259-266`.

### F7. Texture-pack format policy: BC7 DDS, never ship PNG, low-mip placeholder cache

**Finding.** RT64's shipped texture-pack model (verified against current
source + docs): **DDS with BC7 block compression** (~¼ the VRAM of RGBA32
PNG at negligible quality loss; PNG explicitly dev-only — *"Do not ship
texture packs using PNG files to end users"* — and gets no generated mips);
plus a **prebuilt low-mipmap cache** — all lowest mips extracted into one
file loaded at start, used as instant placeholders, *"basically eliminating
most visual pop-in."*

**Adoption concept.** Adopt the *policy* now, ahead of the plumbing (F15):
`tools/texpack/build_pack.py` grows a BC7-DDS emit path (e.g. via
`texconv`/`bc7enc`) and a `--low-mip-cache` output. Loader support is Tier 2.
**Platform gotcha:** macOS OpenGL 4.1 lacks BPTC/BC7; Metal on Apple silicon
supports BCn natively — one more reason Metal is the forward path; GL keeps
PNG fallback.

**Sources.** [RT64 TEXTURE-PACKS.md](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md);
[RT64 PR #34](https://github.com/rt64/rt64/pull/34) (merged 2024-07-26).

### F8. Zelda64Recomp validates per-effect widescreen patching; ultrawide cutscene residual predicted

**Finding.** Z64R's widescreen/ultrawide is per-subsystem patch files
(`culling.c`, `effect_patches.c`, `cutscene_patches.c`, `camera_patches.c`),
any aspect supported, HUD optionally pinned to 16:9 — structurally the same
as our per-fix approach (sky bars, blood overlays, edge-cull, crosshair,
pane-aware rects). Their documented residual: *"Some animation quirks can be
seen at the edges of the screen in certain cutscenes when using very wide
aspect ratios."* Verified 3-0.

**Adoption concept.** No rework needed — external validation. Add one QA
item: sweep intro/outro cinematics at 21:9/32:9 for edge-of-frame animation
artifacts (our anamorphic-squeeze approach fills arbitrary aspect but was
never purpose-tuned past 16:9). Their `culling.c` is a reference if we ever
revisit aspect-aware culling (see F3).

**Sources.** [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)
README + `patches/`.

### F9. Stick-range mapping constant

**Finding.** GoldenRecomp maps full SDL stick deflection to **×0.65** of N64
range, clamped (`src/game/controls.cpp:112`) — GE's tuning expects raw N64
sticks (~±84 practical). An un-scaled full-range stick plays "too hot."

**Adoption concept.** Cross-check our deadzone→curve→scale chain
(`src/platform/radial_deadzone.c`, `Input.GamepadLookSpeed/Curve`): what raw
magnitude does full deflection deliver to game code vs an N64 stick? If we're
delivering ±127-equivalent, movement/turn saturation behavior differs from
hardware — worth one FID lane given FID-0015 already covers deadzone
geometry.

**Sources.** GoldenRecomp `src/game/controls.cpp:78-116`.

### F10. Sky/water is CPU-built raw RDP triangles — our native sky queue is the established pattern

**Finding.** Root cause of their broken skies, stated in code + README: GE
builds **raw RDP triangle commands on the CPU** for sky clouds and water
(`sub_GAME_7F097818` single tri, `sub_GAME_7F098A2C` quad — direct
edge-coefficient emission). RT64's DL interpreter has no path for them.
Their shipping workaround: both functions patched to a **flat fog-colored
fill rect** — Dam sky and Frigate water render as untextured fog color. Their
real recode (`skyRender`, 1,100 lines, preserving GE's 16-case frustum
clipping, `PORTSKY` gSPVertex+gDPTri path) *worked in an emulator but failed
in the recomp* and sits disabled. The Perfect Dark PC port recoded sky
natively (`skybox.c`) for the identical reason. Our native sky queue
(`gfx_pc.c:3569-3603`, `"SKY"` G_NOOP marker → capture → replay with
preserved viewport) is the same class of solution, working.

**Adoption concept.** No action on architecture — validation. Two reuses:
(1) their mapped environment-struct fields (`Clouds`, `IsWater`,
`SkyImageId`, `WaterImageId`, `g_SkyCloudOffset` scroll,
`get_room_data_float1()/30.0f` sky scale) as cross-reference when we next
touch the **train sky leak** (genuine geometry gaps, N64-faithfulness still
unverified); (2) PD's `skybox.c` as a second reference implementation.

**Sources.** GoldenRecomp `patches/skybox.c`, README;
[fgsfds/perfect_dark](https://github.com/fgsfds/perfect_dark) `skybox.c`;
our `gfx_pc.c:3569-3603`.

### F11. Custom Rare GBI (G_TRI4 0xB1, G_SETTEX 0xC0) — externally validated

**Finding.** The decomp's RSP sources confirm GE runs a Rare-customized GBI —
`G_TRI4 = 0xB1` (four tris per command), `G_SETTEX = 0xC0` — the same
boundary GLideN64 needed a dedicated F3DGOLDEN ucode for. Our renderer
already implements it (`gfx_pc.c` case `0xB1`; decoded in `bg.c:11810`,
`lightfixture.c:455`) and correctly builds against base GBI, not F3DEX2.

**Adoption concept.** None — recorded as external validation of a
load-bearing renderer decision (`gfx_pc.c:7-9` comment block).

**Sources.** [goldeneye_src](https://gitlab.com/kholdfuzion/goldeneye_src)
`docs/StructureGuide.md`, `include/gbi_extension.h`;
[GLideN64 #1300](https://github.com/gonetz/GLideN64/issues/1300).

### F12. TLB machinery: the recomp-blocking hazard we're immune to

**Finding.** GE TLB-maps game code into `0x7F000000` (decomp modules
`tlb_manage.c`, `tlb_random.c`, `tlb_resolve.s`, `tlb_hardwire.s`;
128-entry table, 8KB-aligned pages). N64Recomp doesn't support TLB, so
GoldenRecomp requires a refactored **TLBFREE + decompressed** decomp branch
(`TBLFREE_NOCOMPRESSION`), which as a side effect needs the Expansion Pak on
console, keys to a modified ROM hash, and forces users to build from source.
They also hit GE's **jumptables stored outside the code segment** (needed an
N64Recomp fork, ELF-input-only, pinning them to old patched binaries) and
Rare's `rmon` thread (stubbed).

**Adoption concept.** No action — this is the structural moat of the source
port. Keep the decomp TLB modules bookmarked as ground truth for any
addressing-related behavior question. Useful in public comms: the entire
TLBFREE/jumptable/rmon hazard class simply doesn't exist for mgb64.

**Sources.** GoldenRecomp README + `patches/boot.c`;
goldeneye_src `src/tlb_manage.c` and siblings.

### F13. Cross-region byte-matched decomp = PAL/NTSC-J oracle material

**Finding.** kholdfuzion/goldeneye_src (active as of 2026-04) builds
byte-matched **NTSC-U, NTSC-J, and PAL** ROMs, SHA1-verified (U hash matches
TASVideos' verified retail dump). ~89.4% C-matched, remainder ships as
checksum-verified assembly.

**Adoption concept.** Extends the fidelity flywheel: cross-region lanes
(PAL 50Hz timing, region-specific behavior) get retail-truth references for
free. Also the authoritative place to diff when our reimpl and retail ASM
disagree (per the standing rule: `#else` reference bodies lie; the matched
decomp doesn't).

**Sources.** [goldeneye_src](https://gitlab.com/kholdfuzion/goldeneye_src)
(README SHA1s); [n64decomp/007 mirror](https://github.com/n64decomp/007).

---

## Tier 2 — Moderate engineering

### F14. F5 uncapped-FPS plan: three imports from sm64ex + shipped proof from Zelda64Recomp

**Finding.** The sm64ex 60fps patch (canonical matrix-level interpolation,
1,965 lines, verified line-by-line by the research pass) interpolates at the
same architectural hook our plan chose — matrix submission — via a parallel
interpolated matrix stack, patching matrices into the already-built DL
(`mtx_patch_interpolated()`) without re-traversing geometry. Three
correctness patterns transfer directly:

1. **Timestamp validity gating.** Every interpolated state stamps
   `prevTimestamp`; blend only when `gGlobalTimer == prevTimestamp + 1`,
   else snap. Teleports, spawns, level loads self-heal (~10 sites in the
   patch).
2. **Explicit hard-cut suppression.** `skip_camera_interpolation()` stamps
   from every cutscene/fixed-cam transition; objects analogously stamp
   `skipInterpolationTimestamp` on teleport.
3. **Per-frame alpha, not t=0.5.** sm64ex hardwires midpoints (30→60 only);
   sm64coopdx generalized to `patch_interpolations(delta)` for free render
   rates — the shape UNCAPPED_FPS_PLAN.md already specifies.

Field evidence, both directions: **Zelda64Recomp ships exactly our Path A**
(fixed-rate sim — 20Hz there, 60Hz for us — plus interpolated render at any
framerate) with *zero reported gameplay divergence*; issue tracker shows only
visual edge cases (uninterpolated HUD/reticle animations, pacing jitter
>120fps). Meanwhile **GoldenRecomp's interpolation attempt failed and was
rolled back** (`patches/interpolation.c` stub; per-room
`gEXMatrixGroupDecomposed` tagging, commit `14e1e0f`) — RT64's heuristic
draw-call matching couldn't handle GE's CPU-built matrices. Interpolation is
a race a source port wins by construction.

**Adoption concept.** Amend `docs/design/UNCAPPED_FPS_PLAN.md`:
- CHRPROP matrix lerp at `gfx_sp_matrix` (`gfx_pc.c:15914`) keyed by the
  existing `gfx_set_prop_context` identity gains a **per-prop `prevTick`
  stamp**; lerp iff `prevTick == curTick - 1`, else snap-and-restamp.
- Eye lerp gains an explicit **skip-stamp API** called from warp, intro
  camera cuts, death, level transition.
- Interpolators take **alpha = elapsed/16.667ms**, never a constant.
- Import Z64R's edge-case list (HUD animations, >120fps pacing) as F5 QA
  items; extend the 0-tick purity fuzz (FID-0033) to assert widescreen/cull
  state purity on render-only frames (ties to F3).

**Sources.** [sm64ex 60fps_ex.patch](https://github.com/sm64pc/sm64ex/blob/nightly/enhancements/60fps_ex.patch);
[sm64coopdx](https://github.com/coop-deluxe/sm64coopdx);
[Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) README +
issues #654/#514/#351/#651; GoldenRecomp `patches/interpolation.c`.

### F15. Async texture streaming stack (RT64 blueprint)

**Finding.** RT64's full streaming model (verified against current source):
three load modes — async **stream** (default), **preload** (all-in-memory),
**stall** (block renderer) — worker `StreamThread`s, **LRU eviction** by
access age (`TextureMap::evict()`), packs as zip/zstd archives read via
**memory-mapped files** (`rt64_filesystem_zip.cpp`), a filesystem abstraction
allowing **multiple packs stacked**, and the F7 low-mip placeholder cache
integrated at load. Sequencing lesson (F17-adjacent): Zelda64Recomp merged
the whole framework **with no pack-selection UI** (deferred to a later mod
menu) — infra first, launcher later, no coupling.

**Adoption concept.** Successor to our synchronous PNG-on-first-use loader
(`src/platform/texture_pack.c`): keep the `G_SETTEX`-decode hook, add a
stream mode (default) with placeholder-from-low-mip-cache on miss, LRU cap
sized to VRAM budget, `.zip` pack mounting alongside the existing directory
layout. ImGui pack UI deferred without blocking. Natural M-item for the
v0.4.0 remaster track.

**Sources.** [RT64 PR #34](https://github.com/rt64/rt64/pull/34);
RT64 `src/render/rt64_texture_cache.*`, `src/common/rt64_filesystem_zip.cpp`;
[Z64R PR #262](https://github.com/Zelda64Recomp/Zelda64Recomp/pull/262)
(Wiseguy: framework merged, menu later).

### F16. Content-hash texture keying: XXH3 over staged TMEM

**Finding.** RT64 identifies replacement textures by **XXH3 over the bytes
actually staged in TMEM** (load-tile-aware region of the 4KB TMEM; versioned
algorithm, currently v5, handling RGBA32 half-TMEM split, TLUT masking,
odd-row word swap) — robust against assets moving in RAM, unlike Rice's
RDRAM-read hashing (*"often rely on reading unrelated memory"*). The hasher
is isolated in **one standalone header** (`src/common/rt64_tmem_hasher.h`).
Lookup is manifest-driven (`rt64.json`); authoring is dump → `texture_hasher`
→ manifest.

**Adoption concept.** Our `tok####.png` scheme keys on a 12-bit token
(`(w1 & 0xFFF)`, 0–4095) — collision-prone and not stable across asset
reshuffles; fine for our curated pack, fragile for community packs. Since
`gfx_pc` already stages texture bytes at decode, computing a TMEM-style
content hash there is contained work: add hash-keyed lookup alongside
token-keyed (fallback order: content-hash → token), and teach
`tools/texpack/build_pack.py` + `validate_pack.py` to emit/check a manifest.
Check RT64's license before lifting the header verbatim; otherwise
reimplement against their documented algorithm. Bonus: hashes double as
texture-identity assertions for fidelity lanes.

**Sources.** [RT64 TEXTURE-PACKS.md](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md);
RT64 `src/common/rt64_tmem_hasher.h`; our `src/platform/texture_pack.c:23-25`.

### F17. Hardware-verified TMEM/texture-load validation lanes

**Finding.** RT64's accuracy reputation rests on a TMEM loader
reverse-engineered by **observing real-console behavior with purpose-built
homebrew test ROMs** (Wiseguy), not emulator folklore — "effect-accurate HLE
with no game-specific workarounds," not bit-exact LLE. Known RT64 gaps were
in VI post-filtering, not core texture paths. The verifiable edge cases:
RGBA32 half-TMEM split, TLUT masking, odd-row word swap.

**Adoption concept.** A fidelity sense-lane for our texture decode: exercise
the named edge cases against hardware-verified expectations (Wiseguy's ROMs
if public — open question O3 — else
[n64-systemtest](https://github.com/lambertjamesd/n64-systemtest)-class
suites run under ares as proxy). Any divergence found becomes a normal
FID fix cycle. This hardens exactly the paths our HD-pack hook rides on.

**Sources.** [RT64 README](https://github.com/rt64/rt64); Software
Engineering Daily interview w/ Dario Samo + Wiseguy (Oct 2024).

### F18. Fire-rate remediation design (contingent on F1 adjudication)

**Finding/concept.** If the F1 lane confirms divergence and the charter rules
N64-practical cadence as truth: scale the per-frame gun counters by
`g_ClockTimer` (so cadence tracks ticks, not frames), behind a default-per-
charter setting (`Input.FireRateAuthentic` or similar), A/B-verified against
the ares oracle at recorded retail frame costs. GoldenRecomp left this
unfixed — landing it first, oracle-verified, is a visible fidelity
differentiator. Note their demo-cadence gate (F2) as prior art for "replay
paths must honor recorded frame costs."

---

## Tier 3 — Aspirational (reference designs)

### F19. RT64's dual-renderer replay architecture

Two concurrent renderers: one draws at **native res and syncs immediately
with the game at original rate** (so game code reading framebuffer bytes
gets correct values), while a second **replays captured draw calls** at high
res/rate, generating interpolated frames by matching draw calls across
frames and lerping decomposed transforms in 3D space. RT64 itself flags HFR
as "limited game support" (heuristic matching). For us: the
framebuffer-readback-correctness idea is the interesting half (our
memory-blend snapshot is a cousin); the replay design is the reference if we
ever want a render-thread decoupled from the sim thread. Not near-term — our
deterministic matrix-hook interpolation (F14) is strictly easier because we
own the sim. *Source: [RT64 README](https://github.com/rt64/rt64).*

### F20. Path-traced GoldenEye

RT64's in-development path tracer "calculates all lighting in real time and
replaces the contents of the drawn scene entirely" — the eventual answer to
our no-normals lighting ceiling (same wall our per-pixel work hit). Watch,
don't build; if it matures, the question becomes whether scene data can be
exported at our `GfxRenderingAPI` seam. *Source: RT64 README.*

### F21. Mod ecosystem beyond texture packs

N64Recomp has a mod format + function-hook mechanism; Z64R shipped a mod
menu later. Our surface today is config/env/texture packs — no scripting, no
model/audio replacement. A decomp port's natural analog is data-driven asset
replacement + a stable native plugin/scripting seam, which is a design
project of its own. Deferred; see open question O4. *Sources:
[N64Recomp](https://github.com/N64Recomp/N64Recomp), Z64R.*

### F22. Save states

GoldenRecomp carries Zelda64Recomp's complete quicksave recipe, fully
disabled (`src/game/quicksaving.cpp`): freeze threads via message queues,
snapshot RDRAM + per-thread contexts. Doesn't map to a native-structs port.
Our determinism harness (sim-state hash + input tapes) points at the better
native equivalent: checkpoint = tape position + verified state hash,
restore = fast replay. Aspirational convenience feature; fidelity tooling
first. *Source: GoldenRecomp `src/game/quicksaving.cpp`.*

### F23. XBLA remaster as behavioral reference

SunJaycy's GoldenEye-Recomp is a static recomp of the cancelled **Xbox 360
XBLA remaster** via ReXGlue (PPC→C++, XenonRecomp-inspired) — orthogonal
tech, but it is Rare's *own* modernization of GE (dual-analog tuning, 60fps
presentation, HD choices), shipped v1.0–v1.2.4 in 2026. Useful as a
control-feel/presentation reference for our ADS/modern-controls tuning, not
as a code source. *Sources:
[SunJaycy/GoldenEye-Recomp](https://github.com/SunJaycy/GoldenEye-Recomp),
Tom's Hardware coverage.*

---

## Where mgb64 is already ahead (no action; useful for comms)

- **Input**: mouse look, ADS suite, radial deadzones, per-device rebinding UI,
  rumble. GoldenRecomp wires one controller; dual-analog is a README TODO
  (scaffolding exists unused).
- **Sky**: our native sky queue renders real skies; they ship flat
  fog-colored fills after a failed recode (F10).
- **RDP depth**: 2-cycle combiner, coverage/memory-blend emulation, decal
  handling — beyond RT64-under-recomp for GE specifically (RT64 can't even
  ingest GE's sky commands).
- **Interpolation prospects**: their attempt rolled back; our plan has the
  chokepoint, prop identity, and purity oracle already landed (F14).
- **Distribution**: signed, notarized, three-platform releases vs
  build-a-special-decomp-branch-yourself.
- **Recomp-only hazard class**: TLBFREE, external-segment jumptables, rmon,
  the 95KB hand-mirrored `structs.h` drift risk — none apply to us.

## Open questions / next research lanes

- **O1 — Perfect Dark PC port deep-dive** (fgsfds/perfect_dark): closest
  engine cousin (Rare lineage). Its high-FPS/sim-rate handling would validate
  or refine the matrix-lerp plan; `skybox.c` and beyond likely transfer.
- **O2 — Ship of Harkinian / 2S2H input + menu frameworks**: the one lane the
  verified pass didn't cover in depth; mostly relevant if we build the mod
  menu (F21).
- **O3 — Are Wiseguy's TMEM test ROMs public?** Determines whether F17 runs
  on hardware-verified vectors or ares-proxy.
- **O4 — N64Recomp mod format details**: what a decomp-port analog would
  even be.

---

## Appendix A — GoldenRecomp patch inventory (condensed)

Every `RECOMP_PATCH` in their tree, grouped; each is a documented landmine.
(ACTIVE unless noted.)

| Function | Theme | Why |
|---|---|---|
| `bossMainloop` | timing | Reimplemented main loop: retrace tick gate, demo-cadence gate, debug-menu chord, RT64 `gEXSetRefreshRate` per frame |
| `sub_GAME_7F009254` | timing | Gunbarrel intro re-timed (`waitForNextFrame2`, `frameDelay=speedgraphframes`) |
| `select_ramrom_to_play` / `interface_menu00_legalscreen` | timing | Set/clear `demoMode` around attract demos |
| `getinstsize` | timing/ws | Prop draw-in ×4, stock in demoMode (demo desync) |
| `getPlayer_c_lodscalez` | rendering | `/8` LOD fix at high res |
| `sub_GAME_7F097818` / `sub_GAME_7F098A2C` | sky | Raw-RDP sky/water tri emitters → flat fog-color fill (workaround) |
| `skyRender` (disabled) | sky | Full recode w/ PORTSKY gSPVertex path; worked in emu, failed in recomp |
| `applyRoomMatrixToDisplayList` (disabled) | interp | RT64 matrix-group interpolation attempt, rolled back |
| `zbufClearCurrentPlayer` / `bgScissorCurrentPlayerView` | widescreen | RT64 `gEXSetScissor` origin-anchored full-width; noted to break MP |
| `bgUpdateCurrentPlayerScreenMinMax` | widescreen | Screen cull min/max widened ±4 screens; stock in demoMode/Jungle intro |
| `currentPlayerDrawFade` | widescreen | Full-screen fades; black during 320↔440 buffer transitions |
| `sub_GAME_7F01B240` | widescreen | Center-anchored 440px I8 strips (gunbarrel sweep) |
| `modelSetDistanceDisabled` | rendering | Distance-cull off everywhere except Jungle |
| `musicTrack1/2/3Play` | audio | Yield inside `alCSPGetState` busy-wait (×3 copy-paste), `optnone` |
| `joyRumblePakInit` | input | Force "pak inserted" (no PFS emulation) |
| `init` / `rmonCreateThread` | boot | Overlay registration, sync code DMA; rmon stubbed (crashes runtime) |
| libc shims | misc | `__floatundisf`, `__fixunssfdi`, `strtol`, IDO `__ll_*` host intrinsics ("TODO: Validate this") |

Runtime-side fixes: sample-pump volume 0.5→1.0 (`queue_samples`,
`src/main/main.cpp:201`, MM headroom convention halved GE volume);
latency-based sample skip + 2-VI under-report against pops; recompiled
`aspMain` RSP ucode for audio only (16 indirect branch targets enumerated —
usable as an ABI-mixer oracle if ever needed).

## Appendix B — Provenance

- Research pass: 103 agents, 638 tool calls; 15 claims survived 3-vote
  adversarial verification (0 refuted; per-claim votes recorded in the
  session workflow journal). Coverage gaps → O1–O4.
- GoldenRecomp read: shallow clone @ `f31b5d1` ("enable debug mode",
  2026-07-05), full `patches/` + runtime + build pipeline + `git log` review.
- mgb64 grounding: working tree survey on `fix/fid-0032-verify`
  (2026-07-10); file:line references current as of that date.

Related docs: `docs/design/UNCAPPED_FPS_PLAN.md` (F14/F18 target),
`docs/design/FAITHFULNESS_S_TIER_PLAN.md` (F1–F3, F9, F17 lanes),
`docs/BACKLOG_v0.4.0.md` (F7/F15/F16 remaster track).
