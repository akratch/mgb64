# Netplay Port Map — adapting the Perfect Dark port's netcode into MGB64

Companion to [NETPLAY_PLAN.md](NETPLAY_PLAN.md). This is a concrete, file-by-file
map of the **Perfect Dark PC port** netcode (`fgsfdsfgs/perfect_dark`,
[`port-net` branch](https://github.com/fgsfdsfgs/perfect_dark/tree/port-net))
into MGB64's `src/platform/net/`. PD is built on the same engine family as
GoldenEye, so its netcode is the closest working reference that exists.

> **Licensing (verified):** the PD port is **MIT** (Copyright © 2022 Ryan Dwyer)
> and bundles **enet (MIT)**. Both are compatible with MGB64's MIT license, so we
> may **adapt the source directly** (not only clean-room), provided we preserve
> the copyright/permission notice and record it in `THIRD_PARTY.md` / `NOTICE.md`.
> This is a significant accelerator over the clean-room assumption in
> NETPLAY_PLAN §10.

---

## 1. The PD netcode, as actually shipped (ground truth)

Reading the source corrects the architecture sketch in NETPLAY_PLAN §1. The
shipped model is **not** full-sim-on-every-client with reconciliation. It is:

- **Master clock:** `g_NetTick`, a `u32` incremented once per game frame
  (`net.c:808`), pumped by `netStartFrame()`/`netEndFrame()` bracketing the sim.
- **Client-authoritative movement.** Each client simulates **only its own**
  player and sends the *result* — position, view angles, analog lean/crouch,
  and a `ucmd` command bitmask — to the server (`netClientRecordMove`,
  `net.c:185`; `CLC_MOVE`). The server trusts and relays it.
- **Remote players are interpolated puppets, not simulations.** On every peer,
  remote players are normal engine players with **input disabled**
  (`controlmode = CONTROLMODE_NA`, `net.c:981`) and `isremote = true`. Their
  pawn is driven by applying the received `netplayermove` and **lerping**
  position/angles over `g_NetInterpTicks` (default 3) between the last two moves
  (`inmove[2]`). A server-set **force-tick** (`UCMD_FL_FORCE*`) snaps them for
  teleports/respawns (`net.c:255`, `netmsg.c:275`).
- **Discrete world-event layer keyed by sync-ID.** Everything that isn't a
  player body is synced by explicit server→client messages: prop spawn/move,
  damage, pickup, door/lift use, chr damage/disarm, player stats (`SVC_PROP_*`,
  `SVC_CHR_*`, `SVC_PLAYER_STATS`). Each references a prop by a **sync-ID that
  equals its array index** (`prop->syncid = prop - g_Vars.props + 1`,
  `net.c:1017`), resolved back with a linear scan (`netbufReadPropPtr`,
  `netmsg.c:130`).
- **RNG push for cosmetic determinism.** The server pushes its RNG seed
  (`g_NetRngSeeds[]`) and clients latch it (`netClientSyncRng`, `net.c:780`).
- **Transport:** enet, two channels — `NETCHAN_DEFAULT` (game, mixed
  reliable/unreliable) and `NETCHAN_CONTROL` (auth/chat/settings, reliable).
  `NET_BUFSIZE` 1440 (MTU-sized). Default port 27100. Little-endian wire.
- **Connection state machine:** `DISCONNECTED → CONNECTING → AUTH → LOBBY →
  GAME` (`net.h:CLSTATE_*`), with a connectionless **query protocol**
  (`NET_QUERY_MAGIC`, CRC-checked) for a server browser (`net.c:338`).

### What this means for MGB64's split-screen seam

NETPLAY_PLAN leaned on the `osContGetReadData` `data[k]` seam for **remote**
input. Ground truth says: **don't.** The `data[k]` seam is for **local couch
players sharing one host**; remote players are puppets driven by the
move-apply + interpolation layer, never by injected `OSContPad` bytes. "Online
split-screen" = a host runs its 1–2 *local* players through `data[k]` **and**
N *remote* puppets through the net layer. Keep the two paths separate.

> The honest tradeoff: client-authoritative movement is the reason PD has
> position/where-am-I edge cases and is trivially cheatable. It's the fast,
> proven v1. Making movement server-authoritative (server simulates pawns from
> client `ucmd`) is a post-v1 hardening option — see §7.

---

## 2. Source inventory → target layout

| PD source (`port-net`) | Lines | Role | MGB64 target |
|---|---|---|---|
| `port/include/net/netbuf.h` | 50 | LE byte-buffer serializer API | `src/platform/net/netbuf.h` |
| `port/src/net/netbuf.c` | 305 | bounds-checked read/write primitives | `src/platform/net/netbuf.c` |
| `port/include/net/net.h` | 163 | `netplayermove`, `netclient`, globals, API, `UCMD_*` | `src/platform/net/net.h` |
| `port/src/net/net.c` | 1127 | enet host, conn FSM, frame pump, move record, syncid/player alloc, RNG, config, query | `src/platform/net/net.c` |
| `port/include/net/netmsg.h` | 74 | `SVC_*`/`CLC_*` ids + per-message proto | `src/platform/net/netmsg.h` |
| `port/src/net/netmsg.c` | 1582 | per-message (de)serialize + apply; syncid↔prop | `src/platform/net/netmsg.c` |
| `port/src/net/netmenu.c` | 422 | host/join lobby UI | **glue into `src/game/front.c`** MP menu FSM (`front.c:14324`) |
| `port/include/net/netenet.h` | 10 | enet include shim (`#undef bool/near/far`) | `src/platform/net/netenet.h` |
| `port/external/enet.c` + `port/include/external/enet.h` | vendored | UDP transport (MIT) | `lib/enet/` (vendor as-is) |

`netbuf` and `enet` port **verbatim**. `net.c`/`netmsg.c` are ~70% transport/
protocol (ports cleanly) and ~30% engine-coupling (the rewrite — §3/§4).
`netmenu.c` is best **reimplemented** against GE's existing MP menu rather than
ported, since the frontends differ.

---

## 3. Engine-symbol correspondence (PD → MGB64/GoldenEye)

The rewrite is mostly find-and-replace against this table. Verified GE symbols
cited; a few marked *(locate)* need a grep during implementation.

| Concept | PD symbol | MGB64 / GE symbol | Where |
|---|---|---|---|
| Prop array + count | `g_Vars.props[]`, `g_Vars.maxprops` | `pos_data_entry[POS_DATA_ENTRY_LEN]` | `chrprop.c:147` |
| Prop struct | `struct prop` | `PropRecord` | `bondtypes.h:2273` |
| **Prop sync-ID** | `prop->syncid` (`u16`) | **add `u16 syncid` to `PropRecord`** (or use `rec - pos_data_entry + 1` directly) | — |
| Active/paused prop lists | `g_Vars.activeprops`, `pausedprops` | GE prop list heads *(locate in `chrprop.c`)* | — |
| Player array | `g_Vars.players[]` | `g_playerPointers[PLAYER_1..4]` | `player_2.c:68` |
| Player struct | `struct player` | GE player record *(the struct behind `g_playerPointers`)* | `player.h` |
| Current-player select | `g_Vars.currentplayernum`, `setCurrentPlayerNum()` | `get_cur_playernum()`, `set_current_player*()` | many callsites |
| Live player count | `g_MpSetup.chrslots` | `getPlayerCount()` | `player_2.c` |
| Coord type | `struct coord` | `coord3d` / `vec3d` | `bondtypes.h:233` |
| Matrix type | `Mtxf` | `Mtxf` | `bondtypes.h:106` |
| RNG seed(s) | `g_RngSeed`, `g_Rng2Seed` (two streams) | `g_randomSeed` (**one** `u64`) | `random.c:24` |
| Frame-advance count | `g_Vars.diffframe60` | `speedgraphframes` / `g_ClockTimer` | `lvl.c:1977` |
| Frame pump site | `schedStartFrame`/`schedEndFrame` (`pdsched.c:242/281`) | `boss.c` mainloop around `lvlManageMpGame()` | `boss.c:567` |
| Stage id | `g_StageNum` | `g_CurrentStageToLoad` / stage id | `lvl.c` |
| MP setup | `g_MpSetup` (scenario, chrslots) | `multi_stage_setups[].stage_id` + MP globals | `front.c:1124` |
| Player config array | `g_PlayerConfigsArray[]`, `g_PlayerExtCfg[]` | GE char/handicap config | `file.c`/`file2.c` |
| Player input command | `pl->ucmd` (unified bitmask) | **no GE equivalent — derive** from `joyGetButtons()` | `joy.c:827` |
| Gun/weapon state | `pl->gunctrl`, `struct gset` | GE gun/weapon state *(bondgun-family)* | *(locate)* |
| Stage load/teardown | `mainEndStage()`, `mainChangeToStage()`, `titleSetNextStage()` | GE loader + `pc_apply_mp_selection()` | `initmenus.c` |
| Config registration | `configRegisterUInt("Net.*")` | `settings.c` / `config_pc.c` runtime-override system | `settings.c` |
| CLI args | `sysArgGetInt("--port"/"--connect"/"--host")` | `main_pc.c` arg parse (joins `--multiplayer`/`--players`) | `main_pc.c` |
| Logging | `sysLogPrintf(LOG_*)` | GE logging / `port_trace` | `port_trace.c` |
| Endian helpers | `PD_LE16/32/64` | `byteswap.h` | `src/platform/byteswap.h` |

**The two structural gaps** that need engineering, not renaming:

1. **`prop->syncid`** — GE `PropRecord` lacks it. Add a `u16 syncid` field (low
   risk; props are pooled in a fixed array), populated by the `netSyncIdsAllocate`
   port. (Don't rely on the bare index for the *wire* if props can be re-pooled
   mid-match; the explicit field matches PD and is safer.)
2. **`pl->ucmd`** — PD threads a unified per-player command bitmask through the
   sim; GE reads `joyGetButtons()` ad hoc. Build a small adapter that derives a
   `ucmd` (the `UCMD_*` set in `net.h:46`) from GE's `joyGet*` for the local
   player at record time, and that maps an incoming `ucmd` to the actions a
   remote puppet must reproduce (fire/reload/aim/select/duck/squat). This adapter
   is the single most important piece of new code.

---

## 4. Integration points (where MGB64 code must change)

### 4a. Frame pump — `src/boss.c`
Mirror `pdsched.c:242/281`. In the native mainloop, around `lvlManageMpGame()`
(`boss.c:567`, after `joyPoll()`):
- call `netStartFrame()` **before** the sim (pumps enet events, applies inbound
  messages, `++g_NetTick`), gated on the frame actually advancing
  (`g_ClockTimer > 0`), and
- call `netEndFrame()` **after** the sim (records local player move, writes
  `CLC_MOVE`/`SVC_PLAYER_MOVE`, flushes enet).
Pin the netplay timestep per NETPLAY_PLAN §3a (`g_ClockTimer ≡ 1`) so one net
tick = one sim step.

### 4b. Player allocation — after MP players spawn
Port `netPlayersAllocate()` (`net.c:954`): map clients↔`g_playerPointers[]`, set
remote players to the GE equivalent of `CONTROLMODE_NA` + `isremote`, and apply
their name/body/head config. Call it right after the MP match constructs its
players (`init_player_data_ptrs_construct_viewports`, `player_2.c:68`).

### 4c. Sync-ID allocation — after stage load
Port `netSyncIdsAllocate()` (`net.c:1004`): walk the active+paused prop lists and
assign `syncid`. Call once per stage start (server authoritative; clients adopt
the server's mapping). Includes PD's client/server player-prop swap hack
(`net.c:1036`) since the local player is always index 0 locally.

### 4d. Local-player move record — `netClientRecordMove` rewrite (`net.c:185`)
This reads PD player fields into `netplayermove`. Rewrite each field against the
GE player struct: `crouchoffset`, `swaytarget/75`, `speedforwards/sideways`,
`vv_theta/vv_verta`, `crosspos[]`, `prop->pos`, and the derived `ucmd`. This is
~40 lines and is where most "field doesn't exist" friction lives.

### 4e. Remote-puppet apply + interpolation — *new* GE code
PD applies `inmove` and lerps inside its player update. MGB64 needs the
equivalent in the per-player update path (`lvl.c` player loop): for `isremote`
players, set pos/angles from `inmove`, lerp over `g_NetInterpTicks`, and trigger
the `ucmd`-derived actions (fire/reload/switch). This is the consumer side of the
§3 `ucmd` adapter.

### 4f. World-event hooks — the correctness long tail
PD emits `SVC_*` at gameplay moments; the **server** must call the matching
`netmsg…Write` at the equivalent GE call sites, and the **client** read-handlers
apply them. These are the wraps to add (and the PD "known issues" list is the
evidence of which are fiddly):

| Event | PD message | GE call site to wrap *(locate during impl)* |
|---|---|---|
| Projectile / thrown / dropped prop created | `SVC_PROP_SPAWN` (`netmsg.c:912`) | `g_Projectiles[]` alloc, weapon-drop (`chrprop.c:407`) |
| Dynamic prop per-tick update | `SVC_PROP_MOVE` (`netmsg.c:781`) | projectile-in-flight / moving-object update |
| Damage to a prop/chr | `SVC_PROP_DAMAGE` / `SVC_CHR_DAMAGE` (`netmsg.c:1161/1397`) | GE damage application (chr-damage fn, `bondview.c`/`chr*.c`) |
| Pickup (weapon/ammo/armor) | `SVC_PROP_PICKUP` (`netmsg.c:1197`) | GE pickup handler |
| Door / lift use | `SVC_PROP_USE/DOOR/LIFT` (`netmsg.c:1230/1278/1331`) | GE door/lift update |
| Weapon dropped on death/disarm | `SVC_CHR_DISARM` (`netmsg.c:1485`) | GE disarm / death drop |
| Health/armor sync | `SVC_PLAYER_STATS` (`netmsg.c:664`) | periodic + on-change |
| Match RNG seed | latch via `netClientSyncRng` | once per tick where server advances RNG |

### 4g. Config + CLI — `settings.c` / `main_pc.c`
Port the `configRegister*("Net.*")` block (`net.c:1114`) into the runtime-override
system, and the `--host/--connect/--port/--maxclients` args (`net.c:421`) into
`main_pc.c` alongside the existing `--multiplayer/--players/--mp-stage`.

### 4h. Lobby/menu — `front.c`
Reimplement `netmenu.c`'s host/join/address-entry against the existing MP menu
state machine (`front.c:14324`) and stage table (`front.c:1124`). Auth/ROM-check
(`netmsgClcAuthRead`, `netmsg.c:179`) becomes MGB64's compatibility handshake
(NETPLAY_PLAN N5c) — but compare a **build hash + ROM region**, not PD's
filename-only check (its documented weakness).

---

## 5. Port order (maps to NETPLAY_PLAN phases)

1. **Vendor enet + port `netbuf.[ch]` + `net.h` types + frame-pump skeleton +
   connection FSM + loopback.** No gameplay yet; loopback connect/disconnect +
   `g_NetTick` advancing. → *NETPLAY_PLAN N0.*
2. **Auth/lobby:** `CLC_AUTH`/`SVC_AUTH`, settings, ROM/build handshake. → *N5c (early).*
3. **Player move:** `ucmd` adapter (§3), `netClientRecordMove` rewrite,
   `CLC_MOVE`/`SVC_PLAYER_MOVE`, `netPlayersAllocate` (remote = puppets),
   apply + interp (§4e). → **bodies move correctly.** *N1+N2.*
4. **Props:** `syncid` field + `netSyncIdsAllocate` + `SVC_PROP_SPAWN/MOVE` for
   projectiles. → *N2.*
5. **Event layer:** damage, pickup, door/lift, chr damage/disarm, stats. →
   **matches are correct.** *N2/N3.*
6. **Polish:** RNG push, interpolation tuning (`Net.LerpTicks`),
   `Net.Server.UpdateFrames` bandwidth knob, F9 net stats (`netDebugRender`,
   `net.c:1085`), stats screen, server browser/query, lobby UI. → *N4/N5.*

Layer the **state-hash desync oracle** (NETPLAY_PLAN §3c) over steps 3–5 — PD
shipped without it and paid for it in the known-issues list.

---

## 6. Direct-port vs rewrite, at a glance

| Component | Action | Why |
|---|---|---|
| `netbuf.[ch]` | **Port verbatim** | Pure LE serialization; only dep is `byteswap.h` + `coord3d`/`Mtxf` names. |
| enet + `netenet.h` | **Vendor verbatim** | MIT, self-contained. |
| `net.c` transport/FSM/query/config | **Port, light edits** | enet calls, conn state, send buffers, parse-addr are engine-agnostic. |
| `net.c` move-record / player-alloc / syncid-alloc | **Rewrite** | Touch GE player/prop structs (§3 gaps). |
| `netmsg.c` framing + read/write scaffolding | **Port** | The `netbuf` call patterns transfer directly. |
| `netmsg.c` apply-to-engine bodies | **Rewrite** | Each applies to GE structs / call sites (§4f). |
| `netmenu.c` | **Reimplement** | Against GE's `front.c` MP menu, not PD's. |

---

## 7. Gotchas & deviations to plan for

- **One RNG stream, not two.** GE has `g_randomSeed`; PD syncs `g_RngSeed` +
  `g_Rng2Seed`. Collapse `g_NetRngSeeds[2]` to what GE actually has.
- **`ucmd` is the keystone.** No GE equivalent exists; the derive/apply adapter
  (§3.2) gates steps 3–5. Build and unit-test it first.
- **Linear sync-ID lookup.** `netbufReadPropPtr` scans up to `maxprops`
  (`netmsg.c:138`, flagged `// TODO: make a map`). With GE's 600-prop array it's
  fine; if it shows up in profiles, add an index map.
- **Filename-only ROM check is unsafe.** Replace with build-hash + region
  (NETPLAY_PLAN N5c).
- **Client-authoritative movement = cheatable + edge-case desyncs.** Acceptable
  for a v1 party port; document it. Server-authoritative pawns (server simulates
  from client `ucmd`, client predicts + reconciles) is the post-v1 hardening
  path — and the one place NETPLAY_PLAN's prediction/reconciliation design (N3)
  actually applies. PD does **not** do this today.
- **Bots aren't synced** ("Sims don't work in netgames"). Irrelevant to us —
  GoldenEye MP is human-only (MULTIPLAYER_PLAN scope guard), so we **dodge PD's
  single biggest unsolved area** entirely.
- **Per-weapon special modes** (PD's FarSight alt-fire, fly-by-wire) desync in
  PD. GE's analogues to watch: remote mines, the Klobb/auto-aim quirks, throwing
  knives — verify each through the event layer, not movement.
- **`diffframe60` gating.** PD only nets on advancing frames; respect GE's
  `g_ClockTimer > 0` equivalently so paused/loading frames don't desync the tick.

---

## 8. Bottom line

The PD port hands us a **complete, MIT-licensed, same-engine-family netcode** we
can largely adapt rather than invent. The transport (`netbuf` + enet), the
protocol (`SVC_*`/`CLC_*`), the tick/connection model, and the sync-ID scheme
port directly. The real work is the **~30% engine coupling** — the `ucmd`
adapter, the move record/apply against GE's player struct, the `syncid` field,
and wrapping GE's damage/pickup/door/projectile call sites. GoldenEye being
human-only lets us skip PD's worst unsolved problem (bot sync). Build the
desync-hash oracle alongside it and we exceed the reference's quality.

## References
- PD netcode (port-net): [`net.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/net.c) · [`netmsg.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/netmsg.c) · [`net.h`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/include/net/net.h) · [`netbuf.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/netbuf.c) · [`netplay.md`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/docs/netplay.md)
- Internal: [NETPLAY_PLAN.md](NETPLAY_PLAN.md), [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md)
