# Aim-Down-Sights (ADS): Operationalized Backlog

> Status: **design / not yet implemented.** Operationalized, milestone-by-milestone backlog for an
> **opt-in, feature-flagged** modern aim-down-sights mechanic. Every code reference was traced against
> the tree and the load-bearing ones re-verified; line numbers are approximate and drift with edits.
> This supersedes the earlier "Feasibility & Design Plan" — its verdict, engine grounding, accuracy
> story, FOV-arming correction, and per-weapon table are preserved and recast as backlog tickets.

---

## 0. Framing — what we are building and why

ADS is an **optional enhancement** gated by a single master flag `Input.AdsEnabled` that ships **OFF**.
With it off, every ADS branch is bypassed and the build is **byte-identical** to vanilla: replays are
deterministic and split-screen behavior is unchanged. With it on, players who want a modern feel get
FOV-zoom on aim, FOV-correct slow-look, tightened spread, optionally aligned iron sights, and
slower/controlled movement while aimed.

**Player-facing value:** a steadier, more accurate, more deliberate aimed shot — modern "shoulder the
weapon" feel — without taking anything away from purists.

**Design tenets:**

1. **Responsive** — ADS-in is a short, per-weapon *time* (default ~120 ms), well under the ~300 ms
   reaction threshold.
2. **Accurate by construction** — bullets already leave the **eye** toward `crosshair_angle`
   (`gun.c:28456-28493`); ADS feel layers on cosmetically. The only things that move bullets are a
   scalar spread multiplier (no new RNG draws) and the centered-crosshair accumulators (opt-in).
3. **Optional & granular** — one master flag, OFF by default, every sub-behavior independently
   toggleable (sensitivity, FOV, center-pull, spread, movement penalty, pose, recoil).
4. **Faithful when off** — vanilla / replay / split-screen byte-identical; we **add** behavior behind
   flags, we never mutate the matched path.

**Verdict:** feasible behind a feature flag; most machinery already exists. GoldenEye's R-button aim
mode (`g_CurrentPlayer->insightaimmode`, `bondview.h:529`) is a near-complete proto-ADS — it already
drives a smooth FOV-zoom engine, disables auto-turn/auto-aim, and frees the crosshair. This is a
**tuning + wiring** job, not new systems.

**Biggest risk:** the FOV path is the **shared watch-zoom engine** (sniper/camera analog scope + watch
menu, via `zoominfovy`/`zoominfovyold`/`zoominfovynew`, `bondview.h:1183-1188`). ADS must substitute its
target into the **sole** per-frame call (`bondview.c:11344-11358`), arm it correctly (see ADS-2.1 — the
guarded wrapper's duration is **incompatible** with a fixed ADS-in time; rising-edge raw arming is
required), and stay inside `watch_animation_state == WATCH_ANIMATION_0x0`.

---

## 1. Modern ADS reference (the mechanics we emulate)

Distilled from a cross-game research pass (CoD MW2019/MWII/Warzone, Apex, Battlefield, Halo Infinite,
Destiny 2). Universal finding: **ADS is four-to-five orthogonal knobs** — timing, FOV/zoom, sensitivity
coupling, movement penalty, accuracy/sway/flinch — that the best titles keep independent. Each rule
states the target we chose and why; sources in §9.

### 1.1 Timing — ADS-in/out is a *duration*, not a side effect of zoom
- **Rule:** ADS-in is a per-weapon-class **time** to reach full aim, independent of zoom depth; ADS-out
  is faster.
- **Targets:** default `ads_in_time = 0.12 s`, `ads_out_time = 0.10 s` (fast SMG/AR). Scale up by class
  toward sniper ~0.30–0.40 s, never past ~300 ms.
- **Anchors:** pistols ~150–180 ms, SMGs ~150–200 ms, ARs ~240–280 ms (M16A4 = 250 ms), snipers
  ~300–500 ms; MW2019 measured ~160 ms fastest / ~584 ms slowest; ADS-out ≈ 0.6–0.8× ADS-in.
- **Engine consequence (critical):** honoring a fixed per-class time **requires** the raw, rising-edge
  `trigger_watch_zoom(fov, ads_in_time)` arming path. The guarded `bondviewTriggerWatchZoom` derives its
  own duration `(|ΔFOV|·15)/30 = |ΔFOV|/2` (`bondview.c:8103/8108`) — e.g. a 60→30 zoom would take ~15
  time-units, wildly off-spec. See ADS-2.1.

### 1.2 FOV / zoom amount — mild irons vs fixed-magnification scopes
- **Rule:** iron sights apply a **mild** FOV reduction (~1.1–1.5×, "shoulder it"); scopes apply a
  **fixed** magnification. `Zoom==0` weapons get a mild computed zoom; true scopes keep magnification.
- **Correction vs naïve reuse:** `WeaponStats.Zoom` was authored for the analog *scope* path, so it is
  **scope-grade**, not iron-sight-grade — KF7 = 30 (2×), AR33/M16 = 20 (**3×**). Using those verbatim as
  iron-sight ADS FOV contradicts the "mild 1.1–1.5×" rule. We therefore **clamp** iron-sight ADS FOV to
  a mild floor `MIN_IRON_ADS_FOV ≈ base/1.5` (≈ 40 at base 60) and reserve true magnification only for
  weapons flagged as real scopes (sniper/camera, which **yield** to the existing analog zoom). Faithful
  vs modern is an open decision (§8.10) — a "Faithful" option can disable the clamp.
- **Anchors:** Apex@90° — 1× = 90°, 2× = 45°, 4× = 22.5°, 6× = 15°; doubling magnification halves FOV.

### 1.3 FOV transition curve & FOV/pose coupling
- **Rule:** camera-FOV ramp and weapon-pose raise must be driven off the **same alpha** (BFV/BF2042
  shipped the decoupled-zoom bug). One blend fraction (`zoomintime/zoomintimemax`) drives both FOV and
  the Phase-2 pose.
- **Targets:** linear for MVP (short in-times mask it); optional ease-out "settle" later.

### 1.4 Sensitivity coupling — flat multiplier × optional FOV term
- **Rule:** two composable knobs: a flat **ADS sensitivity multiplier** (default 1.0) and an optional
  **FOV-couple** (multiply turn by `viGetFovY()/baseFov` so zooming slows aim). The engine's own turn
  path *is* the FOV-coupled model (`viGetFovY()/FOV_Y_F`, `bondview.c:9385/9401/11813`); the PC mouse
  path is currently a flat fixed ratio.
- **Targets & double-dip fix:** ship the flat multiplier **plus** an opt-in `Input.AdsFovCoupleSens`.
  **When FOV-couple is ON (default), the per-weapon `sens_mult` defaults to 1.0** so the FOV ratio is
  the *sole* zoom-based slowdown — otherwise a KF7 (30/60 = 0.5) would be slowed by `sens_mult` *and*
  the FOV ratio = 0.25× (sniper-grade on an AR). `sens_mult` is then only an *extra* per-weapon trim.
  A Relative/Monitor-Distance-Coefficient model (`atan(C·tan(fov/2))`) is **out of scope** (overkill for
  a GE port). Keep a floor (≈0.2) so the sniper isn't glacial. All inside `pcNativeLiveLookAllowed()`.

### 1.5 Movement / strafe while ADS — modern arcade model (not tactical)
- **Rule:** you **can** move and strafe while ADS, just **slower** — a **speed** scalar only, never
  accuracy/spread (GE fires from the eye with FOV-normalized spread; there is no velocity term, so there
  is no moving-bloom to add and none should be added). **Strafe is penalized harder than forward** (the
  peek/duel axis). Penalty **scales with weapon weight/zoom**.
- **Verified engine premise:** the native port does **not** slow or lock movement while aiming —
  `bondviewApplyNativeMoveIntent` (`bondview.c:10383-10439`, callsite ~11210) force-sets
  `canLookAhead=1`/`canTurnTank=1`, overriding the classic `!insightaimmode` gate; `speedsideways =
  analogStrafe/70` (`bondview.c:11476`) and `speedforwards = analogWalk/70` (`bondview.c:11495`) run at
  full magnitude while aimed. So we **add** a penalty; we do not enable movement.
- **Targets (per-class, anchored on Apex; forward / strafe):** pistol 0.90 / 0.80, SMG 0.88 / 0.78,
  light (KF7) 0.85 / 0.75, AR (AR33/M16) 0.55 / 0.45, heavy (AK47) 0.55 / 0.45, sniper 0.40 / 0.30,
  default fallback 0.70 / 0.60. Strafe runs ~0.75–0.89× of forward (tighter on heavier weapons). Keep
  strafe ≥ 0.30 (never a near-stop). Conservative vs CoD's 0.4–0.6 to respect GoldenEye's faster feel.

### 1.6 Accuracy / sway / flinch
- **Rule:** ADS collapses spread (free here via FOV); recoil/sway/flinch are **viewkick-class**
  (cosmetic camera/model motion) and must drive the existing cosmetic gun-model fields — **never**
  `crosshair_angle`/`field_FFC` (the bullet ray + convergence accumulators).
- **Targets:** explicit per-weapon `spread_mult` (scalar on `Inaccuracy`, default 1.0). Optional Phase-2:
  ~30–50% recoil reduction by scaling the per-hand `hands[hand].field_A84`/`field_A88`; damp
  `gunposamplitude` sway by the blend fraction. Any per-shot logic must keep the fixed **4
  `randomGetNext()` draws/shot** or RAMROM replays desync.

### 1.7 UX / options / accessibility
- **Rule:** modern ADS is a **bundle of independent toggles** (hold-vs-toggle, separate ADS sensitivity,
  FOV-on-aim choice, motion-reduction), not one switch. Classic remasters (GoldenEye XBLA, Perfect Dark
  XBLA) prove the purist-safe pattern: keep the original intact, add modern aiming as opt-in named
  options. Hold-vs-toggle is already inherited from `cur_player_get_aim_control()`.

---

## 2. How it maps to this engine (verified hooks)

| Subsystem | Hook | Notes |
|---|---|---|
| **Aim-mode boolean** | `insightaimmode` (`bondview.h:529`), set in `bondviewProcessInput` (`bondview.c:10496`); fans out to `MoveData` (`bondview.c:10770-10784`) | Per-`g_CurrentPlayer`; hold/toggle via `cur_player_get_aim_control()` |
| **FOV-zoom engine** | per-frame driver `bondview.c:11336-11360`; raw arm `trigger_watch_zoom` (`bondview.c:8071`, UNGUARDED, fixed time); guarded `bondviewTriggerWatchZoom` (`bondview.c:8097`, derives `|ΔFOV|/2`); step `bondviewUpdateWatchZoomIn` (`bondview.c:8193`); apply `viSetFovY` (`fr.c:1929`) | `get_item_in_hand_zoom()` (`gun.c:1718`); base `bondviewGetNativeBaseFovY()` (**`static`**, `bondview.c:74/13936`) |
| **Eye-origin bullets** | `bullet_path_from_screen_center` (`gun.c:28456-28493`); `scaledspread = (120·Inaccuracy)/viGetFovY()` (`gun.c:28471`) | Origin `(0,0,0)`, dir = `crosshair_angle`; muzzle cosmetic; **4 RNG draws/shot** |
| **Sensitivity path** | NATIVE_PORT block `lvl.c:5751-5832`; aim switch ~`lvl.c:5784`; gamepad `gp_scale`→`mdx`/`mdy` then ×`sens` (~`lvl.c:5797/5811`); gate `pcNativeLiveLookAllowed()` (`lvl.c:91-106`); pitch clamp `lvl.c:5819-5822` | Gamepad delta is folded into `mdx` then re-scaled by `sens` — apply the ADS multiplier **once**, at `sens` |
| **Movement speed pipeline** | native override `bondviewApplyNativeMoveIntent` (`bondview.c:10383-10439`); analog assign `speedsideways`/`speedforwards` (`bondview.c:11476/11495`); ×1.08 / ×`speedboost` (`bondview.c:11527-11528`); **crouch ×0.5 seam** (`bondview.c:12288-12289`); world integrate `bondview.c:13080/13199-13228`; walk-anim `bondviewMoveAnimationTick` (~`bondview.c:9800`) | Aim does **not** slow movement today; inject the penalty at the **post-crouch seam (~`bondview.c:12290`)** |
| **Pose seam** | `portBuildFirstPersonWeaponRoot` (`gun.c:2787`, pose `:2933-2998`); X crosshair-follow `gun.c:6037`; per-hand recoil `hands[hand].field_A84/field_A88` (`gun.c:1965/2000`); sway `gunposamplitude` (`gun.c:1821-1865`) | Port-local, env-tunable via `GE007_FP_*`; cosmetic to hits |
| **Settings registry** | `settingsRegisterInt/Float` (`settings.c:52/96`, **10-arg**); `platformRegisterConfig` (`platform_sdl.c:~1346+`); globals near `platform_sdl.c:934`; `ge007.ini` | `int 0..1` bool idiom (no `SETTING_TYPE_BOOL`) |

**`settingsRegisterInt/Float` real signature** (use this in every ticket):
```c
void settingsRegisterInt  (const char *key, s32 *var, s32 def, s32 min, s32 max,
                           SettingScope scope, const char *env, const char *cli,
                           const char *label, const char *help);
void settingsRegisterFloat(const char *key, f32 *var, f32 def, f32 min, f32 max,
                           SettingScope scope, const char *env, const char *cli,
                           const char *label, const char *help);
```
Match the existing `Video.*`/`Input.*` calls at `platform_sdl.c:1346-1451` for the `scope`/`env`/`cli`/
`label`/`help` arguments.

---

## 3. Settings keys (registered across the backlog)

| Key | Type / default | Range | Introduced by |
|---|---|---|---|
| `Input.AdsEnabled` | int, **0** | 0..1 | ADS-0.1 |
| `Input.AdsSensitivity` | float, **1.0** | 0.1..2.0 | ADS-1.1 |
| `Input.AdsFovCoupleSens` | int, **1** | 0..1 | ADS-1.2 |
| `Input.AdsCenterCrosshair` | int, **1** | 0..1 | ADS-3.1 |
| `Input.AdsSpreadEnabled` | int, **1** | 0..1 | ADS-4.1 |
| `Input.AdsMovePenalty` | int, **1** | 0..1 | ADS-5.1 |
| `Input.AdsMoveScale` | float, **1.0** | 0.1..2.0 | ADS-5.1 |
| `Input.AdsStrafeScale` | float, **1.0** | 0.1..2.0 | ADS-5.1 |
| _(Input.AdsSprintLockout — deferred; ADS-5.3 not implemented, no setting registered)_ | — | — | ADS-5.3 |
| `Input.AdsFaithfulZoom` | int, **0** | 0..1 | ADS-2.2 |
| `Input.AdsModelPose` | int, **1** | 0..1 | ADS-6.1 |
| `Input.AdsRecoilReduce` | float, **0.0** | 0.0..1.0 | ADS-7.1 |

Per-weapon `ads_*` values live in the **port-side `AdsProfile` table** (ADS-2.2), never in `WeaponStats`
(ROM-matched), and never as user sliders (avoids per-optic UI creep). **Source-of-truth note:** the
per-weapon table defaults (e.g. movement 0.70/0.60) are the source of truth; the `*Scale` sliders only
*trim* them. To run vanilla movement while still using FOV-zoom, set `Input.AdsMovePenalty=0` — the
`=1.0` sliders do **not** neutralize the table.

**Hand-selection policy (applies to every profile lookup):** sens/FOV/movement are **per-player**, so
resolve the profile from the **dominant (right) hand**; if the right hand is empty, fall back to the left
hand's weapon; if both empty, use the default profile. Per-shot `spread_mult` (ADS-4.1) instead uses the
**firing hand** (`arg2` in `bullet_path_from_screen_center`). State this in each ticket's acceptance
criteria.

---

## 4. The backlog (EPICS → MILESTONES → TICKETS)

Milestones are sequenced so **each is independently shippable behind `Input.AdsEnabled`**; vanilla stays
untouched at every step. Effort/risk are t-shirt sizes (S/M/L).

---

### EPIC A — Foundation, data & test scaffolding (unblocks everything)

#### Milestone M0 — Flags, accessors, gates *(shippable: a no-op build)*

**ADS-0.1 — Register the master flag; prove byte-identity + persistence**
- **Goal:** add `Input.AdsEnabled` with the build provably unchanged when off, and confirm the key
  round-trips through `ge007.ini`.
- **Files:** globals near `platform_sdl.c:934`; `platformRegisterConfig` (`platform_sdl.c:~1346+`);
  `src/platform/settings.c`.
- **Change:** declare `s32 g_pcAdsEnabled = 0;`; register with the full 10-arg form
  (`settingsRegisterInt("Input.AdsEnabled", &g_pcAdsEnabled, 0, 0, 1, <scope>, "GE007_ADS_ENABLED",
  "--ads", "Enable ADS", "Modern aim-down-sights (opt-in)")`). No gameplay code yet.
- **Acceptance:** key appears in generated `ge007.ini`; env/CLI override works; value **persists across a
  write→restart→reload** cycle; no other behavior changes.
- **Validation:** `AdsEnabled=0` ⇒ behavior byte-identical vs baseline (ADS-0.3 gates). Build under
  `-DNATIVE_PORT`.
- **Deps:** none. **Effort/Risk:** S / S.

**ADS-0.2 — Public base-FOV accessor (prerequisite refactor)**
- **Goal:** expose the runtime base FOV so ADS code in `ads_profiles.c`/`lvl.c` can compute mild zooms.
- **Files:** `bondview.c:74/13936` (`static f32 bondviewGetNativeBaseFovY`); `bondview.h`.
- **Change:** add a non-static `f32 bondviewGetBaseFovY(void)` in `bondview.h` that returns
  `bondviewGetNativeBaseFovY()` (or de-static the existing one). **Reason:** it's currently `static`
  (not linkable), and base FOV is the user-settable `Video.FovY` (45..90) — so ADS FOV must be computed
  at **query time**, never baked as a constant.
- **Acceptance:** accessor links from `ads_profiles.c` and `lvl.c`; returns the same value as the
  internal one; changing `Video.FovY` is reflected.
- **Validation:** pure refactor — `AdsEnabled=0/1` both byte-identical to pre-refactor for the FOV value.
- **Deps:** none. **Effort/Risk:** S / S.

**ADS-0.3 — Define the shared validation gates (used by every determinism-sensitive ticket)**
- **Goal:** one concrete, named definition of the three gates the rest of the backlog references.
- **Files/assets:** `scripts/ci/`, `scripts/ares/`, `build/ares-movement-oracle`,
  `scripts/ge007.{u,e,j}-test_basis.csv`; new debug RNG counter.
- **Change:** define and document:
  1. **Byte-identity gate** — with `AdsEnabled=0`, the ares movement-oracle / `*-test_basis.csv`
     comparison and a recorded RAMROM replay diff clean vs baseline (exact command + pass criteria).
  2. **Split-screen smoke gate** — the existing split-screen smoke check used to validate this branch,
     run with **one player ADS-in and the other ADS-out**.
  3. **RNG-draw-count assertion** — a `randomGetNext()` call counter compiled under a debug flag,
     asserting **4 draws/shot**; referenced by ADS-4.1/5.1/7.1 instead of restating prose.
- **Acceptance:** each gate is a runnable command with a documented pass condition.
- **Validation:** gates pass on the untouched baseline.
- **Deps:** none. **Effort/Risk:** M / S.

**ADS-2.2 — Port-side `AdsProfile` table with query-time computed defaults**
- **Goal:** one accessor returning per-weapon ADS params, computing FOV at query time, zero authoring
  required for any item.
- **Files:** **new** `src/game/ads_profiles.{h,c}`; `adsGetProfile(ITEM_IDS)`; `CMakeLists.txt`. Keyed by
  `ITEM_IDS` (same key as `get_ptr_item_statistics`/`get_item_in_hand_zoom`).
- **Change:** define the struct + accessor, `NATIVE_PORT`-guarded; **`WeaponStats` untouched.** Store a
  **factor**, not an absolute FOV, for iron-sight weapons; resolve absolute FOV at query/substitution
  time against `bondviewGetBaseFovY()` (ADS-0.2).
  ```c
  struct AdsProfile {
      f32 ads_fov_factor;             // <0 => yield to analog scope; else mult of runtime base FOV
      f32 ads_in_time, ads_out_time;  // fixed lerp durations (s)
      f32 sens_mult;                  // EXTRA per-weapon look trim (1.0 when FOV-couple is the slowdown)
      f32 spread_mult;                // Inaccuracy multiplier at full ADS
      f32 ads_move_mult, ads_strafe_mult;                  // movement (M5)
      f32 pose_yaw_rad, pose_pitch_rad, pose_roll_rad;     // Phase 2
      f32 pose_off_x, pose_off_y, pose_off_z;              // Phase 2
      u8  is_scope;                   // true scope (sniper/camera) => yield to analog zoom
  };
  const struct AdsProfile *adsGetProfile(ITEM_IDS item);   // authored row, else computed default
  // resolved at query time:
  //   if (p->is_scope) -> caller yields to get_item_in_hand_zoom()
  //   else ads_fov = clampMildIron(p->ads_fov_factor * bondviewGetBaseFovY())
  //   clampMildIron(f) = max(f, base/1.5)   unless Input.AdsFaithfulZoom (then no clamp)
  ```
  **Computed default:** `ads_fov_factor` derived from `WeaponStats.Zoom`: `is_scope` weapons → yield;
  `Zoom>0` iron weapons → `factor = Zoom/base` then **mild-iron clamped** (so AR33's 3× becomes ≤1.5×
  unless `AdsFaithfulZoom`); `Zoom==0` → `factor = 0.85`. `sens_mult=1.0`; `spread_mult=1.0`;
  `ads_in_time=0.12`; `ads_out_time=0.10`; `ads_move_mult=0.70`; `ads_strafe_mult=0.60`; pose=0.
- **Authored rows** (factors, resolved at base 60):

  | Weapon | `ITEM_IDS` | `ads_fov_factor` | `sens_mult` | `spread_mult` | move / strafe | scope? |
  |---|---|---|---|---|---|---|
  | PP7 | `ITEM_WPPK` | 0.85 (→51) | 1.0 | 0.6 | 0.90 / 0.80 | no |
  | RC-P90 | `ITEM_FNP90` | 0.85 (→51) | 1.0 | 0.7 | 0.88 / 0.78 | no |
  | KF7 | `ITEM_AK47` | 0.67 (→40, mild-clamped from 30) | 1.0 | 0.5 | 0.85 / 0.75 | no |
  | AR33 | `ITEM_M16` | 0.67 (→40, mild-clamped from 20=3×) | 1.0 | 0.5 | 0.55 / 0.45 | no |
  | Sniper | `ITEM_SNIPERRIFLE` | **yield** | n/a | 1.0 | 0.40 / 0.30 | **yes** |
  | Spy camera | `ITEM_CAMERA` | **yield** | n/a | — | n/a | **yes** |

  *(With `Input.AdsFaithfulZoom=1`, KF7/AR33 use their true `Zoom` (30/20) for GE-faithful aim instead of
  the mild clamp — see §8.10.)*
- **Acceptance:** `adsGetProfile` returns authored row when present, computed default otherwise; FOV
  resolves at query time and tracks `Video.FovY`; `is_scope` weapons yield; non-weapon `ITEM_IDS`
  (keys/tokens) yield inert profiles; no NaN/zero-FOV path.
- **Validation:** unit-call every `ITEM_IDS`, assert no NaN/degenerate `guPerspectiveF`; `AdsEnabled=0`
  never consults the table.
- **Deps:** ADS-0.2. **Effort/Risk:** M / S.

---

### EPIC B — Look & FOV (the core "aim" feel)

#### Milestone M1 — ADS sensitivity *(shippable: slow-look on aim, no FOV change yet)*

**ADS-1.1 — Flat ADS sensitivity multiplier (applied once)**
- **Goal:** scale look speed while aimed by `profile->sens_mult × Input.AdsSensitivity`.
- **Files:** `lvl.c:5784-5811`, inside `pcNativeLiveLookAllowed()` (`lvl.c:91-106`).
- **Change:** when `g_pcAdsEnabled && insightaimmode`, multiply **the final `sens`** (the mouse/combined
  multiply at ~`lvl.c:5797` feeding `vv_theta`/`vv_verta`/`g_TankTurretAngle` at 5811/5819) by
  `adsGetProfile(<dominant hand weapon>)->sens_mult * g_pcAdsSensitivity`. **Do NOT also scale
  `gp_scale`** (~`lvl.c:5783`) — the gamepad delta is folded into `mdx`/`mdy` and re-multiplied by
  `sens`, so scaling both double-applies (`sens_mult²`). Pitch clamp (`lvl.c:5819-5822`) still applies
  after.
- **Settings:** `Input.AdsSensitivity` float, default **1.0**, 0.1..2.0.
- **Acceptance:** aimed look slows by the multiplier on **both** mouse and pad (single application);
  hip look unchanged; clamp ±90 intact; `sens_mult=1.0 && AdsSensitivity=1.0` ⇒ identical to current aim
  sens. Profile resolved per hand-selection policy (§3).
- **Validation:** `AdsEnabled=0` byte-identical (ADS-0.3); change is inside `pcNativeLiveLookAllowed()`;
  split-screen: P1 aiming slower, P2 not.
- **Deps:** ADS-2.2. **Effort/Risk:** S / S.

**ADS-1.2 — Optional FOV-coupled sensitivity (resolves the double-dip)**
- **Goal:** match the engine's legacy/affected feel so deep-zoom weapons aren't twitchy, without
  double-slowing.
- **Files:** `lvl.c:5784-5811`; uses `viGetFovY()` and `bondviewGetBaseFovY()` (ADS-0.2).
- **Change:** when `g_pcAdsEnabled && g_pcAdsFovCoupleSens && insightaimmode`, additionally multiply the
  final `sens` by `clamp(viGetFovY()/bondviewGetBaseFovY(), 0.2, 1.0)` (the engine's exact
  `viGetFovY()/FOV_Y_F` ratio, with a floor so the sniper stays usable). **Because the table's
  `sens_mult` defaults to 1.0 when coupling is on (ADS-2.2), the FOV ratio is the sole zoom-based
  slowdown** — no `sens_mult²`-style double-dip. Runs after ADS-1.1, inside `pcNativeLiveLookAllowed()`.
- **Settings:** `Input.AdsFovCoupleSens` int, default **1**, 0..1.
- **Acceptance:** with FOV narrowed (post-M2), aimed turn slows proportionally; off ⇒ only the flat
  multiplier applies; verify the mouse delta is not *also* routed through the engine's FOV-scaling turn
  path (no hidden third application).
- **Validation:** `AdsEnabled=0` byte-identical; replay-gated; test mouse/pad/stick; split-screen
  asymmetric. **Deps:** ADS-1.1; fully feel-validated only after ADS-2.1. **Effort/Risk:** S / M.

#### Milestone M2 — FOV-zoom on aim *(shippable: the headline "zoom on aim")*

**ADS-2.1 — FOV substitution into the move loop (rising-edge raw arming)**
- **Goal:** drive the existing FOV-zoom engine to each weapon's resolved ADS FOV on aim, honoring a fixed
  per-class ADS-in time, without freezing the lerp.
- **Files:** per-frame FOV block `bondview.c:11344-11356`; raw arm `trigger_watch_zoom` (`bondview.c:8071`);
  step `bondviewUpdateWatchZoomIn` (`bondview.c:8193`). All inside `if (watch_animation_state ==
  WATCH_ANIMATION_0x0)`. Edge state in a side array `g_adsWasAiming[4]` keyed by `get_cur_playernum()`
  (no struct field).
- **Change:** when `g_pcAdsEnabled && moveData.zooming`, resolve the dominant-hand profile; if
  `is_scope`, **fall through to `get_item_in_hand_zoom()`** (preserves sniper/camera analog scope
  verbatim); else target = `clampMildIron(factor·bondviewGetBaseFovY())`.
  - **ARMING (required, not optional):** on the **aiming rising edge** (`!g_adsWasAiming[p] &&
    moveData.zooming`) **or when the resolved target changes** (weapon switch), call raw
    `trigger_watch_zoom(target, ads_in_time)` (use `ads_out_time` on the falling edge back to base);
    otherwise do nothing and let `bondviewUpdateWatchZoomIn()` step it. Then set `g_adsWasAiming[p]`.
    **Do NOT** call the guarded `bondviewTriggerWatchZoom` here — its derived `|ΔFOV|/2` duration ignores
    `ads_in_time` (a 60→30 zoom ≈ 15 time-units; verified `bondview.c:8103/8108`). **Do NOT** call raw
    `trigger_watch_zoom` *every* frame — it re-captures `zoominfovyold=current` and resets
    `zoomintime=0`, freezing the lerp one frame in (verified `bondview.c:8071`).
  - **EYE-ORIGIN INVARIANT (do not regress):** narrowing FOV already tightens hitscan (both `c_scale*`
    and `scaledspread`) — that **is** the ADS accuracy reward; do not add a second CoD-style accuracy
    bonus on top (double-dip). ADS FOV below the `[45,90]` base clamp is fine (flows through
    `zoominfovynew`, not `g_pcFovY`).
- **Acceptance:** aiming an iron weapon zooms to its mild ADS FOV in ~`ads_in_time`; `is_scope` weapons
  unchanged; releasing eases back in ~`ads_out_time`; the lerp **reaches** target (not frozen); FOV
  tracks `Video.FovY` changes; no thrash with watch menu/scope (`watch_animation_state==0`).
- **Validation:** `AdsEnabled=0` byte-identical; sniper/camera scope regression (zoom in/out + watch
  sub-screen); split-screen with **one player ADS-in, the other ADS-out** (per-`g_CurrentPlayer`
  `zoominfovy`); RAMROM deterministic (FOV driven off replayed `insightaimmode`).
- **Deps:** ADS-2.2. **Effort/Risk:** M / **L** (shared FOV engine is the top risk).

---

### EPIC C — Accuracy & sights

#### Milestone M3 — Sights == impact (center-pull) *(shippable: console/stick alignment)*

**ADS-3.1 — Ramped crosshair center-pull (both accumulators)**
- **Goal:** on the analog-stick path, ramp the aim accumulators toward true center while aimed so sights
  overlay impact.
- **Files:** after `gunSetSightVisible(...)` at `bondview.c:11324`; accumulators `crosshair_x_pos`/
  `crosshair_y_pos` (bullet ray) and `gun_azimuth_angle`/`field_FFC` (gun convergence + `gun_pos.x`
  follow at `gun.c:6037`); reference snap `sub_GAME_7F06802C` (`gun.c:28271`, hard snap — no fraction).
- **Change:** when `g_pcAdsEnabled && g_pcAdsCenterCrosshair && moveData.aiming`, **lerp both
  accumulators toward 0** by the `zoomintime/zoomintimemax` fraction *before*
  `caclulate_gun_crosshair_position_rotation` maps them to screen px (or bias `turn_x/turn_y`). **Both**
  must be biased — `crosshair_angle` drives the bullet ray, `field_FFC` drives gun-model convergence —
  for true what-you-see-is-what-you-hit.
- **Settings:** `Input.AdsCenterCrosshair` int, default **1**, 0..1.
- **Acceptance:** on stick aiming the reticle ramps to center and impacts follow; on **mouse** this is
  largely a **no-op** (crosshair already near-center, `vv_theta` drives the view) — confirm no
  regression there.
- **Validation:** `AdsEnabled=0` byte-identical; this **intentionally changes bullet direction** (gated
  by `AdsCenterCrosshair`); **measurable test:** fire N shots at a wall at fixed range with the stick
  centered, assert the impact centroid is within X px of the reticle center (define X in ADS-0.3); note
  `gunTryAutoAimDir` may override dir in `g_deterministic`; split-screen per-player
  (`getPlayer_c_screenleft/width`).
- **Deps:** ADS-2.1 (shares the blend fraction). **Effort/Risk:** M / M.

#### Milestone M4 — Explicit ADS spread tightening *(shippable: tighter pistols/SMGs)*

**ADS-4.1 — Per-weapon `spread_mult`**
- **Goal:** let `Zoom==0` weapons get tighter ADS grouping without deep zoom.
- **Files:** `bullet_path_from_screen_center` (`gun.c:28464-28471`).
- **Change:** when `g_pcAdsEnabled && g_pcAdsSpreadEnabled && <firing hand aiming>`, `inaccuracy *=
  adsGetProfile(getCurrentPlayerWeaponId(arg2))->spread_mult` **before** `scaledspread =
  (120·Inaccuracy)/viGetFovY()`. Use the **firing hand** (`arg2`), per §3. **Scalar multiply only — the
  4 `randomGetNext()` draws/shot are unchanged.** Default `spread_mult=1.0` so only intended weapons
  tighten.
- **Settings:** `Input.AdsSpreadEnabled` int, default **1**, 0..1.
- **Acceptance:** PP7/RC-P90 group tighter while aimed; `spread_mult=1.0` weapons unchanged; muzzle
  remains cosmetic.
- **Validation:** `AdsEnabled=0` byte-identical; **RNG-draw assertion (ADS-0.3) == 4/shot**; RAMROM
  deterministic.
- **Deps:** ADS-2.2. **Effort/Risk:** S / M (RNG-stream caution).

---

### EPIC D — Movement while ADS (first-class)

> The native port runs aimed movement at **full** speed (premise verified, §1.5). We **add** a penalty at
> the post-crouch seam; modern arcade model: speed scalar only, no moving-bloom, strafe penalized harder.

#### Milestone M5 — ADS movement & strafe penalty *(shippable: slower controlled aimed movement)*

**ADS-5.1 — Inject the ADS speed-scale block at the post-crouch seam**
- **Goal:** apply a forward and a separate, harder strafe multiplier to aimed movement.
- **Files:** `bondview.c:~12290` — immediately after the crouch block
  (`g_CurrentPlayer->speedforwards *= 0.5f; g_CurrentPlayer->speedsideways *= 0.5f;` at `12288-12289`),
  inside the existing `in_tank_flag==0` guard. Reads `ads_move_mult`/`ads_strafe_mult` from the
  dominant-hand profile.
- **Change:**
  ```c
  if (g_pcAdsEnabled && g_pcAdsMovePenalty && g_CurrentPlayer->insightaimmode) {   // per-player
      const struct AdsProfile *p = adsGetProfile(<dominant hand weapon id>);
      g_CurrentPlayer->speedforwards *= p->ads_move_mult   * g_pcAdsMoveScale;
      g_CurrentPlayer->speedsideways *= p->ads_strafe_mult * g_pcAdsStrafeScale;
  }
  ```
  Pure **post-ramp scalar** on the resolved per-frame speed (same idiom as the crouch 0.5f). It rides
  **after** the ×1.08/×`speedboost` (`bondview.c:11527-11528`), so the sprint bonus is implicitly
  throttled below walk (0.55×1.25 = 0.69 < 1.0) with no sprint-cancel state. **Composes
  multiplicatively with crouch** (crouch-ADS forward = 0.5×`ads_move_mult`). Scaling the speed scalars
  (not displacement) means `MAX_SPEED_FACTOR 0.8` and `bondviewMoveAnimationTick` inherit it and the
  gait stays in sync.
  - **DO NOT** inject at the analog assignment (`11476/11495`) or ramp targets — distorts accel /
    desyncs the walk anim.
  - **DO NOT** gate behind `pcNativeLiveLookAllowed()` (that's the *look-delta* replay gate; gating
    movement there desyncs replays). The `AdsEnabled`+`AdsMovePenalty` flags alone bypass the block.
  - **DO NOT** add per-velocity spread/bloom. Speed-only.
- **Settings:** `Input.AdsMovePenalty` int, default **1**, 0..1 (set 0 for vanilla aimed movement +
  FOV-zoom); `Input.AdsMoveScale` / `Input.AdsStrafeScale` float, default **1.0**, 0.1..2.0. Per-weapon
  `ads_move_mult`/`ads_strafe_mult` from ADS-2.2 (the source of truth; sliders only trim). Keep resolved
  strafe ≥ 0.30.
- **Acceptance:** aimed forward/strafe slower per class; strafe < forward; sprint bonus suppressed while
  aimed; crouch+ADS compounds; tank path untouched (`in_tank_flag==0`); accel feel preserved (no ramp
  distortion); walk anim matches movement; `AdsMovePenalty=0` ⇒ full-speed aimed movement.
- **Validation:** `AdsEnabled=0` ⇒ movement byte-identical (pure scalar, no new RNG, not behind the look
  gate); split-screen with **one player ADS-slowed, the other full speed** (per-`g_CurrentPlayer`);
  confirm gait/`MAX_SPEED_FACTOR` sync.
- **Deps:** ADS-2.2. Independent of M1–M4. **Effort/Risk:** S / M.

**ADS-5.2 — Live env-knob tuning for movement**
- **Goal:** dial movement scalars live before baking into the table.
- **Files:** `src/game/ads_profiles.c` (env read at profile load), mirroring `GE007_FP_*`.
- **Change:** read `GE007_ADS_MOVE` / `GE007_ADS_STRAFE` at profile load and override the in-hand
  weapon's `ads_move_mult`/`ads_strafe_mult`.
- **Acceptance:** env vars change aimed speed at runtime; unset ⇒ table defaults.
- **Validation:** env unset ⇒ identical to ADS-5.1; `AdsEnabled=0` unaffected.
- **Deps:** ADS-5.1. **Effort/Risk:** S / S.

**ADS-5.3 — Optional sprint-out lockout (polish; off by default; deferrable)**
- **Goal:** a brief sprint-out feel when entering ADS at near-max forward speed.
- **Files:** the M5 block; reuse `speedmaxtime60`.
- **Change:** when `g_pcAdsSprintLockout` and `speedmaxtime60` shows near-max forward at aim onset,
  scale/defer the slow ramp a few frames. **Requires** a per-player deferral countdown — store it in a
  side array `g_adsSprintLockout[4]` (no struct field; same rule as `g_adsWasAiming[4]`), **not** a new
  struct field, and reuse `speedmaxtime60` rather than a new determinism-perturbing timer. Off by
  default; not required for MVP — drop if it doesn't pull its weight.
- **Settings:** `Input.AdsSprintLockout` int, default **0**, 0..1.
- **Acceptance:** with the flag on, sprint→ADS has a short readiness delay; off ⇒ identical to ADS-5.1.
- **Validation:** `AdsEnabled=0` and `AdsSprintLockout=0` ⇒ byte-identical; RAMROM deterministic.
- **Deps:** ADS-5.1. **Effort/Risk:** S / M.

---

### EPIC E — Phase 2 polish (aligned sights, recoil, sway)

#### Milestone M6 — Sighted weapon-model pose & authoring loop

**ADS-6.1 — Sighted model pose blend + `GE007_ADS_*` authoring loop**
- **Goal:** land the real iron-sight notch / scope reticle on screen center while aimed.
- **Files:** `portBuildFirstPersonWeaponRoot` (`gun.c:2933-2998`); X crosshair-follow `gun.c:6037`; sway
  `gunposamplitude` (`gun.c:1821-1865`); blend factor via `g_adsPoseT[4]` keyed by `get_cur_playernum()`
  (no struct field) or reuse `zoomintime/zoomintimemax`.
- **Change:** additively blend the profile `pose_*` toward the sighted pose by the blend fraction
  (restore via existing `saved_pos`/`matrix_4x4_set_position`); damp the X crosshair-follow and sway by
  `(1-t)`, **gated on per-hand aim** (`gun.c:14345`) so left-hand/MP/watch-preview sway isn't globally
  damped. **Accuracy-safe by construction** (model cosmetic to the eye→crosshair ray). Drive FOV and pose
  off the **same alpha** (avoid the BF decoupling bug). Generalize `GE007_FP_*` → `GE007_ADS_*`
  (`GE007_ADS_FOV`, `GE007_ADS_POSE_X`, …) for live per-weapon authoring, then bake.
- **Settings:** `Input.AdsModelPose` int, default **1**, 0..1. Per-weapon pose fields in the table.
- **Acceptance:** authored weapons show aligned sights on center; unauthored weapons fall back to a
  centered family pose (not mis-sighted); crosshair-follow damped while aimed; left-hand/MP sway not
  globally damped.
- **Validation:** `AdsEnabled=0` / `AdsModelPose=0` byte-identical; dual-wield/left-hand check;
  split-screen; bullets still land at center (cosmetic invariant).
- **Deps:** ADS-2.1, ADS-3.1. **Effort/Risk:** L / M (effort dominated by per-weapon hand-tuning).

**ADS-7.1 — Optional ADS recoil reduction (viewkick-class only)**
- **Goal:** steadier aimed picture via reduced cosmetic recoil.
- **Files:** per-hand `g_CurrentPlayer->hands[hand].field_A84` (raise/pitch, `gun.c:1965`) /
  `field_A88` (bolt, `gun.c:2000`); inject at the pose seam `portBuildFirstPersonWeaponRoot`
  (`gun.c:2933-2998`), using the **hand index the seam is building for**.
- **Change:** multiply `hands[hand].field_A84`/`field_A88` by `(1 - g_pcAdsRecoilReduce × blendFrac)`
  while that hand is aimed (target ~30–50% at full). These are the **viewkick/cosmetic** channel —
  accuracy-safe because bullets fire from the eye, not the muzzle. **NEVER** route recoil/sway/flinch
  into `crosshair_angle`/`field_FFC`. Stay inside `watch_animation_state==0` and the replay gates.
- **Settings:** `Input.AdsRecoilReduce` float, default **0.0** (off), 0.0..1.0.
- **Acceptance:** aimed recoil visibly reduced per the scalar **for the correct hand** (dual-wield not
  mis-scaled); bullet trajectory unchanged; off ⇒ no change.
- **Validation:** `AdsEnabled=0` / `AdsRecoilReduce=0` byte-identical; RNG-draw assertion unchanged;
  trajectory identical with/without (cosmetic).
- **Deps:** ADS-6.1. **Effort/Risk:** M / M.

---

### EPIC F — Interaction correctness (cross-cutting validation)

**ADS-8.1 — Toggle-vs-hold ADS-timing validation**
- **Goal:** confirm ADS-in/out timing and the FOV ramp behave under **toggle** mode (`insightaimmode`
  flips without a held button) as well as **hold**.
- **Files:** `cur_player_get_aim_control()` (`watch.c:612`); ADS-2.1 arming edge logic.
- **Change:** none functional if correct; this is a validation + edge-case ticket. Confirm the rising/
  falling-edge detection in `g_adsWasAiming[4]` fires correctly when `insightaimmode` toggles.
- **Acceptance:** toggle on → ramps in over `ads_in_time`; toggle off → ramps out over `ads_out_time`; no
  stuck/half-zoom state; rapid toggling doesn't freeze the lerp.
- **Validation:** scripted toggle/hold input; `AdsEnabled=0` unaffected.
- **Deps:** ADS-2.1. **Effort/Risk:** S / S.

**ADS-8.2 — Weapon-switch-while-aimed behavior**
- **Goal:** define and test what happens when the weapon changes while `insightaimmode` stays true (FOV
  target, `sens_mult`, movement mult, spread all change).
- **Files:** ADS-2.1 (FOV re-arm on target change), ADS-1.1/5.1 (per-frame profile lookup).
- **Change:** the target-change branch of ADS-2.1 must **re-arm the lerp from the current FOV** to the
  new weapon's ADS FOV over `ads_in_time`; sens/movement update next frame from the new profile.
- **Acceptance:** switching weapons while aimed smoothly retargets FOV (no snap/freeze) and applies the
  new weapon's sens/movement/spread; switching to a scope weapon hands off to the analog path.
- **Validation:** scripted switch-while-aimed; split-screen; RAMROM deterministic.
- **Deps:** ADS-2.1, ADS-1.1, ADS-5.1. **Effort/Risk:** S / M.

---

## 5. Dependency / sequencing overview

```
ADS-0.1 ─┐
ADS-0.2 ─┼─ ADS-2.2 ─┬─ ADS-1.1 ── ADS-1.2              (M1 sensitivity)
ADS-0.3 ─┘           ├─ ADS-2.1 ─┬─ ADS-3.1             (M2 FOV → M3 center-pull)
                     │           ├─ ADS-6.1 ── ADS-7.1  (M6 pose/recoil, Phase 2)
                     │           ├─ ADS-8.1              (toggle/hold validation)
                     │           └─ ADS-8.2              (switch-while-aimed)
                     ├─ ADS-4.1                          (M4 spread)
                     └─ ADS-5.1 ─┬─ ADS-5.2              (M5 movement)
                                 └─ ADS-5.3
```

- **ADS-0.1/0.2/0.3 + ADS-2.2** unblock everything (flag, accessor, gates, data).
- **M1 (sensitivity), M4 (spread), M5 (movement)** are mutually independent, each shippable behind the
  flag.
- **M2 (FOV)** unblocks **M3 (center-pull)**, **M6 (pose)**, and the interaction tickets (F);
  **ADS-1.2** is only *fully* feel-validated once M2 lands.
- **Phase 2 (M6, ADS-7.1)** is purely additive polish.

**Recommended ship order:** M0 → **M5** (movement is self-contained, high player value) → M1 → M2 →
M3/M4 → F → M6. Each milestone gates on the three ADS-0.3 checks: `AdsEnabled=0` byte-identity,
split-screen asymmetric ADS, RAMROM replay determinism.

**MVP = M0–M5 + F** (per-weapon FOV, FOV-correct slow-look, tighter spread, sights==impact on stick,
movement/strafe penalty, interaction correctness). **Phase 2 = M6** (aligned iron sights + optional
recoil reduction).

---

## 6. Definition of done (whole feature)

- All milestones merged behind `Input.AdsEnabled` (default **0**).
- `AdsEnabled=0` ⇒ build/behavior byte-identical to baseline; RAMROM replay diffs clean (ADS-0.3).
- `AdsEnabled=1` ⇒ per-weapon FOV-zoom on aim (sniper/camera analog scope unhijacked), FOV-correct (and
  optionally FOV-coupled) slow-look, tighter per-weapon spread, sights==impact on the stick path, and
  class-scaled movement/strafe penalty — all per-player, all individually toggleable.
- **Determinism:** no ticket adds/removes `randomGetNext()` draws (ADS-0.3 assertion); sensitivity
  changes stay inside `pcNativeLiveLookAllowed()`; movement scaling stays **outside** that gate but
  behind the master/penalty flags.
- **Split-screen:** validated with one player ADS-in and the other ADS-out (FOV, sens, movement all
  asymmetric).
- **No `WeaponStats` / ROM-matched edits**; all per-weapon ADS data in `src/game/ads_profiles.c`; FOV
  computed at query time against the public base-FOV accessor.
- Settings registered with the full 10-arg API, persisted to `ge007.ini`, env/CLI overridable; live
  `GE007_ADS_*` tuning loop available.
- Accuracy invariant documented and preserved: bullets exit the eye toward `crosshair_angle`; model and
  all recoil/sway/flinch are cosmetic; FOV narrowing is the entire ADS accuracy reward (no double-dip).

---

## 7. Risks & footguns (carry-forward)

| Risk / footgun | Severity | Mitigation / ticket |
|---|---|---|
| **Guarded wrapper duration `|ΔFOV|/2` ignores `ads_in_time`** (60→30 ≈ 15 units) | High | Use rising-edge raw `trigger_watch_zoom(fov, ads_in_time)` as the default — ADS-2.1 |
| **Unguarded `trigger_watch_zoom` per-frame freezes the lerp** (re-captures old, resets time) | High | Arm only on rising edge / target change; step with `bondviewUpdateWatchZoomIn` — ADS-2.1 |
| **Two FOV writers / shared watch-zoom thrash** | High | Substitute into the *sole* per-frame caller; stay inside `watch_animation_state==0` — ADS-2.1 |
| **`static bondviewGetNativeBaseFovY` not linkable; base FOV is runtime** | High | Public accessor + query-time FOV; never bake absolute FOV — ADS-0.2/2.2 |
| **`settingsRegister*` is 10-arg** (5-arg won't compile) | High | Full signature in every settings ticket — §2 / all M* |
| **Sens double-dip** (`sens_mult` × FOV ratio = 0.25× on an AR) | High | `sens_mult=1.0` default when FOV-couple on; FOV ratio is sole slowdown — ADS-1.2/2.2 |
| **Gamepad sens double-application** (`gp_scale` folds into `mdx`, re-scaled by `sens`) | Med | Apply ADS multiplier once, at `sens` — ADS-1.1 |
| **Scope-grade `Zoom` as iron ADS FOV** (AR33 = 3×) | Med | Mild-iron clamp `≥ base/1.5`; `is_scope` weapons yield; `AdsFaithfulZoom` opt-out — ADS-2.2 |
| **RNG draw-count must stay 4/shot** | High | Scalar multiplies only; no per-velocity bloom — ADS-4.1/5.1; assertion — ADS-0.3 |
| **Movement gated behind look-delta replay gate desyncs replays** | High | Ride only `AdsEnabled`/`AdsMovePenalty`, not `pcNativeLiveLookAllowed()` — ADS-5.1 |
| **Wrong movement seam distorts accel / desyncs walk anim** | High | Inject only at the post-crouch chokepoint ~`12290` — ADS-5.1 |
| **Movement penalty surprises FOV-only users** (table ≠ no-op at slider 1.0) | Med | `Input.AdsMovePenalty` (default 1) disables it independently — ADS-5.1 |
| **Both aim accumulators must center** for sights==impact | Med | Ramp `crosshair_*` and `field_FFC`/azimuth — ADS-3.1 |
| **`field_A84/A88` are per-hand** | Med | `hands[hand].field_A84`; use the seam's hand index — ADS-7.1 |
| **Profile lookup hardcodes a hand** | Med | Hand-selection policy (dominant; fall back to left; firing hand for spread) — §3 |
| **Struct-offset fragility** | Med | No struct fields; side arrays `g_adsWasAiming[4]`/`g_adsPoseT[4]`/`g_adsSprintLockout[4]` |
| **`in_tank_flag` leak** | Med | Inject inside the `in_tank_flag==0` guard — ADS-5.1 |
| **Mouse vs analog-stick** (center-pull near no-op on mouse) | Low | Scope center-pull/pose to the stick path; FOV+sens apply to both — ADS-3.1 |
| **Strafe set too low feels like a stop** | Low | Keep resolved strafe ≥ 0.30; expose `AdsStrafeScale` — ADS-5.1 |

---

## 8. Open decisions (decide before/while coding)

1. **`Zoom==0` ADS FOV factor** — `0.85` (mild, chosen) vs tighter `0.62`?
2. **PC sensitivity model** — FOV-couple default ON (matches engine) vs flat ratio? *(Rec: ON, with
   `sens_mult=1.0` so no double-dip.)*
3. **Crosshair center-pull default** — ON (sights==impact, changes stick free-aim feel) vs OFF? *(Rec:
   ON, ramped; near no-op on mouse.)*
4. **FOV arming** — rising-edge raw `trigger_watch_zoom(fov, ads_in_time)` is now the **default** (the
   guarded wrapper can't honor a fixed in-time). Keep the guarded wrapper only as a "soft/auto-timed"
   option? *(Rec: rising-edge raw default; expose guarded as an alternate "auto" feel.)*
5. **Easing** — linear (MVP) vs ease-out smoothstep on the blend fraction?
6. **`CAN_ADS` gating** — free `WEAPONSTATBITFLAG` bit (23–31) vs derive from the profile table? *(Rec:
   profile table.)*
7. **Movement defaults** — conservative (chosen, lighter than CoD) vs CoD-heavy 0.4–0.6?
8. **Recoil reduction (ADS-7.1)** in MVP or Phase 2? *(Rec: Phase 2, off by default.)*
9. **Sprint-out lockout (ADS-5.3)** — ship as optional polish or drop? *(Rec: optional, off by default;
   drop if it needs more than the `g_adsSprintLockout[4]` side array.)*
10. **Faithful vs modern zoom** — `Input.AdsFaithfulZoom` lets KF7/AR33 use their true `Zoom` (30/20) for
    GE-authentic aim instead of the mild-iron clamp. Default OFF (modern). Keep, or always-mild?

---

## 9. Research sources (modern-ADS reference)

Timing/attachments: zborgaming `weapon-ads-speeds`, hone.gg `ads-in-call-of-duty`, gameguidehq, mp1st,
CoD wiki `M16A4`. FOV/zoom: west-games Apex FOV calculator, Halo Waypoint settings guide, opticsplanet
scope FOV. Transition/easing: karllewisdesign Unreal immersive-FPS, febucci easing-functions, EA forums
BF2042 FOV-vs-ADS-animation bug. Sensitivity/MDC: kovaak sens-scaling, yawsens coefficient,
mouse-sensitivity.com MDC threads, r6senscalculator ADS-math, edpi-calculator Apex ADS. Movement/strafe:
dexerto Apex strafe-ADS-speed, Activision MW movement basics, CoD wiki Tactical Sprint, charlieintel
MWII movement-penalty debate, CS counter-strafe (for contrast). Accuracy/recoil/sway/flinch: denkirson
viewkick/flinch threads, dotesports Apex Peacekeeper ADS-spread + Cold War flinch, CoD/BF/Destiny wikis
(Recoil/Focus/Spread/Stability). UX/classics: Activision "Gaining Complete Control", GoldenEye wiki
Aim-Mode/Control-style + XBLA, Perfect Dark XBLA control options, sweatygaming toggle-vs-hold, MS Xbox
accessibility guideline 117. (Full URL list retained in the research transcript.)

---

## 10. Rise-to-sights (ADS-6.1) — implemented + remaining scope

### 10.1 What shipped this pass

The weapon now **visually rises to a centered, sighted pose while aiming** (behind
`Input.AdsEnabled` / `Input.AdsModelPose`, off by default). Verified by headless
capture on the Dam (`mission 1`): `AdsEnabled=0` keeps the gun at vanilla hipfire
(bottom-right); `AdsEnabled=1` raises it up/left to center, muzzle under the
crosshair, easing in over `ads_in_time`.

**Critical fix — the pose was in a dormant function.** ADS-6.1 was originally added
to `portBuildFirstPersonWeaponRoot` (`gun.c`), which is **not** the live
first-person draw path (confirmed: its diagnostics never fire; `[VM-ANCHOR]` in
`handles_firing_or_throwing_weapon_in_hand` does). The pose is now applied in the
**active** positioner:

- `portAdsResolvePose(hand, dx,dy,dz, dyaw,dpitch,droll)` (`gun.c`) resolves the
  per-weapon pose (GE007_ADS_POSE_* env override else `AdsProfile`), scaled by the
  ADS blend (`portAdsPoseBlendForHand`, the `zoomintime/zoomintimemax` alpha).
- **Translation** is applied to `gun_pos` in `handles_firing_or_throwing_weapon_in_hand`
  **before** the look-at convergence (weapon moves to a low/centered pose and keeps
  aiming at the crosshair). `gun_pos` space: **+x right, +y up, +z toward the eye**
  (a hipfire KF7 anchors at ~`(11.7, -20.8, -33.5)`).
- **Rotation** (`pose_pitch_rad` etc.) is applied to the converged matrix `mtx_d`
  **after** the look-at copy, before `set_position` (same idiom as the port-root
  `saved_pos` path). A negative pitch **squares the barrel** — flattening the upward
  tilt the convergence would otherwise impart to a low-held weapon — so it reads
  "down the sights" rather than angled up.
- The dormant `portBuildFirstPersonWeaponRoot` ADS code was removed (single source
  of truth). The blend ramp uses the §ADS-2.1 `ADS_ZOOMTIME_UNITS_PER_SEC` timing
  fix, so the raise eases over `ads_in_time` rather than snapping.

**Pose + flat aim (`ads_profiles.c` `ADS_DEFAULT_POSE_*` + `gun.c` look-at flatten).**
A single **UNIVERSAL** pose positions every non-scope weapon: `gun_pos` translation
`(-5, 9, 0)`. Authored rows keep their per-weapon FOV/sens/spread/movement but share
this pose via the `ADS_POSE_FIELDS` macro; untuned weapons get the same from the
computed default. Sniper/spy-camera keep pose 0 (analog scope).

**Barrel orientation is handled by flattening the look-at convergence, not a pose
rotation.** The convergence aims the low-positioned gun *up* at the near aim point —
which reads as an upward tilt that **varies with view pitch** (it looked OK at one
captured frame but tilted up in live mouse-look). A per-gun `pose_pitch_rad` was
tried and abandoned: the right pitch is driven by barrel length and the
post-convergence rotation is **non-monotonic per model** (a pitch that squares a
rifle over-rotates a long pistol to near-vertical). Instead, during ADS the look-at
vector's vertical (and most of its horizontal) component is scaled toward zero by the
blend (`gun.c` convergence block; `GE007_ADS_FLATTEN`/`_X` tunable), so the barrel
points **straight forward / level** independent of weapon model AND view pitch —
verified flat on the KF7 and silenced PP7 including with the view pitched up.
`pose_pitch_rad` is now 0 (rotation path kept, inert, for optional fine-tuning).

**Modern ADS reticle (`gun.c drawModernAdsReticle`, `Input.AdsModernReticle`).** While
aiming with ADS on, the chunky textured crosshair is replaced by a clean center
dot + four short gapped ticks at the player view center (CoD-style), drawn with
`gDPFillRectangle` only (the settex-safe path) and sized off the view height (split-
screen safe). Gated by `g_pcAdsEnabled && Input.AdsModernReticle && insightaimmode`;
off-path is the unchanged classic crosshair. Verified: `AdsEnabled=0` == vanilla
hipfire + classic crosshair; `=1` == flat low weapon + modern reticle.

### 10.2 Authoring workflow (for the remaining weapons)

`tools/ads_pose_capture.sh LABEL ITEM [X Y Z YAW PITCH ROLL]` boots a mission,
force-equips the item, holds aim, applies a `GE007_ADS_POSE_*` override, captures a
screenshot and converts it to PNG. Two debug hooks make headless authoring work:

- **`GE007_ADS_FORCE_POSE=1`** — forces a full pose blend on the right hand
  regardless of aim (env-gated, never set in normal play), so the pose can be dialed
  without the scripted-aim path engaging `insightaimmode`.
- **`GE007_ADS_POSE_X/Y/Z` / `GE007_ADS_POSE_{YAW,PITCH,ROLL}_DEG`** — live override
  for the in-hand weapon; dial, capture, then bake into `ads_profiles.c`.

Workflow per weapon: capture base → nudge X (left, negative) and Y (up) until the
sight line / muzzle sits on the centered crosshair, add Z to pull toward the eye →
bake the values into the authored row.

### 10.3 Remaining next steps (to "excellent")

| # | Item | Notes |
|---|---|---|
| 1 | **Author the remaining weapons** | ~10–15 guns still on the moderate default (DD44/TT33, Klobb, ZMG, D5K, Phantom, Spectre, shotguns, Golden Gun, Magnum, throwing knife, grenades). Use §10.2. Heavy/launcher items (rocket/grenade/taser) should likely get pose 0 or a bespoke pose — verify they don't look odd under the default raise. |
| 2 | **Per-weapon pose rotation** | `portAdsResolvePose` already returns `dyaw/dpitch/droll` (currently unused at the call site). Wire them into `mtx_d` if a weapon needs re-orienting (not just translating) to sit straight down the sights. |
| 3 | **Tune the default by class** | A single fixed `gun_pos` offset over/undershoots weapons with very different base `PosY/PosZ`. Consider per-class defaults (pistol/SMG/rifle) keyed off `WeaponStats`, or scale the offset by the base anchor. |
| 4 | **Harness gaps** | The scripted `GE007_AUTO_EQUIP_ITEM` did not always swap the weapon (the Dam default `ITEM_WPPKSIL` persisted in some runs); and `GE007_AUTO_AIM` engages `insightaimmode` but `GE007_ADS_FORCE_POSE` was needed for reliable headless capture. Fix the scripted-equip/aim path so the harness can become a CI regression (capture-diff) gate. |
| 5 | **Easing** | Optional ease-out on the blend fraction (currently linear; short in-times mask it). |
| 6 | **Recoil (ADS-7.1) on the active path** | Verify the cosmetic recoil cut at the switch-node consumption sites is in the active render path (the pose fix showed `portBuildFirstPersonWeaponRoot` is dormant — re-confirm the recoil sites aren't). Default-off, so low priority. |
| 7 | **Runtime gates (ADS-0.3)** | RAMROM replay byte-identity with `AdsEnabled=0`; split-screen asymmetric ADS; the pose change is cosmetic + flag-gated so determinism should hold — still run the gates. |
| 8 | **In-game options menu** | Surface the `Input.Ads*` flags in the front-end so it's discoverable, not just `ge007.ini`/CLI. |

### 10.4 Verification done

Clean `ge007` build; 7/7 ctest CI guards pass; headless captures confirm
`AdsEnabled=0` == vanilla hipfire and `AdsEnabled=1` == raised sighted pose for the
PP7 and KF7; the FOV/pose blend ramps over multiple frames (timing fix). Per-weapon
visual fine-tuning of the long tail (item 1) and the runtime determinism gates
(item 7) remain.
