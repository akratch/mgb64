# Netplay Plan — Source-Grounded Review & Corrections (2026-06-25)

This is the **correction record** for the online-multiplayer design. It reviews
[NETPLAY_PLAN.md](NETPLAY_PLAN.md), [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md),
and the local base [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) against the actual
MGB64 source and the Perfect Dark `port-net` reference, lists every load-bearing
flaw, and is the authoritative table of **corrected `file:line` anchors**. Where
this document and a plan doc disagree, **this document wins** and the plan doc
carries a pointer back here.

> **Method.** 13-agent adversarial review (run `wf_382b55b6-eb8`): six grounding
> clusters re-verified every cited symbol/line against the tree; one agent
> fetched the PD `port-net` source to confirm the protocol side; five gap lenses
> (scenarios, combat fidelity, split-screen×netplay, infra/loopback/security,
> lifecycle/UX) hunted holes; a completeness critic integrated and ranked. 37
> code-grounded gaps surfaced. No netcode exists yet (confirmed: no
> `src/platform/net/`, no enet, no `--connect/--host` CLI, no `net_smoke.sh`).

---

## 1. Verdict

**Sound-but-incomplete. The architecture is the right call and is buildable, but
the N3 correctness surface, the test harness, the scenario scope, and the
hitscan/RNG model need substantive rewrites — not renames — before build.**

The high-level decision (PD *puppet + event* over deterministic lockstep) is
well-reasoned and correctly grounded. But **three of the plan's stated
assumptions are refuted by the engine itself** (§3), the design is silently
scoped to ~2 of GoldenEye's 8 MP scenarios (§4.3), and "online split-screen" is
structurally blocked by an engine coupling the plan reduces to "a flag" (§4.4).

---

## 2. What is confirmed solid (keep as written)

| Claim | Status | Evidence |
|---|---|---|
| Puppet+event over lockstep | ✅ correct | 793 transcendental calls across 31 sim files; f32-integrated motion → cross-arch bit-identical lockstep is impractical. Same-arch LAN rollback (§10) is the correct scoping for where determinism is reachable. |
| PD is the right reference + protocol ports cleanly | ✅ verified on `port-net` | `UCMD_*`, `netplayermove`, `netclient`, `NETCHAN_DEFAULT/CONTROL`, `g_NetTick++` in `netStartFrame`, `netClientRecordMove`, `prop->syncid = prop - g_Vars.props + 1`, `netClientSyncRng` (two seeds), `CONTROLMODE_NA` puppet marking all exist with the described roles. |
| Licensing | ✅ verified | PD = MIT (© 2022 Ryan Dwyer); enet = MIT (Lee Salzman et al). Compatible with MGB64 MIT. |
| **`PropRecord.syncid` is safe to add** | ✅ confirmed (keystone) | `pos_data_entry` is BSS `{0}`, free-list built at runtime (`initobjects.c:55-58`), populated field-by-field; **zero** `memcpy`/`fread`/`sizeof(PropRecord)` in `src/`; setup data lives in a separate `ObjectRecord` referenced by pointer. **Append at the struct end** (`bondtypes.h:2273`) — the struct carries N64 offset comments; mid-struct insertion shifts them. |
| Frame-pump seam | ✅ exact | `boss.c:568` `joyPoll` / `:602` `lvlManageMpGame` / `:629` `lvlRender`; single-threaded (PC polls explicitly). |
| Synchronous stage load → clean barrier | ✅ verified | `lvlStageLoad` (`lvl.c:555-785`) has no frame yields; **post-load barrier = `boss.c:494`** (after `lvlStageLoad`, after `init_player_data_ptrs_construct_viewports@491`) for `netSyncIdsAllocate`/`netPlayersAllocate`. |
| `g_deterministic` fixed-seed hook exists | ✅ leverage it | `boss.c:393-398` already substitutes seed `0x12345678` — reuse for the RNG-push story. |

---

## 3. Load-bearing corrections (refuted claims — rewrite before build)

### 3.1 Server-authoritative hitscan from raw aim is impossible

> **Plan said** (§3d/§4f): the host reproduces a client's shot "using that
> client's forwarded aim (`vv_theta`/`vv_verta`/crosshair)", and (§3e/§7) "RNG is
> for cosmetic/spawn parity only, not for correctness."

**Refuted.** The live shot path `bullet_path_from_screen_center` (`gun.c:28756`)
builds the bullet vector from:
1. the **auto-aim-deflected `crosshair_angle`** — a 25–30-tick target latch
   (`bondview.c:12178-12202`, `6315-6403`), *not* the raw look angle;
2. **four `RANDOMFRAC()` weapon-spread draws** per shot from the shared
   `g_randomSeed` (`gun.c:28789-28795`) — spread sets bullet **direction**, hence
   what it hits;
3. the firer's per-frame **movement** delta;
4. the shooter's per-peer **FOV** (`Video.FovY` 45–90 → `c_scalex/c_scaley`,
   `bondview.c:1147-1151`) which drives target projection.

Kill credit is also **implicit context** — `ShotData` (`chrobjhandler.h:36`) has
no firer id; the path derives the firer from `get_cur_playernum()` (`chr.c:9784`)
and damage runs via `set_cur_player(victim)` → `record_damage_kills` →
`set_cur_player(restore)` (`explosions.c:1423-1437`).

**Decision:** promote the plan's own fallback — **"shooter-detects-hit +
host-validates"** — from fallback to **primary**. The client forwards the
**resolved** outcome (bullet vector post-spread/auto-aim, or the candidate hit +
target syncid); the host validates against authoritative state and applies
damage. During host-side resolution the host must `set_cur_player` to the
**firing** client's index, and a firer id should be threaded explicitly through
`ShotData`. Update §3e/§7: **RNG is correctness-bearing for hitscan.**

### 3.2 Single-process loopback cannot test divergence

> **Plan said** (N0c/§9.1): `--netplay loopback` runs "two in-process peers on
> one box," and the event-scoped hash oracle gates correctness there.

**Refuted.** The entire simulation state is **file-scope singletons** —
`pos_data_entry[600]` (`chrprop.c:148`), `g_playerPointers[4]`,
`g_playerPlayerData[4]` (`player.c:26-27`), `g_randomSeed`. Two in-process peers
share them, so they are **one** simulation; there is no second state to diverge.
PD itself forbids dual-role-in-one-process (`if (g_NetMode...) return -1`). The
desync oracle is **inert** in loopback (it hashes identical memory).

**Decision:** re-scope in-process loopback to a **transport/serialization
round-trip** lane only. Make a **two-OS-process** `net_smoke.sh` (host + client
over `127.0.0.1`, each with its own address space, per-process scripted input,
inter-process event-hash diff) an **explicit N0 deliverable**. Until it exists,
no N2–N5 gate proves divergence-freedom, and the DoD's "event-hash divergence 0"
is only meaningful **between two processes** (see §3.5).

### 3.3 The §10 rollback budget is understated

> **Plan said** (§10): match-relevant state is "~85–90 KB and mostly contiguous"
> (`pos_data_entry` 28.8 KB + `g_Projectiles` + explosion buffers +
> `g_playerPlayerData` + `g_randomSeed`), cheap (<1 ms) to snapshot.

**Refuted.** The arrays exist, but the framing is wrong: the dominant mutable AI
state **`g_ChrSlots`** (heap-allocated, `ChrRecord` ~0x180–0x200 B each ×
`guards+10`, `initguards.c:33`) is **omitted entirely**, as are `g_ActiveChrs`,
`g_SmokeBuffer`, and explosion/smoke sub-buffers. On the **64-bit native target**
`PropRecord` is **88 B** (8-byte pointers), so `pos_data_entry[600]` alone is
**~51.6 KB**, not 28.8 KB (that's the N64 32-bit figure, itself low at ~30.5 KB).
The regions are **separate per-stage heap allocations**, not one contiguous
block. Also: the `g_ClockTimer ≡ 1` pin §10 requires **changes
`g_GlobalTimerDelta`**, which scales all float-integrated motion — i.e. it
changes the exact values the project's mandated bit-stable ares movement oracle
(`MULTIPLAYER_PLAN.md` line 106) locks. **Reword §10 as a substantial future
investigation; if pursued, the pin must be MP-only and the SP oracle proven
untouched.**

### 3.4 The cross-platform wire primitive is wrong

> **Port map said** (`§3` table, line 97): PD's little-endian wire helpers
> (`PD_LE16/32/64`) map onto `src/platform/byteswap.h`.

**Refuted.** `byteswap.h` is a **big-endian-ROM→host** converter (`read_be*`
byteswaps); using it for an LE netbuf on LE hosts swaps the wrong way. PD's
`netbuf.c` carries its **own** LE codec, so "port `netbuf` verbatim" is fine —
but the `byteswap.h` mapping is misleading and must be removed. The
macOS-ARM↔x86 interop claim also needs an explicit **wire-float contract** (IEEE-
754 LE raw 4-byte copy is fine for both, but say so, incl. a NaN policy).

### 3.5 "Event-hash divergence 0" needs redefinition

Many "server-authoritative" outcomes are **functions of client-auth, lagged
positions**: spawn-pad selection reads all players' live `prop->pos`
(`bondview.c:1700`), proximity-mine arming tests distance to player positions,
explosion splash box-tests prop positions, auto-aim target selection uses the
shooter's FOV projection. A host resolving these against lagged puppet positions
*legitimately* differs from what a client saw. So the metric must be
**host-canonical: clients ADOPT host outcomes**, and the hash is compared
**between two independent host/client processes** — not as a host-vs-client
equality the one-host topology makes vacuous.

---

## 4. Gap register (ranked)

Severity: **blocker** (stalls the milestone) / **high** / **medium**.

### 4.1 Correctness harness doesn't exist and can't (blocker)
See §3.2. The funded CI lane validates transport only; the real two-process lane
is unscoped/unbuilt. Fix in N0.

### 4.2 Server-auth hitscan refuted (blocker)
See §3.1. Promote shooter-detects-hit; forward resolved vector/target; thread
firer id; sync RNG or sidestep it.

### 4.3 6 of 8 MP scenarios are unscored/broken online (blocker for "matches correct")

The wire table covers only kill-count (NORMAL/LTK). `get_points_for_mp_player`
(`mp_watch.c:791-842`) dispatches **four** scoring models off scenario-specific
replicated fields that **never enter the wire**:

| Scenario | Special replicated state | Code |
|---|---|---|
| **Flag Tag (TLD)** | flag owner; pickup/drop/transfer; `flag_counter` accrues only while held; unarmed-carrier force-equip; respawn suppressed | `lvl.c:5916-5940`, `mp_watch.c:821`, `prop.c:377-380`, `bondview.c:20834` |
| **Golden Gun (MWTGG)** | single GG prop owner; one-hit-kill; `killed_gg_owner_count` kill bonus (server must know victim held GG at kill time) | `lvl.c:5943-5951`, `mp_watch.c:815`, `gun.c:33454`, `prop.c:381-384` |
| **License to Kill (LTK)** | global one-hit-kill = `handicap=200` at spawn; handicap UI locked | `lvl.c:693-696`, `front.c:5447` |
| **YOLT** | per-player lives; elimination order; last-man match-end. **Also `GE007_MP_YOLT`-gated, 63% decomp locally** — untrustworthy as-is | `lvl.c:2187-2777`, `mp_watch.c:818` |
| **Team (2v2/3v1/2v1)** | team membership (overloaded `have_token_or_goldengun`); team-relative scoring; team radar | `mp_watch.c:823-838`, `front.c:7910-7937`, `radar.c:102-165` |
| **All** | MP **weapon set** (incl. Slappers Only / golden-only) is synced setup state never listed | `mp_weapon.c:7-17,200-218` |
| **All** | `drop_inventory` on death — the **only** flag/GG transfer — is `#if defined(VERSION_US)` | `bondview.c:20833-20835` |

**Decision:** either re-scope N3 to replicate per-scenario state and feed
`get_points_for_mp_player` from authoritative fields, **or** honestly narrow v1's
DoD to **deathmatch-only** and schedule scenarios as N3.5. Add per-scenario
acceptance tests (current `mp_smoke.sh` only boots a Temple deathmatch).

### 4.4 Online split-screen blocked by `getPlayerCount()` coupling (blocker)

`getPlayerCount()` (`player_2.c:92`) is the **single** axis for match-players
**and** local viewports **and** gfx budget **and** divider geometry:
`lvl.c:1564` (viewport loop), `fr.c:1779-1806` (dividers), `dyn.c:106` (gfx
budget), `boss.c:607` (tick/input loop). Binding all 4 match-players into
`g_playerPointers[]` makes a 2-local box render **4 viewports / 4-way split /
4-player budget.** The puppet gate only skips *input reads* and never touches
these loops.

**Decision:** introduce a second count `getLocalViewportCount()` (or
`g_NumLocalPlayers`) and audit every `getPlayerCount()` callsite — reclassify
each as **match-semantics** (scoreboard, scenario, alive-count → keep
`getPlayerCount`) vs **local-presentation** (viewport loop, dividers, gfx budget,
tick/input loop → switch to local count). Remote players stay as world bodies
(`PROP_TYPE_VIEWER`, `bondview.c:2531`) **without** claiming a pane. Input is
also keyed by player number, not a local-slot index (`lvl.c:5697-5769`;
`bondview.c:10839` even adds `+getPlayerCount()`), so define an explicit
`{playernum → local pad slot}` map; the move-record path must loop over **all**
local players and emit one `netplayermove` each (the plan's "its own player"
singular is wrong for 2 couch players).

### 4.5 Per-weapon specials / explosions / respawn are pointer/context/RNG/position-coupled (high)

- **Mines:** owner in `runtime_bitflags` (`chrobjhandler.c:34382`); remote
  detonation is a **global** `g_RemoteMineOwnerTriggerFlag` (`:44257`); proximity
  arming tests the *current* player only (`:7652-7693`). Sync owner id, timer,
  armed state; make detonation a host event; arm against **all** players.
- **Thrown grenades/knives:** `ownerprop` is a raw `PropRecord*` (`:34074`);
  trajectory uses `RANDOMFRAC()` + firer matrix. Serialize owner as a synced id;
  spawn host-side with the firer's orientation.
- **Explosions:** `explosionInflictDamage` (`explosions.c:1097`) iterates props
  with `set_cur_player` switching; credit from `->player`; **`playerid == -1`
  OOB hazard** on environmental splash (`bondview.c:20795`). Run host-auth single
  pass; guard the `-1` case.
- **Respawn:** `bondviewGetRandomSpawnPadIndex` (`bondview.c:1700`) reads all
  players' positions + RNG fallback → must be host-authoritative, broadcast as a
  forced teleport (`UCMD_FL_FORCE`).
- **Body armor:** sync `actual_armor` too (not just `actual_health`); define
  pickup-vs-damage **ordering** within a tick (`bondview.c:20801-20814`,
  `21225-21228`).
- **Death state:** the `ACT_DIE/ACT_DEAD` transition gates collision bounds and
  auto-aim eligibility (`chr.c:10388-10468`) — sync the **state flip** (animation
  frame may diverge cosmetically).

### 4.6 Lifecycle is design-absent and collides with engine structure (high)

- **Pause** is a global singleton that zeroes `g_ClockTimer` (`lvl.c:2023-2030`,
  driven by `mp_watch.c:622-636`) — any peer pausing freezes only its own sim and
  desyncs. Disable online, or make it host-only and broadcast.
- **Disconnect:** fixed `g_playerPointers[]` slots mean "vanish" breaks viewport
  math + scoreboard, "leave the body" leaves a frozen puppet that still gates
  elimination. Specify: keep the slot, mark "disconnected puppet," preserve
  score, exclude from elimination/continue counts, HUD a "P# disconnected."
- **Results-advance** gate `menu_count == player_count` (`mp_watch.c:649-665`)
  **deadlocks** on a dropped/AFK peer — make it host-authoritative with ready-acks
  + timeout.
- **Names/chat:** there is **no text-entry UI anywhere** (zero
  `SDL_StartTextInput` in the tree); players are P1–P4 by character photo
  (`front.c:6371`). CLC_AUTH "name" + "console chat" (labeled S) are real new UI
  work or should be cut — prefer deriving identity from the chosen character.
- **FOV** is **not cosmetic** for targeting (`bondview.c:1147`); force a canonical
  targeting FOV/aspect online or forward the resolved target.
- **Host-leave / reconnect:** state the contract (v1: host quit ends the match,
  broadcast clean `SVC_STAGE_END`, no migration) and add a short reconnect grace
  window keyed by stable peer identity.
- **Unlocks/saves:** selectable MP characters are gated by **local** unlock bits
  (`front.c:3844`); specify that online awards no cross-peer unlocks and the lobby
  validates the selectable-character set.

### 4.7 Internet play (N5) ships with zero packet memory-safety (high)

Attacker-controlled `syncid` indexes `pos_data_entry[600]` with **no bounds
check** (`netmsg.c:130` resolver, flagged `// TODO: make a map`); the `byteswap.h`
read primitive is a **raw unchecked deref** (over-reads short packets); no
rate-limiting, no max-message cap, no fuzz lane. Add an untrusted-input contract
**before N5**: validate `syncid ∈ [1..600]` (reject, not clamp); length-checked
`netbuf` cursor; per-peer rate/size caps; a fuzz lane on the apply handlers.

### 4.8 Build/infra plumbing under-scoped (medium)

`PLATFORM_SOURCES` is a **non-recursive** glob (`CMakeLists.txt:202`) — won't pick
up `src/platform/net/`. Two targets (`ge007` + `ge007_lib`) each re-enumerate
sources/includes and both need net sources + enet link (+ per-OS socket libs).
The third-party-notice CI gate (`tools/check_third_party_notices.py:61-107`)
hardcodes its allowlist — vendoring `lib/enet/` without updating it + `THIRD_PARTY.md`
fails CI. Make these explicit N0a sub-tasks.

### 4.9 Clock/tick drift has no buffering model (high)

`g_NetTick` is per-peer and advances at variable real rates (the `[1,4]` clamp
means a stuttering peer advances sim-time faster per frame). Only `Net.LerpTicks`
exists — there is **no** clock-offset estimation, jitter-buffer depth policy, or
cross-peer event-ordering model. This is where puppet stutter and event
mis-ordering live. Design it in N4 (or earlier).

---

## 5. Corrected `file:line` anchors (authoritative)

Both netplay docs' anchors are **pervasively stale** (symbols/roles real; lines
drifted). Canonical values:

| Symbol / fact | Plan cited | **Correct** |
|---|---|---|
| `pos_data_entry[POS_DATA_ENTRY_LEN]` | `chrprop.c:147` | **`chrprop.c:148`** (`POS_DATA_ENTRY_LEN=600`, `chrai.h:164`) |
| active-list globals | `chrprop.c:468` | **`chrprop.c:469-471`** (iterate `->prev`, e.g. `:510`) |
| `chrpropAllocate/Free/Activate` | `595/620/629` | **`596/621/630`** |
| `g_WeaponSlots[30]` / `g_AmmoCrates[20]` | `392/402` | **`393/403`** |
| `PropRecord` struct | `bondtypes.h:2273` ✅ | `bondtypes.h:2273` (size **88 B** on 64-bit) |
| `initBONDdataforPlayer` | `player_2.c:154` | def **`player_2.c:104`** (alloc at `:154`) |
| `init_player_data_ptrs_construct_viewports` | `player_2.c:68` ✅ | `player_2.c:68` |
| `struct player` | `bondview.h:334` ✅ | `bondview.h:334`; **also sync `actual_armor`** (`:2311`) |
| `bondviewMovePlayerUpdateViewport` | `bondview.c:13915` | **`bondview.c:14296`** (gate at `MoveBond` call `:14485`) |
| `MoveBond` | `bondview.c:14088` | **`bondview.c:12474`** |
| stick reads / native look / pad | `lvl.c:5672` / `:5703-5757` | **`lvl.c:5736/5740`** (stick), **`:5769`** (pad), angle writes **`:5842-5847`** |
| `record_damage_kills` | `bondview.c:20340` | **`bondview.c:20737`** (player damage only; NPC kills bypass via `chrlv.c:3509`) |
| `bondviewKillCurrentPlayer` | `bondview.c:20279` | **`bondview.c:20676`** |
| `projectileAllocate` / `projectileReset` | `2300/2266` | **`chrobjhandler.c:2350/2316`** |
| `object_interaction` | `chrobjhandler.c:8874` | **`chrobjhandler.c:9032`** (8874 is a dead `#if 0` stub) |
| `propdoorInteract` | (no def line) | def **`chrobjhandler.c:48070`**, caller **`chrprop.c:5295`** |
| `explosionCreate` / `g_ExplosionBuffer` | `844/642` | **`explosions.c:873/671`** |
| `osContGetReadData` / data[0] merge / multi-pad fill | `stubs.c:5280` (NETPLAY) · `4738/4750` (MULTI) | func **`stubs.c:5000`**, local merge **`:5426`**, fill loop **`:5436-5439`** |
| button constants | `platform_os.h:183` | values correct; named aliases **`:199-212`**; `ANY_BUTTON` is **`os_extension.h:7`** |
| MP-menu controller gate | `front.c:3886/5526` | **`3888`** (`>=2`) / **`5567`** (`<2` bounce) |
| `joy.c` input fns | `876/827/887` ✅ | `joy.c:876/827/854/887` (path `src/joy.c`) |
| `boss.c` mainloop | `568/602/629` ✅ | exact |
| `pc_apply_mp_selection` / `bossSetLoadedStage` | `initmenus.c:263` / `boss.c:779` ✅ | exact — **but it does NOT call `frontChangeMenu(MENU_RUN_STAGE)`**; both paths converge on `bossSetLoadedStage`/`g_MainStageNum` |
| MP menu FSM | `front.c:14324` | `menu_init` **`front.c:14273`**; dispatch switches **~`14351/14390/14419`** |
| `lvlManageMpGame` live def | `boss.c:602` (call) ✅ | call `boss.c:602`; **live def `lvl.c:2518`** (`:1995` is dead `PORT_FIXME_STUBS`) |
| `g_GfxSizesByPlayerCount` | `dyn.c:62` | decl **`dyn.c:45`** (NATIVE_PORT); `:62` is the `-mgfx` override |
| `g_randomSeed` / `randomGetNext` / `randomSetSeed` | `random.c:24/116/183` ✅ | exact (single `u64`) |
| clock clamp/cast | `lvl.c:1977-1998` | **`lvl.c:2044-2055`** (NATIVE_PORT `[1,4]` clamp) |
| float-scaled motion | `explosions.c:1539` | **`explosions.c:1568`** (`lvupdate` set `:1557`) |
| wall-clock seed | `boss.c:390` ✅ | `boss.c:390`; `g_deterministic` fixed-seed at `:393-398` |
| shot path / spread RNG | (combat fidelity) | `bullet_path_from_screen_center` **`gun.c:28756`**, 4× `RANDOMFRAC` **`:28789-28795`** |
| auto-aim | (combat fidelity) | `bondview.c:12178-12202`, `6315-6403` |
| pause latch | (lifecycle) | `lvl.c:2023-2030`; FSM `mp_watch.c:622-636`; results gate `:649-665` |

---

## 6. Corrected milestone ladder

The N0→N5 spine is the right shape; corrections folded in (**bold** = new/changed
vs the original plan):

| Phase | Milestone | Now requires |
|---|---|---|
| **N0** | Transport + harness | Vendor enet; port `netbuf` (**own LE codec, not `byteswap.h`**); frame-pump around `boss.c:602-629`; FSM. **Fix non-recursive glob + dual targets + notice gate (§4.8).** **Re-scope in-process loopback to transport-round-trip; make a two-process `net_smoke.sh` an explicit deliverable (§3.2).** |
| **N1** | Auth + lobby | Build-hash + region handshake. **Decide identity now: derive from character (no text UI) or budget real SDL text-input (re-estimate M–L, §4.6).** Lobby replicates **{stage, scenario, player count, team assignment, weapon set, limits}**. |
| **N2** | Bodies online | `ucmd` adapter (**encode resolved actions — crouch is a stateful accumulator, weapon-cycle is PC mouse-wheel, reload is an action call, meaning is control-style dependent**). Puppet gate at `MoveBond` call (`bondview.c:14485`): write `prop->pos` **and** `field_488.collision_position` (+tile/room), bypass `MoveBond`. **Generalize move-record to all local players. Introduce `getLocalViewportCount()` and start the callsite audit (§4.4).** |
| **N3** | Matches correct | Sync-IDs + prop spawn/move. Damage/kills: wrap `record_damage_kills@20737` / `bondviewKillCurrentPlayer@20676`; **adopt shooter-detects-hit (§3.1); thread firer id; sync `actual_armor`; guard `-1` OOB.** Pickups (`9032`), doors (`48070`/`5295`), explosions (`873`). **Per-scenario replication (§4.3) — the hidden bulk. Mines/projectiles/respawn host-auth (§4.5).** Desync oracle **bound to the two-process lane.** Lifecycle contracts (§4.6). |
| **N4** | Hardening | Cadence + bandwidth telemetry; interp tuning; **clock-offset/jitter-buffer/event-ordering model (§4.9)**; adversarial-network lane (needs two-process harness). |
| **N5** | Internet | Query + relay/join-by-code — **gated behind the packet-validation contract (§4.7) and an explicit relay ops/cost/kill-switch plan.** |

> **Honest re-sizing:** N3 is mis-estimated by a large factor. Its L tasks hide
> blocker-class work (firer-id threading, auto-aim/RNG resolution, mine/projectile
> ownership serialization) **and** the entire per-scenario surface the wire table
> never lists.

---

## 7. Game-flow injection map (what changes in the native port)

The concrete edit surface, by subsystem (anchors corrected per §5):

- **Frame loop — `boss.c`:** `netStartFrame()` before `joyPoll@568`,
  `netEndFrame()` after `lvlRender@629` (bracket the whole step incl.
  `lvlViewMoveTick` loop `@607`), gated on `g_ClockTimer>0`. Post-load barrier at
  **`:494`** → `netSyncIdsAllocate` + `netPlayersAllocate`. Reuse `g_deterministic`
  hook `@393-398`.
- **Props/sync — `bondtypes.h`/`chrprop.c`:** append `u16 syncid` (`:2273`);
  assign `(rec-pos_data_entry)+1` at stage start, walk active list `@469`.
- **Player/puppet — `bondview.c`/`lvl.c`:** add `g_NetPuppet[]`; gate at `MoveBond`
  call `@14485` (write `prop->pos` + `field_488.collision_position` + angles +
  `crouchpos`, bypass `MoveBond`); gate stick reads `@5736/5740`, native look
  `@5769`, mouse-look `@5766` off for remotes; loop move-record over local players.
- **Input/ucmd — `joy.c`/`bondview.c`:** encode resolved actions
  (`@11095-11103`) + stick + discrete events; replicate `crouchpos` + `prev_buttons`.
- **Combat — `bondview.c`/`gun.c`/`chr.c`:** wrap `record_damage_kills@20737` /
  `bondviewKillCurrentPlayer@20676`; `set_cur_player`→firer during replay; guard
  `-1@20795`; forward resolved vector / auto-aim target syncid (`gun.c:28756`,
  `bondview.c:12178-12202`).
- **Projectiles/mines/pickups/doors/explosions — `chrobjhandler.c`/`explosions.c`:**
  `projectileAllocate@2350`→spawn (owner as id `@34074`); mine owner `@34382` +
  detonator flag `@44257`→events, proximity vs all players `@7652-7693`;
  `object_interaction@9032`→pickup; `propdoorInteract@48070`/`chrprop.c:5295`→door;
  `explosionInflictDamage@1097` single host-auth pass.
- **Scenarios — `lvl.c`/`mp_watch.c`/`front.c`/`prop.c`:** flag `@5916-5940`; GG
  `@5943-5951` + bonus `gun.c:33454`; LTK `lvl.c:693`; YOLT `@2187-2777`; team
  `front.c:7910`; weapon set `mp_weapon.c:218`; un-gate `drop_inventory@20834`;
  feed `get_points_for_mp_player@791` from authoritative fields.
- **Session/pause/viewport — `front.c`/`initmenus.c`/`lvl.c`/`fr.c`/`dyn.c`/`player_2.c`:**
  host-drives-stage via `pc_apply_mp_selection@263`→`bossSetLoadedStage@779`; menu
  FSM `@14273`; results-advance host-auth (replaces `@649-665`); net-sync/disable
  pause `@2023-2030`; new `getLocalViewportCount()` + reclassify `getPlayerCount()`
  uses (`lvl.c:1564`, `fr.c:1779-1806`, `dyn.c:106`, `boss.c:607`).
- **Plumbing — `settings.c`/`main_pc.c`/`CMakeLists.txt`/`tools/`:** `Net.*` config;
  `--host/--connect/--port/--loopback`; vendor enet; fix glob+targets+notice gate;
  two-process `net_smoke.sh`; bounds/length/rate-limit + fuzz the apply handlers.

---

## 8. Changes applied to the plan docs (2026-06-25)

This review is accompanied by edits memorializing the corrections in-place:

- **NETPLAY_PLAN.md** — revision banner → this doc; §1.4/§3e/§7 RNG-correctness
  correction; §3d hitscan model (shooter-detects-hit promoted); §1.4 `actual_armor`;
  N0c loopback re-scope + two-process harness; §10 honest reframing; scenario-scope
  and split-screen-decoupling and lifecycle and security callouts.
- **NETPLAY_PORT_MAP.md** — revision banner; corrected symbol-table anchors;
  `byteswap.h`→own-LE-codec note; puppet-gate `prop->pos`+`collision_position`
  correction; `record_damage_kills` player-only scoping; stage-flow
  `MENU_RUN_STAGE` correction.
- **MULTIPLAYER_PLAN.md** — fix the lines 27-28 self-contradiction (`stubs.c`
  anchors + the superseded "data[1..3] zeroed" behavior; already-correct row at
  line 62); `front.c` FSM and gate anchors.

> The exhaustive corrected-anchor table (§5) is the single source of truth; the
> plan docs point here rather than duplicating it.
