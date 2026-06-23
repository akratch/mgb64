<!-- Provenance: generated 2026-06-22 from a 14-lane adversarial code survey
(render-port-survey workflow): 14 parallel subsystem audits of the Fast3D
renderer + SDL port + render-coupled decomp gameplay, each finding independently
verified (refuted findings dropped) and scoped. 47 findings -> this ranked plan.
Symptoms that motivated it: Bond height not recovering, glass shards, fog, glass accuracy. -->

# MGB64 Native Port — FINAL Unified Rendering & Playability Roadmap (14 lanes)

## 1. Executive summary

The port is healthy: the solo campaign is playable start-to-finish, split-screen is wired/validated, combat/aiming/accuracy are faithful, and the prior wave of texture/glass/fog regressions in `docs/RENDERING_REGRESSION_NOTES.md` is fixed. The dominant remaining work is still a **band of unfinished-port cosmetic gaps** — already-written draw bodies parked behind `PORT_FIXME_STUBS`/`NATIVE_PORT` no-ops (falling shards, drop-shadows, bullet sparks, autogun beams, shoot-the-lights) plus two split-screen polish items — each a small, contained un-stub.

**What the 5 new lanes change:**

- **Glass material lane is the most consequential addition.** It surfaces **two confirmed, every-instance glass-fidelity bugs** that the prior survey did not have: (a) tinted glass is **floored at 96/255 opacity** on PC, so panes never fade to clear up close (`chrobjhandler.c:542`/`:32291`), and (b) **prop/glass bullet-crack decals render as flat dark squares**, not the crack texture (`explosions.c:121`). These directly answer the user's "**glass accuracy**" symptom with *root-caused, scoped, gameplay-visible* fixes — stronger leads than the prior glass cluster's edge-case `f3d-core-3` straddle tear. **glass-01 and glass-02 are promoted near the top of the roadmap.**
- **Frame loop lane found one real bug:** screenshots read the **post-swap back buffer** with no `glReadBuffer(GL_FRONT)` (`platform_sdl.c:562`). This corrupts validation/oracle/contact-sheet captures (driver-dependent stale pixels) — it does **not** affect gameplay rendering, but it undermines every screenshot-based parity tool the other fixes rely on for validation. **Promoted as a tooling-integrity prerequisite.** The other 4 frameloop findings are confirmed-but-cosmetic (dead resize hook, opt-in CPU spin, pacing-order latency) — hygiene only.
- **Texture cache + filter lanes found NO new player-visible bug.** All 6 findings are `uncertain`/cosmetic latent hardening: the strided-LOADBLOCK pitch heuristic, the deswizzle co-activation, the CI8 split-TLUT cache identity, the decode-footprint over-read bound, and the trilerp N64-filter asymmetry are all defensive guards against unexercised edge patterns. **None outranks existing work**; they are opportunistic-only. Critically, **this clears the texture-smear hypothesis** behind the prior fog/Dam/Cradle worries — the strided heuristic does not fire on tight-block room textures.
- **Faithfulness lane confirmed the combat audit holds.** Two real-but-narrow divergences (`chrTickBeams` dropped last-visible timestamps GF-1; `stan.c` seam disambiguation gated off GF-2) and three "no active bug" confirmations (aim-timer coupling, g_ClockTimer clamp is live, dead-twin `lvlManageMpGame`). GF-2 **re-frames the Bond-height symptom** but does not resolve it.

**Net change to the top of the roadmap:** the new glass-material bugs (glass-01 floored opacity, glass-02 flat crack decals) **outrank** the prior shards/straddle glass items for the "glass accuracy" symptom and are promoted to ranks 2–3. The frameloop screenshot bug is promoted as a **validation-integrity prerequisite** (fix it before trusting any parity capture used to validate the cosmetic fixes). Everything else from the prior roadmap holds, with the **Bond-height symptom still NOT root-caused** — GF-2 adds a plausible seam mechanism but the surveys still cannot localize it; it needs reproduction.

## 2. Deduplication & clustering (all 14 lanes)

| Cluster | Merged findings | Note |
|---|---|---|
| **C1 — Falling glass shards never drawn** | `glass-break-1/-2`, `effects-1` | Identical `PORT_FIXME_STUBS` no-op `sub_GAME_7F0A2C44` (`unk_0A1DA0.c:1079`) + matrix-pointer bug at 1153/1157. Sim faithful/live. **One fix.** |
| **C2 — Split-screen black gutters / 3P quadrant** | `splitscreen-1`, `splitscreen-6` | Skipped `viSetupScreensForNumPlayers` (`lvl.c:1642-1653`). **One fix.** |
| **C3 — Shoot-out-the-lights darkening dead** | `lighting-model-1`, `lighting-model-4` | Empty `sub_GAME_7F0BBE0C` stub starves live `redarken_lights_in_room`. **One fix.** |
| **C4 — Bond height recovery** | `height-1/-4/-5`, `stubs-1`, **`GF-2`** | Soft/uncertain. `height-5` (MP cache) is the one actionable confirmed bug. **`GF-2` is NEW**: it shows the floor-finder's default path omits seam disambiguation (gated `GE007_STAN_ONEDGE` off) — a *plausible* height mechanism but the same `stan.c` reimpl as `stubs-1`, fallback-only and unproven. **Cluster; treat individually; still not root-caused.** |
| **C5 — Glass MATERIAL accuracy (NEW dominant glass cluster)** | **`glass-01`** (floored opacity), **`glass-02`** (flat crack decals), `f3d-core-3` (straddle tear), `glass-05` (shard cull, refuted), `texfilter-4` (trilerp filter) | The new glass-material findings are the *real* "glass accuracy" answers. `glass-01`/`glass-02` are independent confirmed bugs (treat separately). `f3d-core-3` is the edge-case residual. `glass-05`+`texfilter-4` are latent/cosmetic. **Two strong fixes + hardening.** |
| **C6 — Fog interpolation/enable** | `fog-1`, `fog-3`, `f3d-core-4` | All cosmetic/inert vs current content. Low-priority hygiene, gated on N64 reference capture. |
| **C7 — Texture cache hardening (NEW, all latent)** | `texcache-1/-2/-3/-4` | Four `uncertain`/cosmetic guards (strided pitch, deswizzle co-activation, CI8 split-TLUT, over-read bound). **No reproduced defect.** Opportunistic-only. |
| **C8 — Frame-loop hygiene (NEW)** | `frameloop-01` (screenshot — REAL), `frameloop-02/-03/-05` (dead resize, CPU spin, pacing order — cosmetic) | `frameloop-01` is the only real bug (tooling integrity). The other three are confirmed-but-cosmetic. |
| **C9 — Faithfulness no-ops (NEW)** | `GF-5`, `GF-6` | Confirmed "no active bug": aim-timer coupling is safe; g_ClockTimer clamp is live (closes prior residual-risk note); dead-twin `lvlManageMpGame` is a maintenance trap only. |
| Standalone un-stubs | `lighting-model-2` (shadows), `effects-2` (sparks), `effects-3` (autogun), `sky-1` (sky aspect), `sky-5` (water concavity), **`GF-1`** (chrTickBeams timestamps) | Independent. **`GF-1` is NEW**: restores dropped ACT_PATROL/ACT_GOPOS last-visible writes (`chr.c`), already partially masked by existing workarounds. |
| Pure hygiene (no player-visible effect) | `height-3`, `f3d-core-7/8`, `stubs-4/5`, `effects-4/5`, `frameloop-02/-05`, `texcache-1/-2/-3/-4`, `GF-5/-6` | Code-correctness/doc cleanups only. |

## 3. Ranked roadmap (by playability alpha — all 14 lanes)

| Rank | Item | Symptom | Confidence | Effort | Risk | Alpha |
|---|---|---|---|---|---|---|
| 1 | **Re-enable character drop-shadows** (`lighting-model-2`) | Guards/Bond/props float — no ground shadow | confirmed | small | medium | **7** |
| 2 | **Tinted-glass opacity floor → 0** (`glass-01`, NEW) | Glass permanently cloudy/~37%, never fades clear up close (user "glass accuracy") | confirmed | small | medium | **6** |
| 3 | **Split-screen black gutters + 3P quadrant** (C2) | MP gutters/empty quadrant show fog color, not black | confirmed | small | low | **6** |
| 4 | **Textured prop/glass bullet-crack decals** (`glass-02`, NEW) | Glass cracks render as dark flat squares, not crack texture (user "glass accuracy") | confirmed | small | low | **6** |
| 5 | **Sky aspect-ratio correction** (`sky-1`) | Sky stretches ~33% on non-4:3 windows | confirmed | trivial | low | **6** |
| 6 | **Bullet sparks / dust clouds** (`effects-2`) | No spark/dust when shooting characters | confirmed | small | low | **6** |
| 7 | **Falling glass shards** (C1) | Shattered-window fragments invisible (user "shards") | confirmed | small | medium | **5** |
| 8 | **MP per-player floor-Y cache** (`height-5`) | Split-screen ground-ref cross-contamination Y-snap | confirmed | trivial | low | **5** |
| 9 | **Screenshot reads post-swap back buffer** (`frameloop-01`, NEW) | Validation/oracle captures stale/garbage (tooling integrity) | confirmed | trivial | low | **4** |
| 10 | **Autogun turret beams** (`effects-3`) | Turrets fire with no muzzle/tracer billboard | confirmed | trivial | low | **4** |
| 11 | **Shoot-out-the-lights darkening** (C3) | Rooms stay lit after lights destroyed | confirmed | small | low | **4** |
| 12 | **chrTickBeams last-visible timestamps** (`GF-1`, NEW) | Patrol/path guards mis-time WAYMODE_MAGIC re-entry | confirmed | small | low | **4** |
| 13 | **stan.c seam disambiguation default-on** (`GF-2`, NEW) | Wrong floor tile at stacked-tile seams (possible height lead) | confirmed | small | medium | **4** |
| 14 | **Eye-height clamp softening** (`height-1`) | Slope/stair view bob snaps at ±4 wall | uncertain | small | low | **4** |
| 15 | **Glass straddle-pane tear** (`f3d-core-3`) | Glass near camera tears on mixed-w | confirmed | small | medium | **3** |
| — | Water-sky horizon offset (`sky-5`) | Water/sky boundary a few px off | confirmed | small | low | 3 |
| — | Trilerp loaded-tile N64 filter (`texfilter-4`) | 2-tex glass/decal filtered differently | uncertain | small | low | 2.5 |
| — | Texcache hardening (C7: `texcache-1/-2/-3/-4`) | No reproduced defect; latent guards | uncertain | small | low–med | 2–3 |
| — | Frame-loop hygiene (`frameloop-02/-03/-05`) | Dead hook / opt-in CPU spin / latency order | confirmed | trivial | low | 2 |
| — | Fog cluster (C6) | Subtle/inert fog deltas | uncertain | small | med | 1–2 |
| — | Faithfulness no-ops (`GF-5/-6`) + pure hygiene | No player-visible effect | confirmed | trivial | low | 1–2 |

## 4. Per-item detail (top 12)

### 1. Character drop-shadows — `lighting-model-2` (alpha 7)
- **Root cause:** `doshadow` gated to an empty stub at `model.c:10849-10851` (`#if defined(PORT_FIXME_STUBS) || defined(NATIVE_PORT)`); full port-adapted body dead at 10854-10969. CMake defines all flags (`CMakeLists.txt:186-188`) so the stub wins; dispatch reaches it at `model.c:11849-11850`.
- **Fix:** Collapse the gate so the real body compiles under `#ifdef NONMATCHING`. Sibling gunfire-flash renderer (`model.c:10699-10832`, shipping) uses identical `G_MTX_FLOAT_PORT` + `portPackVerticesForGBI` + `sub_GAME_7F073038` idioms — proven port-safe.
- **Why high-alpha:** ~3 preprocessor lines; grounds *every* character/prop/weapon. Flagged in `MULTIPLAYER_PLAN.md:103`.
- **Pathway: CLEAR.** First step: edit the gate at `model.c:10849`, build, capture a guard on flat floor; watch polarity (texSelect mode 2) and the `zbufferenabled` ycoord branch at 10893-10897.

### 2. Tinted-glass opacity floor → 0 — `glass-01` (alpha 6, NEW)
- **Root cause:** `tintedGlassMinRenderOpacity()` returns a hardcoded `int opacity = 96;` (`chrobjhandler.c:542`, **verified live**), applied by `tintedGlassRenderOpacity()` at `chrobjhandler.c:583-585`, which is the only function the NATIVE_PORT tinted-glass branch uses (`chrobjhandler.c:32291`, **verified**: `mrData.envcolour.word = tintedGlassRenderOpacity(...) << 8`). The `#else` original uses unfloored `calculatedopacity`. The floored alpha becomes glass prim-alpha in `modelApplyFullPropMaterial` (`model.c:8979-8980`) via `G_RM_FOG_PRIM_A`. `glassCalculateOpacity` (`chrobjhandler.c:8889-8912`) is a faithful 1:1 port returning near-zero at close range — exactly the fade the floor defeats. Portal occlusion reads *unfloored* `calculatedopacity` (`chrobjhandler.c:10818-10828`), so occlusion is unaffected.
- **Fix:** Change `chrobjhandler.c:542` `int opacity = 96;` → `int opacity = 0;`. Keeps `GE007_TINTED_GLASS_MIN_OPACITY` escape hatch.
- **Why high-alpha:** Directly answers "glass accuracy"; every-pane visual; one-line; occlusion provably untouched.
- **Pathway: CLEAR but VALIDATE.** Commit 429391c introduced the 96 floor as the *deliberate remedy* for a PC visibility symptom with NO accompanying blend fix. First step: preview with `GE007_TINTED_GLASS_MIN_OPACITY=0` on a Facility/Bunker glass scene — confirm glass fades toward clear AND does not vanish entirely. If it disappears on the GL backend at alpha 0, the true fix is in the `G_RM_FOG_PRIM_A` near-zero-alpha path (gfx_opengl/gfx_cc), not a re-floor; a smaller guard (8–16) is the conservative fallback.

### 3. Split-screen black gutters + 3P quadrant — C2 (alpha 6)
- **Root cause:** `viSetupScreensForNumPlayers` (`fr.c:1764-1815`) is called only in the non-native `#else` (`lvl.c:1651`). The NATIVE_PORT branch (`lvl.c:1642-1649`) runs only `skyRender`; framebuffer stays fog-colored (`lvl.c:1423` → `gfx_opengl.c:2047`), so gutters and the empty 3P bottom-right quadrant (`fr.c:1806-1810`) show fog color.
- **Fix:** At `lvl.c:1647` add `if (getPlayerCount() >= 2) DL = viSetupScreensForNumPlayers(DL);`. Emits only `G_CYC_FILL`/`G_SETSCISSOR`/`G_SETFILLCOLOR`/`G_FILLRECT` (all backend-supported, `gfx_pc.c:13044`, 14118-14124) — no `G_SETCOLORIMAGE`, so the reason other vi* funcs were skipped doesn't apply.
- **Why high-alpha:** One guarded call; fixes the most conspicuous MP defect at 2/3/4P. **Pathway: CLEAR.** First step: add call, run `tools/mp_smoke.sh` at 2/3/4P.

### 4. Textured prop/glass bullet-crack decals — `glass-02` (alpha 6, NEW)
- **Root cause:** `portUseFlatBulletImpacts(prop)` returns the flat path for ANY non-NULL prop by default (`explosions.c:121` `return !portTexturedPropBulletImpacts();`, **verified**). That gates out `texSelect(&impactimages[impact_type],...)` (`explosions.c:3650-3658`) and sets `G_CC_SHADE,G_CC_SHADE` with texture off (3556-3564). With `g_BulletImpactDefaultVertex = {...,0x0,0x0,0x0,0xDC}` (`explosions.c:812`, RGBA black α220) each impact draws a dark translucent square. World cracks (prop==NULL) stay textured. Glass-crack impact types 0x11-0x13 (all `unk1==1`) on PROPDEF_GLASS/TINTED_GLASS are the most visible case. The recent depth-lift commit (e0f4ab6) only fixed z-fighting, not the texture.
- **Fix (Tier 1, recommended):** Add `portUseFlatBulletImpactForType(prop, impact_type)` that returns textured when `portPropIsGlassLike(prop) && portImpactIsGlassCrack(impact_type)` (helpers exist at `explosions.c:175-188`); move the flat GBI setup into the per-impact loop. Tier 2 (optional): flip the prop default at `explosions.c:121` to textured, after confirming no texture-state leak on non-glass props.
- **Why high-alpha:** Answers "glass accuracy"; helpers already exist; restores faithful behavior. **Pathway: MOSTLY-CLEAR.** First step: Tier 1 carve-out; test on Dam guard-tower glass; watch for texture leak into following geometry.

### 5. Sky aspect-ratio correction — `sky-1` (alpha 6)
- **Root cause:** Default sky writes raw clip-space x at `gfx_pc.c:15712` (`d->x = xs[i]`), bypassing `gfx_adjust_x_for_aspect_ratio` (`gfx_pc.c:9639`). Player projection is fixed N64 4:3 (`bondview.c:14315`), so on a 16:9 window scene x scales by 0.75 while sky keeps full width → ~33% mismatch.
- **Fix:** Route the clip x in the `clip_space_xy` branch through `gfx_adjust_x_for_aspect_ratio` (self-guarding, no-op on 4:3). **Pathway: CLEAR.** First step: wrap `gfx_pc.c:15712`; verify 640×480 is byte-identical.

### 6. Bullet sparks / dust clouds — `effects-2` (alpha 6)
- **Root cause:** `sub_GAME_7F0A3F04` is a `PORT_FIXME_STUBS` no-op at `unk_0A1DA0.c:3865-3868`; real body dead at 3881-4023. Vertex template `D_80040980` is declared `u32 = 0` (`unk_0A1DA0.c:92`) but read as a 16-byte `Vtx` at 3914 — a 12-byte over-read → fully-transparent vertex even after un-stubbing.
- **Fix:** Scoped `#if 0`/`#undef` for this one function (do NOT flip project-wide `PORT_FIXME_STUBS`); promote `D_80040980` to a real `Vtx` (all-0xFF color, mirror `explosions.c:809`); update the extern at `unk_0A1DA0.h:72`.
- **Pathway: CLEAR.** **⚠️ DL-handoff hazard with C1 — see §7.** First step: fix template type, then un-stub; shoot a guard at close range.

### 7. Falling glass shards — C1 (alpha 5)
- **Root cause:** `sub_GAME_7F0A2C44` is `return gdl;` at `unk_0A1DA0.c:1079`; full body dead at 1081-1161. Sim faithful/live. **Latent bug:** lines 1153/1157 pass `(uintptr_t)currentPlayerGet...Matrix()` *without* `osVirtualToPhysical` → NULL matrix.
- **Fix:** Un-stub the body; wrap 1153/1157 in `osVirtualToPhysical(...)` (mirrors sibling at 4019).
- **Why high-alpha:** Directly the user's "shards"; sim runs invisibly. **Risk medium:** this raw-word GBI path has *never* executed in the port. **Pathway: CLEAR (watch-items).** First step: un-stub + two `osVirtualToPhysical` wraps; shoot a Facility/Dam window with `GE007_DEBUG=1`, watch for `[GFX-BAD] G_MTX`/`[SEG_ADDR]`.

### 8. MP per-player floor-Y cache — `height-5` (alpha 5)
- **Root cause:** `bondviewUpdatePlayerY` uses function-static `last_good_stanHeight`/`have_floor` (`bondview.c:10072-10092`) shared across split-screen players; player A's last-good floor leaks into player B's `stanHeight`.
- **Fix:** Make both statics `[4]` arrays keyed by `get_cur_playernum()` (pattern at `bondview.c:87-88`); clear on `g_GlobalTimer<=1`. Index-0 path unchanged → single-player byte-identical.
- **Pathway: CLEAR.** First step: array-ify, run `tools/mp_smoke.sh`.

### 9. Screenshot reads post-swap back buffer — `frameloop-01` (alpha 4, NEW)
- **Root cause:** `platformSaveScreenshot` calls `glReadPixels(0,0,...)` at `platform_sdl.c:562` with **no preceding `glReadBuffer`** (**verified — no `glReadBuffer` anywhere in src/**). Context is double-buffered (`platform_sdl.c:1721`), so default read buffer is GL_BACK, whose contents are undefined after `SDL_GL_SwapWindow` (`gfx_pc.c:15841`). Per-frame ordering (`platformFrameSync` → screenshot → retrace → build → swap) means the capture reads GL_BACK as it stands after the *previous* swap — stale by 1–2 frames or driver-garbage. The only correct pre-swap path is the separate env-gated `GE007_SCREENSHOT` diag (`gfx_pc.c:15410-15436`), which does NOT cover the F2/auto/oracle/contact-sheet path.
- **Fix:** Add `glReadBuffer(GL_FRONT)` before and `glReadBuffer(GL_BACK)` after the `glReadPixels` in `platformSaveScreenshot`.
- **Why this matters (promoted):** This does NOT change gameplay rendering, but it **corrupts every screenshot-based parity tool** the cosmetic fixes above are validated with (`tools/renderer_parity_capture.sh`, oracle, contact-sheet). Fixing it first makes the rest of the roadmap's visual validations trustworthy.
- **Pathway: CLEAR.** First step: bracket the `glReadPixels` at `platform_sdl.c:562`; confirm a static-scene capture is byte-stable across runs/drivers.

### 10. Autogun turret beams — `effects-3` (alpha 4)
- **Root cause:** Autogun (`PROPDEF_AUTOGUN==13`) beam render in `sub_GAME_7F049B58` is `#ifndef NATIVE_PORT` (`chrobjhandler.c:27464-27473`). Beam is computed every frame (`chrobjhandler.c:12033-12068`); `AutogunRecord.beam` is mapped at 0xCC (`bondtypes.h:3120`).
- **Fix:** Add a NATIVE_PORT branch passing `((AutogunRecord*)data)->beam` (reinterpret as `ChrRecord_f180*`) to `sub_GAME_7F061E18`. Self-gates when inactive (`gun.c:12489`). **Pathway: CLEAR.** First step: add branch; test on a level that *places* autoguns (not Dam).

### 11. Shoot-out-the-lights darkening — C3 (alpha 4)
- **Root cause:** `sub_GAME_7F0BBE0C` empty NATIVE_PORT stub (`lightfixture.c:340-344`); DL-parse helpers (110-115, 135-140) return NULL/0. `darkened_light_table` never populated; live `redarken_lights_in_room` (`bg.c:8277`) starved.
- **Fix (Option B):** The native bullet trace already returns 3 struck vertex pointers (`hit_output.vtx0/1/2`, `bg.c:163-180`). Add a helper calling `darken_vertex_in_room(vtxN, room)` (`lightfixture.c:222-245`, `cn[]>>=2`) on each; swap the call site at `chrprop.c:3050`. (Darkens hit-triangle 3 vertices, not full-fixture flood.) **Pathway: CLEAR.** First step: write helper, swap call site, shoot a Facility/Bunker lamp, re-enter room.

### 12. chrTickBeams last-visible timestamps — `GF-1` (alpha 4, NEW)
- **Root cause:** Native `chrTickBeams` (`chr.c:4917-5273`) flattened the ASM's per-actiontype dispatch and dropped the two visibility-gated `g_GlobalTimer` writes: ACT_PATROL `lastvisible60` @0x78 (ASM `chr.c:5520-5522`) and ACT_GOPOS `unk9c` @0x9c (ASM `chr.c:5526-5528`). Consumers `chrlvTickPatrol` (`chrlv.c:10897`) and `chrlvTickGoPos` (`chrlv.c:10702`) read these for WAYMODE_MAGIC re-entry. Partially masked by `chrlvPropHasRenderedRoom` workaround (`chrlv.c:10911`) and init (`chrlv.c:4906-4913`); listed as deferred task M2 in `docs/COMBAT_DEFERRED_PLAN.md:234`.
- **Fix:** Restore the two visibility-gated `g_GlobalTimer` writes in `chr.c` (behind `GE007_CHRBEAMS_DISPATCH`, gate-off byte-neutral). **Pathway: MOSTLY-CLEAR.** First step: add the two writes; validate with gate-on against a patrolling guard kept continuously on-screen (should not re-enter WAYMODE_MAGIC).

## 5. Named-symptom status (informed by all 14 lanes)

| Symptom | Root cause found? | Fix scoped? | Path |
|---|---|---|---|
| **Bond height not recovering** | **STILL NO — not root-caused.** Height lane gave soft findings; `height-1` refuted; `stubs-1` is fallback-only. **NEW `GF-2`** confirms the live floor-finder default path (`stan.c:786-798`) omits canonical seam disambiguation (gated `GE007_STAN_ONEDGE` off) — a *plausible* stacked-tile-seam mechanism — but it's the same fallback-only `stan.c` reimpl NOT in Bond's per-frame eye/ground path (`current_tile_ptr` via `sub_GAME_7F01F614`). `height-5` (MP cache) is a real but conditional Y-snap. | Partial only: `height-5`, `height-1`, `GF-2` are scoped band-aids; the reported symptom has no confirmed mechanism. | **NEEDS REPRODUCTION.** Capture player Y / `standheight` / `current_tile_ptr` across a specific failing transition before code. `GF-2`'s seam default-on is worth A/B testing *during* that repro (it touches floor-Y at seams) but is unproven. |
| **Shards** | **YES** — render no-op `sub_GAME_7F0A2C44` (`unk_0A1DA0.c:1079`) + matrix-pointer bug 1153/1157. Sim faithful/live. | Yes (C1). | **CLEAR** (medium risk, first-time GBI path). |
| **Fog** | Partially — `fog-1`/`fog-3`/`f3d-core-4`, all cosmetic/inert. **Texcache lanes clear the "texture-smear contributes to fog mismatch" hypothesis** — strided heuristic doesn't fire on tight room textures. | Hygiene-scoped only. | **NEEDS N64 RDP REFERENCE CAPTURE** — not a confirmed bug. |
| **Glass accuracy** | **YES (NEW, stronger than before)** — `glass-01` (96-opacity floor, `chrobjhandler.c:542`) makes panes permanently cloudy; `glass-02` (flat crack decals, `explosions.c:121`) makes cracks dark squares. These are the *real* material-fidelity answers. `f3d-core-3` (straddle tear) is the residual edge case; `f3d-core-1` refuted. `glass-05` shard-cull refuted (6-plane clipper bounds it). | **Yes** — `glass-01` (1-line), `glass-02` (Tier 1 carve-out). | **CLEAR (validate `glass-01` against GL backend disappearance at α0; Tier-1 limits `glass-02` blast radius).** |

## 6. Quick wins vs deep work

**Land immediately (trivial/small, low risk, clear path):**
- Screenshot `glReadBuffer(GL_FRONT)` — `platform_sdl.c:562` (`frameloop-01`) — **do FIRST: it makes the parity tools trustworthy**
- Sky aspect correction — `gfx_pc.c:15712` one-liner (`sky-1`)
- Split-screen black gutters — one guarded call at `lvl.c:1647` (C2)
- Tinted-glass floor → 0 — `chrobjhandler.c:542` one-liner (`glass-01`) *(preview via env first)*
- Textured glass cracks (Tier 1) — `explosions.c` carve-out (`glass-02`)
- MP per-player floor-Y cache — array-ify two statics (`height-5`)
- Autogun beams — one reinterpret-cast branch (`effects-3`)
- Drop-shadows — collapse one preprocessor gate (`lighting-model-2`)
- Bullet sparks + `D_80040980` Vtx fix (`effects-2`)
- Shoot-the-lights 3-vertex darken helper (C3)
- chrTickBeams timestamps (`GF-1`) — two gated writes
- Eye-height clamp → `tanhf` softening (`height-1`)

**Defer / investigate (research, larger blast radius, or unproven):**
- **Bond height-recovery symptom** — needs reproduction + trace before any code (no confirmed cause; `GF-2` is a candidate to A/B during repro)
- Falling glass shards (C1) — clear but medium-risk first-time GBI path
- `glass-01` GL-backend validation — if glass vanishes at α0, escalate to `G_RM_FOG_PRIM_A` near-zero-alpha handling in gfx_opengl/gfx_cc
- `GF-2` seam default-on — medium risk (less-exercised path, eye-height pop), no seam oracle to prove the win
- Glass straddle tear (`f3d-core-3`) — hot renderer path, keep working case byte-identical
- Fog cluster (C6) — gated on N64 RDP reference capture; env-toggle only
- Texcache C7 (`texcache-1/-2/-3/-4`) — opportunistic-only; no reproduced defect; medium risk on the hot decode path for `texcache-1`
- `stan.c` seam (`stubs-1`/`GF-2`) — do NOT flip `GE007_STAN_ONEDGE` globally without a seam oracle
- `chrTickBeams` frustum union (`stubs-2`), split-screen scissor intersection (`splitscreen-2`) — defensive hardening
- Pure hygiene (`frameloop-02/-05`, `GF-5/-6`, `height-3`, `f3d-core-7/8`, `stubs-4/5`, `effects-4/5`, `texfilter-4`, `sky-5`) — opportunistic

## 7. Sequencing recommendation

**Session 0 — tooling integrity (do before validating anything visual):**
0. `frameloop-01` (screenshot `glReadBuffer(GL_FRONT)`) — every parity/oracle/contact-sheet capture used to validate the cosmetic fixes is currently reading a stale/garbage buffer. Fix this first so the A/B captures in later sessions are trustworthy.

**Session 1 — solo-visible quick wins (no inter-dependencies):**
1. `sky-1` (sky aspect) — isolated, instant payoff
2. `lighting-model-2` (drop-shadows) — highest alpha, ubiquitous
3. `glass-01` (tinted-glass floor) — preview with `GE007_TINTED_GLASS_MIN_OPACITY=0` first, then flip default
4. `glass-02` (textured glass cracks, Tier 1)
5. `effects-3` (autogun beams) — trivial
6. C3 (shoot-the-lights) — small, isolated

**Session 2 — the effects/shards block (HAS AN ORDERING DEPENDENCY):**
> ⚠️ **C1 (shards) and `effects-2` (sparks) share a DL-advance hazard at `lvl.c:1790-1794`.** Today the US path `sub_GAME_7F0A4824(DL,1)` doesn't advance lvl.c's `DL`, which is only safe because the *following* `sub_GAME_7F0A2C44` (shards) is still a stub. **If you un-stub shards (C1) and sparks (`effects-2`) without fixing the handoff, the shard commands clobber the spark commands.** Either un-stub both together and switch the US path to the EU-style `sub_GAME_7F0A46A0(&DL,1)` (`Gfx**`) handoff at `lvl.c:1790`, or stage them and leave the documented comment. **Do this cluster in one sitting.**
7. `effects-2` (sparks + `D_80040980` Vtx fix)
8. C1 (shards + `osVirtualToPhysical` fix at 1153/1157) — together with the `lvl.c:1790` handoff

**Session 3 — split-screen + AI faithfulness + height band-aids:**
9. C2 (black gutters) — independent
10. `height-5` (MP floor cache) — independent
11. `GF-1` (chrTickBeams timestamps, gated) — independent
12. `height-1` (clamp softening) — independent, optional

**Session 4 — renderer-hot / research / reproduction-gated:**
13. **Bond height-recovery investigation** — instrument and reproduce before coding; A/B `GF-2`'s seam default-on during the repro
14. `f3d-core-3` (glass straddle) — only after a captured tear repro
15. Texcache C7 + `texfilter-4` — opportunistic, when next touching those paths
16. Fog cluster (C6) — only after N64 RDP reference capture; env-gated

**Dependencies:** C1 ↔ `effects-2` (DL handoff). C3 reuses the same bullet-hit record as `effects-2` — touch carefully in the same area. `glass-02` and the shard/spark block both live in `explosions.c`/`unk_0A1DA0.c` impact paths — sequence `glass-02` before Session 2 so the bullet-impact GBI state is settled. Everything in Sessions 0, 1, and 3 is otherwise independent.

## 8. Open questions / reproduction needed before committing

1. **Bond height recovery (user's #1 symptom):** No confirmed mechanism. **Before any code:** capture `headpos.f[1]`/`standheight` (trace at `bondhead.c:332-340`) + `current_tile_ptr` + player Y across a *specific* failing transition (stairs/lift/ladder/death-cam exit). Determine whether the failure is in the tile-walker (`sub_GAME_7F01F614`), `stanGetPositionYValue`, the head-bob clamp, or the `GF-2` seam path. A/B-test `GF-2` (seam default-on) during this repro since it directly changes floor-Y at stacked-tile seams.
2. **`glass-01` GL-backend behavior at α0:** Commit 429391c added the 96 floor as a deliberate remedy with NO blend fix. **Before shipping:** preview `GE007_TINTED_GLASS_MIN_OPACITY=0` on a Facility/Bunker glass scene. If glass vanishes entirely (vs near-clear-but-present), the true fix is the `G_RM_FOG_PRIM_A` near-zero-alpha rounding/clamp in gfx_opengl/gfx_cc, not a re-floor. Fallback is a small guard (8–16), not 96.
3. **`glass-02` texture-state leak:** The flat default was added to avoid a Fast3D texture-state leak. Tier 1 (glass-only) sidesteps it; Tier 2 (all props textured) requires confirming non-glass prop impacts (crates/barrels/screens) don't leak texture state into following frames. Test on Dam guard-tower glass + a non-glass prop.
4. **Fog correctness (C6):** Is N64 RDP fog screen-linear or perspective-correct? Current perspective-correct default matches upstream Fast3D and is comment-documented. **Need an N64 RDP side-by-side on a long Dam/Surface sightline** before flipping; ship only as `GE007_FORCE_NOPERSPECTIVE_FOG` until proven.
5. **Shard GBI path (C1):** Never executed in the port. **Reproduce on a Facility/Dam window with `GE007_DEBUG=1`** — confirm shard vtx at `piece+0x38` resolves through `VALIDATE_ADDR_TYPED` and clears pathological-shard culling (`gfx_pc.c:2474-2495`).
6. **Texcache premise checks (only if pursuing C7):** Run `GE007_CRITICAL_ROOM_SHARD_LOG=1` (glass-05) / `GE007_TRACE_TEX_FOOTPRINT=1` (texcache-1/-2) on Dam/Cradle and confirm zero real materials hit the suspect paths — these are latent guards, not reproduced bugs; don't spend code unless a trace shows a real hit.
7. **Drop-shadow polarity/z (`lighting-model-2`):** No env probe — visual parity capture is the only signal. Verify polarity (texSelect mode 2) and the `zbufferenabled` ycoord branch (`model.c:10893-10897`).

**The single decisive recommendation:** fix the screenshot read-buffer first (`frameloop-01`) so the parity tools are honest, then knock out the Session-1 un-stubs **plus the two new glass-material one-liners (`glass-01` floored opacity, `glass-02` flat crack decals)** — these are the strongest, cheapest answers to the user's "glass accuracy" symptom and outrank the prior edge-case glass items. Handle the shards+sparks block together respecting the `lvl.c:1790` DL-handoff dependency, validate `glass-01` against the GL backend before flipping the default, and do NOT spend code effort on the Bond height-recovery symptom until it is reproduced and localized — `GF-2` adds a plausible seam mechanism but, like every prior height finding, it is fallback-only and unproven.
