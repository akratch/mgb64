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
| `rng_seed` | `g_randomSeed & 0xFFFFFFFF` (low 32) | `readU32(randomSeedAddress)` | u32 hex | BE | **low 32 bits** — the PC PRNG state is 64-bit; the N64 seed is 32-bit. Emitting the low word makes the two comparable. `"0x%08X"`. |

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
| combat.rng_seed | 138 | documented-expected (§5; ares reads ~0) |
| floor.stan_id / stan_flags | 138 / 60 | FID-0053 (encoding non-comparable; room+height agree) |

`projectiles`, `shots_fired_total`, `hits_landed_total` = 0/0 both sides
(dam_forward_stop is a no-combat route) ⇒ FID-0047/0048 unexercised here (triaged with
disposition; hits_landed_total is a native-only counter with no retail accumulator).
This verify auto-unblocks FID-0011/0012/0013 (`blocked_on: FID-0032`).
