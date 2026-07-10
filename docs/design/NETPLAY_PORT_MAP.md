# Netplay Port Map — adapting the Perfect Dark port's netcode into MGB64

Companion to [NETPLAY_PLAN.md](NETPLAY_PLAN.md). This is the concrete, file-by-
file map of the **Perfect Dark PC port** netcode (`fgsfdsfgs/perfect_dark`,
[`port-net` branch](https://github.com/fgsfdsfgs/perfect_dark/tree/port-net))
into MGB64's `src/platform/net/`. Every GoldenEye symbol below is grounded to a
real `file:line` verified in this tree. PD is built on GoldenEye's engine family,
so its netcode is the closest working reference that exists.

> **Licensing (verified):** PD port is **MIT** (© 2022 Ryan Dwyer) and bundles
> **enet (MIT)**. Both compatible with MGB64's MIT license → **adapt the source
> directly**, preserving the notice and recording it in `THIRD_PARTY.md` /
> `NOTICE.md`.

> **⚠ Revision (2026-06-25):** the 2026-06-25 source-grounded review
> re-verified every anchor in this
> map. **The symbols and roles are real, but the `file:line` numbers below are
> pervasively stale** (chrprop.c off-by-one; world-event sites off 29–460 lines). The
> **authoritative corrected-anchor table is review §5** — use it, not the lines here,
> until this map is re-anchored. Four substantive corrections are marked inline:
> `byteswap.h` is the **wrong wire primitive** (§ source inventory), the puppet gate
> must write `prop->pos` **and** the collision twin (§4c), `record_damage_kills`
> centralizes **player** damage only (§4f), and `pc_apply_mp_selection` does **not**
> drive `MENU_RUN_STAGE` (§4h). The review also adds blocker-class design work this
> map under-scopes (resolved-vector hitscan, two-process test harness, per-scenario
> replication, `getLocalViewportCount` decoupling, packet validation).

---

## 1. The PD netcode, as actually shipped (ground truth)

The model (confirmed by reading the source, not the README):

- **Master clock** `g_NetTick` (`u32`), incremented once per advancing game frame
  (`net.c:808`), pumped by `netStartFrame()`/`netEndFrame()` bracketing the sim.
  Per-peer local — *not* a lockstep counter; moves carry their own tick.
- **Client-authoritative movement.** Each client records only its own player and
  sends pos + angles + a `ucmd` bitmask (`netClientRecordMove`, `net.c:185`;
  `CLC_MOVE`). The host trusts and relays it.
- **Remote players are interpolated puppets** — engine players with input
  disabled (`controlmode = CONTROLMODE_NA`, `net.c:981`), lerped between the last
  two moves (`inmove[2]`) over `g_NetInterpTicks`. A server **force-tick**
  (`UCMD_FL_FORCE*`) snaps them for teleport/respawn (`net.c:255`, `netmsg.c:275`).
- **Server-authoritative event layer.** Props/chrs/scores synced by explicit
  `SVC_*` messages, each keyed by a **sync-ID = prop array index**
  (`prop->syncid = prop - g_Vars.props + 1`, `net.c:1017`; resolver
  `netbufReadPropPtr`, `netmsg.c:130`).
- **RNG push** for cosmetic parity (`netClientSyncRng`, `net.c:780`).
- **Transport:** enet, two channels — `NETCHAN_DEFAULT` (game, mixed) and
  `NETCHAN_CONTROL` (auth/chat/settings, reliable). `NET_BUFSIZE` 1440. Default
  port 27100. Little-endian wire (`PD_LE*`). Connection FSM
  `DISCONNECTED→CONNECTING→AUTH→LOBBY→GAME`, with a connectionless query protocol
  (`NET_QUERY_MAGIC`, CRC) for a server browser (`net.c:338`).

GoldenEye differences we inherit: **4 players max** (not 8), **one RNG stream**,
**human-only** (we skip PD's unsolved bot sync), and **no `controlmode`/`ucmd`
fields** (we add equivalents — §3, §4).

---

## 2. Source inventory → target layout

| PD source (`port-net`) | Lines | Role | MGB64 target | Action |
|---|---|---|---|---|
| `port/include/net/netbuf.h` + `port/src/net/netbuf.c` | 50/305 | LE byte-buffer serializer | `src/platform/net/netbuf.[ch]` | **port verbatim** |
| `port/include/net/net.h` | 163 | `netplayermove`, `netclient`, globals, `UCMD_*`, API | `src/platform/net/net.h` | port, light edit |
| `port/src/net/net.c` | 1127 | enet host, conn FSM, frame pump, move record, syncid/player alloc, RNG, config, query | `src/platform/net/net.c` | port + rewrite engine bits |
| `port/include/net/netmsg.h` + `port/src/net/netmsg.c` | 74/1582 | `SVC_*`/`CLC_*` (de)serialize + apply | `src/platform/net/netmsg.[ch]` | port framing, rewrite apply |
| `port/src/net/netmenu.c` | 422 | host/join lobby UI | glue into `src/game/front.c` (MP FSM `menu_init` `front.c:14273`, switches ~`14351/14390/14419`) | **reimplement** |
| `port/include/net/netenet.h` | 10 | enet include shim | `src/platform/net/netenet.h` | port verbatim |
| `port/external/enet.c` + `include/external/enet.h` | vendored | UDP transport (MIT) | `lib/enet/` | **vendor as-is** |

`netbuf` + enet port verbatim. `net.c`/`netmsg.c` are ~70% transport/protocol
(ports cleanly) and ~30% engine coupling (the rewrite — §3/§4). `netmenu.c` is
reimplemented against GoldenEye's existing MP menu.

---

## 3. Engine-symbol correspondence (PD → MGB64/GoldenEye) — fully grounded

| Concept | PD symbol | MGB64 / GoldenEye symbol | Where (verified) |
|---|---|---|---|
| Prop array + count | `g_Vars.props[]`, `g_Vars.maxprops` | `pos_data_entry[POS_DATA_ENTRY_LEN]` | `chrprop.c:147` |
| Prop struct | `struct prop` | `PropRecord` | `bondtypes.h:2273` |
| **Prop sync-ID** | `prop->syncid` | **add `u16 syncid` to `PropRecord`** | — (new field) |
| Active prop list | `g_Vars.activeprops`/`pausedprops` | `ptr_obj_pos_list_current_entry` / `_first_entry` / `_final_entry` (doubly-linked, iterate via `->prev`) | `chrprop.c:468` |
| Prop alloc/free/activate | (PD equivalents) | `chrpropAllocate()` / `chrpropFree()` / `chrpropActivate()` | `chrprop.c:595` / `:620` / `:629` |
| Player array | `g_Vars.players[]` | `g_playerPointers[4]` | `player.h:76`; init `player_2.c:154` `initBONDdataforPlayer()` |
| Player struct | `struct player` | `struct player` | `bondview.h:334` |
| Player fields (pos/aim/move/crouch/lean/health) | various | `pos` (coord3d), `vv_theta` (yaw), `vv_verta` (pitch), `speedforwards`/`speedsideways`, `crouchpos`, `swaytarget`, `bondhealth`/`bondarmour` (normalized), `bonddead`, `hands[2].weaponnum`, crosshair pos | `struct player`, `bondview.h:334` |
| Current-player select | `g_Vars.currentplayernum`, `setCurrentPlayerNum()` | `get_cur_playernum()`, `set_current_player*()` | many callsites |
| Live player count | `g_MpSetup.chrslots` | `getPlayerCount()` / `get_selected_num_players()` | `player_2.c` / `front.c:5306` |
| Coord / matrix | `struct coord` / `Mtxf` | `coord3d` (`vec3d`) / `Mtxf` | `bondtypes.h:233` / `:106` |
| RNG seed | `g_RngSeed`, `g_Rng2Seed` (two) | `g_randomSeed` (**one** `u64`); `randomGetNext()`/`randomSetSeed()` | `random.c:24` / `:116` / `:183` |
| Frame-advance count | `g_Vars.diffframe60` | `speedgraphframes` / `g_ClockTimer` | `lvl.c:1977` |
| Frame pump site | `schedStartFrame`/`schedEndFrame` (`pdsched.c:242/281`) | `boss.c` mainloop: `joyPoll`(`:568`) → `joyConsumeSamplesWrapper`(`:572`) → `lvlManageMpGame`(`:602`) → `lvlRender`(`:629`) — **single-threaded** | `boss.c:567-629` |
| Player input read | `pl->ucmd` (unified) | `joyGetButtons(p,mask)` / `joyGetStickX/Y` / `joyGetButtonsPressedThisFrame` | `joy.c:876` / `:827` / `:887` |
| Button constants | `UCMD_*` | `Z_TRIG`(0x2000)/`R_TRIG`(0x0010)/`A_BUTTON`(0x8000)/`B_BUTTON`/C-buttons; `ANY_BUTTON`(0xFFFF) | `platform_os.h:183` |
| Player move/aim apply | (in PD player update) | `bondviewMovePlayerUpdateViewport()` → `MoveBond()`; aim `platformGetPadRightStick`→`vv_theta`/`vv_verta` | `bondview.c:14296`/`:12474` (gate at MoveBond call `:14485`); `lvl.c:5736/5740`/`:5769` (angle writes `:5842-5847`) |
| Damage / death | (server applies) | `record_damage_kills()` (player damage only); `bondviewKillCurrentPlayer()` | `bondview.c:20737` / `:20676` |
| Projectile spawn | `SVC_PROP_SPAWN` site | `projectileAllocate()` / `projectileReset()` | `chrobjhandler.c:2350` / `:2316` |
| Pickup | `SVC_PROP_PICKUP` site | `object_interaction()`; `g_WeaponSlots[30]`/`g_AmmoCrates[20]` | `chrobjhandler.c:9032` (8874 is dead `#if 0`); `chrprop.c:393`/`:403` |
| Door / lift | `SVC_PROP_DOOR/USE` site | `propdoorInteract()` (called from `chrprop.c:5295`) | def `chrobjhandler.c:48070` |
| Explosion | (spawn/damage) | `explosionCreate()`; `g_ExplosionBuffer` | `explosions.c:873` / `:671` |
| MP stage table | (PD mpsetups) | `multi_stage_setups[]` (`struct mp_stage_setup`) | `front.c:1123`; `front.h:70` |
| MP active state | `g_MpSetup` | `MP_stage_selected`/`scenario`/`selected_num_players`/`selected_stage`; `get_scenario()` | `front.c:1322`; `get_selected_num_players()` `:5306`; `get_scenario()` `:7705` |
| Stage load / teardown | `mainChangeToStage`/`mainEndStage` | `pc_apply_mp_selection()` → `bossSetLoadedStage()` (`g_MainStageNum`) — **NOT** `MENU_RUN_STAGE` (§4h) | `initmenus.c:263`; `boss.c:779` |
| Config registration | `configRegisterUInt("Net.*")` | `settings.c` / `config_pc.c` runtime-override system | `settings.c` |
| CLI args | `sysArgGetInt("--port"/"--connect"/"--host")` | `main_pc.c` (joins `--multiplayer`/`--players`/`--mp-stage`) | `main_pc.c` |
| Endian helpers | `PD_LE16/32/64` | **PD `netbuf`'s own LE codec — NOT `byteswap.h`** | — (`byteswap.h` is a **big-endian-ROM→host** converter; using it for an LE netbuf swaps the wrong way. PD's `netbuf.c` carries its own LE read/write — port that. Review §3.4.) |
| Logging | `sysLogPrintf` | GE logging / `port_trace` | `port_trace.c` |

### The two structural gaps to build

1. **`PropRecord.syncid`** — add a `u16`; assign `(rec - pos_data_entry) + 1` at
   stage start (port of `netSyncIdsAllocate`, `net.c:1004`). Walk the active list
   via `ptr_obj_pos_list_current_entry` (`chrprop.c:468`).
2. **The `ucmd` adapter** — no GoldenEye equivalent of PD's unified command
   bitmask. The keystone (§4d).

---

## 4. Integration points (where MGB64 code changes)

### 4a. Frame pump — `src/boss.c`
Mirror `pdsched.c:242/281`. In `bossMainloop` (single-threaded — `boss.c:568`
confirms `joyPoll()` is called explicitly on PC, so a network poll here needs no
locks): call **`netStartFrame()` before** `lvlManageMpGame()` (`boss.c:602`) and
**`netEndFrame()` after**, gated on the frame advancing (`g_ClockTimer > 0`).
Optionally pin `g_ClockTimer ≡ 1` in netplay (cheap; mainly helps the §10
rollback path — not load-bearing for the puppet+event baseline).

### 4b. Player↔client allocation — after MP players spawn
Port `netPlayersAllocate()` (`net.c:954`): bind clients to `g_playerPointers[]`,
set the puppet flag (§4c) for remote players, apply their name/skin. Call after
`init_player_data_ptrs_construct_viewports` (`player_2.c:68`).

### 4c. The puppet gate — *new* per-player flag
GoldenEye has no `CONTROLMODE_NA`. Add `g_NetPuppet[playernum]` and gate at the
**`MoveBond()` call site** (`bondview.c:14485`, inside
`bondviewMovePlayerUpdateViewport()` **`bondview.c:14296`**): for a puppet, **skip**
`joyGetStickX/Y` (`lvl.c:5736/5740`) and `platformGetPadRightStick` (`lvl.c:5769`),
**do not call `MoveBond()`/`bondviewProcessInput`** (`MoveBond` def **`bondview.c:12474`**),
and drive actions from the decoded `ucmd` (§4d).

> **⚠ Corrected (review §3 / §4.4):** you **cannot** place a puppet by writing the
> `struct player.pos` field — that is the **camera/eye** position. The authoritative
> world position is **`prop->pos`** mirrored to **`field_488.collision_position`**
> (`bondview.c:5289-5291`), and `MoveBond` re-derives position from speed+collision
> and would overwrite a naive `pos` write. The puppet must write **both `prop->pos`
> and `collision_position`** (+ tile/room pointers) and bypass `MoveBond` entirely.
> The flag must **also** gate the viewport/divider/gfx-budget loops, not just the
> input read (these iterate `getPlayerCount()` *outside* this function — review §4.4).

### 4d. The `ucmd` adapter — keystone
Two-way mapping between GoldenEye input/state and the wire `ucmd` (`net.h:46`):

| `UCMD_*` | Local encode (from `joyGetButtons`, `bondview.h` state) | Puppet decode (apply) |
|---|---|---|
| `FIRE` | `Z_TRIG` (0x2000) held | trigger weapon fire |
| `AIMMODE` | `R_TRIG` (0x0010) held | enter aim/sight mode |
| `ACTIVATE` | action/use button | use/activate |
| `RELOAD` | reload input | reload |
| `SELECT`/`SELECT_DUAL` | weapon-cycle / dual input → set `weaponnum` | switch `hands[].weaponnum` |
| `DUCK`/`SQUAT` | `crouchpos` state | set `crouchpos` |
| `SECONDARY` | secondary-fire active | secondary fire |
| analog `leanofs`/`crouchofs`/angles/crosspos | read from `struct player` | apply to puppet |

Build + unit-test this before N2b. It is where most "field doesn't exist"
friction lives (~40-line rewrite of `netClientRecordMove`, `net.c:185`).

### 4e. Move record/apply + interpolation
- **Record** (local): rewrite `netClientRecordMove` (`net.c:185`) to read
  GoldenEye `struct player` fields (`pos`, `vv_theta/verta`, `speedforwards/
  sideways`, `crouchpos`, `swaytarget`, crosshair) + the encoded `ucmd`.
- **Apply** (puppet): new code in the per-player update — set pos/angles from
  `inmove`, lerp over `g_NetInterpTicks`, fire the decoded `ucmd` actions.
  Force-tick handling per `netmsg.c:275`.

### 4f. Server-authoritative hit & damage
The **host** applies damage through GoldenEye's path — wrap `record_damage_kills()`
(**`bondview.c:20737`**) / `bondviewKillCurrentPlayer()` (**`bondview.c:20676`**) to
emit `SVC_CHR_DAMAGE` + kill/score events. Clients never self-report kills.

> **⚠ Corrected (review §3.1) — "shooter-detects-hit + host-validates" is now the
> PRIMARY model, not a fallback.** The host **cannot** reproduce the shot from
> forwarded `vv_theta`/`vv_verta`: the live path `bullet_path_from_screen_center`
> (`gun.c:28756`) folds in auto-aim crosshair deflection (`bondview.c:12178-12202`),
> **4× `RANDOMFRAC()` spread draws** (`gun.c:28789-28795`), firer movement, and
> per-peer FOV. The client forwards the **resolved** bullet vector / candidate hit +
> target syncid; the host validates and applies. **Thread a firer id through
> `ShotData`** (`chrobjhandler.h:36` has none) and `set_cur_player` to the firer
> during resolution — credit currently rides implicit `get_cur_playernum()` context
> (`chr.c:9784`, `explosions.c:1423-1437`). Note `record_damage_kills` centralizes
> **player** damage only; **NPC kills bypass it** via `self->damage += …`
> (`chrlv.c:3509`) — adequate for human-only deathmatch, not co-op.

Health is normalized (`bondhealth`/`bondarmour` 0..1, scaled by per-player
`actual_health` **and `actual_armor`** — the wire must carry **both** scales,
`bondview.c:20772/20803`); `SVC_PLAYER_STATS` syncs the normalized values. Define
**pickup-vs-damage ordering** within a tick.

### 4g. World-event hooks — the correctness long tail
The **host** calls the matching `netmsg…Write` at these grounded call sites; the
**client** read-handlers apply them (PD's known-issues list = which are fiddly):

| Event | PD message | GoldenEye call site to wrap |
|---|---|---|
| Projectile created (grenade/rocket/mine) | `SVC_PROP_SPAWN` (`netmsg.c:912`) | `projectileAllocate()` `chrobjhandler.c:2350` (owner as id, `:34074`) |
| Projectile/object in flight | `SVC_PROP_MOVE` (`netmsg.c:781`) | projectile update tick |
| Damage / kill | `SVC_CHR_DAMAGE` (`netmsg.c:1397`) | `record_damage_kills()` `bondview.c:20737` |
| Weapon dropped on death | `SVC_CHR_DISARM` (`netmsg.c:1485`) | `drop_inventory()` `chrobjhandler.c:48607` (kill path `bondview.c:20834`, **`#if VERSION_US`** — un-gate for MP flag/GG transfer) |
| Pickup (weapon/ammo/armor) | `SVC_PROP_PICKUP` (`netmsg.c:1197`) | `object_interaction()` `chrobjhandler.c:9032` |
| Door / lift | `SVC_PROP_DOOR/USE` (`netmsg.c:1278/1230`) | `propdoorInteract()` `chrobjhandler.c:48070` (caller `chrprop.c:5295`) |
| Explosion | (via spawn/damage) | `explosionCreate()` `explosions.c:873`; splash `explosionInflictDamage()` `:1097` (guard `playerid==-1` `bondview.c:20795`) |
| **Mine arm/detonate** | (new) | owner `chrobjhandler.c:34382`; global detonator flag `:44257`; proximity vs all players `:7652-7693` |
| **Flag / Golden Gun owner** | `SVC_FLAG_*` / `SVC_GG_OWNER` (new) | flag `lvl.c:5916-5940`; GG `:5943-5951`; bonus `gun.c:33454`; respawn suppressed `prop.c:377-384` |
| Health/armor | `SVC_PLAYER_STATS` (`netmsg.c:664`) | periodic + on-change |
| Match start/end | `SVC_STAGE_START/END` (`netmsg.c:410/568`) | stage flow (§4h) + `mp_watch.c` scoreboard |

### 4h. Session/stage flow — `front.c` + `initmenus.c` + `boss.c`
Port the host-drives-stage handshake:
1. Host configures the match in the MP menu (`multi_stage_setups[]` `front.c:1123`,
   `scenario` `front.c:1322`, player count). **Replicate the full match-setup
   payload, not just scenario+count:** team assignment (`have_token_or_goldengun`
   per player, `front.c:7910`), MP **weapon set** (`mp_weapon.c:218`), time/point
   limits — review §4.3.
2. Host load via `pc_apply_mp_selection()` (`initmenus.c:263`) → `bossSetLoadedStage()`
   (`boss.c:779`), and broadcast `SVC_STAGE_START`. Clients load the **same**
   stage/scenario.
   > **⚠ Corrected:** `pc_apply_mp_selection` does **not** call
   > `frontChangeMenu(MENU_RUN_STAGE)` — it routes `MENU_MP_OPTIONS` as the return
   > menu. Both the direct-boot and the normal-frontend (`init_menu0B_runstage`,
   > `front.c:8813`) paths converge on **`bossSetLoadedStage`/`g_MainStageNum`**,
   > which is the real load trigger the boss loop polls.
3. After load: `netSyncIdsAllocate` (§3.1) + `netPlayersAllocate` (§4b) → `GAME`.
   The clean barrier is **`boss.c:494`** (after `lvlStageLoad`, which is synchronous
   — no frame yields).
4. Match end → host runs `mp_watch.c` scoreboard authoritatively, `SVC_STAGE_END`
   → all peers return to lobby; repeat.

### 4i. Config + CLI + lobby UI
Port `configRegister*("Net.*")` (`net.c:1114`) into `settings.c`; `--host/
--connect/--port/--maxclients` (`net.c:421`) into `main_pc.c`; reimplement
`netmenu.c` host/join against the MP menu FSM (`menu_init` `front.c:14273`).
Replace PD's filename-only ROM check (`netmsg.c:197`) with build-hash + region.

---

## 5. Port order (maps to NETPLAY_PLAN phases)

1. **enet + `netbuf.[ch]` + `net.h` types + frame-pump skeleton + connection FSM
   + loopback.** No gameplay; loopback connect/disconnect, `g_NetTick` advancing.
   → *plan N0.*
2. **Auth/lobby:** `CLC_AUTH`/`SVC_AUTH`, settings, build-hash handshake, chat.
   → *plan N1.*
3. **Player movement:** `ucmd` adapter (§4d), `netClientRecordMove` rewrite
   (§4e), `CLC_MOVE`/`SVC_PLAYER_MOVE`, `netPlayersAllocate` + puppet gate (§4c),
   apply+interp. → **bodies move.** *plan N2.*
4. **Props:** `syncid` field + `netSyncIdsAllocate` + `SVC_PROP_SPAWN/MOVE`.
   → *plan N3a.*
5. **Event layer:** damage/kills (§4f), pickups, doors, explosions, stats, RNG
   push, desync oracle. → **matches correct.** *plan N3b–d.*
6. **Polish + internet:** interp tuning, `UpdateFrames`, F9 stats, server query,
   relay/NAT. → *plan N4–N5.*

Layer the **event-scoped desync hash** (plan §3f) over steps 4–5.

---

## 6. Direct-port vs rewrite, at a glance

| Component | Action | Why |
|---|---|---|
| `netbuf.[ch]`, enet, `netenet.h` | **port/vendor verbatim** | engine-agnostic; only deps are `byteswap.h` + `coord3d`/`Mtxf`. |
| `net.c` transport/FSM/query/config/send | **port, light edits** | enet calls, conn state, parse-addr are engine-agnostic. |
| `net.c` move-record / player-alloc / syncid-alloc | **rewrite** | touch `struct player` / `PropRecord` (§3 gaps). |
| `netmsg.c` framing + read/write scaffolding | **port** | `netbuf` call patterns transfer directly. |
| `netmsg.c` apply-to-engine bodies | **rewrite** | each applies to GoldenEye structs / call sites (§4g). |
| `netmenu.c` | **reimplement** | against `front.c` MP menu. |

---

## 7. Gotchas & deviations

- **`ucmd` is the keystone** (§4d) — build/test first.
- **No `CONTROLMODE_NA`** — must add the `g_NetPuppet` gate (§4c).
- **One RNG stream** — collapse PD's `g_NetRngSeeds[2]` to GoldenEye's single
  `g_randomSeed`.
- **Normalized health** — sync `bondhealth`/`bondarmour` (0..1) + the
  `actual_health` scale, not raw HP.
- **Linear sync-ID lookup** scans `pos_data_entry` (PD's `netmsg.c:138` flags
  `// TODO: make a map`); fine at 600 props, add a map only if profiled.
- **Client-authoritative movement is cheatable** — accepted v1; kills are
  host-resolved (§4f) so scores can't be forged. Server-auth movement is plan §10.
- **Bots not synced** in PD — irrelevant; GoldenEye MP is human-only.
- **Per-weapon specials** (remote/proximity/timed mines, throwing, auto-aim) are
  PD's known-issue class — verify each through the event layer, not movement.
- **Door line** — `propdoorInteract()` is in `chrobjhandler.c` (called from
  `chrprop.c:5294`); confirm the definition line during implementation.

---

## 8. Bottom line

The MIT-licensed PD port hands us a complete, same-engine-family netcode to
**adapt, not invent**. Transport (`netbuf` + enet), protocol (`SVC_*`/`CLC_*`),
the tick/connection model, and the sync-ID scheme port directly. The real work is
the **~30% engine coupling** — the `ucmd` adapter, the puppet gate, the move
record/apply against `struct player`, the `syncid` field, and wrapping
GoldenEye's grounded damage/pickup/door/projectile call sites. Human-only MP lets
us skip PD's worst unsolved problem. Build the event-scoped desync oracle
alongside and we exceed the reference's quality.

## References
- PD netcode (port-net): [`net.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/net.c) · [`netmsg.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/netmsg.c) · [`net.h`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/include/net/net.h) · [`netbuf.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/netbuf.c) · [`netplay.md`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/docs/netplay.md)
- Internal: [NETPLAY_PLAN.md](NETPLAY_PLAN.md), [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md)
