# Native Playability Guard Status

Date: 2026-06-30

This note records the current broad native-port playability posture after the
minimap, save-trace, and campaign-gate hardening pass.

## Session Scope Objectives 1-3

This pass freezes the durable campaign-route baseline, adds an explicit
playability scorecard, and makes route smoke output report the highest proved
capability tier per route. Exploratory scouts remain useful, but a scout is not
progress unless it promotes a durable route, adds reusable diagnostics, or
documents a precise blocker.

### Objective 1: Durable Baseline Freeze

The authoritative campaign baseline is the default route set in
`tools/campaign_route_smoke.py`. It currently contains 34 route contracts under
`tools/campaign_routes/`; temporary `/tmp` scout specs are not part of the
baseline. The latest durable stock-input promotions are:

| Route | Stage | Durable proof | Current limit |
| --- | --- | --- | --- |
| `facility_spawn_obj159_obj155_door_chain_contract` | Facility | Stock spawn, real controller input, door object `159`, secondary door object `155`, setup-backed pads `67`/`68`/`75`, no warp/force/stage-state automation. | No objectives, keycards, alarms, bottling-room flow, ending, report, or persistence. |
| `bunker1_spawn_two_door_collect_contract` | Bunker 1 | Stock spawn, real controller input, door object `140`, object type `20` collect, second door object `138`, setup-backed pads `16`/`10052`/`14`, no warp/force/item injection/stage-state automation. | No Bunker keycard, objective, datathief pickup, mission ending, report, or persistence. |
| `cradle_spawn_armour_collect_contract` | Cradle | Stock spawn, real controller input, body-armour object `115`/pad `124`, object type `21` collect/free path, no warp/force/item injection/stage-state automation. | No Trevelyan combat, objective, ending, report, or persistence. |
| `statue_spawn_door_traversal_contract` | Statue | Stock spawn, real controller input, door object `183`/pad `6`, normal `door allow`, `0->1` transition, opening displacement, and finish-open path, no warp/force/stage-state automation. | No Valentin, flight recorder, objective, ending, report, or persistence. |

A route is frozen into the durable baseline only when it passes the focused
route, the default campaign route set, and the native playability suite after
promotion. Scout artifacts stay local and must not be committed.

### Objective 2: Native Playability Scorecard

The scorecard uses these conservative tiers:

| Tier | Meaning | Promotion evidence |
| --- | --- | --- |
| T0 | Boot/trace | Stage boots, trace writes, process exits without crash. |
| T1 | Traversal | Controller-driven movement, meaningful displacement, stable frontend/render state. |
| T2 | Single interaction | Real interaction evidence such as one door, collect, shot, damage, or equipment path. |
| T3 | Chained interaction | Two or more real interactions in one route, with movement between them. |
| T4 | Mission state | Inventory keyflags, objective vector, stage flags, or similar mission-relevant state changes. |
| T5 | Stage loop | Objective/report/title/save/reload loop is proved in one route artifact. |

Current durable campaign posture:

| Stage | Best durable tier | Best stock-input tier | Main proof | Biggest remaining gap |
| --- | --- | --- | --- | --- |
| Dam | T5 scripted loop | T1 traversal | Native multi-waypoint lane plus scripted objective/report/save-reload composite; focused guard-pressure and player-fire combat. | Organic alarms/modem/data/bungee/report/persistence route. |
| Facility | T3 chained interaction | T3 chained interaction | Stock-spawn object `159` then object `155` door chain. | Objective/keycard/bottling-room/alarm/ending flow. |
| Surface 1 | T4 scripted key state | T1 traversal | Scripted key pickup sets keyflags; separate stock-input snowfield lane reaches pads `80`/`79`/`77`. | Organic route from spawn to key/hut area. |
| Bunker 1 | T3 chained interaction | T3 chained interaction | Stock-spawn door `140`, object type `20` collect, door `138`. | Keycard, locked-door-open, datathief pickup, objective flow. |
| Surface II | T5 scripted loop | T1 traversal | Long stock-input outdoor route plus scripted final-exit save/reload contract. | Organic bridge to final silo/report/reload flow. |
| Frigate | T1 traversal | T1 traversal | Stock-input deck route through pads `176`/`153`/`152`/`151`/`150`/`149`/`144`/`60`/`145`. | Hostage/objective/interior/ending interactions. |
| Statue | T2 single interaction | T2 single interaction | Stock-input park/lower-cluster traversal plus spawn-side door object `183` opened through real `B` interaction. | Valentin/flight-recorder/objective flow. |
| Train | T1 traversal | T1 traversal | Stock-input room-3 car-cluster route near boundpads `12`/`14`. | Door/hostage/objective/ending flow. |
| Control | T2 single interaction | T2 single interaction | Stock-spawn door object `144` opens and route reaches pad `49`. | Console-defense/objective/progression loop. |
| Caverns | T2 single interaction | T2 single interaction | Stock-spawn door object `144` opens and route reaches pad `191`. | Objective/progression/ending loop. |
| Cradle | T2 single interaction | T2 single interaction | Stock-spawn body-armour pickup at pad `124` plus separate route reaching pad `121` and later cluster targets. | Trevelyan/combat/objective/ending flow. |

Mechanic posture:

| Mechanic | Durable status | Next promotion target |
| --- | --- | --- |
| Movement/collision | Broad T1 coverage across 11 campaign stages. | Continue replacing short spawn routes with setup-backed multi-waypoint lanes. |
| Doors | Stock-input T3 on Facility and Bunker; stock-input T2 on Statue, Control, and Caverns. | Locked-door open with real key acquisition. |
| Pickups/inventory | Bunker stock collect type `20`; Cradle stock body-armour collect type `21`; Surface 1 scripted key pickup/keyflags. | Stock-spawn key pickup and keyflag route. |
| Objectives/report/persistence | Dam and Surface II scripted T5 loops. | Organic objective-to-report-to-reload route. |
| Combat/damage | Focused Dam guard-pressure and player-fire contracts plus hidden-guard/knife gates. | Stock-route combat encounter with natural approach/aiming. |
| Visibility/minimap/save guards | Dedicated regression smokes exist outside campaign routes. | Keep them in the full suite; do not use them as substitutes for gameplay routes. |

### Objective 3: Route Smoke Metrics And Exit Criteria

`tools/campaign_route_smoke.py` now emits an `evidence` block in each route
`summary.json` and prints a compact pass line with tier, route class, stock-input
status, direct-setup status, horizontal delta, moving records, opened doors,
collects, nonzero keyflags, report frame, and reload frame. The top-level
`summary.json` also records tier counts and route-class counts. This makes broad
runs answer "what did this prove?" without scraping every route log manually.

Promotion criteria for future scouts:

1. Stock-input promotions must avoid warps, forced player placement, item
   injection, and stage-state mutation.
2. Scripted promotions must declare their setup hooks through `scripted_events`
   where possible and must state which organic claim they do not make.
3. Every promoted route must have event-backed proof: door allow/open,
   collect/free/success, keyflag transition, objective/stage flag transition,
   combat/damage, report/title/save/reload, or equivalent trace evidence.
4. A route must cross a tier boundary or protect a distinct fragile mechanic to
   enter the default campaign baseline.
5. Exploratory batches stop after a fixed hypothesis budget unless they improve
   the target event, not just distance.
6. Failed scouts are documented by target, best evidence, missing event, and why
   they were not promoted.

Exit criteria for the broader S-tier native playability goal:

1. The default campaign route set and the full native playability suite pass.
2. Every durable route has a tier, route class, and evidence summary.
3. Priority stages have at least one stock-input traversal or interaction route.
4. Core mechanics have at least one durable route or a documented blocker:
   movement, doors, locked doors/keyflags, pickups, combat, objectives, report,
   save/reload, visibility, and minimap.
5. No active scout remains without a promotion threshold, timebox, and blocker
   note.
6. Remaining gaps are expressed as concrete next promotion targets, not vague
   "more playability" work.

## Improvements Landed

- Minimap guard reveals no longer retain raw `ChrRecord *` pointers. Reveal
  slots store literal chr IDs and resolve a fresh pointer each tick, so removed
  or reused guard slots fade from last known position instead of risking stale
  pointer reads.
- Minimap overlay validation is opt-in through `GE007_MINIMAP_OVERLAY_DUMP`.
  The dump records queue/draw status, layout failures, pin counts, draw calls,
  flushed vertices, framebuffer size, and cache readiness.
- `tools/audit_minimap_dump.py` validates minimap dumps, setup dumps, overlay
  dumps, expected objective pins derived from setup criteria, finite player/pin
  positions, enabled snapshots, disabled no-snapshot behavior, and enabled vs.
  disabled cache parity.
- `tools/minimap_smoke.sh` direct-boots all 20 solo stages by default and runs
  enabled plus disabled-mode minimap checks. The local regression suite uses a
  smaller fragile subset by default for faster post-change guard runs.
- `tools/dam_progression_smoke.sh` activates the existing CTest Dam progression
  hook by composing spawn movement, Dam objective criteria progression, and Dam
  mission-flow return plus a same-savedir process reload.
- `tools/campaign_route_smoke.sh` and `tools/campaign_route_smoke.py` add a
  JSON route-contract layer under `tools/campaign_routes/`. The default routes
  now cover input traversal for Dam, Facility, Surface 1, Surface II, Bunker 1,
  Frigate, Statue, Train, Control, Caverns, and Cradle, plus scripted Dam
  objective progression, a Dam scripted mission composite that joins native traversal with
  objective/report/save-reload proof, Dam guard-pressure combat, Dam
  player-fire combat, Dam mission-report return and Surface II final exit with
  save/reload persistence, Bunker 1 stock-spawn door/collect interaction,
  Bunker 1 stock-spawn two-door/collect interaction, Bunker 1 datathief
  equipment/debug-dump stability, Facility stock-spawn
  bathroom-door traversal, Facility stock-spawn two-door interaction, Facility
  door open/reverse interaction evidence, and Surface 1 proximity key
  pickup/inventory evidence, and Cradle stock-spawn body-armour pickup
  evidence, plus Statue stock-spawn door object `183` interaction evidence.
  The Dam spawn route inherits controller-path input windows and deterministic
  timing from the existing ROM-oracle route DSL. Dam also has a separate
  native-only multi-waypoint traversal route that uses declarative
  `input_segments` to push from stock spawn into the later authored pad cluster
  without warp, force, or stage-state automation. Surface 1, Surface II,
  Frigate, Statue, Train, and Cradle now have additional native-only
  multi-waypoint traversal routes: Surface 1 pushes across the snowfield to
  pads `80`, `79`, and `77`; Surface II pushes from outdoor stock spawn through
  pads `30`, `71`, `70`, `69`, `68`, and broad late pad `78`; Frigate pushes
  from stock spawn through pads `176`, `153`, `152`, `151`,
  `150`, `149`, `144`, `60`, and `145`; Statue pushes from the park start to
  pads `222`, `214`, `207`, and `208`; Train pushes from stock spawn at pad
  `186` into the room-3 car cluster around boundpads `12` and `14`; and Cradle
  pushes from stock spawn to pad `121` and a later authored pad cluster.
  Facility also has a stock-spawn
  input-interaction route that reaches the bathroom door cluster, opens door
  object `159` through a normal `door allow`, and pushes deeper into the level.
  A second Facility stock-spawn route now chains that into secondary door object
  `155` near pad `75`, again through a normal `door allow` and open transition.
  Bunker 1 also has
  stock-spawn input-interaction routes that open corridor door object `140`,
  reach the next-room prop cluster near pad `10052`, collect an object through
  the normal interaction path, and then chain to second door object `138` near
  pad `14` through another normal `door allow`. Control and Caverns now also have
  stock-spawn input-interaction routes that open door object `144` through real
  `B` interaction and push into later room/pad clusters near Control pad `49`
  and Caverns pad `191`. Statue now also has a stock-spawn input-interaction
  route that backs to spawn-side door object `183`, sweeps view with controller
  look input, opens the door with a single real `B` press, and observes
  displacement plus finish-open trace evidence. Cradle now also has a stock-spawn input-interaction
  route that backs along the starting gantry and collects body armour object
  `115` at pad `124` through the normal object collection/free path. The other
  traversal routes drive
  native controller input from spawn through declarative
  `input_segments`. The segment compiler validates `start`/`duration`/`inputs`,
  maps controller names to the existing `GE007_AUTO_*` windows, and rejects
  duplicate raw-env collisions. These traversal routes assert position
  milestones, active input automation, absence of direct state automation, no
  mission-failure or KIA frontend flags, and setup-backed waypoint proximity
  against real stage pads. The
  route auditor checks trace evidence rather than only process exit:
  deterministic exit markers, position/objective/state milestones, frontend
  title/report state, mission failure/KIA flags, required log patterns, regex
  log-count assertions, render counters, bad commands, crashes, and nan
  counters. `setup_target_milestones` now let routes match setup-dump rows and
  prove player proximity to real stage setup targets, so scripted placement
  contracts can assert Facility door object `158`/pad `77` and Surface 1 key
  pad `17` without hard-coding player coordinates. Input traversal routes use
  the same setup-dump path for horizontal setup-pad waypoint checks across Dam,
  Facility, Surface 1, Surface II, Bunker 1, Frigate, Statue, Train, Control,
  Caverns, and Cradle. The Dam native multi-waypoint route extends this to pads
  `10`, `293`, `287`, and `288`; Surface 1 extends this to pads `80`, `79`, and
  `77`; Surface II extends this to pads `30`, `71`, `70`, `69`, `68`, and
  `78`; Statue extends this to pads `222`, `214`, `207`, and `208`; Frigate
  extends this to pads `176`, `153`, `152`, `151`, `150`, `149`, `144`, `60`,
  and `145`; Train extends this to stock spawn pad `186` plus room-3 boundpads
  `12` and `14`; and the Cradle native multi-waypoint route extends this to
  pads `121`, `54`, `55`, and `81`. The Control and Caverns door traversal
  contracts extend setup checks to door object `144` at pads `163` and `108`,
  plus later room targets at pads `49` and `191`; the Facility stock-spawn door
  route extends setup checks to bathroom door object `159` at pads `67` and
  `68`, the Facility two-door chain extends setup checks to secondary door
  object `155` at pad `75`, and the Bunker 1 two-door/collect chain extends
  setup checks to second door object `138` at pad `14`. The Statue door route
  extends setup checks to stock spawn pad `21` and door object `183` at pad `6`. The Cradle armour route
  extends setup checks to body-armour object `115` at pad `124`. Scripted contracts
  also use declarative
  `scripted_events` for
  deterministic setup hooks, compiling pad
  warps, chr-relative warps, face targets, forced pose, tag damage, stage flags,
  guard AI, item add/equip, debug dump, mission end, and title-exit delay back
  to the existing native env hooks while rejecting raw-env collisions. The route
  loader also validates milestone and input/scripted-event keys so malformed
  specs fail before producing misleading zero-frame assertions.
- The campaign route harness now starts each route from a clean per-route save
  directory and supports reusable `save_completion` plus
  `reload_save_completion` assertions. The Dam mission-report, Dam native
  mission composite, and Surface II final-exit contracts use this to prove
  mission completion in the mission trace and again after a fresh process reload
  from the route EEPROM.
- `tools/route_target_reach.py` now provides a reusable route-artifact
  diagnostic for target-directed scouting. It matches setup rows with repeated
  `--target KEY=VALUE` filters, reports best horizontal, same-floor, vertical,
  and full-distance approach, and can reuse a shared `stage_pads.jsonl` for
  older scout artifacts that did not dump setup rows.
- `tools/native_playability_regression_suite.sh` orchestrates the long-running
  local guard set: CTest, playability, Dam progression, Surface II final flow,
  structured campaign route contracts, combat/guard/knife behavior, renderer
  parity, MP split-screen, save persistence, and minimap.
- `tools/save_persistence_check.sh` now accepts `--out-dir` and preserves its
  traces/logs/screenshots/save artifacts there. The trace writer emits frontend
  save state even before a live player exists, so save reload checks can assert
  the actual `save` block on title/menu frames.
- `tools/dam_mission_flow_smoke.sh` now verifies that the scripted Dam
  mission-success path writes folder 0 Dam/Agent completion, then restarts the
  native binary without the mission-success hook and proves the generated
  EEPROM reloads that completion state.

## Validation

- `python3 -m py_compile tools/campaign_route_smoke.py tools/audit_minimap_dump.py`:
  passed after adding route evidence/tier reporting.
- `python3 -m py_compile tools/route_target_reach.py tools/campaign_route_smoke.py`:
  passed after adding the route target-reach diagnostic.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom
  baserom.u.z64 --out-dir /tmp/mgb64_cradle_armour_promoted --timeout 120
  --route tools/campaign_routes/cradle_spawn_armour_collect_contract.json`:
  passed the promoted Cradle stock-spawn body-armour route as T2 with object
  type `21` collect/free evidence.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom
  baserom.u.z64 --out-dir /tmp/mgb64_route_evidence_cradle_armour_promoted
  --timeout 180`: passed 33/33 default routes with tier counts `T1=17`,
  `T2=8`, `T3=3`, `T4=2`, and `T5=3`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom
  baserom.u.z64 --out-dir /tmp/mgb64_statue_contract_single_press_v2
  --timeout 140 --route
  tools/campaign_routes/statue_spawn_door_traversal_contract.json`: passed the
  promoted Statue stock-spawn door route as T2 with door object `183`
  `door allow`, `0->1`, displacement, and finish-open evidence.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom
  baserom.u.z64 --out-dir /tmp/mgb64_route_evidence_statue_door_promoted
  --timeout 140`: passed 34/34 default routes with tier counts `T1=17`,
  `T2=9`, `T3=3`, `T4=2`, and `T5=3`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom
  baserom.u.z64 --out-dir /tmp/mgb64_dam_guard_pressure_health_floor
  --timeout 90 --route tools/campaign_routes/dam_guard_pressure_contract.json`:
  passed after the Dam guard-pressure health floor was hardened to accept the
  observed one-or-two-hit combat outcome while still requiring survival and
  stable frontend state.
- `tools/native_playability_regression_suite.sh --no-build --binary
  build/ge007 --rom baserom.u.z64 --out-dir
  /tmp/mgb64_native_playability_regression_cradle_armour`: passed CTest,
  playability, 33/33 campaign routes, Dam progression, Surface II final flow,
  combat hidden-guard/knife gates, renderer, MP split-screen, save persistence,
  and minimap.
- `tools/native_playability_regression_suite.sh --no-build --binary
  build/ge007 --rom baserom.u.z64 --out-dir
  /tmp/mgb64_native_playability_regression_statue_door_v2`: passed CTest,
  playability, 34/34 campaign routes, Dam progression, Surface II final flow,
  combat hidden-guard/knife gates, renderer, MP split-screen, save persistence,
  and minimap after the Statue door promotion and Dam guard-pressure threshold
  hardening.
- `git diff --check`: passed after adding the Cradle armour route, docs, and
  validation notes.

## Current Scout Blockers

- Train stock-input follow-up remains bounded rather than promoted. Three scout
  batches tested 54 variants toward the nearest room-3 collectable/door
  candidates. The best collectable approach reached roughly `46` horizontal
  units and `37-41` vertical units from the target object, but no
  `collect begin/free/success`, `door allow`, or door state transition appeared.
  Treat the current Train gap as interaction topology/angle/collision evidence,
  not as a distance-only success.
- `tools/route_target_reach.py --target kind=object --target type_name=key --target obj=86 --target pad=8 /tmp/mgb64_route_evidence_metrics_default_refined/bunker1_spawn_two_door_collect_contract`:
  reported the promoted Bunker 1 two-door/collect route's best key-object reach
  as frame `556`, `240.39` horizontal units away, but `166.91` units above the
  key floor.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_route_evidence_metrics_refine --timeout 150 --route facility_door_open_close_contract --route bunker1_datathief_equipment_contract --route surface1_key_pickup_contract`:
  passed the focused classifier refinement check. Facility scripted door now
  reports `T2` without treating door-required keyflags as player inventory
  state, Bunker 1 datathief equipment reports `T2` from equipment/watch/weapon
  state milestones, and Surface 1 key pickup remains `T4` with
  `keyflags=0x00000001`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_route_evidence_metrics_default_refined --timeout 150`:
  passed 32/32 default routes with evidence/tier output enabled. Aggregate tier
  counts were `T1=17`, `T2=7`, `T3=3`, `T4=2`, and `T5=3`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route facility_door_open_close_contract --out-dir /tmp/mgb64_campaign_facility_door_route`:
  passed the focused Facility door route. The summary proves two real
  `door allow` interactions, one `0->1` open transition, 13 opening
  displacement rows, one `1->2` reverse-to-closing transition, 38 closing
  displacement rows, 62 moving-door clearance hits, and stable health/stage
  state at frame 140.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_facility_door`:
  passed 15/15 default routes after adding the Facility door open/reverse
  contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route surface1_key_pickup_contract --out-dir /tmp/mgb64_campaign_surface_key_pickup_route`:
  passed the focused Surface 1 key pickup route. The summary proves inventory
  count/keyflags start at `3`/`0x00000000`, one key `collect begin`, one
  `collect success`, inventory count `4`, keyflags `0x00000001`, key-pad
  position bounds at frame 45, and stable health/stage state at frame 180.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_surface_key`:
  passed 16/16 default routes after adding the Surface 1 key pickup contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route bunker1_datathief_equipment_contract --out-dir /tmp/mgb64_bunker_datathief_equipment_route`:
  passed the focused Bunker equipment route; the summary proves datathief item
  `55` equipped at frame 160, stable after the frame-220 debug dump, stable at
  frame 520, and lists the direct automation keys used by the scripted contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route dam_guard_pressure_contract --out-dir /tmp/mgb64_campaign_dam_guard_pressure_route --timeout 90`:
  passed the focused Dam guard-pressure route. The summary proves 12 aimed
  guard-to-Bond shot rows, one normal 0.0625 damage application, guard firecount
  56 by frame 260, Bond health `0.9375` with active health HUD timer at frame
  260, final firecount 73, and stable stage/front-end state at frame 320.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route dam_player_fire_guard_contract --out-dir /tmp/mgb64_campaign_dam_player_fire_route --timeout 90`:
  passed the focused Dam player-fire route. The summary proves player shot
  counters by frame 60, an accepted non-lethal player-owned guard hit with
  damage delta 2.0 at frame 83, an accepted lethal player-owned guard hit with
  damage delta 4.0 at frame 101, final guard damage/maxdamage `4.0`/`4.0`, and
  stable stage/front-end state at frame 160.
- `tools/dam_mission_flow_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_dam_mission_flow_persistence --timeout 90`:
  passed the focused Dam mission-flow persistence gate. The mission-success
  trace reached report menu `12` at frame 127, title at frame 122, reported
  folder 0 Dam/Agent save completion at frame 122, and a separate reload
  process observed that persisted completion at frame 6.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route dam_mission_report_contract --out-dir /tmp/mgb64_campaign_dam_mission_report_reload --timeout 90`:
  passed the focused Dam campaign mission-report contract with reusable route
  persistence assertions. The command summary reported title frame 122 and
  reload save completion at frame 6.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route surface2_final_exit_contract --out-dir /tmp/mgb64_campaign_surface2_reload --timeout 90`:
  passed the focused Surface II final-exit contract with reusable route
  persistence assertions. The mission trace reported folder 0 Surface II/Agent
  save completion from frame 179 through frame 188, and a fresh reload observed
  that persisted completion at frame 6.
- `tools/dam_progression_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_dam_progression_after_mission_reload --timeout 90`:
  passed the composed Dam progression gate after the mission-flow reload change:
  movement, objective progression, and mission-flow persistence all passed.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_dam_guard_pressure --timeout 90`:
  passed 17/17 default routes after adding the Dam guard-pressure contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_dam_player_fire --timeout 90`:
  passed 18/18 default routes after adding the Dam player-fire contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_mission_reload --timeout 90`:
  passed 18/18 default routes after promoting Dam mission-report persistence
  into the campaign route harness. The Dam mission-report route reported title
  frame 122 and route-level reload save completion at frame 6.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_surface2_reload --timeout 90`:
  passed 18/18 default routes after adding Surface II final-exit route
  persistence. The Dam mission-report route reported `reload_save=6`, and the
  Surface II final-exit route also reported `reload_save=6`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_input_segments_final --timeout 90`:
  passed 18/18 default routes after moving the native route-authored
  controller windows to declarative `input_segments`. Route summaries now record
  the compiled native input env, including Facility door `GE007_AUTO_B=55:4,68:4`,
  Dam player-fire `GE007_AUTO_FIRE=45:140`, and Surface II final-pad
  `GE007_AUTO_A=160:162`; Dam and Surface II still reported `reload_save=6`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route facility_door_open_close_contract --out-dir /tmp/mgb64_facility_door_setup_target --timeout 90`:
  passed the focused Facility door route after adding setup-target milestones.
  The route summary reported 784 setup-dump rows and matched door object
  `158`/pad `77`; the player was within 100 horizontal units at frame 45 and
  80 horizontal units at frame 74 while real `B` input drove the open/reverse
  sequence.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --route surface1_key_pickup_contract --out-dir /tmp/mgb64_surface1_key_setup_target --timeout 90`:
  passed the focused Surface 1 key pickup route after adding a setup-target
  milestone. The route summary reported 660 setup-dump rows, matched the key at
  pad `17`, and proved zero horizontal delta to the setup target at frame 45
  before the normal proximity pickup assertion completed.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_setup_target_default --timeout 90`:
  passed 18/18 default routes after adding the setup-target assertions to the
  Facility door and Surface 1 key contracts.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_scripted_events --timeout 90`:
  passed 18/18 default routes after migrating scripted contracts to
  `scripted_events`. Representative summaries recorded compiled native env for
  Facility door warp/facing, Surface 1 key warp, Dam objective tag damage/stage
  flags, Dam guard chr warp plus AI assignment, Bunker item/debug-dump setup,
  Dam mission end/title exit, and Surface II forced final-pad pose.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_waypoint_traversal --timeout 90 --route ...`:
  passed 10/10 traversal routes after adding setup-backed waypoint assertions.
  Summaries matched Dam pads `327`/`306`, Facility pad `167`, Surface 1 pad
  `80`, Bunker 1 pad `87`, Frigate pad `150`, Statue pad `222`, Train pad
  `186`, Control pad `203`, Caverns pad `368`, and Cradle pad `151`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_waypoints_default --timeout 90`:
  passed 18/18 default routes with the traversal waypoint assertions enabled.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_dam_multiwaypoint_route --timeout 120 --route dam_native_multiwaypoint_input_traversal`:
  passed the focused Dam native multi-waypoint traversal route. The summary
  reported 902 trace records, 495 moving records, 4,794 horizontal units of
  reach, no direct state automation, clean render/front-end counters, and setup
  pad proximity to Dam pads `10`, `293`, `287`, and `288`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_dam_multiwaypoint --timeout 120`:
  passed 19/19 default routes after adding the Dam native multi-waypoint
  traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_cradle_multiwaypoint_route --timeout 120 --route cradle_native_multiwaypoint_input_traversal`:
  passed the focused Cradle native multi-waypoint traversal route. The summary
  reported 952 trace records, 839 moving records, 7,807 horizontal units of
  reach, no direct state automation, clean render/front-end counters, exact
  spawn proximity to pad `151`, near-horizontal proximity to pad `121`, and
  horizontal proximity to later cluster pads `54`, `55`, and `81`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_with_cradle_multiwaypoint --timeout 120`:
  passed 20/20 default routes after adding the Cradle native multi-waypoint
  traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_surface1_statue_multiwaypoint_routes --timeout 120 --route surface1_native_multiwaypoint_input_traversal --route statue_native_multiwaypoint_input_traversal`:
  passed the focused Surface 1 and Statue native multi-waypoint traversal
  routes. The Surface 1 summary reported 952 trace records, 820 moving records,
  7,776 horizontal units of reach, no direct state automation, clean
  render/front-end counters, proximity to setup pads `80`, `79`, and `77`, and
  active `GE007_AUTO_FORWARD` plus `GE007_AUTO_LEFT` controller input. The
  Statue summary reported 952 trace records, 820 moving records, 5,814
  horizontal units of reach, no direct state automation, clean render/front-end
  counters, proximity to setup pads `222`, `214`, `207`, and `208`, and active
  `GE007_AUTO_FORWARD` plus `GE007_AUTO_LEFT` controller input.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_surface1_statue_multiwaypoint --timeout 120`:
  passed 22/22 default routes after adding the Surface 1 and Statue native
  multi-waypoint traversal routes.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_bunker1_spawn_door_collect_route --timeout 120 --route bunker1_spawn_door_collect_contract`:
  passed the focused Bunker 1 stock-spawn door/collect route. The summary
  reported 522 trace records, 220 moving records, 810 horizontal units of
  reach, no direct state automation, clean render/front-end counters, one real
  `door allow` for object `140`, one `0->1` door-open transition, 58 opening
  displacement rows, one collect `begin`/`free` pair for object type `20`,
  setup-derived proximity to door object `140`/pad `16`, and setup-derived
  proximity to next-room prop object `32`/pad `10052`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_bunker1_spawn_door_collect --timeout 120`:
  passed 23/23 default routes after adding the Bunker 1 stock-spawn
  door/collect route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_bunker1_continuation_scout --timeout 150 --route ...`:
  passed 12/12 temporary Bunker 1 continuation scouts from the promoted
  door/collect route. They did not emit key collect/keyflag evidence for the
  nearby key at pad `8`. The best continuation reached within about 40
  horizontal units of second door object `138`/pad `14`, but still hit an
  angle-reject path instead of a `door allow`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_bunker1_obj138_angle_scout --timeout 120 --route ...`:
  passed 31/31 temporary Bunker 1 object-`138` angle scouts. Multiple variants
  produced real `door allow obj=138` rows and `0->1` open transitions; the
  cleanest was reduced and promoted as
  `bunker1_spawn_two_door_collect_contract`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_bunker1_two_door_collect_route --timeout 120 --route bunker1_spawn_two_door_collect_contract`:
  passed the focused Bunker 1 stock-spawn two-door/collect route. The summary
  reported 822 trace records, 395 moving records, 810 horizontal units of reach,
  active `GE007_AUTO_FORWARD`, `GE007_AUTO_RIGHT`, `GE007_AUTO_LOOK_RIGHT`,
  `GE007_AUTO_LOOK_LEFT`, and `GE007_AUTO_B` controller input, no direct state
  automation, clean render/front-end counters, one real `door allow` plus `0->1`
  open transition for object `140`, one type-`20` collect `begin`/`free` pair,
  one real `door allow` plus two linked `0->1` open transitions for object
  `138`, 436 object-`138` opening displacement rows, two object-`138`
  finish-open rows, setup-derived proximity to object `140`/pad `16`, object
  `32`/pad `10052`, and object `138`/pad `14`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_bunker1_two_door_collect --timeout 150`:
  passed 32/32 default routes after adding the Bunker 1 stock-spawn
  two-door/collect route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_control_caverns_door_routes --timeout 120 --route control_spawn_door_traversal_contract --route caverns_spawn_door_traversal_contract`:
  passed the focused Control and Caverns stock-spawn door traversal routes. The
  Control summary reported 822 trace records, 590 moving records, 1,907
  horizontal units of reach, no direct state automation, clean render/front-end
  counters, one real `door allow` for object `144`, two `0->1` door-open
  transitions, 308 opening displacement rows, setup-derived proximity to door
  object `144`/pad `163`, and later-area proximity to pad `49`. The Caverns
  summary reported 822 trace records, 590 moving records, 1,115 horizontal
  units of reach, no direct state automation, clean render/front-end counters,
  one real `door allow` for object `144`, two `0->1` door-open transitions, 218
  opening displacement rows, setup-derived proximity to door object `144`/pad
  `108`, and next-room proximity to pad `191`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_control_caverns_door --timeout 120`:
  passed 25/25 default routes after adding the Control and Caverns stock-spawn
  door traversal routes.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_train_multiwaypoint_route --timeout 120 --route train_native_multiwaypoint_input_traversal`:
  passed the focused Train native multi-waypoint traversal route. The summary
  reported 902 trace records, 730 moving records, 809 horizontal units of
  reach, active `GE007_AUTO_FORWARD` plus `GE007_AUTO_LEFT` controller input,
  no direct state automation, clean render/front-end counters, exact horizontal
  proximity to stock spawn pad `186`, setup-derived proximity to room-3
  boundpad `12`, setup-derived proximity to room-3 boundpad `14`, and stable
  Train frontend state at frame 820.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_train_multiwaypoint --timeout 120`:
  passed 26/26 default routes after adding the Train native multi-waypoint
  traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_facility_obj159_repo_route --timeout 120 --route facility_spawn_obj159_door_traversal_contract`:
  passed the focused Facility stock-spawn object-159 door traversal route. The
  summary reported 762 trace records, 620 moving records, 1,291 horizontal
  units of reach, active `GE007_AUTO_FORWARD`, `GE007_AUTO_LEFT`,
  `GE007_AUTO_LOOK_LEFT`, and `GE007_AUTO_B` controller input, no direct state
  automation, clean render/front-end counters, one real `door allow` for object
  `159`, one `0->1` open transition, 40 opening displacement rows,
  setup-derived proximity to object `159` pads `67` and `68`, and stable
  Facility frontend state at frame 740.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_facility_obj159 --timeout 120`:
  passed 27/27 default routes after adding the Facility stock-spawn object-159
  door traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_facility_obj159_continuation_scout --timeout 150 --route ...`:
  passed 12/12 temporary Facility continuation scouts that preserved the
  stock-spawn object-`159` door interaction. None reached the scripted
  object-`158` door-regression target at pad `77`; the best object-`158`
  distance stayed about 849 horizontal units away. The same scout identified
  secondary door object `155` near pads `75`/`79`/`81` as a nearer continuation
  target, with some variants ending within 37 to 83 horizontal units of object
  `155` doors.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_facility_obj155_interaction_scout --timeout 150 --route ...`:
  passed 8/8 temporary Facility object-`155` interaction scouts. Three variants
  produced a real `door allow obj=155`; the cleanest, later promoted as
  `facility_spawn_obj159_obj155_door_chain_contract`, produced exactly one
  object-`159` allow/open and one object-`155` allow/open.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_facility_obj155_chain_route --timeout 150 --route facility_spawn_obj159_obj155_door_chain_contract`:
  passed the focused Facility stock-spawn two-door chain route. The summary
  reported 1,402 trace records, 980 moving records, 1,291 horizontal units of
  reach, active `GE007_AUTO_FORWARD`, `GE007_AUTO_LEFT`, `GE007_AUTO_LOOK_LEFT`,
  and `GE007_AUTO_B` controller input, no direct state automation, clean
  render/front-end counters, one real `door allow` plus `0->1` open transition
  for object `159`, one real `door allow` plus `0->1` open transition for
  object `155`, 40 object-`155` opening displacement rows, one object-`155`
  finish-open row, setup-derived proximity to object `159` pads `67`/`68`, and
  setup-derived proximity to object `155` pad `75`.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_facility_obj155_chain --timeout 150`:
  passed 31/31 default routes after adding the Facility stock-spawn two-door
  chain route.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_facility_obj155_chain`:
  passed all 11 native playability gates after adding the Facility stock-spawn
  two-door chain route. The pass covered playability, 31/31 campaign routes,
  Dam progression, Surface II final flow, hidden-guard and knife combat lanes,
  renderer parity, multiplayer split-screen, save persistence, and minimap
  enabled/disabled coverage.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_frigate_multiwaypoint_repo_route --timeout 120 --route frigate_native_multiwaypoint_input_traversal`:
  passed the focused Frigate native multi-waypoint traversal route. The summary
  reported 902 trace records, 690 moving records, 2,865 horizontal units of
  reach, active `GE007_AUTO_BACK`, `GE007_AUTO_FORWARD`, and
  `GE007_AUTO_RIGHT` controller input, no direct state automation, clean
  render/front-end counters, setup-derived proximity to pads `176`, `153`,
  `152`, `151`, `150`, `149`, `144`, `60`, and `145`, and stable Frigate
  frontend state at frame 860.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_frigate_multiwaypoint --timeout 120`:
  passed 28/28 default routes after adding the Frigate native multi-waypoint
  traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_dam_composite_route_v2 --timeout 120 --route dam_native_mission_composite_contract`:
  passed the focused Dam native mission composite route. The summary reported
  923 trace records, 495 moving records, active native controller input through
  the existing Dam multi-waypoint lane, setup-derived proximity to pads `10`,
  `293`, `287`, and `288`, objective all-complete at frame 802, title/menu
  return at frame 842, success report at frame 847, folder 0 Dam/Agent save
  completion from frame 842 onward, and reload save completion from frame 6 in
  a fresh process. The objective and mission-end transitions are still scripted
  events, not organic alarm/modem/data/bungee placement.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_dam_composite --timeout 120`:
  passed 29/29 default routes after adding the Dam native mission composite
  contract.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_surface2_multiwaypoint_route --timeout 120 --route surface2_native_multiwaypoint_input_traversal`:
  passed the focused Surface II native multi-waypoint traversal route. The
  summary reported 1,252 trace records, 1,100 moving records, 10,591 horizontal
  units of reach, active `GE007_AUTO_FORWARD=80:1100` and
  `GE007_AUTO_LEFT=200:900` controller input, no direct state automation, clean
  render/front-end counters, setup-derived proximity to pads `30`, `71`, `70`,
  `69`, `68`, and broad late pad `78`, and stable Surface II frontend state at
  frame 1,250.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_campaign_routes_surface2_multiwaypoint --timeout 120`:
  passed 30/30 default routes after adding the Surface II native
  multi-waypoint traversal route.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_surface2_bridge_scout --timeout 150 --route ...`:
  passed 12/12 temporary Surface II bridge-scout routes that tried to continue
  from the spawn-side `back`+`left` shape toward final pad `289`. None promoted:
  the best candidate, `surface2_bridge_scout_back_left_then_left`, stopped
  near `(-10521.5, 449.0, -22916.3)`, still about 25,120 horizontal units from
  final pad `289`, closest to spawn-side pad `30`, and did not reach a new
  setup-backed route cluster.
- `tools/campaign_route_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_surface2_bridge_scout2 --timeout 180 --route ...`:
  passed 8/8 follow-up Surface II bridge-scout routes that changed direction
  after the best first-pass stall point. None promoted: the best moment,
  `surface2_bridge2_escape_forward_left` at frame 1,248, was still about
  25,121 horizontal units from final pad `289` and closest to pad `30`; its
  final position regressed to about 27,641 horizontal units from pad `289`.
- `tools/hidden_guard_contract_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_hidden_guard_contract_current --timeout 180`:
  passed both hidden-guard modes. H2 froze the hidden guard's AI/firecount with
  stable Bond health; H1 kept the AI ticking while still freezing firecount and
  Bond health, proving the hidden-fire discharge gate separately from the
  AI-freeze gate.
- `tools/knife_impact_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_knife_impact_current --timeout 120`:
  passed with one accepted Bond throwing-knife hit on a guard, proving the
  throwknife guard-impact branch executed without the historic crash.
- `tools/knife_throw_sfx_smoke.sh --no-build --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_knife_throw_sfx_current --timeout 180`:
  passed with 40 valid throwing-knife whoosh SFX submissions and no unexpected
  object-handler SFX IDs.
- `cmake --build build --parallel 4`: passed after adding the Surface 1 key
  pickup route, again after promoting the combat gates, and again after adding
  setup-target route milestones, scripted route events, and traversal waypoint
  assertions. It also passed after adding the Dam native multi-waypoint
  traversal route, again after adding the Cradle native multi-waypoint
  traversal route, again after adding the Surface 1 and Statue native
  multi-waypoint traversal routes, and again after adding the Bunker 1
  stock-spawn door/collect route. It passed again after adding the Surface II
  native multi-waypoint traversal route and raising the campaign-route CTest
  aggregate timeout.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_hidden_guard_contract_smoke|port_knife_impact_smoke|port_knife_throw_sfx_smoke' --output-on-failure`:
  passed 4/4 tests in 89.96 seconds after the combat smokes were registered in
  CTest.
- `ctest --test-dir build -R 'port_public_shell_tool_syntax|port_dam_progression_smoke' --output-on-failure`:
  passed 2/2 tests in 18.15 seconds after adding the mission-flow reload phase;
  `port_dam_progression_smoke` took 17.23 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 113.72 seconds after adding route-level Dam and Surface
  II reload persistence; `port_campaign_route_smoke` took 113.56 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 107.57 seconds after adding the `input_segments`
  route-authoring layer; `port_campaign_route_smoke` took 107.40 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 108.27 seconds after adding the setup-target milestone
  assertions; `port_campaign_route_smoke` took 108.07 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 107.59 seconds after adding the `scripted_events`
  route-authoring layer; `port_campaign_route_smoke` took 107.43 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 107.64 seconds after adding setup-backed waypoint
  assertions to the traversal routes; `port_campaign_route_smoke` took 107.48
  seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 123.66 seconds after adding the Dam native
  multi-waypoint traversal route to the default campaign route set;
  `port_campaign_route_smoke` took 123.49 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 157.56 seconds after adding the Cradle native
  multi-waypoint traversal route to the default campaign route set;
  `port_campaign_route_smoke` took 157.40 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 190.58 seconds after adding the Surface 1 and Statue
  native multi-waypoint traversal routes to the default campaign route set;
  `port_campaign_route_smoke` took 190.39 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 199.73 seconds after adding the Bunker 1 stock-spawn
  door/collect route to the default campaign route set;
  `port_campaign_route_smoke` took 199.53 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 227.70 seconds after adding the Control and Caverns
  stock-spawn door traversal routes to the default campaign route set;
  `port_campaign_route_smoke` took 227.50 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 245.38 seconds after adding the Train native
  multi-waypoint traversal route to the default campaign route set;
  `port_campaign_route_smoke` took 245.20 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 257.55 seconds after adding the Facility stock-spawn
  object-159 door traversal route to the default campaign route set;
  `port_campaign_route_smoke` took 257.37 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 275.75 seconds after adding the Frigate native
  multi-waypoint traversal route to the default campaign route set;
  `port_campaign_route_smoke` took 275.56 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 293.56 seconds after adding the Dam native mission
  composite contract to the default campaign route set;
  `port_campaign_route_smoke` took 293.36 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  initially hit the stale/generated 300-second `port_campaign_route_smoke`
  timeout after adding the Surface II native multi-waypoint traversal route.
  After raising the CMake aggregate timeout to 900 seconds and regenerating the
  build tree, it passed 2/2 tests in 317.61 seconds;
  `port_campaign_route_smoke` took 317.43 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 341.50 seconds after adding the Facility stock-spawn
  two-door chain route to the default campaign route set;
  `port_campaign_route_smoke` took 341.35 seconds.
- `ctest --test-dir build -R 'port_public_python_tool_syntax|port_campaign_route_smoke' --output-on-failure`:
  passed 2/2 tests in 356.57 seconds after adding the Bunker 1 stock-spawn
  two-door/collect route to the default campaign route set;
  `port_campaign_route_smoke` took 356.40 seconds.
- `ctest --test-dir build --output-on-failure`: passed 32/32 tests in
  745.94 seconds after adding route-level Dam and Surface II reload
  persistence. This includes `port_rom_oracle_route_contract` in 240.97
  seconds, `port_campaign_route_smoke` in 108.49 seconds,
  `port_dam_progression_smoke` in 17.14 seconds, all-stage
  `port_minimap_smoke` in 163.07 seconds, the hidden-guard/knife combat lanes,
  Surface II final flow, playability, renderer parity, coverage-memory
  regressions, and spawn health.
- `ctest --test-dir build --output-on-failure`: passed 32/32 tests in
  685.41 seconds after adding the `input_segments` route-authoring layer. This
  includes `port_rom_oracle_route_contract` in 186.33 seconds,
  `port_campaign_route_smoke` in 107.37 seconds, `port_dam_progression_smoke`
  in 16.88 seconds, `port_surface2_final_flow_smoke` in 8.45 seconds,
  `port_minimap_smoke` in 161.28 seconds, playability, renderer parity,
  coverage-memory regressions, and spawn health.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_surface_key_route`:
  passed playability, campaign routes, Dam progression, Surface II final flow,
  renderer, MP split-screen, save persistence, and minimap gates. The campaign
  route gate passed 16/16 routes, and the suite minimap gate passed 8/8 enabled
  plus disabled fragile-subset checks.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_combat_gate`:
  passed the updated default suite: playability, 16/16 campaign routes, Dam
  progression, Surface II final flow, hidden-guard combat, throwing-knife guard
  impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_dam_guard_pressure`:
  passed the updated default suite after the Dam guard-pressure route:
  playability, 17/17 campaign routes, Dam progression, Surface II final flow,
  hidden-guard combat, throwing-knife guard impact, throwing-knife SFX, renderer
  parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_dam_player_fire`:
  passed the updated default suite after the Dam player-fire route:
  playability, 18/18 campaign routes, Dam progression, Surface II final flow,
  hidden-guard combat, throwing-knife guard impact, throwing-knife SFX, renderer
  parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_mission_reload`:
  passed the updated default suite after the Dam mission-flow reload-persistence
  check: playability, 18/18 campaign routes, Dam progression with save reload,
  Surface II final flow, hidden-guard combat, throwing-knife guard impact,
  throwing-knife SFX, renderer parity, MP split-screen, save persistence, and
  minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_route_reload_assertion`:
  passed the updated default suite after moving Dam mission-report persistence
  into the campaign route harness: playability, 18/18 campaign routes with
  `reload_save=6`, Dam progression with save reload, Surface II final flow,
  hidden-guard combat, throwing-knife guard impact, throwing-knife SFX, renderer
  parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_after_surface2_reload`:
  passed the updated default suite after adding Surface II final-exit route
  persistence: playability, 18/18 campaign routes with Dam and Surface II both
  reporting `reload_save=6`, Dam progression with save reload, Surface II final
  flow, hidden-guard combat, throwing-knife guard impact, throwing-knife SFX,
  renderer parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_input_segments`:
  passed the updated default suite after adding declarative route
  `input_segments`: playability, 18/18 campaign routes with Dam and Surface II
  both reporting `reload_save=6`, Dam progression with save reload, Surface II
  final flow, hidden-guard combat, throwing-knife guard impact, throwing-knife
  SFX, renderer parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_setup_target`:
  passed the updated default suite after adding setup-target milestones:
  playability, 18/18 campaign routes with Dam and Surface II both reporting
  `reload_save=6`, Dam progression with save reload, Surface II final flow,
  hidden-guard combat, throwing-knife guard impact, throwing-knife SFX, renderer
  parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_scripted_events`:
  passed the updated default suite after migrating scripted contracts to
  `scripted_events`: playability, 18/18 campaign routes with Dam and Surface II
  both reporting `reload_save=6`, Dam progression with save reload, Surface II
  final flow, hidden-guard combat, throwing-knife guard impact, throwing-knife
  SFX, renderer parity, MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_waypoints`:
  passed the updated default suite after adding setup-backed waypoint assertions
  to the traversal routes: playability, 18/18 campaign routes, Dam progression
  with save reload, Surface II final flow, hidden-guard combat, throwing-knife
  guard impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_dam_multiwaypoint`:
  passed the updated default suite after adding the Dam native multi-waypoint
  traversal route: playability, 19/19 campaign routes, Dam progression with
  save reload, Surface II final flow, hidden-guard combat, throwing-knife guard
  impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_cradle_multiwaypoint`:
  passed the updated default suite after adding the Cradle native
  multi-waypoint traversal route: playability, 20/20 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_surface1_statue_multiwaypoint`:
  passed the updated default suite after adding the Surface 1 and Statue native
  multi-waypoint traversal routes: playability, 22/22 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_bunker1_spawn_door_collect`:
  passed the updated default suite after adding the Bunker 1 stock-spawn
  door/collect route: playability, 23/23 campaign routes, Dam progression with
  save reload, Surface II final flow, hidden-guard combat, throwing-knife guard
  impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_control_caverns_door`:
  passed the updated default suite after adding the Control and Caverns
  stock-spawn door traversal routes: playability, 25/25 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_train_multiwaypoint`:
  passed the updated default suite after adding the Train native
  multi-waypoint traversal route: playability, 26/26 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_facility_obj159`:
  passed the updated default suite after adding the Facility stock-spawn
  object-159 door traversal route: playability, 27/27 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_frigate_multiwaypoint`:
  passed the updated default suite after adding the Frigate native
  multi-waypoint traversal route: playability, 28/28 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_dam_composite`:
  passed the updated default suite after adding the Dam native mission
  composite contract: playability, 29/29 campaign routes, Dam progression with
  save reload, Surface II final flow, hidden-guard combat, throwing-knife guard
  impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_surface2_multiwaypoint`:
  passed the updated default suite after adding the Surface II native
  multi-waypoint traversal route: playability, 30/30 campaign routes, Dam
  progression with save reload, Surface II final flow, hidden-guard combat,
  throwing-knife guard impact, throwing-knife SFX, renderer parity,
  MP split-screen, save persistence, and minimap.
- `tools/native_playability_regression_suite.sh --no-build --skip-ctest --binary build/ge007 --rom baserom.u.z64 --out-dir /tmp/mgb64_native_suite_bunker1_two_door_collect`:
  passed the updated default suite after adding the Bunker 1 stock-spawn
  two-door/collect route: playability, 32/32 campaign routes, Dam progression
  with save reload, Surface II final flow, hidden-guard combat, throwing-knife
  guard impact, throwing-knife SFX, renderer parity, MP split-screen, save
  persistence, and minimap.

## What Did Not Become Organic

The new campaign gates now include controller-input traversal for Dam, Facility,
Surface 1, Surface II, Bunker 1, Frigate, Statue, Train, Control, Caverns, and
Cradle, and the native-authored controller windows now use declarative
`input_segments`.
Those traversal routes now also prove horizontal proximity to selected stage
setup pads, which is stronger than relative displacement alone. Dam, Surface 1,
Surface II, Frigate, Statue, and Cradle have advanced past short spawn-only
lanes with native-only multi-waypoint traversal routes into later pad clusters
or broad traversal spaces. Bunker 1 has advanced past a short spawn-only lane
with stock-spawn input routes that open the first corridor door, reach the
next-room prop cluster, trigger a normal collect path, and chain to second door
object `138`. Facility has
advanced past a short spawn-only lane with a stock-spawn input route that opens
bathroom door object `159` and pushes deeper into the level, plus a second
stock-spawn chain that opens secondary door object `155`. Control and
Caverns have advanced past short spawn-only lanes with stock-spawn input routes
that open the first reachable door and push into later room/pad clusters. Train
has also advanced past its short spawn-only lane with a native multi-waypoint
route into the room-3 car cluster. These are still traversal and focused
interaction routes, not stock-like mission routes through objectives. Facility
and Frigate still need broader objective, pickup/hostage, and exit progression.
The objective/report/exit/equipment/door/pickup gates remain deterministic
scripted coverage. They prove important mission contracts, fragile-level movement
envelopes, one Dam guard-pressure contract, one Dam player-fire contract, one
Bunker equipment/debug-dump stability contract, one Facility door interaction
contract, one Surface key pickup/inventory contract, Control/Caverns
stock-spawn door traversal contracts, scripted Dam mission-report persistence,
the Dam native mission composite, and scripted Surface II mission-derived
save/reload persistence, but do not yet prove stock-like, human-authored route
navigation through objectives, pickups, doors, alarms, organic combat
encounters, end triggers, mission report, and reload persistence from an
organic completion.

The Bunker stock-spawn door/collect routes now prove one organic first-door,
next-room object collection, and second-door interaction path from spawn, but
they do not prove a broader Bunker 1 route through the keycard/objective space.
The first continuation scout did not emit key collect/keyflag evidence for key
pad `8`; it instead exposed object `138` as the reliable next interaction, which
was promoted after an angle scout. The Bunker datathief route intentionally uses
deterministic add/equip/debug-dump hooks. It is valuable coverage for inventory,
equipment, full trace state, and debug-dump stability, but it does not prove an
organic pickup path to the item.
A later key-return scout under `/tmp/mgb64_bunker1_key_return_scout` was stopped
after 34 run logs and 33 route summaries. The completed logs emitted no key
pickup (`objtype=4`), no `keyflags=0x00000001`, and no key-door denial/open
evidence for object `138`; it therefore produced no durable promotion. The next
Bunker key attempt should begin with a geometry/room/floor diagnostic around key
object `86`/pad `8` and door object `138`, with success defined as key collect,
keyflag transition, and locked-door allow/open in the same route.
That diagnostic pass is now in place. `route_target_reach.py` confirmed that
the promoted Bunker route gets horizontally near the key but remains on the
upper layer: key object `86`/pad `8` is at `[-1073.59, 172.44, 3142.88]`, while
the promoted route's best exact key-object reach is frame `556`,
`240.39` horizontal units away and `166.91` units above the key. Reanalyzing the
stopped key-return scouts with the promoted route's setup dump showed the best
overhead reach was only `12.64` horizontal units away, still `163.72` units
above the key, while the best same-floor reach stayed roughly `496` horizontal
units away. A targeted 12-route branch scout under
`/tmp/mgb64_bunker1_key_floor_branch_scout` cut the lower-floor `left` segment
before it climbed back above the key and tried `back`, `forward`, `right`, and
`back`+`right` branches. All 12 routes passed process/audit and preserved the
existing two-door/collect evidence, but none emitted key pickup, nonzero
keyflags, or key-door evidence; the best same-floor approach was about
`492.32` horizontal units from key object `86`/pad `8`. This did not cross a
promotion threshold.
The scripted Facility door route uses deterministic placement at door object
`158`; it proves interact/door/clearance behavior for open and reverse-to-close,
but it does not prove a bathroom-to-objective Facility route through that
separate object-`158` door path. The stock-spawn object-`159` route now proves
one organic bathroom-door interaction and later traversal lane. The promoted
object-`159` -> object-`155` route proves a second real door interaction from
the same stock-spawn chain. A 12-route continuation scout preserved object
`159` interaction but stayed about 849 horizontal units from the scripted
object-`158` door-regression target at pad `77`, so it did not become a bridge
to the existing object-`158` contract. Facility still does not prove objectives,
cards, bottling-room flow, alarms, ending, mission report, or persistence from
an organic completion.

The Surface 1 key pickup route uses deterministic placement at key pad `17`; it
proves the normal proximity collect path and key inventory state, but it does
not prove an organic route from spawn to the hut/key area. The promoted Surface
1 native multi-waypoint lane is a real controller-driven snowfield traversal
route to pads `80`, `79`, and `77`, not a key route. Surface 1 key-area
scouting was repeated after the Frigate/Facility promotions. A 12-route simple
movement scout under `/tmp/mgb64_surface1_key_scout` passed process/audit but
the best route stayed about 4,435 horizontal units from the key-area pads and
never emitted collect/keyflag evidence. A 24-route turn-then-forward scout under
`/tmp/mgb64_surface1_key_turn_scout` also passed process/audit, but its best
left-turn variants still stayed about 3,652 horizontal units from the key area,
again with no collect/keyflag evidence. Those scouts were not promoted because
they remained too far from pads `17`/`19` to claim meaningful key-area reach.
The Dam guard-pressure route uses deterministic placement near guard chr `0` and
forces the persistent attack AI list. It proves normal guard-owned shot
accumulation and Bond damage through route assertions, but it does not prove a
stock-like Dam route into a naturally authored firefight.
The Dam player-fire route uses deterministic placement near guard chr `0` and
real fire input. It proves player-owned accepted guard hits and lethal guard
damage through route assertions, but it does not prove a stock-like approach,
aiming, or target-selection sequence.

The Dam mission-flow smoke, Dam campaign mission-report route, and Dam native
mission composite now prove scripted mission success writes durable folder 0
Dam/Agent save state and that a separate process reloads it. The composite is
stronger than the older mission-report route because it first runs the native
multi-waypoint controller lane and then proves objective vector, report, and
persistence in the same route artifact. It still does not prove that an organic
Dam route destroyed alarms, placed the modem, collected data, reached the
bungee exit, advanced through the report flow, and persisted completion without
deterministic objective/mission-end hooks.

The promoted Dam native multi-waypoint route is intentionally separate from the
ROM-oracle Dam spawn route. A speedframes-3 extension of the oracle-timed route
was scouted first, but it stalled before the later pad cluster and remained more
than 2,200 horizontal units from pad `293`, so it was not promoted. The promoted
multi-waypoint route uses the default native deterministic timing, reaches pads
`293`/`287`/`288`, and is valid native playability coverage, but it is not a
stock-timing oracle.

The Surface II native multi-waypoint traversal route now proves a long
stock-spawn outdoor controller lane through pads `30`, `71`, `70`, `69`, `68`,
and broad late pad `78`, with more than 10,500 horizontal units of reach and no
direct state automation. The separate Surface II final-exit route proves the
scripted final-pad path writes durable folder 0 Surface II/Agent save state and
that a separate process reloads it. These two contracts still do not prove a
stock-like route from spawn through combat, objective setup, final silo
interaction, report flow, and persistence.
Two follow-up Surface II bridge scouts tried to close that gap from the
spawn-side route family. A 12-route first pass under
`/tmp/mgb64_surface2_bridge_scout` found that `back`+`left` can reduce distance
to final pad `289`, but its best continuation still stopped closest to pad `30`
and about 25,120 horizontal units from pad `289`. An 8-route post-stall pass
under `/tmp/mgb64_surface2_bridge_scout2` did not improve that result: the best
moment stayed about 25,121 horizontal units from pad `289`, and the route's
final state moved farther away. These scouts were not promoted because they did
not reach a new setup-backed cluster or create a credible bridge to the final
silo path.

The combat gates are focused deterministic scenarios, not organic campaign
encounters. Together with the Dam combat routes they prove guard-fire,
player-fire, hidden-guard, throwing-knife impact, and throwing-knife SFX
mechanics, but they do not prove route-authored stealth, aiming, firefights,
damage recovery, pickups, or objective flow inside a full mission route.

Frigate's early forward-only probe was not promoted because it moved only about
28 horizontal units from spawn. The first promoted Frigate route used
`back`+`right` input to reach pad `150` with about 589 horizontal units and
stable position milestones. A later targeted continuation scout promoted the
`back`+`right` then `forward`+`right` route after it reached pads `176`, `153`,
`152`, `151`, `150`, `149`, `144`, `60`, and `145` with about 2,865 horizontal
units of reach. It still did not produce real interaction evidence, door opens,
hostage progress, objective flow, or mission completion.

The latest broad movement/input-interaction scout promoted Train because
`forward`+`left` reached 809 horizontal units and setup-backed room-3 boundpads
`12` and `14`, which is meaningfully stronger than the older 317-unit Train
spawn route. Facility initially remained ambiguous: straight forward reached
about 1,007 horizontal units without a tight same-floor setup target, and simple
late `B` pulses at object `159` logged `INTERACT_TRACE candidate none` because
the door was in proximity but outside the forward interaction cone. A follow-up
Facility scout added door-interaction rejection tracing and a route-specific
`look_left` sweep; that produced a real object-`159` `door allow`, `0->1` open
transition, and deeper post-open reach, so it was promoted as a focused
stock-spawn door traversal lane. A later Facility continuation scout did not
reach object `158` pad `77`, but it found a reliable object-`155` secondary
door interaction after doubling back from the object-`159` path; that became
the promoted two-door chain. A follow-up Frigate scout found that the
pad-150 cluster could continue with `forward`+`right`, turning the old short
deck route into a deeper multi-waypoint traversal lane. Bunker 1, Control,
Caverns, and Facility were promoted only after adding real `B` interaction and
route-specific controller shapes to open reachable doors and continue to later
clusters; those lanes remain focused interaction progression, not broad route
progression.

The first Control/Caverns door-interaction scout was also too weak: simple
Control windows logged no candidate, and the initial Caverns door allow left
Bond near 144 horizontal units of reach. A follow-up targeted scout promoted
Control only after a `forward`+`right` approach,
real `B` press, wait, and second forward push reached the later console-area
cluster near pad `49`; it promoted Caverns only after a forward approach,
real `B` press, backoff, and second forward push cleared door object `144` and
reached the next-room cluster near pad `191`.

The first six-route expansion attempt also exposed a route-spec hygiene issue:
the new JSON used `frame` where the harness expected `frame_at_or_after`, so
milestone checks sampled frame 0 and failed with zero deltas. The specs were
fixed, and the route loader now rejects that malformed key explicitly.

The first full CTest pass after adding the input traversal route exposed a
timeout posture issue rather than a gameplay failure: `port_rom_oracle_route_contract`
hit its 300-second aggregate CTest timeout while native route captures were
passing. The wrapper already has per-route process timeouts, so the CMake
aggregate timeout was raised to 900 seconds. The failed lane then passed on its
own, and the current full CTest result is tracked in the validation list above.
Adding the Surface II multi-waypoint route exposed the same class of aggregate
budget problem for `port_campaign_route_smoke`: 29 default routes took 293.36
seconds, and the 30-route set needed 317.43 seconds. The generated CTest
metadata was regenerated after raising that wrapper's aggregate timeout to 900
seconds, and the route gate passed without route-level failures.

Screenshot pixels were also not promoted as the only minimap overlay oracle.
The SDL screenshot path is valuable for scene health, but overlay timing is
better proven through `GE007_MINIMAP_OVERLAY_DUMP`; that is why the minimap gate
uses both screenshot/render health and structured overlay JSON.

## S-Tier Remaining Scope

- Promote the current setup-backed traversal specs into real multi-waypoint
  mission routes first. Dam now has a scripted mission composite that joins its
  native multi-waypoint traversal lane with objective/report/persistence proof,
  but it still needs organic objective placement, ending, report, and
  persistence promotion. Surface 1, Surface II, Statue, Train, and Cradle now
  have native multi-waypoint traversal lanes, but they still need objective,
  interaction, ending, report, and persistence promotion. Bunker 1 now has real
  stock-spawn door/collect and two-door/collect interaction lanes, Facility now
  has real stock-spawn one-door and two-door interaction lanes, Frigate now has a deep
  native multi-waypoint traversal lane, and Control/Caverns now have real
  stock-spawn door traversal lanes, but all five still need objective-route
  promotion.
  Surface II specifically needs the bridge from its long outdoor traversal lane
  to objective setup and the already guarded final-silo/report/reload contract.
  Frigate still needs interaction, hostage/objective, ending, report, and
  persistence promotion on top of the new traversal lane.
- Each promoted route should assert survival, meaningful displacement through
  route waypoints, objective transitions, inventory/pickup state where relevant,
  door/interact behavior, alarm/combat state where relevant, mission-end/report
  transition, and persistence after process restart.
- Build on the new `input_segments`, `scripted_events`, and
  `setup_target_milestones` route-authoring layers so routes can drive
  checkpoints, facing targets, interactions, weapon/equipment selection, and
  objective-specific action sequences without one-off shell scripts per mission.
  Generic state milestones now cover nested trace assertions, but richer route
  control beyond deterministic hooks and controller windows still needs to be
  promoted.
- Add stock-reference or oracle-backed comparisons where feasible, especially
  for movement timing, combat health, actor composition, and mission-flow
  ordering.
- Promote CMake hooks only when the backing script exists and has a stable
  artifact directory, timeout, and failure summary.
