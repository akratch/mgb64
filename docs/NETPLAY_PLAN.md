# Online Multiplayer (Netplay) — Execution Plan

GoldenEye 007's split-screen is the defining feature; **online** split-screen is
the feature that turns this port from "the GoldenEye people remember" into "the
GoldenEye people never got to have." The solo campaign plays start-to-finish and
local 2–4-player split-screen is wired and validated
([MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md)). This document is the plan to take
that simulation online in an S-tier fashion.

> Status: **planning**. No netcode is written yet (`grep -rE 'recvfrom|sendto|enet_|netplay' src/` → 0 hits). This is the design of record before any code lands.
>
> **Companion:** [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md) is a verified
> file-by-file map of the (MIT-licensed, same-engine-family) Perfect Dark PC
> port netcode into `src/platform/net/`. Where its *ground-truth reading of the
> shipped code* differs from the theory below, **the port map wins** — see the
> reconciliation note in §1.

---

## 1. Decision: server-authoritative, input-driven, full-sim clients

We run **one authoritative simulation on the host**, forward every player's
controller input to it, and keep every client's local simulation honest with
periodic authoritative state reconciliation, plus client-side prediction for the
local player. This is the Quake/Source FPS model, adapted to the engine.

We **reject deterministic lockstep / rollback as the v1 spine** (kept as a future
LAN mode — §9). The reasons are concrete, not stylistic:

| Property | Evidence in this codebase | Consequence |
|---|---|---|
| Variable timestep | `g_ClockTimer ∈ [1,4]` derived from wall-clock, fed to **`f32 g_GlobalTimerDelta`** (`lvl.c:1977-1998`) | Two machines tick differently frame-to-frame. |
| Float-scaled gameplay | `pos += 10.0f * lvupdate` etc. (`explosions.c:1539`, throughout) | Bit-identical cross-machine floats (x86 ↔ Apple-Silicon ARM) are a research wall. |
| Default non-determinism | RNG/clock seeded from wall-clock unless `--deterministic` (`boss.c:390`, `stubs.c:247`) | Lockstep would need every nondeterminism source pinned and proven. |

The deciding precedent: **Perfect Dark is built on this exact engine**, and the
[fgsfdsfgs PD PC port](https://github.com/fgsfdsfgs/perfect_dark) shipped
**8-player netplay** using a **host/client authoritative state-sync** model
([`port-net`](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/README.md)),
*not* lockstep. Emulator-level rollback for GoldenEye (RMG-K / GekkoNet)
[caps at 2 players](https://www.xda-developers.com/someone-just-added-rollback-netcode-to-an-n64-emulator-making-goldeneye-actually-playable-online/).
The authoritative model also buys us **cross-platform play** (macOS/ARM ↔ x86)
that lockstep cannot, and sidesteps the PD port's "same ROM required" limitation.

### Reconciliation with the PD port's *actual* shipped model (ground truth)

After reading the PD `port-net` source (see [NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md) §1),
the shipped model is **neither thin-client replication nor full-sim reconcile**.
It is a third thing, and it's the proven baseline we should port first:

- **Movement is client-authoritative.** Each client simulates only *its own*
  player and sends the result (position + angles + a `ucmd` command bitmask) to
  the host, which relays it.
- **Remote players are interpolated puppets** — normal engine players with input
  disabled (`controlmode = CONTROLMODE_NA`), their pawns lerped between the last
  two received moves over `Net.LerpTicks`. They are **not** simulated from
  injected input and **not** reconciled.
- **The world is kept correct by a discrete event layer** (`SVC_PROP_*`,
  `SVC_CHR_*`, stats) keyed by a **sync-ID equal to the prop's array index** —
  exactly the index-based serialization argued for in §3b.

**Consequence for the split-screen seam (correction to §2):** the
`osContGetReadData` `data[k]` injection is for **local couch players sharing one
host**, *not* for remote players. Remote players are network puppets. "Online
split-screen" = a host runs its 1–2 local players via `data[k]` **plus** N remote
puppets via the net layer.

The "full-sim clients + reconciliation" idea below is **retained as the post-v1
hardening target** (it's where the prediction/reconciliation work in Phase N3
actually pays off, and the only way to make movement server-authoritative and
cheat-resistant). But **v1 ports the PD baseline**, because it works on this
engine and ships fastest. The PD known-issues list (rocket rotation, cloak,
per-weapon alt-fire) is exactly the long tail the §3c hash oracle is meant to
surface early — and GoldenEye being human-only lets us skip PD's single biggest
unsolved area (bot sync) entirely.

#### Why not thin clients either

A *thin client* (render only from replicated state) must replicate everything the
renderer reads — positions, anim frames, muzzle flashes, effects. That maximizes
the sync surface. The PD puppet+event model is lighter: each peer renders from
its own local objects, and the wire carries only player moves + discrete events.

---

## 2. The seam is already built

The split-screen work created the exact injection point online needs. The host
already drives up to 4 independent players from 4 input sources via
`osContGetReadData` filling `data[0..3]` (`stubs.c:4844`, per-slot fill at
`stubs.c:5280`). Online **replaces the bytes of `data[k]` for remote players with
bytes that arrived over the network** — nothing in game logic changes.

```
   LOCAL split-screen            ONLINE (host)
   pad k (USB)  → data[k]        net player k → data[k]
                                 local player → data[0]
```

**Per-player input payload** (`OSContPad`, `platform_os.h:369`):

| Field | Type | Bytes |
|---|---|---|
| `button` | `u16` | 2 |
| `stick_x`, `stick_y` | `s8` ×2 | 2 |
| right-stick aim (`platformGetPadRightStick`, `platform_os.h:629` — **not** in `OSContPad`) | `s16` ×2 | 4 |
| **Total "player intent"** | | **~8–10 B / player / tick** |

At a 30 Hz sim tick: 4 players ≈ **1.2 KB/s of input**. Negligible. Mouse-look
for P1 is folded into the look-delta field so all players use one wire format.

---

## 3. Three refinements that make this tractable (the critical bits)

These are the parts the naive "just sync the state" plan gets wrong.

### 3a. Pin the timestep in netplay — convert variable→fixed step for free

The clamp machinery already exists (`lvl.c:1988`). In netplay we force
**`g_ClockTimer ≡ 1` per network tick**: one net tick = one sim step. This:

- removes the variable-dt nondeterminism *at the source* (each tick advances the
  same amount on every peer),
- makes host and client prediction agree far more tightly (less reconciliation
  error to smooth), and
- is the precondition that makes the optional §9 rollback mode realistic later.

Cost: ~zero — it's a stricter case of code already on the `NATIVE_PORT` path. The
host owns the tick clock; clients slave to it.

### 3b. Serialize by **stable array index**, never by raw pointer

The ~85 KB match state (§4) is "mostly contiguous" but **pointer-laden**:
`PropRecord` has `parent/child/prev/next` and a `ChrRecord*` union
(`bondtypes.h:2273`). A host-address-space `memcpy` is meaningless on a client.

Because the live state lives in **fixed-size arrays with stable indices**
(`pos_data_entry[600]`, `g_Projectiles[20]`, `g_Embedments[40]`, the explosion/
smoke buffers), the wire format encodes **indices, not addresses**, and the
receiver relinks pointers from indices on apply. Pointer ↔ index conversion is
mechanical and lossless. This is the real serialization work and it is bounded.

> Corollary: the raw-`memcpy` snapshot **is** valid for *same-process* save/restore
> (the §9 rollback path), just not for the wire. Don't conflate the two.

### 3c. A state-hash desync oracle from day one

The PD port's sync bugs lingered because nothing flagged divergence. We have a
contiguous state region and an already-serialized RNG (`g_randomSeed`,
`random.c:24`, persisted in `ramromreplay.c`). So from the first networked
milestone, host and client **hash the canonical state each tick and exchange the
hash**; any mismatch logs the tick + the first differing field/array immediately.
Netcode that is desync-instrumented from the start is the difference between
"ships" and "perpetual experimental branch."

---

## 4. What gets synchronized (snapshot scope)

Match-relevant mutable state, all in the stage arena (`memp.c`/`mema.c`), is
**~85–90 KB** — cheap to snapshot (<1 ms) and to delta-encode:

| State | Container | Max | Bytes | File |
|---|---|---|---|---|
| All game objects (players, doors, pickups) | `pos_data_entry[600]` | 600 × 48 B | 28.8 KB | `chrprop.c:147` |
| Per-player character | `ChrRecord` (in prop union) | 4 × ~512 B | 2.0 KB | `bondtypes.h:2375` |
| Projectiles (bullets/grenades/mines/rockets) | `g_Projectiles[20]` | 20 × 236 B | 4.7 KB | `chrprop.c:407` |
| Embedments (stuck projectiles) | `g_Embedments[40]` | 40 × 72 B | 2.9 KB | `chrprop.c:412` |
| Explosions / smoke / particles / impacts | stage buffers | — | ~32 KB | `explosions.h` |
| Ground weapon/ammo pickups | `g_WeaponSlots[30]`, `g_AmmoCrates[20]` | — | ~3 KB | `chrai.h:239` |
| Per-player stats / scoreboard | `g_playerPlayerData[4]` | 4 × 112 B | 0.4 KB | `player.h:83` |
| RNG seed | `g_randomSeed` | 1 × 8 B | 8 B | `random.c:24` |
| Match flags (pause/gameover/timer) | globals | — | <0.3 KB | `mp_watch.c:34` |

Effects buffers (impacts/particles/smoke) are **cosmetic** — they can be
excluded from the authoritative snapshot and reproduced locally from synced
events to cut bandwidth, with the hash oracle scoped to gameplay state only.

---

## 5. Phased execution plan

Effort: S/M/L/XL. Every milestone extends the existing validation lanes
(`tools/mp_smoke.sh`, `soak_stability.sh`, `asan_smoke.sh`) — netcode that isn't
CI-gated does not count as done.

### Phase N0 — Transport + loopback harness
| Task | Effort | Detail |
|---|---|---|
| N0a. Transport layer | M | Vendor **enet** (UDP + reliability/ordering; **MIT-licensed**, §10) under `src/platform/net/`. Packet framing, seq, ack, channels. |
| N0b. Loopback mode | M | `--netplay loopback`: route input + snapshots through the net path on one machine. The whole pipeline testable with no second box. |
| N0c. Sim-tick clock | S | Host-owned monotonic tick; pin `g_ClockTimer ≡ 1` in netplay (§3a). |

**Gate:** loopback 2-player match is byte-for-byte identical to local split-screen via the §3c hash; zero new crashes; CTest + solo lanes stay green.

### Phase N1 — Input forwarding (host receives remote players)
| Task | Effort | Detail |
|---|---|---|
| N1a. "Player intent" wire format | S | 8–10 B/player/tick (§2): buttons + move stick + look delta. |
| N1b. Inject at `osContGetReadData` | M | For remote slots, fill `data[k]` from the network ring instead of `pcFillPadFromController` (`stubs.c:5280`); feed right-stick into the per-player aim path (`lvl.c`). |
| N1c. Jitter buffer + input delay | M | Small fixed input delay `D` (configurable) so the host has tick-`T` input for all players before simulating `T`; last-known-input hold on a late packet. |

**Gate:** over LAN, a remote player's real controller moves `g_playerPointers[PLAYER_k]` on the host; `CONTROLLERDEVICEREMOVED`-equivalent (peer drop) → no crash.

### Phase N2 — Authoritative state + full-sim clients (first playable online match)
| Task | Effort | Detail |
|---|---|---|
| N2a. Index-based (de)serializer | L | Encode/decode the §4 state with pointer↔index relink (§3b). |
| N2b. Host broadcast | M | Host sends input-set(T) to all peers + a state snapshot every `UpdateFrames` ticks (start: every tick; tune later). |
| N2c. Client apply (hard snap) | M | Client runs full local sim from input-set; **hard-snaps** to authoritative snapshots (visible but playable on LAN). Renders its single viewport; per-client local audio listener (easier than split-screen's single listener). |

**Gate:** 2-player LAN deathmatch end-to-end into the `mp_watch.c` scoreboard, `--max-crashes 0`, hash divergence logged (allowed to be non-zero here — N3 closes it).

### Phase N3 — Smoothing: prediction, reconciliation, interpolation
| Task | Effort | Detail |
|---|---|---|
| N3a. Local-player prediction + reconciliation | L | Local sim runs ahead on local input; on snapshot, correct error **smoothly** (lerp) instead of hard-snap. |
| N3b. Remote entity interpolation | M | Render remote players/projectiles interpolated between snapshots; back-pressure via interpolation delay. |
| N3c. Delta snapshots + quantization | M | Send only changed fields; quantize positions/angles. Cosmetic effects → event-driven, off the authoritative path (§4). |

**Gate:** feels responsive at 60–100 ms RTT (the PD port's stable range); steady-state hash divergence converges to 0 within `D + interp` ticks under injected latency/jitter/loss.

### Phase N4 — Scale to 4–8 players + bandwidth budget
| Task | Effort | Detail |
|---|---|---|
| N4a. `Net.UpdateFrames` cadence | M | Tunable snapshot rate (PD port uses `=2` for 4+ players). |
| N4b. Relevancy/area-of-interest | M | Per-client snapshots can drop far/occluded entities (FPS levels are small; optional). |
| N4c. Bandwidth instrumentation | S | Log bytes/s per phase into `port_trace` like `rooms_drawn`/`tris`. |

**Gate:** 4-player WAN match within a logged bandwidth budget; soak-stable; `--max-crashes 0`.

### Phase N5 — Matchmaking, NAT traversal, lobby UI
| Task | Effort | Detail |
|---|---|---|
| N5a. Relay / matchmaking server | L | Lightweight relay for NAT punch + join-by-code (sm64coopdx's "CoopNet" TCP relay and the PD port's servegame.com are the references). |
| N5b. Lobby UI | L | Host/join/character/stage/scenario hooked into the existing MP menu state machine (`front.c:14324`) and stage table (`front.c:1124+`). |
| N5c. Compatibility handshake | S | Exchange ROM region (U/J/E) + build hash + protocol version; refuse mismatches with a clear message (avoids the PD port's silent "different ROM" desyncs). |

**Gate:** two machines connect over the internet via join-code with no port-forwarding; mismatched builds are rejected cleanly.

---

## 6. Hard problems & how we beat them

| Problem | Approach |
|---|---|
| **Pointer-laden state on the wire** | Index-based serialization with relink (§3b); never ship addresses. |
| **Non-determinism between peers** | Host is canonical; clients reconcile to it; timestep pinned (§3a) so drift is small and correction is cheap. |
| **The "X not synced" long tail** (PD port's pain) | Hash oracle (§3c) names the first divergent field every tick — turns silent desyncs into a fix list. |
| **Pause in an online match** | GoldenEye's `who_paused` (`mp_watch.c`) can't freeze the world for everyone. v1: pause opens a *local* menu overlay only; the match keeps running (standard online FPS behavior). |
| **Host advantage / cheating** | Accepted tradeoff for a preservation/party port; host authority is the standard model. Document it; don't over-engineer anti-cheat. |
| **Host migration / reconnection** | v1: host leaving ends the match (clear messaging). Migration is a post-v1 stretch. |
| **MP unlocks/characters** (ride `file.c`) | Host's save provides the authoritative stage/character/unlock set; clients use host config for the session. |
| **Effects flooding bandwidth** | Cosmetic effects are event-driven and locally simulated, excluded from the authoritative snapshot and the hash. |

---

## 7. Bandwidth sketch

- **Input:** ~8–10 B × 4 players × 30 Hz ≈ **1.2 KB/s** (constant, trivial).
- **Snapshot:** worst case full ~85 KB; with delta + quantization (N3c) the
  steady-state working set (moving players + active projectiles) is a few KB/tick;
  at `UpdateFrames=2` that's well within a **64–256 KB/s** budget for 4 players —
  comparable to a modern indie FPS and far inside home-broadband uplinks.

---

## 8. Testing & CI (the S-tier moat)

Reuse the existing deterministic harness as a netcode rig:

1. **Loopback CI lane** — `tools/net_smoke.sh`: two in-process peers, scripted
   input (reuse `mp_smoke.sh`'s driver), assert **hash equality every tick** and
   distinct viewports. Runs headless in CI with no second machine.
2. **Adversarial network sim** — inject latency/jitter/packet-loss; assert the
   match still converges (hash divergence → 0) and never crashes (`--max-crashes 0`).
3. **RAMROM as a determinism oracle** — the existing replay system
   (`ramromreplay.c`) records a match's inputs + RNG seed; replaying on one
   machine proves the host sim is reproducible frame-to-frame, isolating *engine*
   nondeterminism from *network* faults.
4. **Soak** — long 2P/4P loopback runs through `soak_stability.sh` + ASan lane.

---

## 9. Future: same-build LAN rollback mode (stretch, post-v1)

This engine has unusually strong assets for rollback that the authoritative spine
doesn't need but a competitive LAN mode could exploit:

- ~85 KB **contiguous** state → save/restore in <1 ms (cheap rollback).
- RNG already serialized (`random.c` / `ramromreplay.c`).
- Timestep already pinned to a fixed step in netplay (§3a).
- An input-replay system already exists (RAMROM).

For **same-binary, same-arch LAN** (where cross-platform float divergence is a
non-issue), a GGPO/GekkoNet-style rollback path becomes realistic for 2–4-player
low-latency competitive play. It is explicitly **out of scope for v1** — the
authoritative model ships first and serves cross-platform internet play; rollback
is a later, opt-in mode for tournaments.

---

## 10. Licensing

MGB64's first-party code is **MIT** (`LICENSE`). **Verified:** the Perfect Dark
PC port is also **MIT** (© 2022 Ryan Dwyer) and bundles **enet (MIT)** — both
compatible with MGB64. So we may **adapt the PD netcode source directly** (not
only clean-room), provided the copyright/permission notice is preserved and
recorded in `THIRD_PARTY.md`/`NOTICE.md`. This supersedes the earlier
clean-room-only assumption. Still avoid GPL-licensed net stacks. See
[NETPLAY_PORT_MAP.md](NETPLAY_PORT_MAP.md) for the file-by-file adaptation.

---

## 11. Definition of done (v1)

`tools/net_smoke.sh` connects 4 peers (loopback + a LAN job), plays a complete
deathmatch into the `mp_watch.c` scoreboard with `--max-crashes 0`, **steady-state
state-hash divergence of 0** under injected 80 ms/jitter/1%-loss, each peer
rendering its own predicted+interpolated viewport with responsive local aim —
while every existing solo, split-screen, soak, and ASan lane stays green. Internet
play works via join-code through the relay with cross-platform (macOS/ARM ↔ x86)
peers, and mismatched builds are rejected at handshake.

---

## References

- [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark) · [netplay branch](https://github.com/fgsfdsfgs/perfect_dark/blob/port-net/README.md) — same engine family; authoritative host/client, 8 players. **Read its `net/` design first.**
- [sm64coopdx architecture](https://deepwiki.com/coop-deluxe/sm64coopdx/1-overview) — client-server, UDP P2P + TCP relay (CoopNet), reliability/compression, per-object ownership.
- [N64 rollback netplay (RMG-K / GekkoNet)](https://www.xda-developers.com/someone-just-added-rollback-netcode-to-an-n64-emulator-making-goldeneye-actually-playable-online/) — reference for the §9 rollback stretch only.
- Internal: [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) (split-screen seam), [PORT.md](../PORT.md) (timing/determinism caveats), [INSTRUMENTATION.md](INSTRUMENTATION.md) (trace tooling).
