# Combat / Floor-Field Oracle — Field-Offset Dossier (FID-0032)

> **Review artifact** for the combat/floor oracle extension (S-Tier Plan Task 1.1,
> `docs/design/COMBAT_DEFERRED_PLAN.md` §2.1). This is the authority document the
> ares-patch and native-emitter changes are derived from and reviewed against.
>
> Two emitters produce **identical JSON keys** (schema parity is the contract):
> - **ares** (`tools/prepare_ares_movement_oracle_build.sh`) reads N64 RDRAM at the
>   US VRAM addresses / struct offsets below.
> - **native** (`src/platform/port_trace.c`) reads the same live structs by C field
>   access; the compiler resolves the (wider, pointer-inflated `NATIVE_PORT`) offsets,
>   so the native side never hard-codes an offset — but the *values* must match.
>
> The comparator is `tools/compare_combat_trace.py`.

## 0. Schema container decision (collision avoidance)

The plan lists four new per-frame field groups: `guards`, `floor`, `combat`,
`projectiles`. The existing frame record **already uses two of those top-level keys
for different data**:

- `"floor": <float>` — a scalar (player floor-Y, `g_CurrentPlayer->field_70`), read by
  `compare_movement_trace.py`.
- `"combat": { aim_mode, health{…}, shots{…}, scan{…}, target_x, target_y, tank… }` — the
  existing rich combat block.

Emitting a *second* top-level `"floor"` object or a parallel `"combat"` object would
create a **duplicate key within one JSON object**. Python's `json.loads` keeps the last
occurrence, which would silently destroy the scalar `"floor"` the movement comparator
depends on. That is unacceptable.

**Resolution:** all four new field groups are emitted under a single new top-level
namespace **`"combat_oracle"`**, preserving every plan sub-key name verbatim:

```json
"combat_oracle": {
  "guards": [ {"chrnum","pos":[x,y,z],"actiontype","aimode","health",
               "shotbondsum","flags_onscreen","target_visible","anim_hash","room"} ],
  "floor":  {"stan_id","stan_room","stan_flags","height"},
  "combat": {"player_health","player_armor","shots_fired_total","hits_landed_total","rng_seed"},
  "projectiles": [ {"kind","pos":[x,y,z],"owner_chrnum"} ]
}
```

Both emitters produce `combat_oracle` identically. `compare_combat_trace.py` reads only
this namespace, so it cannot collide with, or be confused by, the legacy `floor`/`combat`
keys. The plan's field *names* are all retained as documented.

## 1. Guards array — `combat_oracle.guards[]`

Source: iterate the `ChrRecord` slot table. On native: `g_ChrSlots[0..g_NumChrSlots)`.
On ares: base `chrSlotsAddress` holds a **pointer** to the slot array; each slot is
`ChrStride = 0x01DC` bytes (N64 layout). `numChrSlotsAddress` holds the count.

- ares slot base pointer: `MGB64_ARES_CHR_SLOTS` (US default `0x8002cc64`), deref once.
- ares slot count: `MGB64_ARES_NUM_CHR_SLOTS` (US default `0x8002cc68`).

**Active-chr filter** (both sides, identical): `model != NULL` (native `chr->model`,
ares `readU32(chr+ChrModel)`), `prop != NULL`, `prop->type == PROP_TYPE_CHR (3)`, and
`0 <= chrnum < 1000`. Matches the existing `traceBuildActorSummaryJson` filter
(`port_trace.c:4218`) and `traceFindNearestCombatGuard` (`:3945`).

| field | struct.field | US ChrRecord offset | size / type | endian | notes |
|---|---|---|---|---|---|
| `chrnum` | `ChrRecord.chrnum` | `0x0000` | s16 | BE | `ChrChrnum` |
| `pos[3]` | `ChrRecord.prop->pos` (`coord3d`) | prop `0x08` | 3×f32 | BE | chr has no own pos; use `chr->prop->pos` (`PropRecord.pos` @ `0x08`). `ChrProp` @ `0x18`. |
| `actiontype` | `ChrRecord.actiontype` | `0x0007` | u8 (`ACT_TYPE:8`) | — | `ChrActiontype` |
| `aimode` | `ChrRecord.alertness` | `0x010D` | u8 | — | **Interpretation:** the plan's `aimode` = the guard's AI alert *mode* (0..N). `alertness` (canonical `ChrRecord.alertness`) is the discrete AI-state selector used by AI commands 0x86–0x8A; it is the single best scalar for "what AI mode is this guard in". New ares const `ChrAlertness = 0x010d`. Existing native trace already surfaces it as scan `alert`. |
| `health` | `ChrRecord.maxdamage - ChrRecord.damage` | `0x0100`, `0x00FC` | f32 | BE | remaining health. `ChrDamage=0x00fc`, `ChrMaxDamage=0x0100` (both present ares-side). Emitted `%.4f`. See §5 on "raw fixed-point". |
| `shotbondsum` | `ChrRecord.shotbondsum` | `0x013C` | f32 | BE | `ChrShotbondsum` (already present ares-side). Accumulated shot-at-Bond metric. Emitted `%.4f`. |
| `flags_onscreen` | `ChrRecord.prop->flags & PROPFLAG_ONSCREEN` | prop `0x01`, mask `0x02` | u8 flag | — | `PROPFLAG_ONSCREEN=0x02` (`bondconstants.h:356`). New ares const `PropFlags=0x0001`. |
| `target_visible` | derived from `ChrRecord.lastseetarget60` | `0x00D4` | s32 | BE | **Interpretation:** `1` iff the guard perceives the target *recently* — `lastseetarget60 > 0 && (globalTimer - lastseetarget60) < CHRLV_10_SEC_TIMER(600)`. Pure read (no stan-scratch line test — keeps the emitter sim-hash-neutral). Matches existing `seen_recent`. New ares const `ChrLastSeeTarget60 = 0x00d4`. |
| `anim_hash` | header hash of `ChrRecord.model->anim` | model `0x20` → anim header | u64 hex | BE | FNV-style hash over the animation header words. Native reuses `traceModelAnimationHeaderHash`. ares reuses the movement-oracle anim-hash helper (`ModelAnim=0x20`). `"0x%016llX"`. `0x0` when no anim. |
| `room` | `ChrRecord.prop->stan->room` | stan `0x03` | u8 | — | `-1` when no stan. `StandTile.room` @ `0x03`. |

Ordering: guards are emitted in **slot order** (ascending slot index) on both sides so the
comparator can align by `chrnum` within a frame without ambiguity. Bounded to ≤ 64 emitted
guards; overflow is reported (`guards_overflow`) per charter rule 9.

## 2. Floor — `combat_oracle.floor` (player's current tile)

Player's collision block holds the current stan tile pointer.

- native: `g_CurrentPlayer->field_488.current_tile_ptr` (a `StandTile*`); height from
  `g_CurrentPlayer->stanHeight` (already traced as `stan_h`).
- ares: player collision base = `PlayerField488` region; current tile ptr at
  `CollisionCurrentTile = 0x0000` of the collision struct (already defined ares-side).
  Player struct base via `currentPlayerAddress`. Height via `PlayerStanHeight` (deferred —
  see §6 ares note).

| field | struct.field | offset | size | endian | notes |
|---|---|---|---|---|---|
| `stan_id` | `StandTile.id` (24-bit) | tile `0x0000`, mask `0x00FFFFFF` | u32:24 | BE | `-1` when no tile. Unique per-tile id (GroupID/RoomID packed). |
| `stan_room` | `StandTile.room` | tile `0x0003` | u8 | — | `-1` when no tile. |
| `stan_flags` | `StandTile.mid.half` (special/colour nibbles) | tile `0x0004` | s16 | BE | the header-mid halfword (special=0 normal / 1 kneel / 3 ladder + rgb nibbles). `-1` when no tile. |
| `height` | player floor Y | — | f32 | BE | `g_CurrentPlayer->stanHeight`; ares `PlayerStanHeight`. Emitted `%.2f` (float tolerance). |

## 3. Combat block extension — `combat_oracle.combat`

Scalar player-combat summary. All already available on the native side; ares needs the
player health/armor offsets and the (already-present) random-seed address.

| field | source (native) | source (ares) | size | endian | notes |
|---|---|---|---|---|---|
| `player_health` | `g_CurrentPlayer->bondhealth` | player `+PlayerBondHealth` | f32 | BE | `%.4f`. |
| `player_armor` | `g_CurrentPlayer->bondarmour` | player `+PlayerBondArmour` | f32 | BE | `%.4f`. |
| `shots_fired_total` | sum of per-weapon shot regs (existing `shots.total`) | derived / `0` if unavailable ares-side | s32 | — | integer-exact. |
| `hits_landed_total` | accumulated guard-hit counter (`s_traceGuardHitEventCount` running total) | `0` ares-side placeholder | s32 | — | integer-exact; only-native counter ⇒ expected divergence / finding (§5). |
| `rng_seed` | `g_randomSeed & 0xFFFFFFFF` (low 32) | `readU32(randomSeedAddress + 4)` | u32 hex | BE | **low 32 bits** — `g_randomSeed` is a 64-bit doubleword on BOTH sides (the state collapses to 33 bits after one step); on big-endian N64 the low word is at base+4. ⚠ FID-0063: the first ares cut read `readU32(base)` = the HIGH word (0/1 after the first step), so this field diverged every frame BY CONSTRUCTION until the patch fix (needs an ares oracle rebuild to take effect). The movement-record `rng` block (`readU64`) was always correct. `"0x%08X"`. |

## 4. Projectiles array — `combat_oracle.projectiles[]`

Live thrown/airborne ordnance: mines, grenades, rockets. Source: traverse the prop
position list. On native: `for (prop = get_ptr_obj_pos_list_current_entry(); prop; prop = prop->prev)`
(canonical traversal, `chrobjhandler.c:44663`). On ares this list is not currently mapped
(no `ptr_obj_pos_list` symbol in the layout table); **projectiles is native-emitted and
ares-emitted-empty in the first cut** — see §5 / the ares-side note. Kept bounded (≤ 32).

Selection: `prop->type == PROP_TYPE_WEAPON(4)` with `prop->obj->projectile != NULL`
(`ObjectRecord.projectile` @ `0x6C`), or `prop->type == PROP_TYPE_EXPLOSION(7)`.

| field | struct.field | offset | size | endian | notes |
|---|---|---|---|---|---|
| `kind` | `Projectile.droptype` (weapon) / `-1` (explosion) | proj `0xB8` | s32 | BE | `DROPTYPE` (1 default,2 surrender,3 grenade,4 hat). `-1` for explosion props. |
| `pos[3]` | `PropRecord.pos` | prop `0x08` | 3×f32 | BE | `%.2f`. |
| `owner_chrnum` | `Projectile.ownerprop->chr->chrnum` | proj `0x88` → prop `0x04` → chr `0x00` | s16 | BE | `-1` when owner is not a chr / unresolved. |

## 5. Tolerance table (for `compare_combat_trace.py`)

Integer-exact fields (mismatch = candidate finding, filed via `ledger.py new`):
`chrnum`, `actiontype`, `aimode`, `flags_onscreen`, `target_visible`,
`room`, `stan_id`, `stan_room`, `stan_flags`, `shots_fired_total`, `hits_landed_total`,
`rng_seed`, projectile `kind`, `owner_chrnum`, `anim_hash`.

Float fields, movement-comparator epsilons: guard `pos` and projectile `pos` — `position`
tolerance; `height` — `position` tolerance; `health`, `shotbondsum`, `player_health`,
`player_armor` — a dedicated `health` tolerance (default `1.0` raw-health unit).

**"health as raw fixed-point" note.** The retail game stores `damage`/`maxdamage`/`bondhealth`
as `f32`. Bit-exact cross-platform float equality (N64 vs x86 SSE) is not achievable, so
`health` is emitted as a float and compared with a `health` epsilon rather than exact. The
dossier records this deliberate softening of the plan's "exact fixed-point" wording; a
health divergence beyond epsilon is still a real finding.

**Known-expected divergences (the *product* of Task 1.1 step 3/4 — file as FIDs, do not
fix here):**
- `rng_seed`: the port advances its 64-bit PRNG on a schedule that differs from N64 in
  call count; low-32 parity at checkpoint frames is the thing to measure — expect drift.
- `hits_landed_total`: no retail-equivalent accumulator is read ares-side yet ⇒ divergence
  by construction (instrumentation-gap sub-finding).
- `projectiles`: ares list unmapped in first cut ⇒ native-populated, ares-empty ⇒ divergence
  by construction (instrumentation-gap sub-finding; close when the prop-list symbol is added).

## 6. ares implementation — reused base addresses + struct constants

The ares tracer parametrizes **base pointers** through the `MGB64_ARES_*` symbol-layout
env table, but resolves **struct field offsets at compile time** (e.g. `ChrActiontype`,
`ChrAlertness`, `PropPos` are `enum` constants, not envs). The combat oracle follows that
existing design: no new base-address envs are needed because every base it reads is already
in the table.

Reused base addresses (already in the layout table, §1202-1246):
`MGB64_ARES_CHR_SLOTS` (slot array pointer), `MGB64_ARES_NUM_CHR_SLOTS`,
`MGB64_ARES_RANDOM_SEED` (rng_seed), `MGB64_ARES_CURRENT_PLAYER` (floor/combat base),
`MGB64_ARES_GLOBAL_TIMER` (target_visible age).

Reused struct constants (already defined): `ChrStride`, `ChrModel`, `ChrProp`, `ChrChrnum`,
`ChrActiontype`, `ChrAlertness`, `ChrDamage`, `ChrMaxDamage`, `ChrShotbondsum`, `ModelAnim`,
`PropPos`, `PropFlags`, `PropStan`, `PropType`, `PlayerField488`, `CollisionCurrentTile`,
`PlayerBondHealth (0xdc)`, `PlayerBondArmour (0xe0)`, `PlayerStanHeight (0x74)`, plus
`stanRoom()`.

New struct constants added by this patch:
| const | value | meaning |
|---|---|---|
| `ChrLastseetarget60` | `0x00d4` | ChrRecord.lastseetarget60 (target_visible) |
| `StandTileId` | `0x0000` | StandTile.id (u32:24) — masked `0x00FFFFFF` |
| `StandTileRoom` | `0x0003` | StandTile.room |
| `StandTileMid` | `0x0004` | StandTile.mid halfword (stan_flags) |

New ares helpers: `stanId(u32)`, `stanFlags(u32)`, `animHeaderHash(u32)` (bit-for-bit
identical to native `traceModelAnimationHeaderHash`), and `formatCombatOracleJson(...)`.
The player health/armor/stanHeight offsets were **already present** in the US layout, so
the ares `combat`/`floor` blocks are fully populated (no `0` placeholders); only
`shots_fired_total`, `hits_landed_total` (no retail accumulator read) and `projectiles`
(prop list unmapped) are ares-side placeholders ⇒ documented divergences (§5).

## 7. Sim-hash neutrality

Every field above is a **read** of existing sim state (or a pure arithmetic derivation of
reads). The emitter allocates no sim state, calls no stan line-test (target_visible uses a
plain `lastseetarget60` read, not `stanTestLineUnobstructed`), and mutates nothing. It runs
only on the trace path. Therefore the new fields are trace-only and must not perturb the
sim-state hash; verified by `tools/sim_invariance_gate.sh` at defaults.

## 8. Verification — both-sides live Dam capture (FID-0032 → verified, 2026-07-10)

Durable evidence anchor for the `landed → verified` transition. Per-run ROM traces
and the raw diff JSON are ROM-derived (`/tmp`, charter rule 7); this section records
the recipe + divergence counts only.

**Recipe** (determinism envelope, charter rule 6):

```
tools/movement_oracle_capture.sh --route dam_forward_stop \
  --native-full-trace --no-compare \
  --binary build/ge007 --ares-bin build/ares-movement-oracle/.../ares \
  --rom baserom.u.z64 --out-dir /tmp/cap --timeout 400
tools/compare_combat_trace.py \
  --baseline /tmp/cap/stock_dam_forward_stop.jsonl \
  --test    /tmp/cap/native_dam_forward_stop.jsonl \
  --align move --json-out /tmp/cap/combat_diff.json
```

Two infra additions the verify surfaced as missing from the first cut (both landed):
`--native-full-trace` (the slim flow-only movement trace omits `combat_oracle`) and
`--align move` (native direct-boot `g_GlobalTimer` 0..652 = gameplay cannot align by
absolute timer against ares menu-boot where gameplay starts ~global 1146; motion-onset
anchoring pairs them).

**Result:** alignment SUCCEEDED — **138 aligned frames**. Anchor validated (not just
"it ran"): `floor.height` 138/138 exact (-107.0), `floor.stan_room` 138/138, guard
ROSTER 36/36 common every frame, `player_health` agrees. Sim-hash neutral: whole-sim
`--sim-state-hash-out` byte-identical (`27ce75d02a10abcc`) with combat_oracle OFF vs
ON — the trace fields did not enter the sim hash. `combat_oracle_contract` ctest PASS.

**Divergences (the product — filed as findings):**

| family | field-hits | finding |
|---|---|---|
| guards.anim_hash | 4968 | FID-0055 (anim-phase cadence) |
| guards.flags_onscreen | 1568 | FID-0055 (render visibility) |
| guards.pos[0/1/2] | 1379/1295/1378 | FID-0054 (~10/36 guards, stable — not drift) |
| guards.actiontype / room | 293 / 217 | FID-0054 (AI-state) |
| guards.health / target_visible | 115 / 104 | FID-0054 |
| combat.rng_seed | 138 | documented-expected (§5; ares reads ~0). ⚠ Artifact caveat (§3, FID-0063): this capture predates the ares low-word fix — `combat.rng_seed` was the HIGH word of `g_randomSeed` (0/1 after one step), so its 138/138 per-frame hit count was true BY CONSTRUCTION and carries no call-count information. The underlying phase desync is real but is established by the seed-locked differential (FID-0063 derivation §6), not by this row. |
| floor.stan_id / stan_flags | 138 / 60 | FID-0053 (encoding non-comparable; room+height agree) |

`projectiles`, `shots_fired_total`, `hits_landed_total` = 0/0 both sides
(dam_forward_stop is a no-combat route) ⇒ FID-0047/0048 unexercised here (triaged with
disposition; hits_landed_total is a native-only counter with no retail accumulator).
This verify auto-unblocks FID-0011/0012/0013 (`blocked_on: FID-0032`).

## 9. Combat route — FID-0054 frame-lock (root-caused, 2026-07-10)

Durable evidence anchor for the `discovered → root-caused` transition of FID-0054.
Per-run ROM traces + the raw diff JSON are ROM-derived (`/tmp`, charter rule 7); this
section records the recipe, the alignment result, and the frame-locked divergence only.

**Route:** `tools/rom_oracle_routes/dam_combat_guard6.json` — Dam spawn, walk forward
toward guard 6 (nearest of the 36-roster; becomes onscreen/target_visible) while firing
the PP7 (`GE007_AUTO_FORWARD=80:120` + `GE007_AUTO_FIRE=100:160`). Unlike dam_forward_stop
this is a **combat** route: shots + projectiles are non-zero, so the guard-AI divergence
is frame-locked to a live weapon discharge.

**Recipe** (determinism envelope, charter rule 6):

```
tools/movement_oracle_capture.sh --route dam_combat_guard6 \
  --native-full-trace --no-compare \
  --binary <release-ge007> --ares-bin <ares-movement-oracle>/.../ares \
  --rom baserom.u.z64 --out-dir /tmp/cap-combat --timeout 400
tools/compare_combat_trace.py \
  --baseline /tmp/cap-combat/stock_dam_combat_guard6.jsonl \
  --test    /tmp/cap-combat/native_dam_combat_guard6.jsonl \
  --align move --json-out /tmp/cap-combat/combat_diff.json
```

**Result:** alignment SUCCEEDED — **218 aligned frames** (stock motion-onset idx 2531,
native idx 81). Native side exercises the combat counters: **shots_fired_total 7,
projectiles up to 2** (first shot at aligned frame 20). Stock/ares side reports
shots 0 / projectiles 0 (ares placeholders, §5).

**FID-0047 / FID-0048 now exercised** (were 0/0 on dam_forward_stop): the combat route
surfaces them as divergences-by-construction — `combat.shots_fired_total` 198 field-hits
(native counts, ares emits 0) and `projectiles.count` 40 field-hits (native populates,
ares empty). This confirms both triaged dispositions (instrumentation gaps, not sim
defects); neither is closable from this repo alone (no in-tree US shot-register / prop-
list addresses). Left triaged.

**FID-0054 frame-lock — two components:**

1. **Combat-triggered (the clean anchor).** Guard **chrnum 6** tracks *identically* on
   both sides (actiontype 1/1, target_visible 0/0, room 135/135) for aligned frames
   0..35, then at **aligned frame 36** — ~16 frames after Bond's first shot (frame 20) —
   diverges: **`target_visible` stock=0 → native=1** co-incident with **`actiontype`
   stock=1 → native=8**. The native port has guard 6 *acquire Bond as a visible target*
   and dispatch into the combat/alert action (actiontype 8), while the N64 stock guard 6
   stays unaware (target_visible 0, actiontype 1) over the whole window. Guard-6 health
   stays 4.0 both sides (Bond's fire missed — the divergence is *perception/dispatch*,
   not damage).
   - First field to diverge = **`target_visible`** (tied with actiontype at the same
     frame). `target_visible` is a pure read of `ChrRecord.lastseetarget60` (§1): the
     native guard's see-target update sets `lastseetarget60` (→ recent-perception true)
     ~16 frames into the firefight; stock never does in-window.
   - **Hypothesis + cited path:** the divergence source is the native guard **see-target
     / alertness update cadence and its actiontype-8 dispatch** in the guard AI tick —
     `src/game/chr.c` guard tick (the path that stamps `lastseetarget60` when the guard
     perceives the target) and the actiontype dispatch it feeds, plus the stan
     line-of-sight test that gates perception (`src/game/stan.c` LOS) — versus the N64,
     where the ASM guard never registers Bond as seen in this window. Native is
     *over-/earlier-perceiving* relative to stock: a **port-defect candidate** in the
     see-target / alertness scheduling (FID-0054 suspect list `chr.c chrTickBeams`,
     `chrai.c`), not an N64 quirk. The ACT fix follow-on must reconcile the native
     `lastseetarget60`/alertness update against the ASM see-target cadence.

2. **Pre-existing baseline gap (context).** The *absolute* first divergence is at aligned
   frame **0**, on **background** guards (chrnum 2, 3 …) — before any shot: guard 3
   actiontype 1/3 + health 6.0/4.0, guard 2 room 122/113 + pos (Y 98.4/-0.5,
   Z 7203/8697). This is the stable ~10/36-guard subset already noted at `discovered`
   (background-AI / off-engagement position parity), independent of combat. FID-0054
   therefore has two lanes: (a) this baseline background-AI/position gap (frame 0), and
   (b) the combat-triggered perception/dispatch divergence on the engaged guard 6
   (frame 36) isolated here.

**Determinism finding:** combat introduced **no** nondeterminism under the seed envelope
— the committed input tape `baselines/tapes/dam_combat_guard6.ge7tape` replays to a
byte-exact sim-state hash `3c8939968e0eb50e` across 3 runs (record == replay×3). The
permanent guard is `tools/fidelity/combat_route_capture_smoke.sh` (ctest
`port_combat_route_capture_smoke`); the both-sides ares comparison stays a manual oracle
step (ares not in CI).

## 10. Full-auto fire cadence — FID-0056 (root-caused → landed, 2026-07-10)

Durable evidence anchor for the `discovered → root-caused → landed` transitions
of FID-0056 (full-auto fire rate coupled to the per-rendered-frame counter
`field_88C`, unscaled). Per-run traces are ROM-derived (`/tmp`, charter rule 7);
this section records the mechanism, the quantified cadence ratio, and the fix.

**Mechanism (ASM-confirmed in source).** `src/game/gun.c` advances the fire
counter once per rendered frame — `hand_ptr->field_88C += 1` (was `field_88C++`,
`gun.c:~17936`) — while the adjacent `field_890 += g_ClockTimer` IS tick-scaled.
Full-auto fire is gated on `field_88C % fire_rate` (`gun.c:~18377`) plus a burst
catch-up `((field_88C - 1) / fire_rate) + 1`. On N64 a rendered frame cost
`g_ClockTimer` = 2–4 sim ticks (Dam combat ~20fps ⇒ 3), so the counter advanced
at 15–30Hz; the port runs a locked 60Hz loop with `g_ClockTimer == 1`, so the
counter advances at 60Hz and the gate fires 2–4× too often.

**Quantified cadence (native, Dam, AK47 = ITEM_AK47, determinism envelope).**
Sustained full-auto held for a fixed sim-tick window, measuring
`combat_oracle.combat.shots_fired_total` per 100 ticks of `move.global`:

| run | field_88C advance | shots / 100 ticks | rounds/s |
|---|---|---|---|
| `GE007_DETERMINISTIC_SPEEDFRAMES=1` (mgb64 locked 60Hz) | 60Hz (1/tick) | **33.3** | ~20 |
| `GE007_DETERMINISTIC_SPEEDFRAMES=3` (N64 ~20fps frame cost) | 20Hz (1/3 ticks) | **11.3** | ~6.7 |

**Ratio ≈ 2.95× (≈3×)** overspeed at locked 60Hz. The sf=3 run is a faithful
N64-truth: it makes `field_88C` advance once per 3 ticks, exactly reproducing
the N64 per-frame-at-20fps advance (the same `stock_speedframes: 3` the ares
Dam gameplay oracle uses). Consistent with the 2–4× prediction (2× at the 30fps
nominal design, 3× at 20fps combat, 4× at 15fps heavy) and with GoldenRecomp's
README known-issue (unfixed in their tree).

**Fix (`Input.FireRateAuthentic`, opt-in, default OFF — owner policy, core
combat feel).** Behind the flag: (a) the counter advances by `g_ClockTimer`
(mirrors `field_890`, tick-scaled, no tick remainder dropped —
`fireRateCounterAdvance`); (b) the automatic divisor is multiplied by
`Input.FireRateN64FrameCost` (default 3, the measured Dam value; range 2–4) so
the gate fires once per `fire_rate * frame_cost` ticks
(`fireRateEffectiveAutoRate`). At locked 60Hz `g_ClockTimer == 1`, so the
counter advance is byte-identical to `++`; only the divisor scaling changes
cadence.

**A/B (native, locked 60Hz).** Flag OFF: 33.3 shots/100 ticks (today's rate).
Flag ON: **11.0 shots/100 ticks**, matching the N64-truth 11.3 within
reload-edge phase noise (3.03× correction). Determinism holds in both states
(sim-state hash repeats byte-exact across runs, both flag values).

**Byte-identity under default (flag OFF).** The AK47 sustained-fire scenario
sim-state hash is identical between the fixed binary (flag OFF) and the pre-fix
main binary (`d06b1c7a4e504fd1`, same build config); the committed
`dam_combat_guard6.ge7tape` replays to the identical hash on both. In the
canonical Release build the fixed tree (flag OFF) passes
`combat_route_capture_smoke` with the recorded baseline hash `3c8939968e0eb50e`.

**Regression lane.** `tests/test_fire_rate_authentic.c` (ctest
`fire_rate_authentic`) — ROM-free guard on the counter-advance + divisor-scaling
helpers and the simulated sustained-fire cadence. Proven fail-on-revert:
reverting the divisor scaling collapses the ON/OFF cadence ratio to 1.00× and
reddens 8 assertions.

## 11. Combat perception — FID-0054 ACT: root-cause CORRECTED (2026-07-10)

Durable evidence anchor for the FID-0054 ACT iteration. Per-run traces are
ROM-derived (`/tmp`, charter rule 7); this section records the corrected
mechanism and the follow-on proposal. **This section revises the §9 root-cause
hypothesis** — the "native perceives / N64 stays unaware" framing was largely an
alignment artifact, not a guard-AI port-defect.

**Recipe** (determinism envelope, charter rule 6). Native built in
`/tmp/mgb64-perception` (Release); combat route `dam_combat_guard6` both sides:

```
tools/movement_oracle_capture.sh --route dam_combat_guard6 --native-full-trace \
  --no-compare --no-build --binary /tmp/mgb64-perception/ge007 \
  --ares-bin build/ares-movement-oracle/.../ares --rom baserom.u.z64 \
  --out-dir /tmp/cap-combat --timeout 400
tools/compare_combat_trace.py --baseline <stock>.jsonl --test <native>.jsonl --align move
```

### 11.1 What §9 reported vs what actually happens

§9 (record-index `--align move`) reported: guard chrnum 6 identical frames 0..35,
then at "aligned frame 36" `target_visible` 0→1 and `actiontype` 1(STAND)→8(ATTACK)
on native while stock stays STAND/unaware. Re-captured and re-analysed both sides
by **true sim-tick** (each record's `move.global` = `g_GlobalTimer`, relative to the
shared motion onset — Bond starts forward at gameplay frame 80: native onset
global 241, stock onset global 1387, both == gameplay-frame-80 × speedframes 3):

| event (relative sim-tick past onset) | STOCK / ares (N64) | NATIVE |
|---|---|---|
| guard 6 first `target_visible=1` | **+76 ticks** (enters ACT_SIDESTEP) | **+108 ticks** |
| guard 6 first `actiontype=8` (ATTACK) | later, phase-offset | +108 ticks |
| guards that engage over the run | **6, 7 AND 44** all reach ACT8 | 6 (run ended at +~650t before 7/44) |

So **both sides' guard 6 perceive Bond and enter combat**; in true sim-time the
STOCK guard perceives ~32 ticks **earlier** than native (native is *later*, not
earlier). The subsequent AI phases (SIDESTEP → ATTACK → GOPOS(15) → ATTACK) occur
on both sides, offset by tens of ticks. The "N64 stays unaware" reading was false.

### 11.2 The dominant cause: trace-cadence mismatch (instrumentation-gap)

`compare_combat_trace.py --align move` pairs `baseline[start+i]` with
`test[start+i]` by **record index**. The two emitters run at different record
cadences on this route:

- **native**: exactly 1 combat_oracle record per game-frame — `move.global`
  advances by `g_ClockTimer` (=3, speedframes 3) **every** record.
- **ares/stock**: ~2 records per advancing global-tick (mean 2.01, max 5 in the
  combat window) — the emitter samples multiple times per game-frame and captures
  intra-frame AI evolution. Example: at stock global 1463 four consecutive records
  show guard 6 evolving `act 1,tv0 → 1,tv1 → 11,tv1 → 11,tv1` **within one frame**.
  It also interleaves 0-guard / partial-roster records (2789 of 6500 records have
  an empty guard list — menu / not-yet-populated samples).

Consequence: record-index alignment advances ~2× faster in sim-tick space on the
native side. At "aligned frame 36" native sits at +108 sim-ticks (already
attacking) while stock sits at only ~+27..54 sim-ticks (still standing) — a pure
time-base skew, reported as a guard-AI divergence. Quantified: aligning the same
two traces by sim-tick (dedupe to last roster-complete record per `g_GlobalTimer`)
cuts `guards.actiontype` field-hits from **666 → 249 (−63%)** and total combat
divergences from **18657 → ~9327 (−50%)**. A large fraction of FID-0054's reported
`actiontype`/`room`/`target_visible`/`pos` divergence is this artifact.

### 11.3 The residual (genuine) divergence is systemic RNG-phase, not a guard-AI bug

After tick-alignment a real residual remains (~249 `actiontype`, ~81
`target_visible` hits, phase-offset AI). Its mechanism is **not** a bounded
guard-AI counter error:

- **AI decision cadence is faithful.** The guard AI tick
  (`chrlvActionTick`, `chrlv.c:11309`) gates on `self->sleep -= g_ClockTimer` and
  runs `ai()` when sleep expires — tick-scaled. This capture runs `g_ClockTimer==3`
  throughout (speedframes 3, N64 Dam frame cost), so the FID-0056 class
  (per-rendered-frame counter unscaled at locked 60Hz) **does not apply** here.
- **Perception logic is a faithful ASM match.** Perception is
  `chrCheckTargetInSight` (`chrlv.c:5671`, US `0x7F029D70`) → `chrCanSeeBond`
  (`chrlv.c:5358`) → `chrlvSetTargetToPlayer` (`chrlv.c:5604`, stamps
  `lastseetarget60` = `target_visible`). The decisive gate is **probabilistic**:
  after the vision-cone/range/fog checks, `pass = (randomGetNext() % (u32)distance)
  == 0` (`chrlv.c:5749-5755`), where `distance` derives from range, facing angle and
  the guard's speed-rating. Instrumented native run (`GE007_TRACE_SIGHT6`, temporary)
  confirmed guard 6's perception fires via this path once it has turned to face Bond
  (radChangeToFaceBond 5.85→0.047 rad) after being alerted by the shots.
- **The PRNG is bit-faithful** to the retail `GLOBAL_ASM` (`random.c:279-311`;
  `randomGetNext` xor/shift sequence matches `glabel randomGetNext`). So identical
  seed + identical call-count + identical inputs ⇒ identical draw. The perception
  frame therefore shifts iff the **RNG stream desyncs** (call-count parity) or the
  probability inputs (Bond/guard position, facing) drift upstream.
- **RNG call-count parity is a known systemic port gap** (§5/§8: `combat.rng_seed`
  diverges every frame — ⚠ but note the §3/FID-0063 artifact caveat: those
  comparator hit-counts predate the ares low-word fix (the field compared the
  HIGH word, 0/1 after one step, so "every frame" was constructional). The
  systemic desync itself is REAL and is proven seed-locked (FID-0063 derivation
  §6: +2 draws/frame retail excess from an identical locked seed); the
  extensive ramrom deferral machinery in
  `chrlv.c:824-897` exists precisely to re-phase the port's PRNG schedule against
  N64). A guard's probabilistic perception draw succeeding a few ticks earlier/later
  is the expected downstream symptom of that whole-engine schedule difference — plus
  small guard-position differences (guard 6 rests ~3.7u off between sides, within the
  comparator position tolerance) feeding `distance`.

**Classification.** FID-0054 combat-perception lane = (a) an **instrumentation-gap**
(comparator record-index alignment over mismatched-cadence emitters — the actionable,
bounded part) layered on (b) a **deferred systemic parity item** (PRNG call-count
schedule divergence, the true driver of the residual perception-timing). It is **not**
a bounded, ASM-clear guard-AI/LOS correction. Per the ACT charter, no guard-AI change
was landed (a targeted change would chase an artifact + RNG phase and risk the faithful
path). Sim left byte-identical.

### 11.4 Proposal (follow-on, not landed this iteration)

1. **Comparator sim-tick alignment (`compare_combat_trace.py`).** Add an `--align tick`
   mode: motion-onset anchor + resample each side to one record per advancing
   `move.global`. A naive "last record per tick" is insufficient — it can land on the
   ares 0-guard/partial-roster samples and spuriously report the whole roster absent
   (present-divergence artifact observed: 3492 hits). The correct rule needs the ares
   emission pattern characterised first (why are full/empty records interleaved
   mid-gameplay?) and then "last **roster-complete** record per tick", or better, drop
   the ares over-sampling at the emitter so both sides emit once per game-frame. This is
   the bounded fix for the artifact half of FID-0054 and removes ~60% of its reported
   `actiontype` divergence. Land with a ROM-free regression test using synthetic
   over-sampled fixtures (one side 2-3 records/tick, the other 1/tick) asserting
   record-index inflates divergences while tick-align does not.
2. **Residual perception-timing parity** is gated on RNG call-count parity (the systemic
   `rng_seed` gap) and is not closable from the guard-AI code; it should be tracked
   against that item, not fixed in `chr.c`/`chrlv.c`/`chrai.c`/`stan.c`.

**Determinism.** The committed `baselines/tapes/dam_combat_guard6.ge7tape` still
replays to sim-state hash `3c8939968e0eb50e` (ctest `port_combat_route_capture_smoke`,
Release) — this iteration changed no sim code, so the combat sim is byte-identical.

## 12. Sim-tick alignment — FID-0062 (`--align tick`, landed 2026-07-10)

Durable evidence anchor for FID-0062 (`discovered → landed`). This lands §11.4 item 1:
the trustworthy sim-tick alignment that removes the ~2x record-index skew. **Tooling
only — no sim/gameplay code changed** (sim byte-identical; `combat_route_capture_smoke`
Release baseline `3c8939968e0eb50e` unchanged).

### 12.1 The alignment rule

`compare_combat_trace.py --align tick` (`canonical_by_tick`):

1. **Motion-onset anchor** (reused from `--align move`): find the first record on each
   side whose `move.speed/raw` exceeds threshold. `onset_global = records[start].move.global`.
   Native onset = g_GlobalTimer **241**, stock onset **1386** (both == gameplay-frame-80 ×
   speedframes 3, offset by the menu boot). Absolute `move.global` cannot pair the sides;
   the **relative** sim-tick `move.global − onset_global` can (both step by g_ClockTimer=3).
2. **Collapse each side to one canonical record per relative sim-tick.** Native is already
   1 record/tick; ares emits ~2/tick (intra-frame AI substeps) interleaved with
   EMPTY-roster sampling records (2789/6500 on this route).
   - **EMPTY-roster records (`roster_size == 0`) are dropped** — never canonical. A tick
     whose only samples are empty produces no entry and simply does not pair (dropped, not
     a divergence). This is what kills the naive-tick **`present` artifact (§11.4: 3492
     hits)**: an empty ares roster is never compared against native's full roster.
   - Among the roster-bearing records for a tick, keep the **last record with the maximum
     roster size** (the roster-complete, latest intra-frame substep) — matches native's
     end-of-frame once-per-tick snapshot.
3. Pair by the intersection of relative sim-ticks present on both sides.

### 12.2 Before/after (dam_combat_guard6, both-sides, Release native)

| metric | `--align move` (index) | `--align tick` (sim-tick) |
|---|---|---|
| aligned frames | 218 | 116 (native emits 1/tick; move double-counted the ares over-sampling) |
| divergences total | 18588 | **9874 (−47%)** |
| `guards.actiontype` | 596 | **315 (−47%)** |
| `guards.target_visible` | 41 | 91 (tick *reveals* the real perception phase-gap; move masked it) |
| `guards[...].present` | (none here) | **none** — interleave handled (naive-tick was 3492) |

Consistent with the FID-0054 §11.2 manual sim-tick estimate (666→249 actiontype, 18657→
9327 total, ~−50%/−63%); exact counts differ by capture (RNG/AI phase noise) but direction
and magnitude match.

### 12.3 FID-0054 inversion confirmed under clean alignment

Tick-align reproduces the §11 manual re-analysis exactly (guard-6 first `target_visible=1`,
relative sim-ticks past shared onset):

| guard | STOCK / ares (N64) | NATIVE |
|---|---|---|
| 6 | tv=1 **+76t**, act8 +153t | tv=1 **+108t**, act8 +108t |
| 7 | tv=1 +520t, act8 +538t | (run ended before) |
| 44 | tv=1 +269t, act8 +269t | (run ended before) |

So **both sides' guard 6 perceive Bond and enter ACT_ATTACK**; the STOCK guard perceives
~32 ticks **earlier** (native is *later*, not earlier). The §9 "native over-perceives / N64
stays unaware" reading was the index-skew artifact. Residual under clean alignment =
systemic **PRNG call-count phase** (`combat.rng_seed` 116 field-hits, every aligned frame
— ⚠ §3/FID-0063 artifact caveat: this capture predates the ares low-word fix, so the
per-frame `rng_seed` hit-count was constructional; the phase desync conclusion stands
via the seed-locked differential, FID-0063 derivation §6)
+ small guard-position drift (`guards.pos` ~1.2–1.3k), i.e. the deferred RNG-parity item
(§11.3) — **not** a bounded guard-AI bug.

### 12.4 Canonical combat-route recipe (use `--align tick`)

```
tools/movement_oracle_capture.sh --route dam_combat_guard6 --native-full-trace \
  --no-compare --no-build --binary <release-ge007> \
  --ares-bin <ares-movement-oracle>/.../ares --rom baserom.u.z64 \
  --out-dir /tmp/cap-combat --timeout 400
tools/compare_combat_trace.py \
  --baseline /tmp/cap-combat/stock_dam_combat_guard6.jsonl \
  --test    /tmp/cap-combat/native_dam_combat_guard6.jsonl \
  --align tick --json-out /tmp/cap-combat/combat_diff.json
```

`--align tick` is the **trustworthy** mode for combat both-sides comparison. `--align move`
is retained (motion-onset, index-paired) but is only valid where both emitters share a
record cadence; on a live combat route it skews ~2x and **must not** be used for divergence
attribution. The CLI *default* stays `global` for the generic tool (the ROM-free contract
smoke uses single-tick fixtures); combat routes pass `--align tick` explicitly. The route
JSON's `compare_align` field targets the separate *movement* comparator
(`compare_movement_trace.py`, no tick mode) and is unchanged.

### 12.5 Regression lane

`tools/tests/test_compare_combat_trace.py` (ROM-free, auto-discovered by ctest
`intro_tools_unittests` **and** the dedicated ctest `combat_comparator_align_unittest`).
Synthetic streams — native 1 record/tick, ares 2–3 records/tick incl. empty-roster
interleave, guard actiontype a function of sim-tick so both sides agree per tick. Asserts:
tick-align pairs by sim-tick with **0** divergences; `--align move` (index) inflates
divergences AND surfaces phantom `guards.present`; tick < move strictly. **Fail-on-revert
proven**: reverting the tick branch to index pairing reddens 3 assertions with 144
divergences incl. 32 phantom `guards.present`.

### 12.6 Re-validation guidance for the combat findings seeded from `--align move`

Under clean sim-tick alignment:

- **FID-0054** (guard perception): the "native perceives, N64 unaware" divergence was
  **largely an artifact** (index skew); the real residual is the RNG-phase perception shift
  (+76 vs +108t) + position drift = the deferred RNG-parity item, not a guard-AI defect.
  `actiontype` divergence drops ~47%. **Verdict: mostly artifact + systemic RNG-phase.**
- **FID-0011 / FID-0012 / FID-0013** (chrTickBeams / stan LOS / guard-coupling, all seeded
  from `--align move`): their evidence must be **re-captured under `--align tick`** before
  any ACT. Whatever survives will be the same systemic RNG-call-count phase + small position
  drift class, not a bounded per-guard counter bug. They should be tracked against RNG-parity,
  not fixed in `chr.c`/`chrlv.c`/`stan.c` on the strength of the index-skewed numbers.
