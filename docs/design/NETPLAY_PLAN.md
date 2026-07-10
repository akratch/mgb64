# Online Multiplayer (Netplay) — Execution Plan

GoldenEye 007's split-screen is the defining feature; **online** split-screen is
the feature that turns this port from "the GoldenEye people remember" into "the
GoldenEye people never got to have." The solo campaign plays start-to-finish and
local 2–4-player split-screen is wired and validated
([MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md)). This document is the design of
record for taking that simulation online.

> **Status:** planning. No netcode exists yet (`grep -rE 'recvfrom|sendto|enet_|netplay' src/` → 0 hits).
>
> **Companion:** [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md) — the file-by-file map
> of the MIT-licensed Perfect Dark PC port netcode into `src/platform/net/`, with
> every GoldenEye symbol grounded to a real `file:line`. This plan is the *what
> and why*; the port map is the *where and how*.

> **⚠ Revision (2026-06-25):** A source-grounded adversarial review
> confirmed the architecture is
> the right call but **refuted three load-bearing assumptions** that are corrected
> inline below: (1) the host **cannot** replay a client's shot from raw
> `vv_theta`/`vv_verta` and **RNG is correctness-bearing for hitscan** (§3d/§3e/§7)
> — the "shooter-detects-hit + host-validates" model is now **primary**; (2)
> single-process loopback **cannot** test host↔client divergence (sim state is
> file-scope singletons) — N0c is re-scoped and a **two-process** `net_smoke.sh` is
> the real correctness gate; (3) the §10 rollback budget is understated. The review
> also flags **blocker-class scope gaps** the plan understates: **6 of 8 MP
> scenarios** carry per-scenario state the wire never replicates (§4 / review §4.3),
> "online split-screen" is blocked by `getPlayerCount()` coupling (review §4.4), and
> lifecycle (pause/disconnect/results/names) + packet memory-safety (N5) are
> design-absent. **Line numbers throughout this doc are stale; the corrected-anchor
> table lives in the review (§5).** Read the review before acting on this plan.

---

## 1. The decision

**Build the Perfect Dark port's proven model: a host-authoritative session with
client-authoritative player movement, interpolated remote puppets, and a
server-authoritative discrete event layer.** Adapt it from the MIT-licensed PD
source (§11). Concretely, four mechanisms:

1. **Client-authoritative movement.** Each peer simulates only *its own* player
   and sends the result — position, view angles, analog crouch/lean, and a
   `ucmd` command bitmask — to the host, which relays it to everyone.
2. **Remote players are interpolated puppets.** On every machine, the other
   players are normal engine players with **controller input disabled**, their
   pawns positioned from received moves and **lerped** between the last two over
   `Net.LerpTicks`. They are not simulated from injected input.
3. **Server-authoritative event layer.** Everything that isn't a player body —
   damage/kills, projectile spawns, pickups, doors, scores — is synced by
   explicit host→client messages keyed by a **sync-ID equal to the object's
   array index**. The host is the source of truth for the world.
4. **Host pushes the RNG seed** so cosmetic/spawn randomness matches.

Transport is **enet** (UDP + selective reliability). GoldenEye MP caps at **4
players**; "online split-screen" = up to 2 boxes each running 1–2 *local* couch
players plus remote puppets.

### Why this model (and not the alternatives)

| Candidate | Verdict | Reason |
|---|---|---|
| **Deterministic lockstep / rollback** | ❌ v1 (✅ post-v1 LAN, §10) | Sim is variable-timestep and float-scaled: `g_ClockTimer ∈ [1,4]` → `f32 g_GlobalTimerDelta` (`lvl.c:1977-1998`), `pos += 10.0f*lvupdate` (`explosions.c:1539`); RNG/clock seeded from wall-clock (`boss.c:390`). Bit-identical cross-machine floats (x86 ↔ Apple-Silicon) are a research wall. Emulator rollback for GE [caps at 2P](https://www.xda-developers.com/someone-just-added-rollback-netcode-to-an-n64-emulator-making-goldeneye-actually-playable-online/). |
| **Thin client (render from replicated state)** | ❌ | Must replicate *everything the renderer reads* — anim frames, muzzle flashes, effects. Maximizes the sync surface. |
| **Full-sim clients + reconciliation** | ❌ v1 (✅ post-v1, §10) | Theoretically cleaner and cheat-resistant, but requires server-side pawn simulation + client prediction the engine isn't structured for. Higher risk, slower to first playable. |
| **PD puppet + event model** | ✅ **v1** | Proven shipping on this exact engine family (PD port, 8P). Light wire surface. Robust to non-determinism. Gives **cross-platform play** (macOS/ARM ↔ x86) lockstep can't. |

**The deciding precedent:** Perfect Dark is built on GoldenEye's engine, and the
[fgsfdsfgs PD PC port](https://github.com/fgsfdsfgs/perfect_dark) shipped this
model for 8 players. We inherit a working design *and* its hard-won bug list
(see §7). GoldenEye being **human-only** lets us skip PD's single biggest
unsolved area — bot/simulant sync — entirely.

> **Honest tradeoff:** client-authoritative movement is trivially cheatable and
> causes minor position edge-cases. That is an accepted v1 cost for a
> preservation/party port; the hardening path to server-authoritative movement is
> §10.

---

## 2. Architecture in one picture

```
        CLIENT  (player N)                         HOST  (authoritative)
  ┌───────────────────────────┐             ┌──────────────────────────────┐
  │ read local pad/kbm        │             │ run full MP sim (lvlManageMp- │
  │ simulate MY player        │  CLC_MOVE   │   Game) for LOCAL players     │
  │ (move/aim/fire)           │ ──pos+ucmd─▶│ apply remote clients' moves   │
  │                           │             │   onto their puppets          │
  │ apply OTHER players as     │◀─SVC_PLAYER_│ resolve hitscan/damage from   │
  │   puppets (lerp)          │   MOVE      │   forwarded fire+aim          │
  │ apply world events        │◀─SVC_PROP_*─│ emit world events (damage,    │
  │ (damage/spawn/pickup/door)│  SVC_CHR_*  │   spawn, pickup, door, stats) │
  │ adopt host RNG seed       │◀─RNG seed───│ own RNG, scoreboard, match end│
  └───────────────────────────┘             └──────────────────────────────┘
            enet: NETCHAN_DEFAULT (game, mixed)  +  NETCHAN_CONTROL (reliable)
```

**Master clock:** `g_NetTick`, a per-peer `u32` incremented once per advancing
game frame. It is *not* a globally-agreed lockstep counter — moves carry their
own tick and interpolation works on tick deltas. The host's tick is canonical
for event ordering.

**The split-screen seam is for local couch players only.** The existing
`osContGetReadData` `data[0..3]` routing (func **`stubs.c:5000`**, local merge
`:5426`, multi-pad fill loop `:5436`) feeds a host's *local* players. Remote
players are puppets driven by the move-apply/interp path (§3b) — never by injected
`OSContPad` bytes. Keep the two paths separate.

> **⚠ Corrected (review §4.4):** "online split-screen" is **blocked** by an engine
> coupling this plan reduces to "a puppet flag." `getPlayerCount()` (`player_2.c:92`)
> is the **single** axis for match-players **and** local viewports **and** gfx budget
> **and** divider geometry (`lvl.c:1564`, `fr.c:1779-1806`, `dyn.c:106`, `boss.c:607`).
> Binding all 4 match-players into `g_playerPointers[]` makes a 2-local box render
> **4 viewports / 4-way split / 4-player budget.** Required: a **second count**
> `getLocalViewportCount()` and an audit of every `getPlayerCount()` callsite
> (match-semantics → keep; local-presentation → switch). Input is keyed by player
> number not local-slot (`lvl.c:5697-5769`, `bondview.c:10839` even adds
> `+getPlayerCount()`) — define a `{playernum → local pad}` map, and make
> move-record loop over **all** local players (the "its own player" singular below is
> wrong for 2 couch players).

---

## 3. The keystone pieces (grounded)

These six items are the real engine work. Everything else is transport/protocol
that ports near-verbatim from PD (port map §6).

### 3a. The `ucmd` adapter — *the single most important new code*
PD threads a unified per-player command bitmask (`ucmd`, `net.h` `UCMD_*`)
through the sim; **GoldenEye has no such field** — it reads `joyGetButtons()`
ad hoc (`joy.c:876`). Build a two-way adapter:
- **Encode** (local player → wire): derive `ucmd` from `joyGetButtons(p, …)`
  using the verified button constants (`platform_os.h:183`): `Z_TRIG`(0x2000)→
  FIRE, `R_TRIG`(0x0010)→AIMMODE, `B_BUTTON`→ACTIVATE/RELOAD, C-buttons/weapon
  cycle→SELECT, plus `crouchpos`→DUCK/SQUAT.
- **Decode** (wire → puppet): apply the actions a remote puppet must reproduce —
  fire, reload, aim-mode, weapon switch (`hands[].weaponnum`), crouch.

Build and unit-test this first; phases N2–N4 depend on it.

### 3b. The puppet gate — disable input for remote players
GoldenEye has **no `controlmode == CONTROLMODE_NA` equivalent** (PD's mechanism).
Add a per-player `g_NetPuppet[playernum]` flag and gate at the **`MoveBond()` call
site** (`bondview.c:14485`, inside `bondviewMovePlayerUpdateViewport()`
`bondview.c:14296`, called from `lvl.c:5736/5740`): for puppets, **skip** the
`joyGetStickX/Y` (`lvl.c:5736/5740`) + `platformGetPadRightStick` (`lvl.c:5769`)
reads, **do not call `MoveBond()`** (`bondview.c:12474`), and **set** the
authoritative **`prop->pos` *and* `field_488.collision_position`** (+ tile/room) +
`vv_theta`/`vv_verta`/`crouchpos` from the interpolated network move. The local
player keeps the normal path.

> **⚠ Corrected (review §3 / port-map §4c):** you cannot place a puppet by writing
> the `struct player.pos` (camera) field — `MoveBond` re-derives position from
> speed+collision and overwrites it. Write `prop->pos` + the collision twin and
> bypass `MoveBond`. The flag must also gate the viewport/divider/gfx-budget loops
> (review §4.4), which live *outside* this function.

### 3c. Sync-ID = prop array index
`PropRecord` lives in the fixed array `pos_data_entry[POS_DATA_ENTRY_LEN]`
(`chrprop.c:147`) on a doubly-linked active list
(`ptr_obj_pos_list_current_entry`, `chrprop.c:468`). Add a `u16 syncid` field to
`PropRecord` and assign `syncid = (rec - pos_data_entry) + 1` at stage start.
The wire references objects by `syncid`; the receiver resolves it back by index.
Never put a raw pointer on the wire.

### 3d. Server-authoritative hit & damage resolution
When a client's `ucmd` carries FIRE, the **host** applies damage through
GoldenEye's existing path — `record_damage_kills()` (**`bondview.c:20737`**), death
via `bondviewKillCurrentPlayer()` (**`bondview.c:20676`**). That call site is
**wrapped to emit `SVC_CHR_DAMAGE`/kill events**. Clients never self-report
kills; this is both correct and the main anti-cheat for scoring.

> **⚠ Corrected (review §3.1):** the host **cannot** reproduce the shot from raw
> forwarded `vv_theta`/`vv_verta`. The live shot path
> `bullet_path_from_screen_center` (`gun.c:28756`) folds in the **auto-aim-deflected
> crosshair** (`bondview.c:12178-12202`, a 25–30-tick target latch), **four
> `RANDOMFRAC()` spread draws** from `g_randomSeed` per shot (`gun.c:28789-28795`),
> the firer's movement, and the shooter's per-peer **FOV** — none in the raw angles.
> Kill credit is also implicit `get_cur_playernum()` context (`ShotData` has no
> firer id, `chrobjhandler.h:36`). **Primary model = "shooter-detects-hit +
> host-validates":** the client forwards the **resolved** bullet vector / candidate
> hit + target syncid; the host validates against authoritative state, applies
> damage with `set_cur_player` set to the **firer**, and emits the event. Thread an
> explicit firer id through `ShotData`.

> GoldenEye health is **normalized** `bondhealth`/`bondarmour` (0..1) scaled by
> per-player `actual_health` **and `actual_armor`** (the paired scale; the wire must
> carry **both** to reconstruct real HP — `bondview.c:20772/20803`). `SVC_PLAYER_STATS`
> syncs the normalized values; the scale is set at spawn from the synced
> scenario/handicap. **Define pickup-vs-damage ordering within a tick** (review §4.5).

### 3e. RNG push
GoldenEye has **one** global seed `g_randomSeed` (`random.c:24`) — not PD's two.
The host sends its seed; clients latch it.

> **⚠ Corrected (review §3.1):** RNG is **not** cosmetic-only. Weapon spread draws
> `g_randomSeed` (4× per shot, `gun.c:28789-28795`), so it sets bullet direction
> and is **correctness-bearing for hitscan**. Because the primary hit model forwards
> the **resolved** vector (§3d), the host re-runs no spread RNG for shot geometry —
> which is precisely why the resolved-vector model is now primary. The RNG push
> remains useful for spawn/cosmetic parity, but the §7 risk-table entry "cross-
> platform float drift in cosmetic RNG → irrelevant to correctness" is **withdrawn**
> for the shooting path.

### 3f. Desync oracle — scoped correctly
Hash and exchange the **server-authoritative subset only** — prop states by
syncid, scores (`g_playerPlayerData`, `player.h:83`), door states, RNG seed —
**not** player positions (those legitimately differ under client-auth movement +
interpolation). A mismatch logs the tick and the first divergent object. PD
shipped without this and paid for it in its known-issues list; we build it from
N3 onward so the long tail (§7) becomes a fix-list instead of bug reports.

---

## 4. What's on the wire (v1)

Two kinds of traffic — **not** a 85 KB state snapshot (that's the §10 rollback
path, deliberately excluded here):

**Per-player move** (`netplayermove`, every ~tick per active player): tick,
`ucmd`, analog lean/crouch, view angles, crosshair, selected weapon, position —
**~50 B/player**, sent unreliable, upgraded to reliable when "important" bits
change (fire/reload/aim/select/activate). The host relays each client's move to
all others.

**Discrete world events** (only when they happen), each keyed by `syncid`:

| Event | Message | GoldenEye call site to wrap |
|---|---|---|
| Damage / kill | `SVC_CHR_DAMAGE` | `record_damage_kills()` `bondview.c:20737` |
| Weapon dropped on death | `SVC_CHR_DISARM` | `drop_inventory()` `chrobjhandler.c:48607` (kill path `bondview.c:20834`, `#if VERSION_US`) |
| Projectile spawned | `SVC_PROP_SPAWN` | `projectileAllocate()` `chrobjhandler.c:2350` |
| Projectile/object in flight | `SVC_PROP_MOVE` | projectile update tick |
| Pickup (weapon/ammo/armor) | `SVC_PROP_PICKUP` | `object_interaction()` `chrobjhandler.c:9032` |
| Door / lift | `SVC_PROP_DOOR/USE` | `propdoorInteract()` (`chrobjhandler.c:48070`) |
| Explosion | (via spawn/damage) | `explosionCreate()` `explosions.c:873` |
| Health/armor | `SVC_PLAYER_STATS` | periodic + on-change |
| Scoreboard / match end | `SVC_STAGE_END` + stats | `mp_watch.c` |

This event set *is* the correctness surface. Getting every weapon/door/pickup
path wrapped is the bulk of the work — see the risk register (§7).

> **⚠ Scope gap (review §4.3):** the table above covers **deathmatch
> (kill-count) only**. GoldenEye has **8 MP scenarios**, and
> `get_points_for_mp_player` (`mp_watch.c:791-842`) dispatches **four** scoring
> models off scenario-specific state that **no event above replicates**: Flag Tag
> `flag_counter`/possession (`lvl.c:5916-5940`), Golden Gun ownership +
> `killed_gg_owner_count` bonus (`lvl.c:5943`, `gun.c:33454`), YOLT lives/elimination
> (`lvl.c:2187-2777`, itself `GE007_MP_YOLT`-gated at 63% decomp), and team
> assignment (overloaded `have_token_or_goldengun`, `front.c:7910`). Also missing:
> the synced **MP weapon set** (`mp_weapon.c:218`) and `drop_inventory` on death —
> the only flag/GG transfer — which is `#if defined(VERSION_US)` (`bondview.c:20834`).
> **Decision:** either add per-scenario replication to N3 (feed
> `get_points_for_mp_player` from authoritative fields) **or** narrow v1's DoD to
> deathmatch-only and schedule scenarios as N3.5. Add new events: `SVC_FLAG_*`,
> `SVC_GG_OWNER`, team/lives/weapon-set in the synced match-setup payload.

---

## 5. Session lifecycle

The concrete connect→play→repeat flow, grounded in the GoldenEye menu/stage path:

1. **Connect & auth.** Client connects (enet), sends `CLC_AUTH` with name +
   build/ROM identity; host validates against its own and admits to `LOBBY`
   (state machine `CLSTATE_*`). Late joins rejected once in-game.
2. **Lobby.** Host configures the match in the existing MP menu — stage
   (`multi_stage_setups[]` `front.c:1123`), scenario (`scenario` `front.c:1322`,
   the GE DM variants), player count. Clients wait; chat available.
3. **Stage start.** Host triggers the load via the existing
   `pc_apply_mp_selection()` → `bossSetLoadedStage()` (`boss.c:779`) path and
   broadcasts `SVC_STAGE_START`. Every peer loads the **same** stage/scenario.
4. **Allocate.** After load: assign sync-IDs to all props (§3c) and bind
   clients↔`g_playerPointers[]`, flagging remote players as puppets (§3b). Enter
   `GAME`; `g_NetTick` begins.
5. **Play.** Per advancing frame: `netStartFrame()` (pump enet, apply inbound) →
   `lvlManageMpGame()` (`boss.c:602`) → `netEndFrame()` (record local move, emit
   events, flush). Single-threaded, no locks (§7).
6. **End.** Match hits the limit → host runs `mp_watch.c` scoreboard
   authoritatively, broadcasts `SVC_STAGE_END`; everyone returns to lobby and the
   process repeats from step 2.

> **⚠ Lifecycle contracts missing (review §4.6) — design these in N3:**
> - **Pause** is a global singleton that zeroes `g_ClockTimer` (`lvl.c:2023-2030`,
>   `mp_watch.c:622-636`): any peer pausing freezes only its own sim and desyncs.
>   Disable online, or make host-only + broadcast.
> - **Disconnect:** fixed `g_playerPointers[]` slots mean "vanish" breaks viewport
>   math + scoreboard, "leave the body" leaves a frozen puppet that still gates
>   elimination. Keep the slot, mark "disconnected puppet," preserve score, exclude
>   from elimination/continue counts.
> - **Results-advance** gate `menu_count == player_count` (`mp_watch.c:649-665`)
>   **deadlocks** on a dropped/AFK peer — make host-authoritative with ready-acks +
>   timeout.
> - **Names/chat:** no text-entry UI exists (zero `SDL_StartTextInput`); players are
>   P1–P4 by character photo (`front.c:6371`). Derive identity from the chosen
>   character or budget real SDL text-input UI — **not** an "S" task.
> - **FOV is not cosmetic for targeting** (`bondview.c:1147`): force a canonical
>   targeting FOV online or forward the resolved target.
> - **Host-leave (v1):** match ends for all, clean `SVC_STAGE_END`, no migration —
>   state it. Add a short reconnect grace window keyed by stable peer identity.

---

## 6. Phased execution plan

Effort S/M/L/XL. Every milestone extends the existing lanes (`mp_smoke.sh`,
`soak_stability.sh`, `asan_smoke.sh`); netcode that isn't CI-gated isn't done.
Phases map 1:1 to the port map's port order (§5 there).

### N0 — Transport + loopback harness
| Task | Effort | Detail |
|---|---|---|
| N0a. Vendor enet + port `netbuf.[ch]` | M | enet (MIT) under `lib/enet/`; LE byte-buffer serializer (`src/platform/net/netbuf.[ch]`). |
| N0b. Frame pump + connection FSM | M | `netStartFrame`/`netEndFrame` in `boss.c` around `lvlManageMpGame` (`boss.c:602`); `CLSTATE_*` machine; enet host create/service (non-blocking, §7). |
| N0c. Loopback mode (transport only) | M | `--netplay loopback`: in-process **transport/serialization round-trip** only — see warning. |
| N0d. **Two-process** `net_smoke.sh` | M | **The real correctness lane.** Launch host + client(s) as separate OS processes over `127.0.0.1`, per-process scripted input, inter-process event-hash diff. |

> **⚠ Corrected (review §3.2):** in-process loopback **cannot** test host↔client
> divergence — sim state is file-scope singletons (`pos_data_entry[600]`,
> `g_playerPointers[4]`, `g_playerPlayerData[4]`, `g_randomSeed`: one per process),
> so two in-process peers are the **same** simulation and the desync oracle hashes
> identical memory. PD itself forbids dual-role-in-one-process. Loopback is a
> transport/serialization round-trip lane; the **two-process** lane (N0d) is what
> gates correctness for N2–N5. "Event-hash divergence 0" is only meaningful measured
> **between two processes** (review §3.5).

**Gate:** loopback round-trips the wire format cleanly; **N0d** two-process match
connects, `g_NetTick` advances independently on each peer, clean disconnect, zero
new crashes, CTest + solo + split-screen lanes green.

### N1 — Auth, lobby, handshake
| Task | Effort | Detail |
|---|---|---|
| N1a. `CLC_AUTH`/`SVC_AUTH` + settings | M | Name/body/head config exchange. |
| N1b. Compatibility handshake | S | Build hash + ROM region (U/J/E) + protocol version; reject mismatches clearly (fixes PD's filename-only weakness). |
| N1c. Lobby + chat | S | Host configures match; clients wait; console chat. |

**Gate:** client joins to `LOBBY` over LAN; mismatched build rejected with a message; chat round-trips.

### N2 — Player movement + puppets (first bodies online)
| Task | Effort | Detail |
|---|---|---|
| N2a. `ucmd` adapter | M | Encode/decode + unit tests (§3a). |
| N2b. Move record/send + apply/interp | L | Read local `struct player` → `netplayermove` → `CLC_MOVE`/`SVC_PLAYER_MOVE`; puppet gate + lerp (§3b). |
| N2c. Player↔client allocation | M | Bind to `g_playerPointers[]`, flag puppets, sync names/skins. |

**Gate:** over LAN, all players see each other move and aim smoothly (interpolated); no crashes on a peer dropping mid-match.

### N3 — World event layer (matches become real)
| Task | Effort | Detail |
|---|---|---|
| N3a. Sync-IDs + prop spawn/move | L | `syncid` field + assignment (§3c); `SVC_PROP_SPAWN/MOVE` for projectiles. |
| N3b. Damage/kills server-authoritative | L | Wrap `record_damage_kills` (§3d); `SVC_CHR_DAMAGE`/disarm; scoreboard. |
| N3c. Pickups, doors, explosions | L | Wrap `object_interaction`, `propdoorInteract`, `explosionCreate`. |
| N3d. RNG push + stats + desync oracle | M | `SVC_PLAYER_STATS`; RNG latch (§3e); event-scoped hash oracle (§3f). |

**Gate:** 2–4-player LAN deathmatch end-to-end into the `mp_watch.c` scoreboard, `--max-crashes 0`, kills/pickups/doors correct, event-scoped hash divergence 0.

### N4 — Hardening, scale, polish
| Task | Effort | Detail |
|---|---|---|
| N4a. `Net.*UpdateFrames` cadence + bandwidth instrumentation | M | Tunable send rate (PD uses `=2` for 4P); log bytes/s into `port_trace`. |
| N4b. Interp tuning + F9 net stats | S | `Net.LerpTicks`; on-screen ping/throughput (`netDebugRender`). |
| N4c. Adversarial-network CI lane | M | Latency/jitter/loss injection (§9). |

**Gate:** 4-player match within a logged bandwidth budget; soak-stable; smooth at injected 80 ms/jitter/1% loss.

### N5 — Internet play (matchmaking + NAT)
| Task | Effort | Detail |
|---|---|---|
| N5a. Server browser / query | S | Port the connectionless query protocol (`NET_QUERY_MAGIC`, CRC) for LAN/direct. |
| N5b. Relay / join-by-code | L | Lightweight relay for NAT traversal (sm64coopdx's CoopNet and PD's servegame.com are references). |
| N5c. **Untrusted-input contract (prerequisite)** | M | Validate `syncid ∈ [1..POS_DATA_ENTRY_LEN]` (reject, don't clamp); length-checked `netbuf` cursor; per-peer rate/size caps; fuzz lane on the apply handlers. |

> **⚠ Security gap (review §4.7):** join-by-code exposes the apply path to untrusted
> peers, but attacker `syncid` indexes `pos_data_entry[600]` with **no bounds check**
> (`netmsg.c:130`), and the `byteswap.h`-style read is a **raw deref** that over-reads
> short packets. N5c is a **hard prerequisite** for N5b, not polish — without it
> join-by-code is an RCE/DoS surface. The plan's only "security" elsewhere is
> anti-cheat fairness. Also scope the relay's **ops/cost/kill-switch** (who runs it,
> egress at host-relays-N fan-out, what happens to "online" if the service dies).

**Gate:** two machines connect over the internet via join-code with no port-forwarding; cross-platform (macOS/ARM ↔ x86) confirmed; **N5c validation + fuzz lane green before N5b ships.**

---

## 7. Risk register (ranked) — "what could stall this"

| Risk | Likelihood × Impact | Mitigation |
|---|---|---|
| **`ucmd` adapter incompleteness** — a weapon action that doesn't map → that action desyncs | High × Med | Build/test it first (N2a); enumerate every weapon's inputs against the `UCMD_*` set; the desync oracle (§3f) flags gaps. |
| **Per-weapon special behaviors** — remote/proximity/timed mines, throwing knives, auto-aim, Klobb spread | Med × High | These are exactly PD's known-issue class. Drive each through the event layer (N3), verify per-weapon; budget explicit time here. |
| **Hitscan reproduction on host** — replaying a client's shot from forwarded aim couples to GE shot code | Med × High | Prototype with the hitscan path early in N3b; if coupling is deep, fall back to shooter-detects-hit + host-validates. |
| **Door/lift/glass state edge cases** | Med × Med | Wrap `propdoorInteract`; cover breakable glass in the prop event set; soak test. |
| **NAT traversal for internet** | High × Med | Relay (N5b) is separate infra; LAN/direct + ZeroTier works without it as an interim (as PD does). |
| **Sync-ID instability if props re-pool mid-match** | Low × High | Explicit `syncid` field (not bare index); reassign only at stage start; assert uniqueness. |
| **Cheating via client-auth movement/aim** | High × Low (v1) | Accepted for v1; kills are server-resolved so scoreboard can't be forged; §10 hardens movement. |
| **Cross-platform float drift in cosmetic RNG** | Low × Low | Irrelevant to correctness (event-synced); only affects particle cosmetics. |

**Two structural gaps to build (not just rename):** `PropRecord.syncid` (§3c) and
the `ucmd` adapter (§3a). Everything else is renaming against the port map's
symbol table.

---

## 8. Bandwidth sketch (move + event model)

- **Moves:** ~50 B × 4 players × ~20–30 send-Hz ≈ **4–6 KB/s/peer** baseline,
  mostly unreliable; the host relays, so its uplink ≈ N × that.
- **Events:** bursty and small (a kill, a pickup, a door = tens of bytes each).
- **Tuning:** `Net.Server.UpdateFrames=2` halves move traffic for 4P. Total sits
  comfortably in a **<128 KB/s/peer** budget on home broadband — far below the
  snapshot model's cost, which is the point of choosing puppets+events.

---

## 9. Testing & CI (the S-tier moat)

Reuse the deterministic harness as a netcode rig:
1. **Loopback CI lane** — `tools/net_smoke.sh`: two in-process peers, scripted
   input (reuse `mp_smoke.sh`'s driver), assert the **event-scoped hash** matches
   and both viewports render. Headless, no second machine.
2. **Adversarial network sim** — injected latency/jitter/loss; assert no crash
   (`--max-crashes 0`), correct scores, and event-hash convergence.
3. **RAMROM determinism oracle** — the existing replay system (`ramromreplay.c`)
   proves the *host* sim is reproducible frame-to-frame, isolating engine
   nondeterminism from network faults.
4. **Soak + ASan** — long 2P/4P loopback through `soak_stability.sh` + the ASan
   lane.

---

## 10. Post-v1 hardening (explicitly out of scope for v1)

Two upgrades the v1 baseline is deliberately built to *enable* later:

**A. Server-authoritative movement + client prediction/reconciliation.** Make the
host simulate remote pawns from `ucmd` (not trust client `pos`), with the client
predicting its own player and reconciling to host corrections. This is the
cheat-resistant, "true Source-model" upgrade — and the only place prediction/
reconciliation actually applies. PD does **not** do this today.

**B. Same-build LAN rollback (tournament mode).** A GGPO/GekkoNet-style rollback
path is plausible for **same-binary, same-arch LAN** 1v1–4P, where cross-platform
float divergence is a non-issue. This is where "snapshot the whole state" and
"fixed timestep for determinism" live — *not* in the v1 wire format.

> **⚠ Corrected (review §3.3):** the "~85–90 KB, mostly contiguous, <1 ms memcpy"
> framing is **understated**. It omits the dominant mutable AI state **`g_ChrSlots`**
> (heap-allocated, `ChrRecord` ~0x180–0x200 B each × `guards+10`, `initguards.c:33`),
> plus `g_ActiveChrs`, `g_SmokeBuffer`, and explosion/smoke sub-buffers. On the
> 64-bit target `PropRecord` is **88 B** so `pos_data_entry[600]` alone is **~51.6 KB**
> (the 28.8 KB figure is N64 32-bit, itself low). The state is **separate per-stage
> heap allocations**, not one block. And `ramromreplay.c` serializes seed+input,
> **not** a state snapshot — that snapshot/restore code does not exist. Also: pinning
> `g_ClockTimer ≡ 1` **changes `g_GlobalTimerDelta`**, scaling float-integrated motion
> — the exact values the project's mandated **bit-stable ares movement oracle**
> ([MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) line 106) locks; the pin must be MP-only
> and the SP oracle proven untouched. Treat §10.B as a **substantial future
> investigation**, not "realistic" as written.

---

## 11. Licensing

MGB64 first-party code is **MIT** (`LICENSE`). **Verified:** the Perfect Dark PC
port is also **MIT** (© 2022 Ryan Dwyer) and bundles **enet (MIT)** — both
compatible. We may **adapt the PD netcode source directly** (preserve the
copyright/permission notice; record in `THIRD_PARTY.md`/`NOTICE.md`). Avoid
GPL-licensed net stacks. See [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md) for the
file-by-file adaptation.

---

## 12. Definition of done (v1)

A 4-player online GoldenEye deathmatch: players join via the lobby (or join-by-
code over the internet), play a complete match into the `mp_watch.c` scoreboard
with `--max-crashes 0`, with **remote players moving smoothly (interpolated)** and
**kills, damage, pickups, doors, and scores all correct** (event-scoped hash
divergence 0 under injected 80 ms/jitter/1% loss). Cross-platform peers
(macOS/ARM ↔ x86) interoperate; mismatched builds are rejected at handshake; and
every existing solo, split-screen, soak, and ASan lane stays green. `tools/net_smoke.sh`
gates all of it in CI.

---

## References
- PD netcode (port-net): [`net.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/net.c) · [`netmsg.c`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/src/net/netmsg.c) · [`net.h`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/port/include/net/net.h) · [`netplay.md`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/docs/netplay.md)
- [sm64coopdx architecture](https://deepwiki.com/coop-deluxe/sm64coopdx/1-overview) — client-server, UDP P2P + TCP relay (CoopNet), reliability/compression.
- [N64 rollback netplay (RMG-K/GekkoNet)](https://www.xda-developers.com/someone-just-added-rollback-netcode-to-an-n64-emulator-making-goldeneye-actually-playable-online/) — §10 rollback reference only.
- Internal: [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md), [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md), [PORT.md](../../PORT.md), [INSTRUMENTATION.md](../INSTRUMENTATION.md).
