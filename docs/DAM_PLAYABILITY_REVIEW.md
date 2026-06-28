# Dam Playability Review

Date: 2026-06-28

This note tracks the current Dam playability/rendering pass. Captured screenshots,
state traces, and logs are ROM-derived local artifacts and should remain in `/tmp`.

## Current Sprint Status

- Current aggregate gate: `tools/dam_visual_regression_suite.sh` now runs the
  focused Dam visual/playability regression set in one pass: camera-basis tilt,
  tunnel visibility, purple effect textures, paletted guard/alarm colors, glass
  material health, and the pad-`10092` actor-masked active-shard visual fixture.
  It also includes the pad-`10004` impact-aligned pane/decal fixture. Latest
  suite artifact `/tmp/mgb64_dam_visual_regression_suite_current` passed all
  seven gates and wrote `summary.json` plus `index.tsv` for the per-gate
  artifacts.
- New coverage: stock/native active-shard projection comparison now exists for
  the pad-`10092` actor-masked route. The local instrumented ares hook and native
  trace both emit top-level `glass_projection` records, and
  `tools/compare_glass_projection_trace.py` compares them. Current proof:
  `/tmp/mgb64_dam_visual_regression_suite_current/glass_actor_masked/pad10092_actor_masked/projection_dam_regular_glass_shatter_pad10092_actor_masked_visual_probe.json`.
  Stock and native both have `active=88`, `projected=88`, `onscreen=88`, and
  `behind=0` under native `scale=inv_vis_full`, with max area
  `0.206% -> 0.244%` and union area `6.860% -> 6.119%`.
- Closed: the clean pad-`103` active-shard route now has strict stock/native
  projection parity while keeping visibility-scaled `field_10E0`. The cause of
  the remaining `sqrt_basis` mismatch was native `field_10E0` carrying the
  large-room visibility scale, which made shard clip `w` about `14%` of stock
  even when shard world state matched. The shard pass now scales the full shard
  model by `1.0 / bgGetLevelVisibilityScale()` before the scaled projection.
  Current proof `/tmp/mgb64_invvis_active_gate2` passes the active-shard gate:
  `active/projected/onscreen/behind` match `90/90/82/0`, max area is
  `5.421% -> 5.392%`, and union area is `65.245% -> 65.003%`. The all-piece
  diagnostic comparison at
  `/tmp/mgb64_glass_projection_invvis_selected_1782580888/native_probe/projection_pieces_selected_vs_stock.json`
  covers all `90` pieces with screen-center mean error `0.44` pixels, p90 `0.73`,
  max `3.99`, and median clip-w ratio `1.0006`. Pixel presentation remains open.
- Closed: the Dam service-tunnel draw-distance/blue-sky regression is fixed and
  covered by `tools/dam_tunnel_visibility_regression.sh`. The current default
  capture renders the tunnel continuation (`6` rooms) with `0.000%` bright-blue
  cap pixels, while the negative-control fallback-off capture reproduces the
  regression (`3` rooms with a higher blue-cap signature than default). Latest
  validation in the full suite: `/tmp/mgb64_dam_visual_regression_suite_current`
  (`default=6` rooms, `fallback_off=3`, `no_portal_bfs=54`).
- Closed: the Surface 1 sky-dominance/player-above-map visual regression is fixed
  and covered by `tools/surface_projection_regression.sh`. The cause was the
  glass investigation promoting a stock-style unscaled `field_10E0` default;
  native large-room rendering expects the level visibility scale in `field_10E0`.
  Latest proof: `/tmp/mgb64_surface_projection_final3` passed with default sky
  `8.05%` versus unscaled-control sky `73.77%`.
- Closed: the follow-up Surface direct-boot "slow/player above map" report was
  caused by the non-deterministic `--level surface` path entering authored intro
  camera mode, while deterministic validation skipped straight to first-person.
  The submitted setup counts were normal Surface 1 data, not Castle/Citadel data.
  Native direct level boots now default to the immediate first-person handoff;
  authored intros are opt-in with `GE007_ENABLE_LEVEL_INTRO=1`.
- Guarded: the Bunker darkness report is not caused by the `field_10E0` Surface
  fix or an effect-texture/material regression, and now has a repeatable native
  guard in `tools/bunker_brightness_regression.sh`. Current proof
  `/tmp/mgb64_bunker_brightness_current_guard` renders Bunker 1 stage `9` with
  `7` rooms, `1540` tris, zero DL/crash counters, default faithful center luma
  `67.71`, and `0.00%` center black pixels. The bright remaster A/B reaches
  center luma `127.81`, proving the metric catches saved visual-profile
  brightness changes. Treat Bunker as stock-dark unless this guard fails or a
  stock-backed Bunker route proves a real output regression.
- Closed: the pad-140 portal over-admission guard remains intact. The current
  `tools/dam_portal_regression.sh` run keeps default traversal at `5` rooms,
  while the legacy fallback renders `40` rooms and changes the image by
  `19.735%`.
- Closed: the purple muzzle/explosion texture regression is covered by
  `tools/effect_texture_regression.sh`. The current lane directly captures the
  visible first-person Dam muzzle `settex` textures `2157..2160` and rejects
  purple/low-green channel output. It also proves route-selected muzzle texture
  `2128` is loaded through static cache key `0x8000000000000850`, dumps
  explosion-smoke textures `2176..2181` from `0x8000000000000880..0885`, audits
  decoded IA8 payloads as grayscale (`0.00%` purple), and checks sampled smoke
  material rows for alpha texture passthrough and warm-white shade.
- Closed: the gray guard faces, gray tan-uniform, missing red/tan paletted
  material, and missing red alarm-lens regression class is fixed and covered by
  `tools/dam_palette_regression.sh`. Native `texLoadFromDisplayList` now
  preloads image-table textures without rewriting `0xabcdNNNN` display-list
  tokens into heap pointers, so fast3d keeps static cache identity and CI/TLUT
  palette identity. The current Dam palette run rejects no-palette/grayscale
  fallback logs, proves static CI/TLUT trace coverage, and decodes the sampled
  guard/material textures with strong non-gray/red/tan counts. It also frames
  alarm tag `0` at pad `10070` and requires the red lens cluster
  (`211` red pixels, bbox `[315,219,331,236]` in the latest run). Latest
  artifact: `/tmp/mgb64_dam_palette_alarm_regression_1782506333`.
- Closed: the movement-plus-look world-tilt regression is fixed and covered by
  `tools/camera_basis_regression.sh`. The original stale-basis bug was live
  mouse/gamepad look changing `vv_theta`/`vv_verta` after movement without
  refreshing the derived render basis. The follow-up visible roll came from
  movement `headup`/gait animation leaking into the native world-camera up
  vector after that refresh. Native `Input.SteadyView=1` is now the default:
  movement/head animation still drives camera position and weapon sway, but the
  world camera orientation is derived from yaw/pitch. The current Dam probe
  reports max facing-vs-theta error `0.000067` and max up-vector error
  `0.000001`; the negative control `Input.SteadyView=0` reproduces max
  up-vector error `0.012503`. Latest post-palette validation:
  `/tmp/mgb64_camera_basis_after_palette_1782511100`.
- New coverage: the scripted Dam mission-report return is covered by
  `tools/dam_mission_flow_smoke.sh`. The current run direct-boots Dam, traces
  the four objectives as initially incomplete, triggers deterministic mission
  success at frame `120`, observes the title stage at frame `122`, and reaches
  the mission-report menu at frame `127` with `all_obj_complete=1`,
  `mission_failed=0`, `bond_kia=0`, and zero DL resolve counters. This is
  end-state plumbing coverage, not a full organic objective/bungee route.
- New coverage: Dam objective criteria progression is covered by
  `tools/dam_objective_progression_smoke.sh`. The current run direct-boots Dam,
  dumps the runtime objective criteria, destroys alarm tags `0..3`, sets the
  modem/data/bungee stage flags, and requires the traced objective vector to
  advance `[0,0,0,0] -> [1,0,0,0] -> [1,1,0,0] -> [1,1,1,0] -> [1,1,1,1]`
  with final flags `0x1500`, no fail flags, and zero DL resolve counters. This
  proves objective-condition wiring, not organic route navigation or stock-like
  bungee timing.
- Closed: the live `[GFX-DL] unregistered` regression from the latest run logs
  is fixed in fast3d provenance handling. The logged targets landed inside the
  current native VTX pool, but that pool was being admitted through the finite
  extra-PC-DL registry, which can go stale/full across level/front-end
  allocations. The renderer now treats the current GFX and VTX pools as explicit
  current dynamic ranges, runs an opcode plausibility gate before recursive PC
  DL execution, and counts data-looking PC targets as non-DL skips instead of
  unknown pointers. `dynInitMemory` no longer registers every new VTX malloc in
  the extra DL table. The current Dam shatter route render-health audit reports
  `dl_max ... non_dl_skip_pc:0, non_dl_skip_n64:0, unregistered_skip:0`. The
  submitted Castle/Citadel-style setup counts match Surface 1 (`136` bound
  pads, `251` waypoints, `25` waygroups, `16` patrol paths, `47` AI lists,
  `9` guards, `203` objects). `tools/gfxdl_provenance_regression.sh` now
  covers this as a reusable long-playback gate. The fresh current-build rerun
  `/tmp/mgb64_gfxdl_provenance_current_1782503913` exited through
  `GE007_AUTO_EXIT_FRAME=11600`, produced `11602` trace records, crossed the
  submitted warning window around frame `11450`, and kept all DL resolve,
  bad-command, texture, matrix, vertex, and crash counters at zero with no
  `[GFX-DL]` log rows. The newly pasted Castle/Citadel-style playback log has
  the same setup counts and warning window, so treat it as the fixed provenance
  signature unless a rerun against the current `build/ge007` still prints
  `[GFX-DL]`.
- Closed for the known repros: render-camera clearance remains render-only.
  The current camera-clearance regression covers Dam exterior wall, long-contact
  and soak, glass area aim/crouch/lean, control-room look/weapon-switch, Dam
  moving-truck vehicle contact, plus Surface, Runway, Facility door, and
  Facility tinted-glass controls, all with gameplay `pos_delta=0.000` and
  `col_delta=0.000`.
- Closed for gameplay state: stock-backed regular-glass routes prove pane
  destruction/removal parity, `90 -> 90` active shards, exact first-sample shard
  parity on the RNG-isolation route, and first world bullet-impact semantic
  match with `2.140` world-unit impact-center delta under the current tolerance.
- Closed as a native-only glass gameplay regression: the pad-`10004`
  `item=8` pane damage is chr `11` firing an AK47, and the same stock-backed
  route shows chr `11` attacking, active glass, and later Bond damage in stock.
  The rebuilt-Ares chr `11` probe
  `/tmp/mgb64_chr11_stock_native_fields_1782490698` records stock chr `11`
  `firecount=15`, `shotbondsum=0.0` after the first health drop at stock frame
  `2441`/global `1292`; native `[GUARD_BOND_SHOT]` proves the matching source at
  native frame `153`/global `155` with `shotbond=(0.96205,0.05363,0.00000)`,
  `damage_amount=0.06250`, and `health=(1.00000,0.93750)`. Keep future chr `11`
  work scoped to composition/timing evidence, not blocking stock-compatible
  guard fire.
- Active visual backlog: the remaining bright burst in
  `dam_regular_glass_shatter_visual_probe` is not the old full-screen falling
  shard bug. The current first-active native control proves default native and
  `GE007_GLASS_SHARDS=0` are byte-identical (`0/307200` pixels changed), including
  `0.000%` changed under the full `82` projected shard masks. `GE007_NO_FOG=1`
  also leaves the `glass_burst` ROI bright count unchanged.
  `GE007_TRACE_BULLET_IMPACT_MATERIALS=1` now shows the visible world impact as
  two alpha-blended `G_CC_MODULATEIA` triangles using a 32x32 CI8 texture whose
  decoded alpha maxes at `169/255`, so it is not being emitted as an opaque
  white quad. The current evidence scopes the remaining mismatch to
  stock/native viewport, brightness, damage-HUD, and presentation alignment.
  The visual route now requires checkpoint health/HUD parity before pixel
  comparison, writes `combat_health_compare_<route>.json`, and only writes a
  masked screenshot aggregate that excludes the `damage_arc` and `hud_viewmodel`
  ROIs after health, actor, and impact pre-pixel guards pass.
- Pad-`10092` impact backlog: the refined instrumentation is answering the right
  ownership questions, but it now rules out another tempting false lead. Current
  proof `/tmp/mgb64_glass_pad10092_texgen_roi_material_probe` shows default
  native emits `14` aligned-crop texgen rows over `projected_impact`, all room
  `glass`; `GE007_GLASS_SHARDS=0` leaves those same `14` room-glass rows.
  Default versus shard-off still changes `0.000%` of the full frame, `0.000%` of
  `projected_impact`, and `0.000%` over `5062` rasterized full-sample shard-mask
  pixels. This corrects the older logical-coordinate false lead: the primary ROI
  is not falling-shard owned at this checkpoint. The current clean base case also
  keeps matching the world bullet-impact sequence (`[1,1,1,1]`) and the
  presentation search remains insufficient (`tower_pane` best `94.067%`,
  `impact_side` best `90.233%`). Corrected texgen ownership records `tower_pane`
  as `46` rows (`38` room `glass`, `8` shard bboxes) and `impact_side` as `22`
  room `glass` rows. Current room-glass A/B proof
  `/tmp/mgb64_glass_pad10092_room_glass_visibility` shows room-glass controls
  are pixel-visible, but the only stock-directed change is tiny
  (`GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5`, `tower_pane=-0.222` percentage
  points); `1.5` and `GE007_ROOM_ALPHA_AS_TEXEDGE=1` are default-equivalent, and
  opaque/filter variants are broad side effects or negative controls. The
  control-footprint scorer makes that conclusion stronger: the largest
  `tower_pane` footprint is `GE007_DISABLE_N64_FILTER=1` at `44.802%`, but it
  worsens stock parity by `+0.080` percentage points; the only improving
  room-alpha control covers only `16.517%` of `tower_pane` and `0.000%` of
  `projected_impact`; and point filtering reaches only `8.750%` of
  `projected_impact`. The trace-to-pixel correlation
  `/tmp/mgb64_glass_pad10092_texgen_roi_pixel_correlation_current` now shows the
  exact split: `projected_impact` is `94.474%` changed and `100.000%` room-glass
  bbox-covered, while `tower_pane` is `95.734%` changed with only `21.896%`
  texgen bbox coverage and `impact_side` is `93.702%` changed with `39.965%`
  room-glass coverage. The refined approach is working because it now separates
  bbox/material overlap from visible wrong-pixel ownership. The next read-only
  stock/native pixel-semantics probe
  `/tmp/mgb64_glass_pad10092_roi_pixel_semantics_current` keeps that approach
  honest: it reports unmasked and route-masked ROI semantics separately after
  catching that route masking samples only `80 / 380` `projected_impact` pixels.
  Unmasked ROIs rule out a blue-sky leak in this glass fixture: `tower_pane` is
  `95.734%` changed with mean luma delta `+16.28` and only `1.099%`
  native-bluer changed pixels; `impact_side` is `93.702%` changed with luma
  `+17.29` and only `1.666%` native-bluer; `projected_impact` is `94.474%`
  changed with luma `+4.23` and `0.000%` native-bluer. Keep the next work scoped
  to pane/background blend, coverage, memory-color, fog/lighting, or
  post-composite output semantics, not shard suppression, crop-only fixes, sky
  visibility, or simple room-alpha and room-filter toggles.
  Current refined-output sweeps finish that existing-toggle lane. The focused
  run `/tmp/mgb64_glass_pad10092_room_glass_output_sweep` adds fog,
  alpha-blend, noperspective settex interpolation, targeted settex filter, and
  small color-scale variants for room-glass combiner `0x00738e4f020a2d12`. The
  largest moving control, `GE007_DIAG_ALPHA_BLEND=copy`, covers `31.047%` of the
  `tower_pane` mismatch but worsens stock parity (`tower_pane=+0.783`,
  `projected_impact=+1.579`), while `premult` worsens the primary ROI by
  `+3.158`. The repaired RDP-memory run
  `/tmp/mgb64_glass_pad10092_room_glass_rdp_memory_sweep` confirms the
  `G_SETTEX` memory-blend diagnostics are now internally consistent and still
  negative: memory and coverage-memory variants both move room-glass pixels but
  worsen `projected_impact` by `+0.789` and cover `0.000%` of the
  stock/native mismatch footprint. No renderer fix was promoted from these
  results; the next step is a cleaner stock/ares pixel-output or translucent
  ordering oracle for the room-glass path.
  The follow-up room-glass pixel oracle
  `/tmp/mgb64_glass_pad10092_room_glass_pixel_oracle_current` now provides that
  split using the selected room-glass TEXGEN bbox mask. `projected_impact` is
  entirely inside the mask (`100.000%` ROI coverage and `100.000%` changed-pixel
  coverage), so its `94.474%` changed density is an exact room-glass output
  semantics target. The broader `tower_pane` mismatch is mostly not in that
  material mask: room glass covers `21.186%` of the ROI and `20.209%` of changed
  pixels, while outside-mask pixels have higher changed density (`96.921%`) and
  larger luma error (`+17.876`) than in-mask pixels (`91.322%`, `+9.992`).
  `impact_side` is also split (`39.965%` ROI coverage and `38.776%`
  changed-pixel coverage). Treat this as two workstreams: exact room-glass
  output for the tiny impact ROI, and non-texgen/background/post-composite
  contributors for the large pane/side deficiencies.
  The corrected material-state proof
  `/tmp/mgb64_glass_pad10092_room_glass_settex_sample_current_v2` now passes too:
  `120` raw `[SETTEX-MATERIAL-CC]` rows reduce to `30` true room-glass rows
  (`class=room`, `texnum=654`, `wh=54x54`, `blend=alpha`, `alpha=1`, `fog=1`,
  `effcc=0x00738e4f020a2d12`, `opts=0x00043C13`,
  `oml_raw=0xC41049D8`). On selected frame `122`, four filtered rows cover
  `100.000%` of `projected_impact`; center samples have texture alpha `102`
  from both levels. The old `combL_float` trace was incomplete for alpha: it
  reports alpha `255` because it only modeled RGB LOD/shade output. The new
  shader-mirrored fields show `shaderL_comb` alpha `102` for all four rows and
  `shaderL_frag` alpha still `102` after fog, with luma moving from
  `0.0/12.75/30.0` pre-fog to `0.0/11.0/23.0` post-fog. Center fog alpha counts
  are `{0: 2, 60: 2}`. This confirms the refined instrumentation is answering
  the right question; the next fix should target exact room-glass
  blend/coverage/memory composition or stock-reference semantics for texnum
  `654`, not a blind opacity clamp.
  The K4/K5 follow-up
  `/tmp/mgb64_glass_pad10092_room_glass_k4k5_sweep` rules out the obvious
  combiner-constant shortcut: `GE007_DIAG_CONVERT_K4K5=1` moves the entire target
  regions (`100.000%` native A/B in `projected_impact`, `tower_pane`, and
  `impact_side`) but worsens stock parity. `projected_impact` changes from
  `94.474%` to `100.000%`, with mean luma error jumping from `+4.231` to
  `+94.803`.
  The framebuffer-underlay diagnostic
  `/tmp/mgb64_glass_pad10092_room_glass_skip_tex654` also passes as a negative
  control. `GE007_SKIP_TEX=654` removes the target `G_SETTEX` room-glass draw
  and exposes the native background underneath; it is visibly active
  (`projected_impact` default-vs-skip `64.737%`, `tower_pane` `14.908%`,
  `impact_side` `31.904%`) but worsens stock parity. `projected_impact` rises
  from `94.474%` to `95.526%` changed and luma error rises from `+4.231` to
  `+12.389`. The native underlay is not the stock-looking target.
  The scalar-composition oracle
  `/tmp/mgb64_glass_pad10092_room_glass_scalar_oracle_probe` is also now in the
  evidence set. It models `underlay + t * (default - underlay)` per pixel and
  rejects simple opacity/coverage scaling as a robust fix. Unmasked
  `projected_impact` only improves mean absolute RGB from `12.689` to `11.979`
  at `t=1.490`, with changed pixels still `94.474%`; the route-masked slice
  prefers `t=0.750` but only improves `9.700 -> 9.500`, with changed pixels
  still `100.000%`. Keep the next glass work on per-pixel source color,
  ordering, or exact RDP translucent composition.
  The required-source inversion
  `/tmp/mgb64_glass_pad10092_room_glass_required_source_probe` now makes that
  conclusion sharper. With alpha `102/255` and the skip-tex654 underlay,
  unmasked `projected_impact` says stock would require darker source luma
  (`9.63`) than native (`19.58`), but only `67.105%` of those stock-required
  source pixels are in-gamut, `32.895%` require a negative channel, and only
  `40.526%` sit inside the measured `shaderL_frag` luma band. The route-masked
  projected slice excludes `300/380` pixels due to the lower-actor mask, so the
  unmasked ROI is the primary evidence. Next glass work should target exact
  ordering/coverage/framebuffer semantics or per-pixel source/raster tracing,
  not another global opacity tweak.
  The source-reconstruction probe
  `/tmp/mgb64_glass_room_glass_source_recon_oracle_fix_verify` now answers
  that tracing question in native space. It captures texnum `654`, captures a
  same-run `GE007_SKIP_TEX=654` underlay, captures same-frame pre/post
  framebuffer PPMs around the matching room-glass draws, reconstructs the two
  owning room-glass triangles over `projected_impact`, and validates the math
  against trace center samples exactly (`uv` max delta `0.000004`, `uv1` max
  delta `0.000002`, and `t0l/t0p/t1l/t1p/shaderL/shaderP` max delta `0`).
  Coverage is `380/380` pixels. The analyzer now reconstructs both `G_SETTEX`
  texture units and evaluates the decoded two-cycle combiner; the ROM-free
  guard is `python3 tools/check_room_glass_source_reconstruction_regression.py`.
  Best synthetic-vs-default is still linear, now `mean_abs_rgb=4.066` with
  `64.211%` changed, and best synthetic-vs-stock is nearest with
  `mean_abs_rgb=11.983` and `94.211%` changed. The reconstructed linear source
  is darker than stock-required by luma mean `-12.216`; keep that
  stock-required number as a hint because stock is resized into native space.
  The remaining glass defect is not “stale underlay,” “dumped texture/UV/alpha
  is too bright,” “missing tile1/LOD combiner,” or a simple target-alpha scalar.
  It is exact translucent ordering, coverage, framebuffer-memory, or
  stock-reference pixel-output semantics. The same-frame capture now proves that
  the native destination can be sampled at the exact draw boundary: two captures
  overlap the ROI on frame `123`, with capture-local movement
  `mean_abs_rgb=2.739` and changed mean `19.213%`. The analyzer also reports the
  presentation-normalization caveat explicitly: first-pre versus skip-underlay
  is `mean_abs_rgb=5.636`, and last-post versus native-final is
  `mean_abs_rgb=3.658` because the capture is pre-output-filter while the
  screenshots are post-presentation. The same artifact also rules out a
  target-triangle self-overlap/order mistake for this ROI: all `380` pixels have
  exactly one owning room-glass row, ordered/reverse composition does not improve
  either default or stock, and the best composition stays `linear/single_last`.
  The ares stock-material follow-up
  `/tmp/mgb64_glass_pad10092_stock_rdp_tex_probe_full_heap` adds that
  instrumentation and corrects the premise: stock rooms `132` and `136` do not
  issue room-glass `G_SETTEX` at frame `2541` (`settex=0`,
  `target_settex=0` on all primary/secondary lists). They use ordinary RDP
  texture state instead. The new checkpoint trace is complete and
  non-truncated: `296` total RDP texture samples across the four room sides,
  including all `24/24` `G_SETTIMG` commands. The next stock oracle step is to
  map native texnum `654` to those stock `G_SETTIMG` image/tile candidates and
  compare exact shaded/blended output, not to search for stock `G_SETTEX` rows.
  The dump-backed mapping pass
  `/tmp/mgb64_glass_pad10092_stock_rdp_tex_dump_probe` is now the current
  reference. `tools/analyze_stock_rdp_texture_candidates.py` validates all
  `24/24` stock payload dumps at frame `2541`, finds `0` exact native
  source/source-chain hash matches for texnum `654`, and reports only weak
  naive decode similarity (`best: room=136 primary idx=41 image=0x80137a88`,
  CI-index placeholder, `MAD=25.035`, `corr=0.118`). This proves the refined
  instrumentation is productive, but also proves the next Dam glass task is not
  a direct texture remap. The next oracle must capture stock draw-state and
  pixel output for the target pane: current tile/TMEM interpretation, owning
  triangles/coverage, and same-draw framebuffer state.
  The draw-state follow-up
  `/tmp/mgb64_glass_pad10092_stock_draw_state_probe` tightens that again:
  primary/secondary texture streams for rooms `132/136` contain no `G_TRI1`,
  `G_TRI2`, or raw RDP triangle commands, so the analyzer reports
  `draw-associated texture groups: 0/24` while still capturing all `24/24`
  texture payloads. The explicit room-point scan
	  `/tmp/mgb64_glass_pad10092_stock_point_draw_state_probe` should be treated as
	  a negative control too; point data is not a display list and produced bogus
	  texture-looking values (`0x9e9e9eff`, `0x434343ff`). Next Dam glass work must
	  move to actual stock RSP/RDP execution or framebuffer-output instrumentation,
	  not more room-info stream inference.
	  The new RDP command-stream sidecar is the first positive proof that this
	  refined approach is working. The wide-window artifact
	  `/tmp/mgb64_glass_pad10092_stock_rdp_command_window_stateful_probe` captures
	  actual stock RDP execution for frames `2500..2541`: `141174` records,
	  `30558` draw ops, `192` unique draw states, and real draw-state use of the
	  room texture candidates that primary/secondary room-list tracing could not
	  associate. Candidate `0x80132c80` appears under `fmt/siz=3/2`, tile `6`,
	  combine `fc26a004/1f1493ff`, other-mode `ef992c6f/c81049d8`; candidates
	  `0x801504f8`, `0x8013b990`, and `0x8014a250` are also actually drawn. That
	  wide run was useful, but its older output had `990` truncated records.
	  The follow-up `/tmp/mgb64_glass_pad10092_stock_rdp_command_target_only_probe`
	  is now the cleaner reference after tightening the hook to maintain RDP state
	  outside the write window: stock frame `2541`, `2578` command records,
	  `690` draw ops, `46` unique draw states, zero truncation, and nonzero matches
	  for the same candidate images. Use
	  `tools/analyze_stock_rdp_command_stream.py` to summarize those sidecars.
	  The next milestone is not another texture-identity scan; it is an ROI/pixel
	  join from these stock draw states to the target pane pixels, then a faithful
	  shaded/blended-output comparison.
	  `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe` adds that ROI join at
	  conservative bbox precision. With `MGB64_ARES_TRACE_RDP_DRAW_OPS=1`, stock
	  frame `2541` records `690` draw ops, `688` valid bboxes, zero truncation, and
	  a full draw union of `[1,10,319,230]`. The `projected_impact` ROI is covered
	  by `55` stock draw ops across `9` states, with `380/380` unique ROI pixels
	  covered. The useful shortlist now includes `0x80149b28`, `0x8012f2f0`,
	  `0x8012b150`, `0x80132c80`, `0x8014a250`, `0x80141f60`, `0x801504f8`, and
	  `0x80137a88`. The analyzer now preserves ordered ROI hits and can tighten
	  attribution with `--coverage-model span`. The span summary,
	  `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack.json`,
	  still covers `380/380` `projected_impact` pixels, but corrects the bbox
	  interpretation: the conservative final owners are `0x8012b150` for `216/380`
	  pixels and late `0x80132c80` for `134/380` pixels. The bbox-only
	  `0x80132c80` sequence `559` hit drops out under span coverage; sequences
	  `552` and `553` are the late stock glass-like owners. This confirms the
	  refined approach is working, but it also draws the line: next glass work
	  needs per-pixel shaded blended output inside `projected_impact`, not another
	  room-list or raw texture search.
	  The color-attached analyzer run
	  `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_color.json`
	  turns that stack into stock screenshot targets: final-owner `0x8012b150`
	  pixels average RGB `27.171,27.072,27.174` / luma `27.113`, while late
	  `0x80132c80` pixels average RGB `36.948,36.201,36.146` / luma `36.418`.
	  These values use the route presentation frames: stock active bbox
	  `[8,2,625,474]` and native full frame `[0,0,640,480]`. The next
	  renderer/material pass should match that split output, not the bbox-only
	  `0x80132c80` dominance. Use the stock Parallel-RDP pixel probe
	  (`tools/analyze_stock_rdp_pixel_probe.py`) at the `projected_impact`
	  route logical center `183,165`. Do not map this through the `640x480`
	  screenshot or `source=640x240` presentation size; the probe reads the
	  active RDP color image. The repaired runtime proof
	  `/tmp/mgb64_rdp_pixel_probe_183.rjP13r` passed the route and recorded
	  `6387` successful readbacks, `3059` changed sample rows, and zero readback
	  failures. Late stock samples are on `fb=0x3da800/320/0/2`; the selected
	  authoritative center sample is `frame_context=2669`, `frame_draw_sequence=321`,
	  `texture_image=0x12f2f0`, bbox `[176,154,190,172]`, raw `0x000018c7`,
	  hidden `0x00000003`, decoded RGBA `[24,24,24,224]`. The handoff analyzer
	  now uses `stock_pixel.selected_sample` (last changed stock sample by
	  default) for native comparisons, not merely the last emitted row. It also
	  reports the previous emitted same-`frame_context` sample as
	  `selected_framebuffer_input`, with RGB, raw-word, and hidden-coverage
	  transitions, so the next multi-pixel pass can separate stock source,
	  framebuffer memory, hidden coverage, and final output per target.
	  The handoff join `/tmp/mgb64_glass_center_handoff_current` makes the
	  remaining ownership nuance explicit: the command-stream span/bbox model
	  contains five `0x8012f2f0` hits in `projected_impact` but also three later
	  different-texture hits covering the same center target. Treat the command
	  stream as a draw-family shortlist and the pixel probe as final-pixel
	  authority. Native settex evidence for texnum `654` remains fragment-source
	  evidence (`shaderL_frag` luma `0.0/11.0/23.0`, alpha `{102: 4}`), so the
	  next fix target is exact translucent composition from native fragment
	  source plus framebuffer/coverage, checked against the stock post-draw pixel.
	  The first native single-pixel handoff probe
	  `/tmp/mgb64_native_settex_pixel_probe_mapped_1782630417` proves the refined
	  instrumentation is answering the right question. A direct native probe at
	  stock/aligned target `183,165` found no covering texnum-`654` triangles;
	  mapping through the route presentation (`640x440` aligned capture over the
	  native visual viewport `[0,10,320,220]`) gives native logical target
	  `92,93`. The mapped native SETTEX framebuffer probe records `7`
	  target-covering changed room-glass rows across frames `120..123`, but the
	  complete selected material frame is `122`, not the screenshot-exit tail
	  frame `123`. On frame `122`, the target has two covering rows and the final
	  native post pixel is `[25,25,25]` versus stock `[24,24,24]`
	  (`mean_abs_rgb=1.0`). This corrects the earlier stale `[32,32,32]`
	  interpretation and stops a bad renderer fix: the center pixel is already
	  near stock. The next code pass needs multi-pixel stock/native final-output
	  evidence across the room-glass ROI before changing texnum-`654`
	  translucent composition, coverage, or framebuffer-memory semantics.
	  That lane now has a first batch reducer:
	  `/tmp/mgb64_glass_handoff_points_current_1782655031` refreshes center,
	  `176,158`, and `188,170` with the same analyzer. It reports center
	  `mean_abs_rgb=1.0`, left `10.0`, and lower-right `21.0`, with different
	  stock same-frame framebuffer inputs and a lower-right hidden-coverage
	  transition from `0x1` to `0x3`. Treat the next fix as per-pixel RDP
	  source/framebuffer/coverage/output work, not as a global alpha or color
	  scalar.
	  That multi-point lane has now started: `/tmp/mgb64_pixel_handoff_176_158_1782631121`
	  samples stock/aligned `176,158` mapped to native `88,89` and reports stock
	  `[32,32,32]` versus native `[22,22,22]`, while
	  `/tmp/mgb64_pixel_handoff_188_170_1782631053` samples stock/aligned
	  `188,170` mapped to native `94,95` and reports stock `[32,32,32]` versus
	  native `[11,11,11]`. The refreshed source probe
	  `/tmp/mgb64_native_pixel_source_94_95_rgba_1782631504` proves the new
	  target-source instrumentation works: the selected frame-`122` row has
	  `src_valid=1`, populated texture samples, `shaderL_frag=[0,0,0,102]`, and
	  framebuffer movement `[7,7,7] -> [11,11,11]`. Treat this as evidence for a
	  per-pixel source/filter/raster/RDP-semantics problem, not for a uniform
	  alpha or brightness scalar.
	  2026-06-28 tie-off: stop glass work at this evidence boundary unless the
	  next task is the bounded raw-state handoff. The local ares pixel probe now
	  emits raw `other`, `combine`, `env`, draw tile/tile state, decoded
	  blend/depth/combiner fields, and draw words. Resume by recapturing the
	  off-center stock/aligned `188,170` sample and comparing it to native
	  logical `94,95`; do not reopen broad screenshot sweeps or global
	  alpha/brightness changes from this trace.
	  Sampling the same stock-owner masks against the current native screenshot in
	  `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_stock_native_color.json`
	  shows the dominant native error is over-bright output: `0x8012b150` is
	  `56.632` luma versus stock `27.113` (`+29.519`), and late `0x80132c80` is
	  `56.459` versus stock `36.418` (`+20.041`). Treat those as
	  stock-mask/native-screenshot deltas, not proof of native draw ownership.
	  The targeted alpha follow-up
	  `/tmp/mgb64_glass_pad10092_room_glass_alpha_scale_probe` proves the last point:
  `settex_alpha_scale_081` worsens `projected_impact` by `+2.368` percentage
  points, while `settex_alpha_scale_125` improves it by only `-0.526` points and
  covers just `0.043%` of the tower-pane mismatch.
  The targeted order follow-up
  `/tmp/mgb64_glass_pad10092_room_glass_order_probe` re-ran
  `xlu_rdp_memory_blend` and `xlu_rdp_cvg_memory_blend`. Both variants move the
  room-glass pixels but worsen `projected_impact` stock parity by `+0.789`
  percentage points and cover `0.000%` of the tower-pane stock/native mismatch
  footprint. Keep them as negative controls, not fix candidates.
  Re-attack milestone 1 is complete: native-only glass correctness is frozen by
  `tools/glass_material_regression.sh`. The tightened gate now refuses
  too-short captures and fails on any `[GFX-DL]` diagnostic row. Latest
  milestone artifact `/tmp/mgb64_glass_material_final2` passes with
  tinted pane `10059` `opacity=0 renderOpacity=16`, legacy opacity floor
  `96`, prop pad `100` textured impacts by default (`flat=0`, legacy `flat=1`),
  one regular-glass shatter at frame `108`, a `10x9=90` shard grid, `25`
  full-active shard frames, `90` max active shards, zero screen-spanning shard
  triangles, `500` emitted shard material rows, `200` `TEXGEN-MATERIAL` rows,
  render mode `0C1849D8`, combiner `00F38E4F020A2D12`, and max shard material
  triangle `area2=0.29559` under the scaled-`field_10E0` default. The old
  unscaled negative-control run `/tmp/mgb64_glass_material_unscaled_control`
  still passes with `max_area2=0.31842`, proving the promoted fix keeps
  Surface-safe large-room projection without regressing native shard material
  locality. Start the next pass at stock/native route parity;
  do not re-open native glass basics unless this gate fails. The stock-backed
  route-parity milestone is now also complete: `tools/glass_route_parity_regression.sh`
  runs the shatter-state route and the RNG-isolation route with semantic glass
  invariants as the gate. It intentionally uses local route copies without the
  brittle first-gameplay-global audit, because the state routes pass on both
  observed startup cadences while the visual route remains composition-sensitive.
  Latest milestone artifact
  `/tmp/mgb64_glass_m2_route_gate_semantic_1782508103` passes with
  `active=90 -> 90`, destroyed pad `10004 -> 10004`, prop-position delta `0.000`,
  impact-center delta `2.140`, and RNG-isolation first-sample parity
  `match=True max_delta=0.000`. Re-attack milestone 3 is complete:
  `tools/glass_visual_oracle_regression.sh` now gates a clean static Dam-glass
  stock/native screenshot oracle. The route `dam_glass_visual_probe` now
  requires stock first gameplay global `1146`, full-health/no-HUD-damage phase
  parity, and rendered actor-composition parity for chr `10`, `11`, and `12`
  at the actual screenshot checkpoints. `tools/movement_oracle_capture.sh` now
  feeds screenshot actor guards from the concrete
  `combat_health_compare_<route>.json` checkpoint frames, which fixes the old
  timer-keyed route gap where nominal global `1190` did not name the trace
  record actually compared. Latest artifact
  `/tmp/mgb64_glass_m3_visual_oracle_gate_1782510050` passes native
  render-health, stock screenshot/control audit, health/HUD parity
  (`Bond 1.0000 -> 1.0000`, `damage_show=-1 -> -1`), no active shards or impact
  state, actor max position delta `8.111` under the `12.0` composition gate,
  no `[GFX-DL]` rows, and scoped visual sanity metrics (`whole=82.006%`,
  `center_glass=72.325%`, `left_room=65.189%`, `pp7_hud=95.916%`). This is a
  clean route/comparator milestone, not a solved material/presentation
  threshold. Re-attack milestone 4 is complete:
  `tools/glass_active_visual_isolation_regression.sh` now gates a first-active
  active-shard visual renderer-isolation route. The new route
  `dam_regular_glass_shatter_rng_visual_probe` keeps the force-player visual
  pose from the active Dam shatter route, uses the call-level RNG seed that
  proved exact shard-state parity, and now runs strict glass/prop/sample guards
  before pixel comparison even though the route is a screenshot route. Latest
  milestone artifact
  `/tmp/mgb64_glass_m4_active_visual_isolation_gate_1782543592` passes stock
  first gameplay global `1146`, clean native render health, no `[GFX-DL]` rows,
  full-health/no-damage-HUD phase, active shards `90 -> 90`, first shard timer
  `1 -> 1`, destroyed pad `10004 -> 10004`, prop-position delta `0.000`, and
  first active `glass.sample` parity (`match=True`, `max_delta=0.000`,
  `mismatches=0`). It then writes bounded visual evidence
  (`whole=92.199%`, `masked=91.831%`, `glass_burst=98.556%`,
  `damage_arc=88.542%`, `hud_viewmodel=97.414%`). This is a renderer-isolation
  milestone, not final active-shard pixel parity: the report intentionally
  records actor composition as a warning, with stock showing chr `12` as the
  only onscreen actor while native has chr `12`, chr `45`, and chr `10` onscreen.
  The next glass milestone is therefore a clean pane/view or actor-composition
  isolation for pad `10004`, followed by tighter active-shard pixel thresholds.
  Re-attack milestone 5 has now moved that fixture work to pad `10001`, the
  cleanest regular pane by guard proximity, and produced a scoped negative
  result instead of a promoted pixel gate. `tools/score_actor_composition_checkpoints.py`
  scores full visible actor composition across stock/native active-glass
  checkpoints, and `tools/glass_actor_clean_candidate_scout.sh` runs native-only
  route variants against a supplied stock active trace. The current matrix
  `/tmp/mgb64_m5_actor_clean_scout_1782557026` tested six late-warp/late-fire
  pad-`10001` variants. All six passed native screenshot/config/render-health
  checks and reached active glass, but none produced strict actor parity. The
  best organic candidate, `latewarp1385_fire1398`, aligns health, HUD timers,
  active shards, shard timer, and visible actor set at stock frame `2616` versus
  native frame `1472` (`active=90`, timer `19`, visible `[10,12]` on both), but
  still fails actor composition: chr `10` is action `15 -> 8` with position
  delta `108.542`, and chr `12` is `onscreen 0 -> 1`. The actor-force isolation
  probe `/tmp/mgb64_m5_actor_force_probe_1782557347` is also a negative control:
  forcing chr `10`/`45` positions keeps native render health clean but perturbs
  chr `45` rendered state and still fails the fixed-frame actor comparison, so
  do not promote native actor-forcing as the milestone-5 fixture.
  The checked-in candidate route was re-run end-to-end at
  `/tmp/mgb64_m5_pad10001_checked_full_1782557635`: native render health,
  stock screenshot/control audit, health/HUD phase, `max_active=90 -> 90`,
  first shard position delta `0.000`, destroyed/remove parity, and prop-position
  delta `0.000` all pass, then the visual pre-pixel actor guard rejects the
  route on the same known blockers (`chr10` action/position and chr12 onscreen).
  The wrapper correctly refuses screenshot pixel comparison for this candidate.
  The next broader scout is also complete. `tools/score_native_glass_fixture.py`
  scores a single native/stock trace for target-pane destruction, active-shard
  count, first-active timing, health/HUD state, and visible actors, while
  `tools/glass_native_fixture_scout.sh` generates round-robin native-only route
  candidates across ranked panes, yaw angles, and distances. The first broad
  run `/tmp/mgb64_m5_native_fixture_scout_1782558079` covered the top three
  panes and found no zero-actor active fixture. After fixing the scout to
  round-robin panes under the candidate cap, `/tmp/mgb64_m5_native_fixture_scout_roundrobin_1782558170`
  covered the top eight panes at distance `650`. The best target-destroyed
  actor-light candidate was pad `10092`, yaw `315`, distance `650`
  (`active=88`, target destroyed, visible actors `[44,7]`); pad `10093` also
  shatters but creates `196` active shards, so it is a multi-pane/dirty fixture.
  A stock/native no-compare probe for pad `10092`
  `/tmp/mgb64_m5_pad10092_stock_probe_1782558344` proves the glass state is
  viable (`max_active=88 -> 88`, first shard position delta `0.000`,
  destroyed/remove parity, prop-position delta `0.000`) and the visible actor
  set matches `[7,44]`, but actor composition still fails (`chr7 onscreen
  0 -> 1`, chr44 position delta `574.977`). The follow-up timing matrix
  `/tmp/mgb64_m5_pad10092_timing_scout_1782558492` improves chr44 position
  delta to `345.862` in its best case, but `best_strict` remains empty for all
  six candidates. Pad `10092` is therefore better actor-light evidence than
  pad `10001`, but still not a final actor-clean pixel fixture.
  Re-attack milestone 6 promotes that actor-light branch into a guarded
  renderer-regression harness, not a strict pixel-parity oracle.
  `tools/compare_actor_masked_visual.py` applies the same logical-viewport crop
  as `compare_screenshots.py`, then reports full and actor/viewmodel-masked
  metrics for named ROIs. `tools/glass_actor_masked_visual_regression.sh` runs
  the permanent route
  `dam_regular_glass_shatter_pad10092_actor_masked_visual_probe` as a
  no-compare stock/native capture, checks first-active health and glass state,
  scores actor composition, and only then runs the masked visual comparison.
  Latest artifact `/tmp/mgb64_glass_m6_actor_masked_1782559438` passes: stock
  first gameplay global `1147`, native render health clean, no `[GFX-DL]` rows,
  full health/no damage HUD at stock frame `2541` and native trace frame `126`,
  destroyed pad `10092 -> 10092`, active shards `88 -> 88`, first shard timer
  `1 -> 1`, first position delta `0.000`, and prop-position delta `0.000`.
  The actor score keeps the known caveat visible (`best_strict=0`, visible set
  `[7,44]` matches, chr `7` `onscreen 0 -> 1`, chr `44` position delta
  `574.977`). Masked visual metrics are bounded but still high
  (`full=91.630%`, `masked=92.092%`, `tower_pane_masked=97.325%`,
  `impact_side_masked=95.533%`, masked-excluded `22.129%`). This gate is now
  suitable for catching catastrophic active-shard/full-screen visual
  regressions while renderer parity work continues.
  The static visual oracle was also rechecked after the wrapper change at
  `/tmp/mgb64_glass_m4_static_visual_recheck_1782543649` and still passes with
  stock first gameplay global `1146`, actor max position delta `8.111`, and
  scoped visual metrics (`whole=82.006%`, `center_glass=72.325%`,
  `left_room=65.189%`, `pp7_hud=95.916%`). The current unseeded
  `dam_regular_glass_shatter_visual_probe` rerun
  `/tmp/mgb64_glass_m4_active_visual_baseline_1782542553` still fails before
  pixels on health/phase and chr `12` composition (`Bond 0.8750 -> 0.9375`,
  first shard timer `55 -> 91`, actor position delta `49.761`). Scoring those
  traces found a best active pair that aligns health and first shard timer
  (`10 -> 10`) but still diverges on chr `12` action (`15 -> 8`), actor delta
  `27.252`, and visible actor count (`2 -> 3`). The
  stock-backed visual route at
  `/tmp/mgb64_dam_regular_glass_visual_after_palette_1782505772` still fails
  before pixel comparison because chr `12` position delta is `49.761` over the
  `25.000` tolerance, while glass active count, destroyed/remove parity, and
  impact-center delta (`3.707`) pass. Manual comparison of those screenshots
  still shows the known presentation delta (`85.4%` changed, masked `84.6%`,
  `glass_burst` bright `152 -> 0`). A phase-aligned temporary route
  `/tmp/mgb64_dam_regular_glass_phase_candidate_1782505971` captures stock
  frame `2438` vs native frame `117`; it aligns health and shard timer better
  but is visually worse overall (`89.1%` changed, masked `87.8%`) and still
  carries chr `12` action drift. Continue treating this as route cadence and
  presentation-normalization work, not as a revived native shard/material bug.
  The fresh `/tmp/mgb64_dam_visual_force_player_1782479874` run passed the
  pre-pixel stock/native glass/actor guard (`90 -> 90` active shards, same
  destroyed/remove pane, chr `12` gameplay fields match) and proved exact pose
  parity at the native screenshot (`pos/cam_pos=14274.10,-474.70,1900.36`,
  `theta=0.0000`, `floor=-642.01`, room `108`). The remaining visual route is
  still evidence-only, not a hard renderer gate. At the screenshot checkpoint
  the health ring timer is aligned (`health_show=42`), but the shard phase and
  health value are not (`stock first_timer=55`, native `91`, Bond
  `0.8750 -> 0.9375`). Stock reaches the first chr `11` damage event on
  `clock=2/3`; native reaches the same source on `clock=1`. The stock ares
  route can also still land on a startup cadence where the same global-timer
  shot hits the same pane through its non-damaging local prop-impact path. The
  dirty cadence reports local prop
  impacts (`room=1`, `impact=17..19`) on `PROPDEF_GLASS` object `104`, pad
  `10004`, and `stock max_active=0`; the clean cadence
  reports world glass impact `room=107`, `impact=7`, destroyed/remove parity,
  and `90` active shards. Treat those stock misses as route-origin evidence,
  not as renderer/material regressions. The current retry-backed clean visual
  route `/tmp/mgb64_dam_regular_glass_visual_seed67_178248` passes the
  stock-origin audit, glass guard, and the older field-only chr `12` actor
  guard, but remains evidence-only: the masked aggregate reports `85.2%`
  changed pixels, the
  `glass_burst` ROI still carries a real presentation delta (`bright 152 -> 0`,
  `67.3%` changed), and combat health differs at the checkpoint
  (`Bond 0.8750 -> 0.9375`). The refreshed chr `11` route shows why:
  stock reaches the first post-glass damage after `12` stock frames with
  `clock=2/3`, while native reaches the same source after `48` native frames
  with `clock=1`. That is a route timing/cadence mismatch, not a different
  shooter or a renderer defect. The newer
  `/tmp/mgb64_dam_visual_center_guard_1782495600` run replaces the native
  world-direction shot override with the native crosshair hook, passes native
  render health, stock trace control, the field-only actor/glass guards, and the
  impact-center guard (`impact=7`, room `107`, world center delta `3.707`). The
  same run records the remaining visual backlog: masked logical-viewport change
  `84.6%`, `glass_burst=65.4%`, stock bright burst `152 -> 0`, shard phase
  `first_timer=55 -> 91`, and Bond health `0.8750 -> 0.9375`. The current
  pre-pixel route rerun
  `/tmp/mgb64_dam_visual_prepixel_healthfail_1782502757` passes stock-origin,
  native render-health, impact-center, and active glass checks, then refuses
  screenshot pixel comparison with `summary.status=fail`. The health guard fails
  because Bond health differs (`0.8750 -> 0.9375`) while stock/native first-shard
  timers are still out of phase (`55 -> 91`), and the screenshot-frame chr `12`
  actor guard fails at stock frame `2480`/global `1334` versus native trace frame
  `198`/global `197` with `position_delta=49.761` (`dx=-22.270`, `dy=34.200`,
  `dz=28.470`, `xz=36.145`). The failed artifact intentionally has no
  `visual_compare_*` output. This proves the route is still health/phase and
  foreground-composition contaminated, not clean glass/material evidence. Two
  continuation controls explain why this is still
  an evidence route, not a pixel gate:
  `/tmp/mgb64_dam_visual_native163_1782496900` aligns the sampled shard timer
  (`55 -> 55`) but worsens the screenshot because native is still in the
  damage-flash/HUD phase, while
  `/tmp/mgb64_dam_visual_native_sf2_1782497200` makes native run at speedframe 2
  and fails the impact-center guard with the shard timer over-advanced
  (`55 -> 147`). A pre-damage timing prototype
  `/tmp/mgb64_dam_visual_predamage_1782497344` captures stock frame `2436` and
  native frame `117` with matching health/HUD phase (`Bond 1.0000 -> 1.0000`,
  `damage_show=-1 -> -1`, `health_show=-1 -> -1`) and matching shard timer
  (`10 -> 10`), but it fails the chr `12` actor guard because stock chr `12` is
  action `15` and large in the foreground while native chr `12` is action `8`
  and mostly out of the burst area. Manual pixel metrics are worse
  (`glass_burst=73.4%`, masked aggregate `87.0%`) than the current checkpoint,
  so do not promote either timing shortcut or the pre-damage prototype.
- Fresh current visual-route rerun
  `/tmp/mgb64_dam_visual_prepixel_healthfail_1782502757` now fails before pixel
  comparison on both required health/HUD phase and screenshot-frame chr `12`
  actor guards. The impact-center guard passes (`3.707` world units), native
  render health is clean, stock trace control is clean, and active glass exists
  on both sides. The health guard reports `bond health differs by +0.0625`; the
  actor fields still match (`alive`, `hidden`, `onscreen`, `action`), but actor
  position does not: `position_delta=49.761` with components `dx=-22.270`,
  `dy=34.200`, `dz=28.470`, and horizontal `xz=36.145`. This supersedes the older
  last-active actor comparison from
  `/tmp/mgb64_dam_visual_current_goal_1782500828`, which reported
  `position_delta=67.706` at a different actor checkpoint. The route now fails
  on the right preconditions and writes a fail summary instead of screenshot
  metrics.
- Checkpoint-scorer result: `tools/score_visual_checkpoints.py --require-active`
  on `/tmp/mgb64_dam_visual_prepixel_healthfail_1782502757` finds no strict
  stock/native active-glass checkpoint pair for pad `10004`. The best active pair
  is stock frame `2438` versus native frame `117`: it matches health/HUD phase
  and shard timer (`10 -> 10`) but still has chr `12` action `15 -> 8`,
  actor-position delta `27.252` (`xz=26.327`), and visible actor count `2 -> 3`.
  A temporary route using that pair,
  `/tmp/mgb64_dam_visual_predamage_candidate2520_retry12_1782503463`, is checked
  negative: the clean stock branch at screenshot frame `2438` has no active glass
  or impact, Bond is already damaged, and chr `12` state/position diverges.
  Do not promote this timing shortcut.
- Negative route-control result: direct native chr `12` pose forcing is not a
  valid fix for the current active-shard visual route. Forcing chr `12` from
  frame `45` blocked the native shatter path and left native `max_active=0`.
  Delaying the force until frame `130` preserved the first shatter but caused a
  second native pane/shard activation (`max_active=180`) and still failed actor
  position (`47.651 > 25.0`). Keep `GE007_AUTO_FORCE_CHR_SCRIPT` as a
  route-only diagnostic hook, not a promoted Dam glass visual normalization.
- Native active-shard material coverage is now a hard regression guard, not just
  loose read-only evidence. `tools/glass_material_regression.sh` requires
  emitted `glass_shards` material rows, `TEXGEN-MATERIAL` rows, render mode
  `0C1849D8`, `other_mode_h=00992C60`, combiner `00F38E4F020A2D12`, geometry
  `00060205`, depth tuple `1,0,1,0,0x800`, texture tuple `56x54,32x27`, and a
  bounded material-triangle envelope. Current milestone artifact
  `/tmp/mgb64_glass_material_final2` passed with `500` emitted material rows,
  `200` texgen rows, `90` active shards, and maximum material triangle
  `area2=0.29559`.
- New route-planning evidence: `glass_props.sample` now lists all authored
  regular/tinted glass panes in a trace. Fresh native inventory
  `/tmp/mgb64_dam_glass_inventory_1782498333` records all `25` Dam panes with
  `sample_truncated=0`, and
  `/tmp/mgb64_dam_glass_actor_inventory_1782498378` ranks them against the
  native stage chr dump. The current shatter pane pad `10004` is only
  `106.7` world units from chr `12`, explaining the foreground contamination;
  pad `10001` is the cleanest regular-glass candidate by guard proximity
  (`487.3` units from nearest guard chr `10`). Existing `dam_glass_visual_probe`
  views pad `10003`, while the shatter route hits pad `10004`.
- Pad-`10001` scout result: older native-only control-room probes
  (`/tmp/mgb64_dam_pad10001_view_scout_1782498546/off_400`,
  `/tmp/mgb64_dam_pad10001_shot_dir_native_1782498695`, and
  `/tmp/mgb64_dam_pad10001_burst_dir_native_1782498744`) only cracked the pane.
  Fresh stage inventory `/tmp/mgb64_m5_stage_inventory_1782555623` reranked the
  full pane set and again selected pad `10001` as the best regular-glass
  candidate (`nearest chr=10 dist=505.6`, no guards within `500`; current pad
  `10004` has chr `12` at `131.1`). The later native grid
  `/tmp/mgb64_m5_pad10001_view_grid_1782555742` found a real shatter route
  (`r0_f400`) and the force-pose native proof
  `/tmp/mgb64_m5_pad10001_force_pose_native_long_1782555965` breaks pad `10001`
  with `max_active=90`, full health, and no damage HUD. Stock-backed route
  work then proved same-pane/same-shard-state potential but not actor-clean
  parity: `/tmp/mgb64_m5_pad10001_stock_candidate3_1782556294` passed stock
  trace control and glass/prop invariants but sampled actors before stock first
  active; after moving the stock checkpoint, `/tmp/mgb64_m5_pad10001_stock_candidate4_1782556388`
  showed stock first active at frame `2600` with matching `90` active shards,
  timer `1`, destroyed pad `10001`, and prop-position delta `0`, while actor
  state still diverged. The checked-in
  `dam_regular_glass_shatter_pad10001_visual_probe` now records the best
  organic late-warp candidate from the actor-clean scout, but it remains a
  failing pre-pixel candidate rather than a replacement active-shard visual gate;
  `/tmp/mgb64_m5_pad10001_checked_full_1782557635` is the current proof run.
  Broad Dam pane scouting now adds pad `10092` as the strongest alternate
  backlog item: it gives same-pane stock/native shard-state parity with fewer
  foreground actors than pad `10001`, but still fails actor-position/onscreen
  composition. Use the pad `10092` artifacts above before spending more time on
  lower-control-room pad `10001` timing tweaks.
- Continuation route-scouting result: pad `10092`/`10090` and the upper
  guardhouse clusters can produce active shards from native forced views, but
  they are actor-heavy. `/tmp/mgb64_dam_pad10092_shot_scout_1782499326` shatters
  pad `10090` from the pad-`10092` off-`400`/off-`800` views, while chr `44`
  runs across the foreground. `/tmp/mgb64_dam_pad10085_view_scout_1782499375`
  and `/tmp/mgb64_dam_pad10080_view_scout_1782499428` keep their local guards
  centered in the glass structure. Lower-control-room side-angle scouting
  `/tmp/mgb64_dam_lower_side_view_scout_1782499634` pushes actors farther away
  only by exposing cutaway sky/room composition, and the original lower shatter
  path still destroys pad `10004` with chr `10`/`12`/`45`/`11` onscreen by the
  useful post-shatter frames. These are candidate-history artifacts, not new
  hard visual gates.
- Instrumentation update: native deterministic coordinate and aim scripts now
  accept inclusive ranges (`START-END:x:y:z`). Native also supports
  `GE007_AUTO_FORCE_PLAYER_SCRIPT=START-END:X:Y:Z:YAW_DEG:PITCH_DEG[:EYE_OFFSET[:PAD]]`
  so stock-backed visual routes can hold the same absolute pose, camera height,
  and stan anchor as `MGB64_ARES_FORCE_PLAYER_SCRIPT`. The stock ares oracle
  supports `MGB64_ARES_CROSSHAIR_SCRIPT` and records `combat.crosshair` /
  `oracle.crosshair_force`; native now mirrors that with
  `GE007_AUTO_CROSSHAIR_SCRIPT`, which writes `crosshair_angle` and
  `field_FFC` before bullet rays are generated. The checked-in visual route now
  uses stock global-timer fire (`1278:10`) plus matching stock/native crosshair
  control (`159,114`) and repeats the pre-shatter stock RNG seed at gameplay
  frame `67`, the clean shot frame. The earlier `160,114` control landed one trace quantum
  too far right on a failing branch (`x=14278.38` instead of the clean
  `x=14276.24`), and the old gameplay-frame-`69` seed was too late for the
  global-fire route because shards activate at gameplay frame `68`.
  Continuation probes also checked `159,112`, `159,113`, `159,115`,
  `158,113`, `158,114`, `160,113`, `160,114`, gameplay-frame fire `66..69`,
  and global fire `1272,1274,1275,1276,1277`; none proved stable across both
  observed ares cadence paths. For state-parity routes, the first-gameplay-global
  split is now classified as a brittle route-control audit rather than a glass
  mismatch when the semantic invariants pass; for the visual route, it remains
  unsafe because foreground actor, health/HUD, and shot-selection phase still
  affect the screenshot. The latest identity probe proves the visual miss is not
  a guard interception; stock queues glass props with `do_damage=0`, so that
  branch creates a crack/decal but never shatters. Do not promote the visual
  route until stock shot selection is controlled independently of this cadence
  and foreground composition is clean.
- Instrumentation update: stock-backed visual routes now run declared
  glass/actor guards before pixel comparison. Dirty Dam shatter captures that
  miss the pane now fail at the active-glass/chr guard and do not write
  `visual_compare_*` artifacts. The stock ares input adapter also supports
  `phase: "global"` events keyed to `g_GlobalTimer`; this helps route-control
  probes avoid frontend-derived gameplay-frame jitter. `compare_screenshots.py`
  also supports masked aggregates via `--exclude-region`, and route JSON exposes
  that through `visual_mask_exclude_regions`. The stock trace audit now supports
  `stock_require_first_gameplay_global`; all Dam regular-glass shatter routes
  use `1146` so the known dirty-origin `1147` capture fails before glass or
  pixel comparison. The active-shard visual route also now sets
  `compare_actor_position_tolerance=25.0` for chr `12`, so future captures with
  a materially different foreground guard position fail before screenshot
  metrics are written.
- Instrumentation update: `GE007_TRACE_CHRNUM=N` and
  `MGB64_ARES_TRACE_CHRNUM=N` now expose `firecount`, `shotbondsum`, and
  `accuracyrating` for one-actor combat cadence work. Native also has
  `GE007_TRACE_GUARD_BOND_SHOTS=1` with optional
  `GE007_TRACE_GUARD_BOND_SHOTS_CHRNUM=N` and budget controls to log the exact
  guard-to-Bond accumulator decision, damage amount, clock/delta, and Bond
  health before/after.
- Ares location reminder: in this workspace, check
  `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`
  first; Linux builds use
  `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares`.
  Rebuild this local movement-oracle binary with
  `tools/prepare_ares_movement_oracle_build.sh` after trace-hook changes such as
  `glass_projection`, `MGB64_ARES_TRACE_RDP_COMMANDS`, draw-op tracing, or the
  Parallel-RDP pixel probe. A system `/Applications/ares.app` does not contain
  these hooks.
  The recent Castle/Citadel-style run log with repeated `[GFX-DL] unregistered`
  rows has the same symptom family as the dynamic-DL provenance regression; use
  the same local ares path when reproducing stock/native route evidence, and
  keep new Castle-specific artifacts in `/tmp`. If a newly pasted Castle log was
  captured before rebuilding `build/ge007`, rerun it against the current binary
  before reopening the bug; if `[GFX-DL]` rows survive on the current binary,
  add that artifact to `tools/gfxdl_provenance_regression.sh` instead of
  treating it as Dam-only evidence. Fresh current-build rerun
  `/tmp/mgb64_gfxdl_provenance_current_1782503913` matches the submitted setup
  counts, writes `11602` trace records through target frame `11600`, and keeps
  every DL resolve, bad-command, and crash counter at zero, so the pasted log is
  not reproduced by the current build on the Surface-1-equivalent setup.

## Validated Fixes

- Glass shard full-screen corruption is fixed. Runtime shard vertices are registered
  as PC-native vertex data, glass shard matrices use the native float-matrix path,
  image-table texture IDs are resolved before native `G_SETTIMG` emission, and
  CPU-clipped `glass_shards` triangles that span nearly the whole viewport are
  rejected as postclip artifacts.
- Global display-list texture preload now scans compiled native `Gfx` commands
  instead of N64 byte layout, so static impact/glass/effect textures stay in the
  texture pool before render.
- Native `texSelect()` now emits IMAGESEG-style tokens for resolved static game
  textures instead of heap pointers. This keeps fast3d on the static texture
  cache/decode path, preserving RGBA32 native-word channel order for muzzle
  flares and stable static identity for explosion-smoke frames.
- Native reset now preloads the runtime generic, explosion-smoke, scattered
  explosion, and flare image-table entries, matching N64 reset coverage plus the
  native-only flare entries that are used by muzzle/effect rendering.
- Visible first-person muzzle textures `2157..2160` are now dumped by the effect
  regression lane and audited for active alpha plus warm, non-purple RGB output.
- Direct native `--level` boots now skip authored intro camera mode by default so
  playability/dev runs start in first-person. Use `GE007_ENABLE_LEVEL_INTRO=1`
  for intro parity captures.
- First-person render camera clearance is now applied only to native gameplay
  camera rendering. It uses the existing stan/prop collision queries and moves
  only the temporary render eye; gameplay collision, prop position, and movement
  state are unchanged. Disable with `GE007_RENDER_CAMERA_CLEARANCE=0` or
  `GE007_DISABLE_RENDER_CAMERA_CLEARANCE=1`. Tune the extra surface skin with
  `GE007_RENDER_CAMERA_EXTRA_CLEARANCE`.
- Dam portal BFS now keeps the pad-140 water/sky over-admission case tight while
  also admitting projected-visible backface continuations in the service tunnel.
  `GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=0` remains a negative-control repro
  for the pad-164 blue tunnel cap, and the legacy projection/widening bundle
  remains A/B-only for the old pad-140 over-admission signature.
- Tinted glass no longer uses the old always-cloudy 96/255 alpha floor. The
  default render floor is 16/255, with `GE007_TINTED_GLASS_MIN_OPACITY=96` kept
  as a negative-control A/B and `=0` available for raw distance-opacity parity
  probes.
- Prop-attached bullet impacts now default to textured decals for all props,
  matching the N64-style path and avoiding the old white flat quads. The legacy
  flat path remains available with `GE007_FLAT_PROP_BULLET_IMPACTS=1`.
- Native gameplay now runs the stock `viSetupScreensForNumPlayers` pass before
  `skyRender`, restoring black viewport guard bands and split-screen separators
  instead of leaving the full-frame fog/sky clear color visible outside the
  active player view.
- Clamped non-texture-edge `G_SETTEX` materials now keep the shader-side N64
  filter on the full 3-point path instead of falling through the footprint
  nearest shortcut. This is a broad material policy fix, not a Dam-specific
  glass opacity hack: it improves the static Dam glass/room ROIs while avoiding
  the over-softening of globally forcing every N64-filtered texture to 3-point.
- Authored intro cameras no longer pull background magic-travel patrols out of
  stock-equivalent hidden travel state. Native now suppresses rendered-path
  magic exits and chr visibility latches for `WAYMODE_MAGIC` background guards
  while the player is in the frozen intro camera, preserving stock behavior
  without level- or chr-specific handling.
- Live mouse/gamepad look now refreshes the derived Bond view basis immediately
  after the late-frame look update in `lvlViewMoveTick`. This keeps
  `vv_theta`/`vv_verta`, `theta_transform`, `applied_view`, and the synced PC
  room camera on the same frame's basis while moving and looking.
- Fast3D PC display-list provenance now has a current dynamic range for both
  the native GFX pool and the current VTX pool. Dynamic VTX remains a possible
  sub-DL source because native `dynAllocate` stores some runtime sub-DLs there,
  but recursive PC DL dispatch now requires a plausible command stream first.
  This removes stale heap/VTX registrations from the small extra-PC-DL table
  while keeping invalid VTX/matrix/vertex-data targets from being executed as
  command streams.

## Evidence

- Build and diff hygiene:
  `cmake --build build --target ge007 -j8`
  `git diff --check`
- Current steady-view/camera validation, 2026-06-26:
  `/tmp/mgb64_camera_basis_steady_1782491772`
  - `camera_basis_regression.sh --no-build`: passed; `239` records,
    `150` moving records, `180.000` degrees of theta sweep,
    max basis error `0.000067 <= 0.001`, and max up-vector error
    `0.000001 <= 0.002`.
  `/tmp/mgb64_camera_roll_negative_1782492012`
  - Negative-control `Input.SteadyView=0` on the same moving+looking Dam probe
    reached max up-vector error `0.012503`, proving the old world tilt came
    from the movement head-up/gait basis path rather than portal/draw-distance
    code.
  `/tmp/mgb64_dam_tunnel_after_steady_1782491788`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms, fallback-off reproduced lower room admission with `3` rooms, and
    no-portal-BFS rendered `54`.
  `/tmp/mgb64_dam_portal_after_steady_1782491800`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs old
    fallback changed `19.724%`.
  `/tmp/mgb64_effect_texture_after_steady_1782491814`
  - `effect_texture_regression.sh --no-build`: passed; muzzle flare and
    explosion-smoke texture uploads still use static game texture keys.
  `/tmp/mgb64_render_camera_after_steady_1782491827`
  - `render_camera_clearance_regression.sh --no-build`: passed across Dam wall,
    glass-area aim/crouch/lean, control-room, Surface, Runway, and Facility
    contact controls; all pairs kept `pos_delta=0.000` and `col_delta=0.000`.
  `/tmp/mgb64_render_camera_vehicle_1782494706`
  - `render_camera_clearance_regression.sh --no-build`: passed all `18`
    enabled/disabled contact cases after adding Dam moving-truck coverage. The
    new truck case recorded `158` clearance hits on vehicle object `279`
    (vehicle object type `39`, pad `317`) and `40` accepted vehicle moves before
    contact, while the disabled run emitted zero clearance lines and gameplay
    `pos_delta=0.000` / `col_delta=0.000`.
  `/tmp/mgb64_glass_material_after_steady_1782491976`
  - `glass_material_regression.sh --no-build`: passed; regular glass still
    shatters into `90` pieces with `shard_large_tris=0`.
  `/tmp/mgb64_dam_mission_flow_final_1782492669`
  - `dam_mission_flow_smoke.sh --no-build`: passed; `183` records,
    `121` objective records, initial Dam objective statuses observed as
    incomplete, title stage reached at frame `122`, mission-report menu reached
    at frame `127` with `all_obj_complete=1`, `mission_failed=0`,
    `bond_kia=0`, and `dl_fail/non_dl_skip_pc/non_dl_skip_n64/unregistered_skip`
    all stayed at `0`.
  `/tmp/mgb64_dam_objective_progression_final_1782494414`
  - `dam_objective_progression_smoke.sh --no-build`: passed; `242` records and
    `242` objective records. Objective status advanced through alarms at frame
    `95`, modem flag at frame `125`, data flag at frame `165`, and bungee flag
    at frame `205`; final stage flags were `0x00001500` with Dam fail flags
    clear and all DL counters zero.
- Current DL-provenance validation, 2026-06-26:
  `/tmp/mgb64_camera_basis_dl_provenance`
  - `camera_basis_regression.sh --no-build`: passed; `239` records,
    `150` moving records, `180.000` degrees of theta sweep, and max basis error
    `0.000067 <= 0.001`.
  `/tmp/mgb64_dam_tunnel_dl_provenance`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms with `0.000%` bright-blue cap pixels, fallback-off rendered `3`
    rooms with `4.578%`, and no-portal-BFS rendered `54`.
  `/tmp/mgb64_dam_portal_dl_provenance`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    old fallback changed `19.735%`.
  `/tmp/mgb64_effect_texture_dl_provenance`
  - `effect_texture_regression.sh --no-build`: passed; muzzle flare and
    explosion-smoke texture uploads use static game texture keys, and the native
    Dam shatter render-health audit reports zero bad commands and zero DL
    resolve skips (`unregistered_skip:0`).
  `/tmp/mgb64_glass_material_dl_provenance`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `renderOpacity=16`, legacy floor at `96`, prop impacts remained textured by
    default, and regular-glass shatter produced `90` active pieces with no large
    shard triangles.
  - `tools/route_contract_smoke.sh --no-build`: passed `8/8` routes.
- Current force-player validation, 2026-06-26:
  `/tmp/mgb64_dam_visual_force_player_1782479874`
  - `movement_oracle_capture.sh --route dam_regular_glass_shatter_visual_probe
    --no-build --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`:
    passed native render health, stock oracle control audit, glass trace
    comparison, actor guard, and screenshot comparison. Native final trace now
    exactly matches the stock forced pose (`pos/cam_pos=14274.10,-474.70,1900.36`,
    `theta=0`, room `108`, floor `-642.01`) with `unregistered_skip:0`.
  - The same capture keeps the active-shard visual route as an evidence route:
    stock/native shard RNG and age still differ at the screenshot, and
    `combat_health_compare_*` warns on health/damage-HUD phase.
  `/tmp/mgb64_dam_visual_native_f199_probe`
  - Native-only continuation probe for the updated visual route passed
    screenshot health, native config audit, and strict render-health audit with
    zero bad commands, zero room fallback, and `unregistered_skip:0`. The final
    forced pose remained exact (`pos/cam_pos=14274.10,-474.70,1900.36`,
    `theta=0`, room `108`, floor `-642.01`), active shards stayed at `90`,
    and the first shard reached `age=4.97`, `piece=91`, matching the clean
    stock screenshot phase from `/tmp/mgb64_dam_visual_force_player_1782479874`
    (`age=5.00`) far better than the old frame-158 native sample (`age=2.71`).
  `/tmp/mgb64_dam_visual_f199_full_probe`
  - Fresh full stock/native rerun with the same native frame-199 checkpoint
    passed native render health and stock screenshot health, but failed the
    pre-pixel glass guard because the stock ares shot landed on a local prop
    impact (`room=1`, `impact=17`) and never destroyed pane pad `10004`
    (`stock max_active=0`, native `90`). This is the current proof that the
    visual route remains stock-harness-cadence-sensitive.
  `/tmp/mgb64_dam_visual_f199_cross_*`,
  `/tmp/mgb64_dam_visual_f199_gameplay_*`,
  `/tmp/mgb64_dam_visual_f199_global_*`,
  `/tmp/mgb64_dam_visual_f159_g1276_full_probe`
  - Continuation route controls checked nearby stock crosshair offsets,
    gameplay-frame fire starts `66..69`, and global fire starts
    `1272,1274,1275,1276,1277`. Clean runs hit world glass
    (`room=107`, `impact=7`, destroyed/remove parity, `90` active shards);
    dirty cadence runs hit local prop faces (`room=1`, `impact=17..19`) and
    fail before visual comparison. `global 1276` had successful repeats but
    still failed a later full comparison, so it is not a proven replacement for
    the checked-in stock fire timing.
  `/tmp/mgb64_dam_visual_seed_*`,
  `/tmp/mgb64_dam_visual_stable_guard_f158_full`,
  `/tmp/mgb64_dam_visual_guard_seed69_f158_*`,
  `/tmp/mgb64_dam_visual_guard_normalized_f158_*`
  - Continuation negative controls checked moving the pre-shatter stock RNG seed
    to gameplay frames `66..68`, retiming the visual checkpoint to the stable
    guard/state-route shard phase (`native frame 158`, stock first-active
    guard path), restoring the old gameplay-frame-`69` seed on that guard path,
    and an experimental stock-side global-timer normalization. None made the
    visual route robust across both observed stock startup cadences: dirty
    origins still routed the shot into local prop impact `17..19` with
    `stock max_active=0`. The stock global-normalization hook mutated stock
    state, did not solve the route, and was removed from
    `tools/prepare_ares_movement_oracle_build.sh`; the local instrumented ares
    checkout was rebuilt afterward and no longer contains
    `MGB64_ARES_NORMALIZE_GAMEPLAY_GLOBAL` / `normalize_global`.
  `/tmp/mgb64_camera_basis_force_player_guard`
  - `camera_basis_regression.sh --no-build`: passed; `239` records,
    `150` moving records, `180.000` degrees of theta sweep, and max basis error
    `0.000067 <= 0.001`.
  `/tmp/mgb64_dam_tunnel_force_player_guard`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms with `0.000%` bright-blue cap pixels; fallback-off reproduced
    `3` rooms and `4.578%`.
  `/tmp/mgb64_dam_portal_force_player_guard`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    old fallback changed `19.735%`.
  `/tmp/mgb64_effect_texture_force_player_guard`
  - `effect_texture_regression.sh --no-build`: passed; muzzle flare and
    explosion smoke still use static game texture keys and the native render
    audit reports zero bad commands and zero DL resolve skips.
  `/tmp/mgb64_glass_material_force_player_guard`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `renderOpacity=16`, prop impacts remained textured by default, regular
    glass shatter produced `90` active pieces, and large shard triangles stayed
    at `0`.
  - `tools/route_contract_smoke.sh --no-build`: passed `8/8` routes.
- Current continuation validation, 2026-06-26:
  `/tmp/mgb64_dam_visual_checked_native_f199`
  - Checked-in `dam_regular_glass_shatter_visual_probe` native-only capture
    passed screenshot health, config audit, and render-health audit at frame
    `199`, with `90` active shards and zero DL resolve skips.
  `/tmp/mgb64_camera_basis_current_178248`
  - `camera_basis_regression.sh --no-build`: passed; `239` records,
    `150` moving records, `180.000` degrees of theta sweep, and max basis error
    `0.000067 <= 0.001`.
  `/tmp/mgb64_dam_tunnel_current_178248`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms with `0.000%` bright-blue cap pixels, fallback-off reproduced
    `3` rooms and `4.578%`, and no-portal-BFS rendered `54`.
  `/tmp/mgb64_dam_portal_current_178248`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs old
    fallback changed `19.735%`.
  `/tmp/mgb64_effect_texture_current_178248`
  - `effect_texture_regression.sh --no-build`: passed; muzzle flare and
    explosion smoke still use static game texture keys, and the route render
    audit reports zero bad commands and zero DL resolve skips.
  `/tmp/mgb64_glass_material_current_178248`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `renderOpacity=16`, legacy floor at `96`, prop impacts remained textured by
    default, regular glass shatter produced `90` pieces, and large shard
    triangles stayed at `0`.
  `/tmp/mgb64_render_camera_current_178248`
  - `render_camera_clearance_regression.sh --no-build`: passed across Dam wall,
    glass-area aim/crouch/lean, control-room, Surface, Runway, and Facility
    contact controls; every pair preserved gameplay position/collision
    invariants (`pos_delta=0.000`, `col_delta=0.000`).
  `/tmp/mgb64_dam_regular_glass_probe_rebuilt_178248`
  - Rebuilt the local instrumented ares binary after removing the experimental
    stock global-normalization hook. `dam_regular_glass_shatter_probe` passed
    with the rebuilt binary: native render health stayed at zero bad commands,
    zero room fallback, and `unregistered_skip:0`; stock/native glass comparison
    stayed green with `max_active=90 -> 90`, the same destroyed/remove pane, and
    `impact_center_delta=2.140` world units under the route tolerance.
  `/tmp/mgb64_stock_dirty_identity_178248`
  - Added stock/native `impact_state.sample` identity fields and rebuilt both
    binaries. The dirty stock origin was traced to the same authored Dam pane,
    not to chr `12` or another foreground prop: first impact
    `prop_type=1`, `prop_obj_type=42`, `prop_obj=104`, `prop_pad=10004`,
    `room=1`, `impact=18`; second impact was the background/world glass hit.
    Because stock queues glass props with `do_damage=0`, the first branch creates
    a local crack/decal and leaves `max_active=0`.
  `/tmp/mgb64_dam_regular_glass_probe_origin_retry_178248`
  - Added `stock_require_first_gameplay_global=1146` plus a stock-capture retry
    wrapper for cadence-sensitive ares routes. Attempt `1/4` preserved a dirty
    `1147` origin; attempt `2/4` reached clean `1146` and the hard
    `dam_regular_glass_shatter_probe` route passed: native render health clean,
    stock trace audit clean, `max_active=90 -> 90`, same destroyed/remove pane,
    and `impact_center_delta=2.140`.
  `/tmp/mgb64_dam_regular_glass_rng_origin_retry_178248`
  - The exact first-shard-sample diagnostic route now uses the same stock-origin
    guard. Attempt `1/4` preserved a dirty `1147` origin; attempt `2/4` reached
    clean `1146` and `dam_regular_glass_shatter_rng_isolation_probe` passed
    with `first_sample_match=True`, `max_delta=0.000`, `max_active=90 -> 90`,
    and the same destroyed/remove pane.
  `/tmp/mgb64_dam_rng_isolation_fresh_1782499929`
  - Fresh retry-backed RNG-isolation rerun passed after the same dirty-origin
    rejection: stock attempt `1/4` preserved global `1147`, attempt `2/4` reached
    expected global `1146`, and stock/native glass state still matched exactly
    (`max_active=90 -> 90`, `first_shard_timer=1 -> 1`,
    `first_position_delta=0.000`, `first_sample_match=True max_delta=0.000`,
    same destroyed/remove pane).
  `/tmp/mgb64_glass_material_shard_guard_1782500397` (superseded by
  `/tmp/mgb64_glass_material_final2`)
  - Strengthened `glass_material_regression.sh --no-build` passed. In addition to
    the existing tinted-glass, prop-impact, shatter-count, and no-large-triangle
    checks, it now captured `500` emitted `glass_shards` material rows and `48`
    `TEXGEN-MATERIAL` rows, all with render mode `0C1849D8`,
    `other_mode_h=00992C60`, combiner `00F38E4F020A2D12`, geometry `00060205`,
    depth tuple `1,0,1,0,0x800`, texture tuple `56x54,32x27`, and maximum
    material triangle `area2=0.01978`; the current scaled-`field_10E0` guard
    passes with maximum material triangle `area2=0.29559`.
  `/tmp/mgb64_dam_regular_glass_visual_origin_retry_178248`
  - The current `dam_regular_glass_shatter_visual_probe` evidence route now
    rejects dirty stock origins before comparison. Attempts `1..3/4` preserved
    `1147` dirty-origin traces; attempt `4/4` reached clean `1146`, passed the
    glass guard (`90 -> 90`), chr `12` actor guard, native render health, and
    screenshot comparison plumbing. This run also exposed that the old stock
    RNG seed frame `69` was too late for the global-fire route: the clean stock
    shot starts at gameplay frame `67` and shards activate at `68`.
  `/tmp/mgb64_dam_regular_glass_visual_seed67_178248`
  - Retimed the visual route's stock pre-shatter RNG seed to gameplay frame
    `67`. Attempts `1..3/4` preserved dirty `1147` origins; attempt `4/4`
    reached clean `1146` and passed the same stock-origin, glass, actor, native
    render-health, and screenshot plumbing. The visual comparison improved but
    remains an evidence route: whole changed pixels dropped `88.7% -> 85.7%`,
    masked changed pixels dropped `90.4% -> 85.2%`, and `glass_burst` changed
    `84.6% -> 67.3%` with bright stock pixels `540 -> 152`; combat health still
    warns on Bond health `0.8750 -> 0.9375`.
  `/tmp/mgb64_surface1_gfxdl_repro_178248`
  - Follow-up for the submitted Surface 1/Castle-style run log. The setup counts
    exactly match the user log (`136` bound pads, `251` waypoints, `25`
    waygroups, `16` patrol paths, `47` AI lists, `9` guards, `203` objects).
    A deterministic forward-movement capture ran to screenshot frame `12000`;
    `run.log` contains no `[GFX-DL]` warning rows, screenshot health passed, and
    render trace health passed with `dl_fail:0`, `non_dl_skip_pc:0`,
    `non_dl_skip_n64:0`, and `unregistered_skip:0`.
  `/tmp/mgb64_gfxdl_provenance_surface1_1782493652`
  - `gfxdl_provenance_regression.sh --no-build`: passed on the same submitted
    setup counts (`136` bound pads, `251` waypoints, `25` waygroups, `16` patrol
    paths, `47` AI lists, `9` guards, `203` objects). The trace reached
    `last_frame=11602`, crossing the submitted warning window around frame
    `11450`, with no `[GFX-DL]` log rows, zero bad commands, zero crash recovery,
    and all DL resolve counters still zero (`dl_fail`, `non_dl_skip_pc`,
    `non_dl_skip_n64`, and `unregistered_skip`).
  `/tmp/mgb64_gfxdl_provenance_fresh_1782498932`
  - Fresh continuation rerun after the latest pasted playback log:
    `tools/gfxdl_provenance_regression.sh --no-build` matched the submitted
    setup counts, produced `11602` trace records, reached
    `last_frame=11602`, and kept every display-list resolve counter at zero
    (`dl_fail`, `non_dl_skip_pc`, `non_dl_skip_n64`, `unregistered_skip`, plus
    texture/matrix/vtx/settimg counters), with `max_bad_cmds=0` and
    `max_crashes=0`.
  `/tmp/mgb64_citadel_gfxdl_repro_178248`
  - Internal `--stage-id 40` is not the same setup as the submitted log because
    `UsetupcatZ` is absent and the stage boots with an empty setup plus room
    fallback. Keep this artifact as a shared-renderer smoke only: it also ran to
    frame `12000` with screenshot health passing and zero display-list resolve
    skips, but it is not a Citadel gameplay validation.
- Current final validation, 2026-06-26:
  `/tmp/mgb64_camera_basis_final`
  - `camera_basis_regression.sh --no-build`: passed; `239` records,
    `150` moving records, `180.000` degrees of theta sweep, and
    max basis error `0.000067 <= 0.001`. The same focused forward-plus-look
    capture measured `0.021000` before the fix.
  `/tmp/mgb64_effect_texture_after_basis_fix`
  - `effect_texture_regression.sh --no-build`: passed; muzzle texture
    provenance and explosion-smoke texture uploads used static game texture keys
    instead of host-pointer-derived cache identity.
  `/tmp/mgb64_dam_tunnel_visibility_after_basis_fix`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms with `0.000%` bright-blue cap pixels, fallback-off rendered `3`
    rooms with `4.578%`, and no-portal-BFS rendered `54`.
  `/tmp/mgb64_dam_portal_after_basis_fix`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    no-portal-BFS stayed `0.000%` changed.
  `/tmp/mgb64_glass_material_after_basis_fix`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `renderOpacity=16`, legacy floor at `96`, prop impacts remained textured by
    default, and regular-glass shatter produced `90` active pieces with no large
    shard triangles.
  `/tmp/mgb64_render_camera_after_basis_fix`
  - `render_camera_clearance_regression.sh --no-build`: passed across Dam,
    Surface, Runway, and Facility controls; all enabled/disabled pairs preserved
    gameplay position/collision invariants (`pos_delta=0.000`,
    `col_delta=0.000`).
  `/tmp/mgb64_dam_visual_dirty_guard_check`
  - Forced dirty visual-route control: failed before screenshot pixel comparison
    with stock `max_active=0` vs native `90`, missing stock `last-active` chr
    `12`, and no `visual_compare_*` artifacts written.
  `/tmp/mgb64_dam_visual_stock_global_y114`
  - Temporary stock global-timer fire control passed the pre-pixel glass/actor
    guard on the clean global-`1146` path (`90 -> 90` active shards, chr `12`
    matched at `last-active`) and then wrote visual metrics. This proves the new
    guard order still allows clean evidence routes through.
  `/tmp/mgb64_dam_visual_1147_global_y114`
  - Forced global-`1147` control still failed before pixel comparison
    (`stock max_active=0`, native `90`). The stock trace already has Bond damage
    at the first fire input, so keep this classified as dirty route state rather
    than a renderer/material fix target.
  `/tmp/mgb64_effect_texture_regression_after`
  - `effect_texture_regression.sh --no-build`: passed; muzzle flare
    `img=2128` resolved through cache `0x8000000000000850` with
    `static=1 lods=1`, and explosion-smoke frames `2176..2181` resolved through
    `0x8000000000000880..0885` with `static=1 lods=0`.
  `/tmp/mgb64_dam_tunnel_visibility_effect_after`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed on the current
    effect-fix binary; default rendered `6` rooms with `0.000%` bright-blue cap
    pixels, fallback-off rendered `3` rooms with `4.578%`, and no-portal-BFS
    rendered `54`.
  `/tmp/mgb64_dam_portal_effect_after`
  - `dam_portal_regression.sh --no-build`: passed on the current effect-fix
    binary; default rendered `5` rooms, old fallback rendered `40`,
    no-portal-BFS rendered `16`, and default vs no-portal-BFS stayed `0.000%`
    changed.
  `/tmp/mgb64_glass_material_effect_after`
  - `glass_material_regression.sh --no-build`: passed on the current effect-fix
    binary; tinted default stayed at `renderOpacity=16`, legacy floor at `96`,
    prop impacts remained textured by default, and regular-glass shatter
    produced `90` active pieces with no large shard triangles.
  `/tmp/mgb64_dam_glass_guard_object_trace`
  - Native-only guard-object trace passed and logs chr `11` AK47 object hits on
    pane pad `10004`: `[GUARD_OBJECT_SHOT]` rows at native frames `99`, `102`,
    and `105` target object `104` / type `42`, with `item=8`, action `8`, and
    firecount advancing `3 -> 6 -> 9`; the final hit triggers the shatter.
  `/tmp/mgb64_dam_glass_chr11_stock_native`
  - Fresh stock/native chr `11` tracking passed with the local instrumented ares
    binary. Native reaches active glass at frame `108` with chr `11`
    `action=8`, `hidden_bits=512`, held right item `8`, and firecount `9`.
    Stock reaches active glass at frames `2431..2432` (`global=1282`,
    gameplay `69`) with chr `11` present, alive, `action=8`,
    `hidden_bits=520/640`, `bg_ai=1`, the same pane pad `10004`
    destroyed/removed, and later Bond damage (`bond=0.875`,
    `damage_show=33` by frame `2479`). Treat the older no-active-stock capture
    as stale route/timing evidence.
  `/tmp/mgb64_render_camera_effect_after`
  - `render_camera_clearance_regression.sh --no-build`: passed on the current
    effect-fix binary across Dam, Surface, Runway, and Facility controls; all
    enabled/disabled pairs preserved gameplay position/collision invariants
    (`pos_delta=0.000`, `col_delta=0.000`).
  `/tmp/mgb64_dam_tunnel_visibility_final_1782468600`
  - `dam_tunnel_visibility_regression.sh --no-build`: passed; default rendered
    `6` rooms with `0.000%` bright-blue cap pixels, fallback-off rendered `3`
    rooms with `4.578%`, and no-portal-BFS rendered `54`.
  `/tmp/mgb64_dam_portal_final_seq_1782468700`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    no-portal-BFS stayed `0.000%` changed.
  `/tmp/mgb64_render_camera_final_1782468600`
  - `render_camera_clearance_regression.sh --no-build`: passed across Dam,
    Surface, Runway, and Facility controls; all enabled/disabled pairs preserved
    gameplay position/collision invariants (`pos_delta=0.000`,
    `col_delta=0.000`).
  `/tmp/mgb64_glass_material_final_1782468600`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `renderOpacity=16`, legacy floor at `96`, prop impacts remained textured by
    default, and regular-glass shatter produced `90` active pieces with no large
    shard triangles.
  `/tmp/mgb64_playability_dam_final_seq_1782468700`
  - `playability_smoke.sh --level 33 --no-build`: passed; movement,
    screenshot-health, render-health, and config audit passed.
  `/tmp/mgb64_rng_isolation_final_1782468800`
  - `dam_regular_glass_shatter_rng_isolation_probe`: stock-backed route passed
    exact first-sample shard parity (`max_delta=0.000`), first position delta
    `0.000`, destroyed/removed pane parity, and `max_active=90 -> 90`.
  `/tmp/mgb64_regular_glass_probe_final_1782468800`
  - `dam_regular_glass_shatter_probe`: stock-backed route passed impact-state
    semantic match, destroyed/removed pane parity, and first world impact-center
    delta `2.140`.
  `/tmp/mgb64_shatter_visual_range_hold_1782468300`
  - `dam_regular_glass_shatter_visual_probe`: stock-backed visual route passed
    capture, render-health, control audit, glass-state comparison, and chr `12`
    actor guard with the new range-held native face/aim scripts. Pixel deltas
    remain high and are tracked as presentation/material backlog, not as shard
    state failure.
  `/tmp/mgb64_visual_seeded_1782470241`,
  `/tmp/mgb64_visual_origin1147_1782471166`
  - `dam_regular_glass_shatter_visual_probe`: the new stock
    `combat.health` trace and `combat_health_compare_<route>.json` artifact
    work. The global-`1146` stock startup path passes glass/actor state and shows
    damage-HUD phase mismatch (`18` stock frames after first active glass versus
    `48` native frames). The global-`1147` stock startup path still misses the
    pane (`glass.active=0`) and therefore fails the active-shard guard. Do not
    promote this route to a hard pixel gate until the stock shot is controlled
    independently of that frontend origin jitter.
  - `tools/route_contract_smoke.sh`: passed `8/8` route contracts, including
    the compact range-held Dam shatter visual route.
  - `bash -n` passed for the focused shell tools, and `python3 -m py_compile`
    passed for the focused route/comparison/audit Python tools.
- Current post-settex-filter validation:
  `/tmp/mgb64_dam_clamped_3point_default_1782417569`
  - `dam_glass_visual_probe` stock/native route passed native render-health,
    native config audit, stock screenshot-health, stock control audit, and
    visual-comparison plumbing after promoting the clamped non-texture-edge
    `G_SETTEX` 3-point policy. Active-normalized whole-frame delta is now
    `255434 / 296250` (`86.2%`), with named ROI deltas `center_glass`
    `20033 / 25200` (`79.5%`), `pp7_hud` `31180 / 33000` (`94.5%`), and
    `left_room` `26309 / 39600` (`66.4%`).
  `/tmp/mgb64_glass_material_regression_96930`
  - `glass_material_regression.sh --no-build`: passed; tinted opacity floor,
    textured prop impacts, and deterministic regular-glass shatter stayed
    clean (`first_shatter_frame=108`, `pieces=90`, `shatters=1`,
    `shard_large_tris=0`).
  `/tmp/mgb64_playability_smoke_97051`
  - `playability_smoke.sh --level 33 --no-build`: passed; movement,
    screenshot-health, render-health, and config-override audit stayed clean.
  `/tmp/mgb64_render_camera_clearance_97125`
  - `render_camera_clearance_regression.sh --no-build`: passed; Dam
    wall/glass/control-room, Surface, Runway, and Facility contact probes all
    preserved gameplay `pos_delta=0.000` and `col_delta=0.000`.
  `/tmp/mgb64_dam_portal_regression_97677`
  - `dam_portal_regression.sh --no-build`: passed; default traversal stayed at
    `5` rendered rooms, old fallback rendered `40`, and default vs
    no-portal-BFS stayed `0.000%` different.
  `/tmp/mgb64_dam_viewmodel_mtx_1782418510`
  - `dam_glass_visual_probe` passed again after adding stock/native sampled
    viewmodel matrix projections. Visual metrics stayed unchanged
    (`255434 / 296250`, `86.2%`; `center_glass` `79.5%`; `pp7_hud` `94.5%`),
    as expected for diagnostic-only tracing.
  - `tools/compare_viewmodel_projection.py --hand right` reports stock PP7
    projection metadata as `fovy=60.0`, native as fixed `fovy=50.0`, and valid
    sampled matrix anchors with max view-space drift `1.985`. Comparing stock
    `screen50` to native projected screen leaves at most `6.40` logical pixels
    of drift, while comparing stock's current world-FOV projection to native
    fixed-50 leaves up to `33.69` pixels. Treat the fixed-50 viewmodel
    projection/root path as checked for this checkpoint; the remaining PP7 ROI
    mismatch is more likely weapon material/lighting/presentation, fine mesh
    draw state, or timing than a gross root/projection error.
  `/tmp/mgb64_dam_zmode_sweep_1782419007`
  - A/B-tested `ZMODE_XLU` `GL_LESS`, `ZMODE_DEC` `GL_LESS`,
    `ZMODE_DEC` no polygon offset, and `ZMODE_DEC` offsets `0,0`, `-1,-1`,
    and `-4,-4` on the current cleaned route. Every case was pixel-identical to
    default at the checkpoint: whole-frame `86.222%`, `center_glass`
    `79.496%`, `left_room` `66.437%`, and `pp7_hud` `94.485%`. Treat those
    depth-function and polygon-offset variants as checked negative controls for
    this static glass view.
  `/tmp/mgb64_dam_draw_order_1782419462`
  - Added native `rooms.vis.draw_sample` telemetry from the same room draw-list
    array used by the stock oracle. At the checkpoint, native draw order is
    `[109,120,108,107]`, matching stock `rooms.vis.rendered_sample`; the older
    native `rooms.vis.sample` value `[107,108,109,120]` is only a sorted room
    set. Translucent room draw order is therefore checked for this view.
  `/tmp/mgb64_dam_visual_full_post_draworder_1782419659`
  - Full stock/native `dam_glass_visual_probe` capture passed after the native
    draw-order telemetry change. Position delta at the checkpoint is `0.0` on
    all axes, yaw delta is `-0.0043` degrees, and room order still matches
    stock (`[109,120,108,107]`). Active-normalized whole-frame delta is
    `255725 / 296250` (`86.3%`), with `center_glass` `20030 / 25200`
    (`79.5%`), `pp7_hud` `31641 / 33000` (`95.9%`), and `left_room`
    `26131 / 39600` (`66.0%`).
  `/tmp/mgb64_dam_blend_color_diag_1782420349`
  - Native-only A/B matrix against the same stock frame checked alternate
    alpha blend factors. The fresh default stayed at whole-frame `86.321%`,
    `center_glass` `79.484%`, `left_room` `65.987%`, and `pp7_hud` `95.882%`.
  - `GE007_DIAG_ALPHA_BLEND=premult`, `add`, `inv_alpha`, and `copy` all
    worsened the glass/room ROIs dramatically (`center_glass` `94.036%` to
    `96.242%`, `left_room` `97.530%` to `98.449%`). The current
    `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` backend blend remains the stock-backed
    baseline for these effective XLU modes.
  `/tmp/mgb64_dam_rgb555_finalpass_diag_1782420890`
  - Native-only A/B subset checked final-pass-only RGB555 output quantization.
    The fresh default again stayed at whole-frame `86.321%`, `center_glass`
    `79.484%`, `left_room` `65.987%`, and `pp7_hud` `95.882%`.
  - `GE007_DIAG_OUTPUT_RGB555=1` worsened the checkpoint to whole-frame
    `91.620%`, `center_glass` `88.365%`, and `left_room` `83.646%`; `=dither`
    also worsened it to whole-frame `88.790%`, `center_glass` `84.183%`, and
    `left_room` `73.631%`. Final RGB555 output quantization is therefore a
    checked negative control for this view.
  `/tmp/mgb64_glass_material_regression_5561`
  - `glass_material_regression.sh --no-build`: passed after adding the
    default-off blend/color diagnostics; tinted opacity floor, prop-impact
    texture path, and deterministic regular-glass shatter stayed healthy
    (`first_shatter_frame=108`, `pieces=90`, `shatters=1`).
  `/tmp/mgb64_playability_smoke_5637`
  - `playability_smoke.sh --level 33 --no-build`: passed; Dam movement,
    screenshot-health, render-health, and config-override audit stayed clean.
  `/tmp/mgb64_render_camera_clearance_5672`
  - `render_camera_clearance_regression.sh --no-build`: passed; all Dam
    wall/glass/control-room contact probes plus Surface, Runway, and Facility
    cross-level controls preserved gameplay `pos_delta=0.000` and
    `col_delta=0.000`.
  `/tmp/mgb64_dam_portal_regression_5560`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    no-portal-BFS stayed `0.000%` different.
- Current local reruns from this review pass:
  `/tmp/mgb64_playability_smoke_40400`
  - `playability_smoke.sh --level 33 --no-build`: 1/1 passed; movement,
    screenshot-health, render-health, and config-override audit all passed
  `/tmp/mgb64_glass_material_regression_40439`
  - `glass_material_regression.sh --no-build`: passed; tinted opacity floor,
    prop-impact texture path, deterministic regular-glass shatter, and shard
    bounds stayed clean
  `/tmp/mgb64_render_camera_clearance_40440`
  - `render_camera_clearance_regression.sh --no-build`: passed; Dam exterior,
    glass-area aim/crouch/lean, control-room look/weapon-switch, Surface,
    Runway tank, Facility door, and Facility tinted-glass contact probes all
    preserved gameplay `pos`/`col` while exercising render-eye clearance
  `/tmp/mgb64_dam_portal_regression_40990`
  - `dam_portal_regression.sh --no-build`: passed; default room count stayed
    `5`, old fallback rendered `40`, and default vs no-portal-BFS remained
    `0.000%` different at the repro frame
  `/tmp/mgb64_dam_glass_visual_force_preinput_1782396226`
  - `dam_glass_visual_probe`: stock/native route passed capture and audits,
    but the old stock force hook still overwrote the renderer's room basis
  `/tmp/mgb64_dam_glass_visual_route_audited_1782398848`
  - `dam_glass_visual_probe`: stock/native route passed after the force hook was
    changed to preserve ROM-owned `current_model_pos` and the stock screenshot
    hook was moved to the post-presentation ares viewport. The native route now
    runs with an isolated `native_savedir`, pins `Video.RetroFilter=on`, and
    audits the generated config so route overrides cannot silently miss; repo-root
    `ge007.ini` remains untouched. The stock PPM shows the same Dam
    glass/control-room composition as native at full 640x480 output; the
    active-normalized diff remains high at `95.9%`
  `/tmp/mgb64_dam_glass_room_dl_modes_fixed_1782399669`
  - `dam_glass_visual_probe`: stock/native route passed with stock
    `rooms.dl` summaries enabled. The stock force hook applied 998 times with
    998 stan resolutions; stock screenshot-health and native screenshot-health
    both passed. The active-normalized diff is still `95.928%`
    (`284188 / 296250`), so the route remains useful evidence but not a
    thresholded visual parity pass.
  `/tmp/mgb64_dam_glass_intro_route_1782408618`
  - `dam_glass_visual_probe`: stock/native route passed after adding native
    authored-intro warmup. Native now captures at mission timer `1190` after
    warping at deterministic input frame `288`, rather than warping at frame
    `40` before hidden patrols have advanced. Chr `45` is not rendered on either
    side at the checkpoint, and rendered actor counts match at `3`, so the old
    visible hidden-background guard no longer contaminates the glass material
    comparison. The trace still exposes patrol-state drift: stock keeps chr `45`
    unseen in `WAYMODE_MAGIC` at room `[106,105]`, while native has already
    latched `CHRFLAG_HAS_BEEN_ON_SCREEN`, alternates `WAYMODE_0`/`WAYMODE_4`,
    and sits in room `[105]` by the native checkpoint. The active-normalized
    diff remains high at `95.3%` (`282468 / 296250`).
  `/tmp/mgb64_dam_fov_sweep_1782408748`
  - Native-only A/B against the updated stock frame: `Video.ViewmodelFov=0` did
    not materially change the checkpoint, and `Video.FovY=50` lowered coarse
    changed-pixel percentage to `94.6%` mostly by zooming/cropping the room away
    from stock composition. Keep the route at `Video.FovY=60` until a stronger
    projection/viewport proof exists.
  `/tmp/mgb64_dam_chr45_frustum_probe_1782409547`
  - Native-only A/B with `GE007_CHRBEAMS_FRUSTUM=1` on the same intro-warmed
    route stayed render-clean and kept the screenshot healthy. It reduced chr
    `45` `PROPFLAG_ONSCREEN` duration (`549` to `304` frames), but did not
    prevent the first native latch at frame `290` / global `289`. The root cause
    is therefore not solved by simply enabling the frustum gate; the remaining
    defect is hidden/background patrol visibility and `WAYMODE_MAGIC` exit
    parity.
  `/tmp/mgb64_dam_render_ab_1782409194`
  - Native-only renderer negative controls (`GE007_DISABLE_AUTO_GAMEPLAY_VI_FILTER`,
    `GE007_DISABLE_N64_FILTER`, point filtering, `GE007_NO_FOG`,
    `GE007_FOG_USE_LINEAR_DEPTH`, and
    `GE007_SKIP_FP_WEAPON_PROJECTION=1`) did not materially improve the
    intro-warmed checkpoint. Simple output filtering, fog toggles, and the broad
    foreground projection skip are not the Dam-glass fix.
  `/tmp/mgb64_dam_glass_review_refresh_1782409845`
  - Pre-fix full stock/native refresh of `dam_glass_visual_probe` passed native
    screenshot-health, native config audit, native render-health, stock
    screenshot-health, stock oracle control audit, and visual-comparison
    plumbing with the route's current HiDPI-disabled native metadata.
    Active-normalized diff is `95.2%` (`282091 / 296250`). At the checkpoint,
    both sides report `slots=46`, `live=36`, `alive=36`, `hidden=10`, and
    `rendered=3`; chr `45` is not on the on-screen prop list and is not rendered
    on either side. Its state still diverges: stock has `mode=6`, `seen=0`,
    rooms `[106,105]`, pos `[13456.60,-633.45,2371.17]`; native has `mode=4`,
    `seen=1`, rooms `[105]`, pos `[13259.26,-546.35,2246.56]`. The first native
    seen/on-screen latch remains frame `290` / global `289`.
  `/tmp/mgb64_dam_magic_trace_long_1782410572`
  - Native focused `GE007_TRACE_MAGIC_TRAVEL=1` probe proved the frame-290
    divergence was not a stale `PROPFLAG_ONSCREEN` read. At global `289`,
    `chrlvTickPatrol` saw `onscreen=0` and `stan_related=0`, so the stock-shaped
    rendered-path shortcut was waking chr `45` because the native authored intro
    camera had rendered the hidden route path.
  `/tmp/mgb64_dam_magic_fix_route_1782410743`
  - Current full stock/native refresh after the intro magic-travel fix passed
    native screenshot-health, native config audit, native render-health, stock
    screenshot-health, stock oracle control audit, and visual-comparison
    plumbing. Chr `45` now matches stock at the checkpoint: `mode=6`,
    `seen=0`, not on-screen, not on the native on-screen prop list, not
    rendered, rooms `[106,105]`, and position
    `[13456.60,-633.45,2371.17]`. Native no longer records any first-seen frame
    for chr `45`. Active-normalized visual diff remains high at `95.3%`
    (`282243 / 296250`), confirming the remaining Dam-glass work is material,
    foreground, sampling, and draw-order parity rather than this hidden patrol.
- Fresh current-binary reruns:
  `/tmp/mgb64_glass_material_current_1782400572`
  - `glass_material_regression.sh --no-build`: passed; tinted default stayed at
    `opacity=0 renderOpacity=16`, legacy floor stayed at `96`, prop impacts used
    textured decals by default, and the deterministic regular-glass hit stayed
    bounded (`first_shatter_frame=108`, `pieces=90`, `shatters=2`,
    `max_active=180`, `max_area2=2.30`).
  `/tmp/mgb64_dam_portal_current_1782400623`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, default vs
    no-portal-BFS stayed `0.000%`, and default vs old fallback changed `18.932%`.
  `/tmp/mgb64_render_camera_clearance_current_1782400636`
  - `render_camera_clearance_regression.sh --no-build`: passed; Dam
    wall/glass/control-room, Surface, Runway tank, Facility door, and Facility
    tinted-glass contact probes all applied clearance only in enabled captures
    while preserving gameplay `pos_delta=0.000` and `col_delta=0.000`.
  `/tmp/mgb64_dam_fog_room_trace_1782400527`
  - native-only focused trace for `dam_glass_visual_probe`: screenshot and
    render-health passed. Room-alpha logs for rooms `107`, `108`, and `109` at
    the probe show the same env-alpha families already observed (`102`, `165`,
    `135`, `127`) and `fog=(0,0,0)` on the glass triangles; the active fog
    coefficients (`fog_mul=25600`, `fog_off=-25344`) clamp these near panes to
    zero distance fog rather than exposing a missing fog-factor bug.
- Native VI setup restoration:
  `/tmp/mgb64_dam_vi_setup_fix_1782401018`
  - `dam_glass_visual_probe` native capture passed screenshot-health,
    render-health, and config-override audits after restoring
    `viSetupScreensForNumPlayers` before native `skyRender`.
  - Native top/bottom guard rows changed from solid fog clear `(16,48,96)` to
    black/near-black, matching the stock black-bar pass. Full-frame
    stock/native delta improved from `96.1%` to `88.5%`; active-normalized
    delta remains high at `95.4%`, so this fixes a presentation defect without
    closing material/foreground parity.
  `/tmp/mgb64_dam_portal_vi_setup_fix_1782401036`
  - `dam_portal_regression.sh --no-build`: passed; default rendered `5` rooms,
    old fallback rendered `40`, no-portal-BFS rendered `16`, and default vs
    no-portal-BFS stayed `0.000%`.
  `/tmp/mgb64_glass_material_vi_setup_fix_1782401036`
  - `glass_material_regression.sh --no-build`: passed; tinted opacity, textured
    prop impacts, and deterministic regular-glass shatter stayed healthy
    (`first_shatter_frame=108`, `pieces=90`, `shatters=2`, `max_active=180`).
  `/tmp/mgb64_render_camera_clearance_vi_setup_fix_1782401070`
  - `render_camera_clearance_regression.sh --no-build`: passed; Dam
    wall/glass/control-room, Surface, Runway tank, Facility door, and Facility
    tinted-glass contact probes still preserve gameplay `pos_delta=0.000` and
    `col_delta=0.000`.
- Focused post-VI material/foreground probes:
  - `/tmp/mgb64_dam_vi_filter_off_1782401501`,
    `/tmp/mgb64_dam_vi_filter_320x220_1782401502`, and
    `/tmp/mgb64_dam_vi_filter_320x240_1782401503` checked the native output VI
    filter path after black-bar restoration. Filter-off and forced `320x220`
    were slightly worse than the current auto/forced-`320x240` path; sampling
    policy is not the next broad Dam-glass fix.
  - `/tmp/mgb64_dam_transparency_trace_1782401504` traced drawclass,
    room-alpha, blend-state, prop context, and shard candidates at the same
    Dam-glass checkpoint. Room secondary glass remains the expected stock-owned
    content, with room `109` secondary env alphas `102`, `165`, `135`, and
    `127`, rooms `108`/`107` secondary env alphas `102`/`165`, and no
    pathological `effect=glass_shards` emission at this static checkpoint.
    Large visible full-height transparent triangles are labelled as
    `effect=glass`/prop glass, not `glass_shards`.
  - `/tmp/mgb64_dam_skip_room_secondary_1782401506` and
    `/tmp/mgb64_dam_skip_raw_c41049d8_1782401507` are negative controls:
    skipping secondary room display lists or all `0xC41049D8` draws removes
    expected glass/furniture content and worsens stock comparison. The fix
    should not be a display-list skip or opacity clamp.
  - `/tmp/mgb64_dam_hand_trace_1782403331` rebuilt the local ares oracle with
    raw first-person hand-state fields and reran the stock/native Dam-glass
    route. The capture passes stock screenshot-health, stock control audit,
    native screenshot-health, native render-health, and active-normalized visual
    comparison plumbing. The visual delta remains `282,907 / 296,250`
    (`95.5%`), with stock active bbox `(8,2,625,474)` and native active bbox
    `(0,19,640,442)`. Raw foreground equip state matches at the checkpoint:
    right hand item/weapon `5`, magazine `7`, pending `-1`, invisible `1`;
    left hand item/weapon `0`, pending `-1`, invisible `0`. The remaining
    foreground mismatch is therefore transform/projection/material parity, not
    an obvious wrong equipped item.
  - `/tmp/mgb64_dam_weapon_proj_baseline_1782403740` and
    `/tmp/mgb64_dam_weapon_proj_skip_1782403740` A/B-tested the native
    first-person weapon projection load with `GE007_SKIP_FP_WEAPON_PROJECTION=1`.
    Skipping the native projection changed only the foreground-heavy region
    (`12,078 / 307,200`, `3.9%`) and worsened stock comparison from `95.5%` to
    `95.7%`; the current native weapon projection is not the broad Dam-glass
    fix candidate.
  - `/tmp/mgb64_dam_viewmodel_trace_1782404148` rebuilt the local ares oracle
    with read-only stock viewmodel matrix fields and reran the route. Stock
    trace now includes `watch.hands.*.vm` with hand root/world/muzzle vectors,
    embedded model pointer evidence, and render_pos slot 0 decoded from N64
    `Mtx`. At the checkpoint, stock raw equip state still matches. The stock
    screenshot was written at ares frame `2337` / global `1191`, where the PP7
    render_pos root is `[11.11,-21.20,-32.15]`; later same-global samples move
    toward `[11.76,-20.94,-33.75]`, so frame phase matters for foreground
    comparisons.
  - `/tmp/mgb64_dam_native_render_root_1782404503` exposed native
    `wr_render_root` / `wr_render_root_diag`. Native render_pos slot 0 matches
    the native authored root (`[12.52,-20.85,-33.45]` at screenshot exit), so
    the remaining foreground offset is introduced before renderer submission,
    not by decoding segment-3 matrices in Fast3D.
  - `/tmp/mgb64_dam_sway0_1782405174` A/B-tested stock-parity
    `Input.ViewmodelSway=0`. It moved native PP7 root closer to the stock
    render_pos band (`[11.93,-20.94,-33.48]` at native global `44`), proving
    default native sway contributes to first-person root drift. Whole-checkpoint
    active-normalized comparison stayed effectively unchanged (`95.5%` changed),
    so sway is now pinned off in the Dam visual route but is not the broad
    Dam-glass parity fix. `Video.ViewmodelFov=60` was also A/B-tested and
    worsened active-normalized comparison (`95.6%` changed), so it was not
    promoted into this route.
  - `/tmp/mgb64_dam_room_alpha_cc_1782405758` extended the native
    `GE007_TRACE_ROOM_ALPHA` output with the effective combiner/options after
    room-alpha LUT handling. At the Dam glass checkpoint, rooms `107`, `108`,
    and `109` secondary room glass use `cc=0x00F78E4F020A2D12` with
    `effcc=cc`, `cc_changed=0`, `settex=1`, `blend=alpha`, `fog=1`, and
    `texedge=0`. Decoding that combiner shows the expected stock secondary-LUT
    env-alpha form in alpha cycle 1: `(COMBINED - 0) * ENVIRONMENT + 0`.
    Therefore the static pane mismatch is not a missing room-alpha combiner
    rewrite.
  - `/tmp/mgb64_dam_room_alpha_scale_15_1782406166` A/B-tested
    `GE007_DIAG_ROOM_ALPHA_ENV_SCALE=1.5` on room-matrix `G_SETTEX` XLU
    materials. The capture stayed healthy, but stock comparison worsened from
    `95.5%` to `95.9%` changed, center/right pane SAD worsened, and the left
    pane became brighter rather than more stock-like. Simple env-alpha scaling
    is not the Dam glass material fix.
  - `/tmp/mgb64_dam_warp1_1782406219` moved native `GE007_AUTO_WARP_FRAME` from
    `40` to `1` to match the stock force-player script's early gameplay timing.
    The final rooms, player pose, and PP7 root stayed the same, and targeted
    pane ROI metrics were effectively unchanged. Native warp timing is not the
    visible guard/pane mismatch cause for this checkpoint.
  - `/tmp/mgb64_dam_camera_basis_1782411530` added read-only stock/native
    camera-basis trace fields (`render_cam_*`, `view_basis.vv_verta*`,
    `headlook`, `headup`). It proved that the stock force-player checkpoint was
    repeatedly writing a horizontal `CollisionAppliedView`/`CollisionAppliedUp`
    while leaving `vv_verta=-4`, whereas native's one-shot pad warp later
    recomputed `applied_view` from `vv_verta` and tilted `cam_up` to about
    `[0.05,1.00,0.04]`. This was validation-route drift, not a room-basis or
    render-camera-position defect.
  - `/tmp/mgb64_dam_visual_route_patched_1782411979` then applied a native
    same-height face-coordinate sync immediately after the pad warp. Native
    `cam_up` became stock-like (`[-0.00,1.00,0.00]`), full-frame changed pixels
    dropped to `55.37%`, and active-normalized changed pixels dropped from
    `95.3%` to `87.02%`. The Dam visual route now includes that sync so the
    remaining pixel delta is not polluted by a native-only checkpoint pitch.
  - `/tmp/mgb64_dam_slot_layout_1782414276` rebuilt native and the stock oracle
    after correcting the first-person hand smoothing slot layout to match the
    ROM assembly: `field_954..field_974` are the blend accumulators and
    `field_930..field_950` are the scaled offsets consumed by gun position and
    look/up smoothing. The route passed native render-health, stock screenshot
    health, stock control audit, and stock JSON validity. The active-normalized
    visual delta stayed in the same band (`257846 / 296250`, `87.04%`), so this
    was a real state/parity fix, not the visible Dam-glass fix.
  - `/tmp/mgb64_glass_material_after_slot_1782414382` reran the focused Dam
    glass regression after the slot-layout fix. It passed: tinted default still
    renders near-clear (`opacity=0 renderOpacity=16`), prop impacts use textured
    decals by default, and the deterministic regular-glass hit creates bounded
    shards (`90` active, no large shard triangles).
  - `/tmp/mgb64_dam_chr12_trace_1782414497` proved the intro-warmed visual route
    still contained actor-state noise: native chr `12` reached `ACT_ANIM` with
    `CHRHIDDEN_BACKGROUND_AI` at the checkpoint while stock chr `12` remained
    visible in `ACT_STAND`. `/tmp/mgb64_dam_no_intro_full_1782414655` showed
    that a direct-gameplay native warp keeps chr `12` stock-like and preserves
    the chr `45` magic-travel fix.
  - `/tmp/mgb64_dam_route_no_intro_promoted_1782414766` is the current checked-in
    route shape: direct native gameplay warp at frame `40`, same-height face
    sync at frame `41`, stock force-player basis sync, and `Input.ViewmodelSway=0`.
    It passes native render-health/config audit, stock screenshot/control
    audits, and visual-comparison plumbing. Active-normalized delta improves to
    `256274 / 296250` (`86.5%`). Remaining visible noise is still substantial:
    foreground PP7 placement/projection, scan/filter/shading, translucent glass
    semantics, and at least chr `11` actor-state drift.
  - `/tmp/mgb64_dam_vmfov60_1782415138` and
    `/tmp/mgb64_dam_vmfov0_1782415198` rechecked native
    `Video.ViewmodelFov` on the cleaned no-intro route. `60` worsened the
    active-normalized delta to `257482 / 296250` (`86.9%`), and `0` worsened it
    to `257039 / 296250` (`86.8%`). The route's default fixed weapon FOV is not
    the dominant PP7 or glass parity issue.
  - `/tmp/mgb64_dam_drawbbox_1782415461` added default-off renderer
    `GE007_TRACE_DRAWCLASS_BBOX=1` evidence. At the checkpoint, native weapon
    triangles occupy logical bbox `[193.4,168.2]-[251.8,240.0]` (roughly
    `[386.8,336.4]-[503.6,480.0]` at the 640x480 capture), while room geometry
    spans the full logical viewport and effect triangles are absent. This
    separates the foreground PP7 footprint from static room-alpha glass and
    confirms the visible center pane is not a shard/effect artifact.
  - `/tmp/mgb64_dam_roomalpha05_1782415614` A/B-tested
    `GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5` on the cleaned route. It darkened the
    native frame but worsened comparison to `273682 / 296250` (`92.4%`), so the
    Dam static glass mismatch is not solved by simply reducing room env alpha.
    Keep the next target on N64 XLU blender/coverage/depth/sampling/order
    semantics for `0xC8104DD8`/`0xC81049D8`, not an opacity clamp.
  - `/tmp/mgb64_dam_stockshot1188_1782415763` checked stock screenshot timing
    sensitivity. It reproduced the best current delta (`256274 / 296250`), but
    stock runs still show a one-global-frame startup/timer jitter around
    `first_gameplay_global=1146/1147`. Treat one-frame PP7 and chr animation
    differences in this route as noisy until the stock oracle timing is made
    more deterministic.
  - `/tmp/mgb64_dam_regions_route_1782416314` added checked-in
    `visual_regions` for the cleaned Dam route and proved the region metrics
    persist through `summary_dam_glass_visual_probe.json`. The current
    active-normalized whole-frame delta is `256695 / 296250` (`86.6%`). Named
    ROIs split the remaining failure into `center_glass`
    `20543 / 25200` (`81.5%`), `pp7_hud` `31635 / 33000` (`95.9%`), and
    `left_room` `26461 / 39600` (`66.8%`). Earlier A/B runs on the same ROIs
    show `Video.ViewmodelFov=0`/`60` primarily churns the foreground
    `pp7_hud` region without improving room/glass parity, while
    `GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5` severely worsens room/glass regions
    (`center_glass` `93.5%`, `left_room` `95.9%`). Use these regions as the
    first acceptance slice for future PP7, room-surface, and static-glass fixes.
  - Focused static-material A/Bs on the named ROIs ruled out simple foreground
    FOV/no-weapon changes and broad texture-mode toggles as the static glass
    fix. `Video.ViewmodelFov=50` remained the best foreground setting in a
    `50/60/70/80/90/0` sweep; skipping first-person weapon rendering did not
    change `center_glass`. `GE007_FORCE_ROOM_POINT_FILTER=1`,
    `GE007_DIAG_NOPERSPECTIVE_SETTEX_CC=0x00F78E4F020A2D12`, and simple
    no-perspective/fog toggles worsened or no-oped the glass ROI. The strongest
    material signal was the existing
    `GE007_DIAG_SETTEX_CLAMPED_NON_TEXEDGE_N64_FILTER_ALWAYS_3POINT=1` policy:
    `/tmp/mgb64_dam_material_clamped_3point_1782417289` improved whole-frame
    delta to `255725 / 296250` (`86.3%`) and `center_glass` to
    `20030 / 25200` (`79.5%`), with `left_room` also improving to
    `26131 / 39600` (`66.0%`). Global
    `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1` reached similar ROI numbers but a
    slightly worse whole-frame result, so the narrower clamped non-cutout
    `G_SETTEX` policy was promoted instead.
  - Focused z-mode/depth A/Bs on the current route did not move a pixel:
    `ZMODE_XLU`/`ZMODE_DEC` `GL_LESS`, disabling `ZMODE_DEC` polygon offset,
    and offset values `0,0`, `-1,-1`, and `-4,-4` all matched default exactly.
    Native draw-order telemetry now also proves the checkpoint's room draw order
    matches stock (`109,120,108,107`). A later native-only blend/color matrix
    also ruled out final RGB555 output quantization and simple global alpha
    blend-factor substitutions. Keep static-glass work focused on N64
    coverage/color behavior, per-material presentation semantics, or missing
    fine-grained inherited RDP state unless a new repro shows actual
    depth/order/blend-factor sensitivity.
- General regression bundle:
  `/tmp/mgb64_validation_camera_clearance_1782379033`
  - `playability_smoke.sh`: 2/2 passed
  - `scripted_look_smoke.sh`: passed
  - `spawn_health_check.sh`: passed
  - `damage_hud_smoke.sh`: 1/1 passed
  - `route_contract_smoke.sh` for `dam_forward_stop` and `dam_strafe_matrix`: 2/2 passed
  - `soak_stability.sh --level 33 --frames 1800`: 1/1 passed
- Glass A/B:
  `/tmp/mgb64_glass_validation_1782379146`
  - shards on/off both have zero render-health failures
  - shards-on shows 99 active shards after Dam tag 19 shatter
  - visual diff is localized; no full-screen triangles
- Current focused glass regression:
  `/tmp/mgb64_glass_material_regression_1782405253`
  - passed tinted-glass, prop-impact, and regular-glass bullet-hit probes
  - default tinted pane `opacity=0` renders at floor `16`; legacy A/B floor is
    `96`
  - prop impacts use textured decals by default (`flat=0`) and legacy flat only
    behind the A/B switch
  - regular-glass shot produced `90` shards with no large
    `effect=glass_shards` triangles (`max_area2=0.00`)
- Wall-contact probes:
  `/tmp/mgb64_dam_camera_clearance_1782378846`
  - pad 140 volume-contact repro triggers render-camera clearance
  - render health stays clean
  `/tmp/mgb64_dam_wall_look_1782378980`
  - left/right look sweeps while pressed into the collision volume stay stable
- Automated render-camera clearance regression:
  `tools/render_camera_clearance_regression.sh`
  - `/tmp/mgb64_render_camera_clearance_1782405286`
  - Current run passes Dam exterior wall, long-contact/soak, glass-area
    aim/crouch/lean, control-room look/weapon-switch, Surface wall, Runway tank,
    Facility door, and Facility tinted-glass contact cases
  - enabled/disabled captures keep `pos_delta=0.000` and `col_delta=0.000`,
    confirming the clearance helper remains render-only
  - `/tmp/mgb64_render_camera_clearance_soak_1782385681`
  - Dam exterior wall, long wall contact, 600-frame exterior wall soak, Dam
    glass-area, Dam control-room, Surface spawn wall, Runway tank contact,
    Facility spawn wall, and Facility tinted-glass contact cases all trigger the
    render-eye clearance helper
  - Dam glass/contact variants cover aim, crouch, aim+C-left/C-right peek input,
    control-room look sweep, and weapon switching
  - Dam exterior/glass contacts assert `PROP_TYPE_OBJ` blockers; the Facility
    door-contact case asserts `PROP_TYPE_DOOR` hits on door object `158`, pad
    `77`
  - The Facility opening-door case uses normal `B` interaction to move door
    `158` from stationary to opening, then asserts 46 clearance hits with
    `hit_prop_type=2`, `hit_door_state=1`, and nonzero `hit_door_open`
  - The Facility closing-door case opens door `158`, reverses it with a second
    normal `B` interaction, then asserts 62 moving-door clearance hits with
    `hit_prop_type=2`, `hit_door_state=2`, and nonzero `hit_door_open`
  - The Runway tank contact case asserts 240 clearance hits with
    `hit_prop_type=1` and `hit_obj_type=45` against tank object `288`, pad `44`
  - The Dam exterior wall soak runs to frame `599/600` and asserts 1,038
    clearance hits with `hit_prop_type=1`, `hit_obj_type=3`, and tile room `69`
    while gameplay `pos`, collision `col`, and current room remain identical
    between enabled and disabled captures
  - The Facility tinted-glass contact case asserts 178 clearance hits in room
    `19` while `GE007_TRACE_GLASS=1` records 240 traces for tinted pane pad
    `10098`; the clearance collision metadata remains `hit_prop_type=-1`
    because this pane resolves as a room-edge/pathblocker contact rather than a
    direct `PROPDEF_TINTED_GLASS` prop hit
  - Disabled-mode captures emit zero clearance traces
  - Gameplay `pos`, collision `col`, and current room remain identical between
    enabled and disabled captures for every case
- Portal/room visibility probe:
  `/tmp/mgb64_dam_backface_fallback_gate_1782380110`
  - default portal behavior at the pad 140 repro renders 15 rooms and removes
    the water/sky opening
  - the old legacy projection/widening bundle restores the old 54-room image
    and the water/sky opening
  - default output is pixel-identical to the `GE007_PORTAL_BFS=0` diagnostic for
    this frame, without disabling portal BFS globally
- Automated portal regression:
  `tools/dam_portal_regression.sh`
  - `/tmp/mgb64_dam_portal_regression_1782405272`
  - Current run keeps default traversal tight (`5` rendered rooms), while the
    old legacy projection/widening bundle over-admits (`40` rooms) and remains
    an A/B-only negative control
  - `/tmp/mgb64_dam_portal_regression_after_threshold_1782393668`
  - default portal BFS renders 5 rooms at the pad-140 wall-contact repro after
    stock-style portal bbox clipping defaults
  - the legacy A/B bundle (`GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=1`,
    `GE007_PORTAL_LEGACY_PROJECT_CLAMP=1`,
    `GE007_PORTAL_PARENT_CLIP_MIN_SPAN=8`,
    `GE007_PORTAL_ACCEPTED_MIN_SPAN=24`,
    `GE007_PORTAL_RETRY_SCREEN_CLIP=1`) renders 40 rooms and differs from
    default by 18.932% of pixels
  - default and `GE007_PORTAL_BFS=0` differ by 0.000% of pixels at the repro
    frame
- Dam tunnel under-admission probe:
  `tools/dam_tunnel_visibility_regression.sh`
  - pad 164 is the current service-tunnel draw-distance repro: with projected
    backface traversal disabled, default BFS admits only rooms `[99,100,101]`
    and exposes the blue tunnel cap; with projected-visible traversal enabled,
    it admits the visible continuation including room `98`
  - the regression fails if default renders fewer than 6 tunnel rooms, misses
    room `98`, or has more than `0.50%` bright-blue cap pixels
- Post-fix validation:
  `/tmp/mgb64_validation_portal_backface_gate_1782380133`
  - `playability_smoke.sh --level "33 41"`: 2/2 passed
  - `scripted_look_smoke.sh --level 33`: passed
  `/tmp/mgb64_validation_portal_backface_gate_remaining_1782380173`
  - `spawn_health_check.sh --level "33 41"`: 2/2 passed
  - `damage_hud_smoke.sh --level 33`: passed
  - `route_contract_smoke.sh` for `dam_forward_stop` and `dam_strafe_matrix`: 2/2 passed
  - `soak_stability.sh --level 33 --frames 1800`: 1/1 passed
  - `git diff --check`: passed
  - `spawn_health_check.sh --all`: 20/20 passed after the portal fallback gate
- Post-fix glass A/B:
  `/tmp/mgb64_glass_validation_after_portal_1782380294`
  - shards on/off both pass render-health and screenshot-health audits
  - shards-on records one Dam tag 19 shatter and 39 shard-frame trace lines
  - visual diff is localized: 2,592 pixels, 0.844% of the screenshot
- Automated glass material regression:
  `tools/glass_material_regression.sh`
  - `/tmp/mgb64_glass_material_regression_shard_guard_short_1782395457`
  - tinted pane `10059` logs raw `opacity=0`, default `renderOpacity=16`, and
    legacy-floor `renderOpacity=96`
  - pad-100 prop impact logs three create events on prop pad `100`
  - default prop-impact render logs `flat=0`; legacy A/B logs `flat=1`
  - deterministic Dam regular-glass bullet hit logs a first shatter at frame
    `108` with a `10x9=90` shard grid, then a second nearby pane shatter at
    frame `123`; maximum active shard count is `180`
  - shard-effect triangle tracing records seven large but bounded
    `effect=glass_shards` emitted triangles, max `area2=2.30` at frame `127`,
    and zero pathological preclip `glass_shards` candidates
  - all five captures pass render-health and screenshot-health audits
- Automated render camera clearance regression:
  `tools/render_camera_clearance_regression.sh`
  - `/tmp/mgb64_render_camera_clearance_after_portal_clip_1782394057`
  - Dam exterior wall, glass-area aim/crouch/lean, control-room, and long wall
    soak cases all preserve simulation position/collision (`pos_delta=0`,
    `col_delta=0`) while recording render-camera clearance hits
  - cross-level contact probes for Surface, Runway, and Facility still pass
- ROM-oracle screenshot trigger plumbing:
  - `/tmp/mgb64_movement_oracle_timer_plumbing_1782386096`
  - `/tmp/mgb64_movement_oracle_game_timer_1782386130`
  - `tools/movement_oracle_capture.sh --native-only --route dam_forward_stop`
    still passes the existing frame-triggered native route after the route-schema
    changes
  - a temporary `dam_forward_stop_timer_probe` route using
    `native_screenshot_game_timer=650` passes native screenshot-health,
    render-health, and movement audits, proving the wrapper's game-timer
    screenshot path without weakening built-in route contracts
  - the generated ares oracle now accepts `MGB64_ARES_SCREENSHOT_GAME_TIMER` and
    gates it on the configured stock target stage before dumping the stock PPM
- ROM-oracle stock direct-position hook:
  - `/tmp/mgb64_ares_force_oracle_1782386575`
  - `/tmp/mgb64_stock_force_player_probe_1782386826`
  - `/tmp/mgb64_stock_force_threshold_wrapper_1782386959`
  - a fresh temporary instrumented ares build compiles with
    `MGB64_ARES_FORCE_PLAYER_SCRIPT` support
  - a temporary stock Dam route loads one force-player event, writes a stock PPM
    screenshot, passes stock screenshot-health and stock control audits, and
    records 19 force-player applications in the stock oracle trace
  - `tools/audit_oracle_trace.py --min-force-player-applies 5` and
    `tools/movement_oracle_capture.sh --stock-trace ... --no-compare` both pass
    against that stock trace, proving the hook is auditable through route
    metadata instead of manual log inspection
- Dam glass stock/native visual oracle:
  - route: `tools/rom_oracle_routes/dam_glass_visual_probe.json`
  - earlier room-set capture:
    `/tmp/mgb64_dam_glass_stock_clip_defaults_1782393411`
  - earlier 4:3/pre-input force-hook capture:
    `/tmp/mgb64_dam_glass_visual_force_preinput_1782396226`
  - room-basis-correct raw-VI capture:
    `/tmp/mgb64_dam_glass_visual_active_profile_1782397294`
  - current post-presentation/config-audited capture:
    `/tmp/mgb64_dam_glass_visual_route_audited_1782398848`
  - current room-DL evidence capture:
    `/tmp/mgb64_dam_glass_room_dl_modes_fixed_1782399669`
  - wrapper status: `ROM Oracle Capture: PASS`
  - native screenshot-health and render-health pass at game timer `1190`
  - stock screenshot-health and stock control audit pass; the latest capture
    records 998 stock force-player applications with resolved pad `100`, stan
    `0x801c5a70`, stan room `109`, `bg_current_room=109`, camera
    `(14051.53,-474.7,1545.11)`, and floor Y `-642.01`
  - the rebuilt instrumented ares hook also applies direct-position events from
    the scripted controller path before input is consumed. The earlier raw-VI
    capture still showed the same top-half presentation after that timing fix,
    so late force timing was not the screenshot mismatch cause; the later
    post-presentation dump closes the buffer/presentation issue separately.
  - the force-player hook no longer writes `player->current_model_pos` as the
    forced camera position. That field is the room-render basis; preserving the
    ROM-maintained value changes the stock PPM from an exterior/control-room
    mismatch into the same Dam glass/control-room composition as native.
  - native visual capture now pins stock-like local config through route
    `native_config` overrides, including `Video.FovY=60`,
    `Video.WindowWidth=640`, `Video.WindowHeight=480`, and
    `Video.WindowMode=windowed`, plus `Video.RetroFilter=on` for stock-like
    240p output filtering. The native wrapper now passes `--savedir
    <out-dir>/native_savedir`, so local `ge007.ini` play settings do not
    contaminate the oracle and route overrides do not persist back into the repo
    config. The wrapper also audits the saved native config after capture and
    fails if a route override is missing or resolved to the wrong value.
  - stock and native both render the same portal-admitted room set at this
    checkpoint: stock sample `[109,120,108,107]`, native sample
    `[107,108,109,120]`
  - actor visibility is now traced on both sides through the top-level `actors`
    summary. Fresh capture `/tmp/mgb64_dam_actor_visibility_1782406837` passes
    native screenshot-health/render-health, stock screenshot-health/control
    audit, and active-normalized visual comparison plumbing. It proves matching
    chr-slot population (`slots=46`, `live=36`, `alive=36`) but exposes a
    stock/native actor-visibility divergence at the same forced glass
    checkpoint: stock has `onscreen=1`, `rendered=3`, hidden count `10`, with
    nearest visible/rendered guard chr `12` in room `108`; native has
    `onscreen=4`, `rendered=4`, hidden count `9`, and includes hidden
    background-AI chr `45` near the camera at `[14389.69,-535.41,1595.58]`
    in room `109`. Stock chr `45` is also hidden/background-AI, but remains
    farther away at `[13456.60,-633.45,2371.17]` in room `106` and is not in
    the rendered room set. That capture was not valid glass-opacity evidence by
    itself; the follow-up chr tracking below resolves this specific actor leak
    for normal direct-boot play.
  - `/tmp/mgb64_dam_actor_frustum_1782406940` A/B-tested the existing
    default-off native `GE007_CHRBEAMS_FRUSTUM=1` gate. It stayed render-clean
    and reduced native onscreen count from `4` to `2`, culling chr `11` and
    chr `10` from `PROPFLAG_ONSCREEN`, but it did not fix the chr `45`
    position/state divergence. The broad next step is not simply enabling the
    frustum gate; it is background-AI/offscreen-patrol movement and onscreen
    parity.
  - follow-up `GE007_TRACE_CHRNUM=45` /
    `MGB64_ARES_TRACE_CHRNUM=45` captures isolated that divergence to
    `WAYMODE_MAGIC` patrol travel. Stock advances the path segment internally
    (`segdistdone`) while keeping the guard prop snapped until segment
    completion. Native was restoring model-root interpolation into
    `PropRecord.pos`, letting hidden/background chr `45` drift through rendered
    rooms. The native fix now holds prop updates for `ACT_PATROL`/`ACT_GOPOS`
    while waydata mode is `WAYMODE_MAGIC`. A user-like direct boot after the fix
    keeps chr `45` at `[14458.14,-633.45,1540.83]` through the first segment,
    snaps it to stock's room-106 position `[13456.60,-633.45,2371.17]`, and
    records no visible/rendered rows through frame 899. The glass visual route
    now enables the authored native intro and delays the deterministic pad-100
    warp until input frame `288`; refreshed route capture
    `/tmp/mgb64_dam_glass_intro_route_1782408618` keeps chr `45` hidden,
    offscreen, and not rendered at the stock/native screenshot checkpoint.
  - portal `191` (`[106,107]`, control `[0,6]`) is now rejected by native for
    the same reason stock leaves depth byte `0`: projected left edge is just
    outside the viewport (`x≈340.5`) and the clipped bbox collapses to
    `(319.0,79.8,319.0,230.0)`
  - default-off A/B switches remain for the old native over-admission aids:
    `GE007_PORTAL_PARENT_CLIP_MIN_SPAN`,
    `GE007_PORTAL_ACCEPTED_MIN_SPAN`, and
    `GE007_PORTAL_RETRY_SCREEN_CLIP=1`
  - stock screenshots are now dumped from the post-`Screen::refresh()` presented
    viewport rather than the raw top-half VI input. The current stock log records
    `source=640x240 output=640x480`; stock active bbox is `(8,2,625,474)` with
    96.4% bbox coverage and 10.9% black pixels.
  - screenshot diff is still intentionally not thresholded. The latest
    intro-warm route uses active-normalized comparison by default and reports
    282,468 changed pixels out of 296,250 (`95.3%`). Stock screenshot-health
    reports 21,230 unique colors, 10.70% black pixels, and mean luma 50.75;
    native reports 22,812 unique colors, 9.86% black pixels, and mean luma
    59.91. This is now a foreground projection/screen-mapping,
    material/color/alpha, sampling, and draw-order backlog item, not a visible
    hidden-actor artifact.
- Dam glass presentation metrics:
  - `/tmp/mgb64_dam_glass_stock_clip_defaults_1782393411/visual_compare_presentation_recheck.json`
  - stock active bbox is `(8,1,625,237)`, active bbox coverage `48.2%`, margins
    `{left:8, top:1, right:7, bottom:242}`, center offset `(0.5,-120.5)`,
    classified as an offset/top-aligned presentation
  - native active bbox is `(0,57,640,366)`, active bbox coverage `76.2%`,
    margins `{left:0, top:57, right:0, bottom:57}`, center offset `(0,0)`,
    classified as centered letterbox presentation
  - `/tmp/mgb64_dam_glass_stock_clip_defaults_1782393411/visual_compare_active_presentation_recheck.json`
    keeps active-normalized diff at 98.9%, so presentation policy is a
    prerequisite for thresholding, but it does not explain the full material
    mismatch by itself
  - `/tmp/mgb64_dam_glass_visual_active_profile_1782397294` proved the native
    side captures a full 640x480 4:3 frame while the older stock PPM was still a
    raw top-half VI-style dump with active bbox `(8,1,625,237)` and black
    scanlines below y=237.
  - `/tmp/mgb64_dam_glass_visual_route_audited_1782398848` closes that raw-VI
    dump problem and proves route config isolation/audit: stock now writes the
    post-presentation viewport as 640x480, with active bbox `(8,2,625,474)`,
    margins `{left:8, top:2, right:7, bottom:4}`, and center offset
    `(0.5,-1.0)`. Native remains full-frame with active bbox `(0,0,640,480)`.
    Visual inspection shows the same
    corridor/control-room composition on both sides; the remaining mismatch is
    color/material/alpha, glass visibility, and likely translucent draw-order
    parity.
- Dam glass room-alpha attribution:
  - `/tmp/mgb64_dam_room_alpha_attr_1782394790`
  - current isolated route-config trace:
    `/tmp/mgb64_dam_glass_room_alpha_trace_current_1782398636`
  - current combiner/options trace:
    `/tmp/mgb64_dam_room_alpha_cc_1782405758`
  - the Dam glass checkpoint's translucent room geometry is now attributed to
    secondary room display lists rather than anonymous vertex room ids
  - frames `40..44` log room `109` secondary alpha as the dominant contributor
    (135 lines), with room `107` secondary and room `108` secondary also present
    (20 lines each)
  - room `109` secondary emits raw modes `0xC81049D8` at env alpha `127` and
    `0xC8104DD8` at env alphas `102`, `135`, and `165`; room `107`/`108`
    secondary use matching `0xC8104DD8` pane modes at offsets `0x60`/`0x78`
  - the current trace keeps these modes classified as blended room alpha, not
    texture-edge/cutout: `0xC8104DD8` records `blend=1`, `texedge=0`,
    `zcmp=1`, `zupd=0`, `zmode=0xC00`; `0xC81049D8` records matching blend and
    depth-compare behavior with `zmode=0x800`
  - the combiner/options trace records `cc=effcc=0x00F78E4F020A2D12` for these
    room glass triangles with `cc_changed=0`. That value is already the
    stock-style env-alpha replacement form for secondary room glass, so the
    next material work should not chase a missing `DL_LUT_SECONDARY` combiner
    rewrite.
  - `/tmp/mgb64_dam_room_skip_1782394845/room_skip_contact.png` confirms visual
    ownership: skipping room `109` changes 34.377% of pixels on the left pane
    span, room `108` changes 21.019% on the right span, room `107` changes
    7.042% near the right edge, and room `120` is negligible at 0.706%
- Dam glass stock room-DL material evidence:
  - `/tmp/mgb64_dam_glass_room_dl_modes_fixed_1782399669`
  - the stock oracle now records read-only `rooms.dl` summaries from
    `g_BgRoomInfo` for the rendered-room sample. At the checkpoint the stock
    sample is `[109,120,108,107]`
  - room `109` secondary uses pointer `0x8009ec20`, 87 scanned commands,
    `setenv=4`, env alphas `[102,165,135,127]`, raw hash
    `0xDC23ED0F18651E89`, and combiner hash `0xF04B461DD904DD13`
  - room `108` secondary uses pointer `0x8009f860`, 35 scanned commands,
    `setenv=2`, env alphas `[102,165]`, raw hash `0x8851AD38B3A298DD`, and
    combiner hash `0x99A6310A7629DCAE`
  - room `107` secondary uses pointer `0x800a0310`, 35 scanned commands,
    `setenv=2`, env alphas `[102,165]`, raw hash `0xEFF3176D62B31B2D`, and
    combiner hash `0x99A6310A7629DCAE`
  - those stock secondary env-alpha sequences exactly match the current native
    room-alpha trace for rooms `109`, `108`, and `107`: room `109` logs env
    alphas `102`, `165`, `135`, and `127`; rooms `108` and `107` log `102` and
    `165`
  - the scanned stock primary/secondary room buffers show material setup but no
    direct render-mode or triangle commands at this checkpoint
    (`setothermode=0`, `modes=[]`, `rdptri=0`). The native raw modes
    `0xC81049D8` and `0xC8104DD8` therefore need to be treated as inherited or
    effective draw state outside those scanned room buffers, or as renderer
    interpretation state, rather than as missing env-alpha data in the room
    secondary DLs.
- Dam glass material/presentation A/B sanity:
  - `/tmp/mgb64_dam_glass_material_ab_1782398340`
  - `GE007_ROOM_XLU_AS_OPAQUE=1` is a clear negative control, worsening the
    active-normalized delta to `97.9%`
  - `GE007_ROOM_ALPHA_AS_TEXEDGE=1`, `GE007_DIAG_QUANTIZE_COMBINER=1`,
    `GE007_FORCE_ROOM_POINT_FILTER=1`, and `GE007_DISABLE_N64_FILTER=1` all
    remain near `95.9%..96.1%`, so the current broad mismatch is not explained
    by a single accidental room-alpha cutout, combiner quantization, or room
    texture-filter toggle
- Dam glass focused material-semantics A/B sanity:
  - `/tmp/mgb64_dam_material_diag_1782421821`
  - the default checkpoint is still `86.321%` changed overall, `79.484%`
    changed in `center_glass`, `65.987%` in `left_room`, and `95.882%` in
    `pp7_hud`
  - `GE007_DIAG_LOD_FRACTION=0` is pixel-identical to default; forcing
    `64`, `128`, or `255` worsens the whole-frame delta to `86.919%`,
    `89.885%`, and `92.571%`
  - `GE007_DIAG_SETTEX_MIRROR_TEX1=1` is pixel-identical to default, while
    `GE007_DIAG_NO_SETTEX_LINEARIZE=1`, `GE007_DIAG_DISABLE_SHADER_CLAMP=1`,
    and `GE007_DIAG_QUANTIZE_COMBINER=1` only produce tiny mixed changes
    (`86.097%`, `86.490%`, and `86.381%` changed overall) without cleanly
    improving the room-glass ROIs
  - `GE007_DIAG_CONVERT_K4K5=1` is a strong negative control (`99.969%`
    changed overall), proving the combiner enum/K4/K5 conversion path is not
    the missing Dam glass behavior
  - broad shade scaling is also negative: `GE007_DIAG_SHADE_SCALE=0.85`
    worsens to `94.159%` changed overall and `1.15` worsens to `91.138%`
  - do not promote any of these toggles as Dam glass fixes; the remaining
    static-glass target is narrower N64 blender/coverage/color semantics, not a
    blanket texture, shade, LOD, K4/K5, clamp, or combiner-precision switch
- Dam glass viewport/presentation trace:
  - `/tmp/mgb64_dam_viewport_trace_1782422228`
  - native checkpoint frames `1175..1189` set the same gameplay viewport every
    frame: raw N64 viewport `scale=(640,440)`, `trans=(640,480)`, logical
    `320x240`, drawable `640x480`, and final `xywh=(0,20,640,440)`
  - this matches GoldenEye's normal NTSC single-player 320x220 view at
    `viewtop=10` inside a 320x240 VI buffer, scaled 2x to 20-pixel top/bottom
    guard bands
  - stock post-presentation screenshots have an active bbox near
    `(8,2,625,474)`, while native's presented capture is near
    `(0,19,640,442)`. Treat that as an oracle presentation-normalization issue
    until the stock provider's VI/overscan transform is encoded, not as proof
    that native should stretch the gameplay viewport to full height.
- Dam glass logical-viewport comparator:
  - route/comparator plumbing now supports `compare_profile:
    "logical-viewport"` with `visual_logical_size` and
    `visual_logical_viewport`; the checked-in Dam visual route compares the
    original `320x240` logical VI viewport `[0,10,320,220]`
  - `/tmp/mgb64_dam_logical_viewport_compare_recheck.json` compares the current
    stock/native checkpoint through that transform and lowers the changed-pixel
    rate from the old active-normalized `86.321%` to `81.004%`
  - the named ROIs are now in logical-viewport coordinates:
    `center_glass=75.575%`, `left_room=57.669%`, and `pp7_hud=95.815%`
    changed. This proves the old presentation mismatch inflated the material
    signal, but it does not erase the room-glass or foreground mismatch.
  - `/tmp/mgb64_dam_material_diag_logical_1782422599` rescored the existing
    material A/B matrix through the logical viewport. It keeps the broad
    material toggles rejected: only tiny mixed movements appear for
    `GE007_DIAG_NO_SETTEX_LINEARIZE=1` (`81.608%` overall),
    `GE007_DIAG_DISABLE_SHADER_CLAMP=1` (`80.982%` overall, worse glass/room
    ROIs), and `GE007_DIAG_QUANTIZE_COMBINER=1` (`81.043%` overall), while
    LOD, K4/K5 conversion, and shade scaling remain clear negatives.
  - `/tmp/mgb64_dam_logical_route_native_1782422670` confirms the updated route
    still passes native screenshot health, config override audit, and render
    trace health.
  - `/tmp/mgb64_dam_logical_route_native_final` reran the updated checked-in
    route with `--native-only --no-compare --no-build`: native screenshot
    health, config override audit, and render trace health still pass. The
    direct logical-viewport comparator also reproduced the same stock/native
    metrics in `/tmp/mgb64_dam_logical_viewport_compare_final.json`.
- Focused shard trace:
  - `/tmp/mgb64_shard_tri_trace_label_1782395234`
  - a short deterministic shot reproduces the two-pane shatter (`108`, `123`)
    and shows the largest emitted `effect=glass_shards` triangle is bounded
    (`area2=2.30`, bbox `[-1.00,-1.00]-[0.76,0.31]`)
  - the visually larger preclip outliers in this capture are not labelled
    `glass_shards`, which keeps the shard backlog scoped to the effect path
    instead of unrelated room/prop/guard triangles
- Structured glass shard trace:
  - native and stock-oracle traces now expose a top-level `glass` summary for
    the shattered-window ring buffer: buffer length, next shard index, active
    count, first active shard sample, and active-state hash. The stock oracle
    uses US symbol defaults with `MGB64_ARES_SHATTERED_WINDOW_LEN`,
    `MGB64_ARES_SHATTERED_WINDOW_PTR`, and `MGB64_ARES_NEXT_SHARD_NUM`
    overrides for future layouts.
  - `/tmp/mgb64_glass_material_structured_trace` reran
    `glass_material_regression.sh --no-build`; tinted opacity, textured
    prop-impact decals, regular-glass shatter, and shard locality still pass.
    The structured trace asserts `first_active_frame=111`, `max_active=90`,
    and `buffer_len=200` for the deterministic Dam regular-glass shot.
  - `tools/compare_glass_trace.py` passed a self-compare smoke with
    `--require-active --require-hash-match`, proving the new trace field is
    machine-comparable. The next stock-backed shatter route should use that
    comparator without `--require-hash-match` until firing timing/random state
    parity is proven.
  - `/tmp/mgb64_dam_glass_visual_with_glass_trace` reran the static
    `dam_glass_visual_probe` native route with the expanded trace schema:
    screenshot health, config override audit, and render trace health still
    pass. The static checkpoint reports `glass.present=1`, `buffer_len=200`,
    `next=0`, and `active=0`, as expected before a shatter.
  - `tools/rom_oracle_routes/dam_regular_glass_shatter_probe.json` is now a
    stock/native shatter-state route for the pad-103 Dam regular-glass shot.
    `/tmp/mgb64_dam_regular_glass_props_final` passed a fresh full native+stock
    capture with rebuilt ares: native screenshot/config/render health, stock
    screenshot health, stock control audit, and glass comparison.
  - `tools/compare_glass_trace.py` now gates active shard presence and peak
    active count, and can gate first active shard sample position plus authored
    pane lifecycle. Current stock/native state comparison passes with
    `max_active=90 -> 90`, `first_position_delta=0.000`, `glass_props`
    `destroyed 1 -> 1`, `remove 1 -> 1`, and `prop_position_delta=0.000`.
    Both sides break setup index `259`, object `104`, pad `10004`, at
    `[14246.01, -547.87, 2052.66]`. First-active frames `2879 -> 111`
    (`delta=-2768`) remain diagnostic because stock includes menu/frontend
    capture time and the firing timing is not yet hash-equivalent.
  - `tools/rom_oracle_routes/dam_regular_glass_shatter_visual_probe.json` now
    captures a stock-backed active-shard visual checkpoint for the same Dam
    regular-glass pane. `/tmp/mgb64_dam_regular_glass_visual_probe_final`
    passed native screenshot/config/render health, stock screenshot health, and
    stock control audit with both sides fixed at the same +Z pose. Trace context
    at the screenshot shows `active=90` on both sides and the same destroyed /
    remove-pending pane, but the visual comparator still reports `88.4%`
    changed over the logical viewport under the old broad ROIs. Current caveats are
    deliberate: stock includes the N64 overlay/radar-style presentation, native
    shows a pickup banner, and actor/HUD composition is not yet isolated enough
    for hard pixel thresholds.
  - Read-only review refresh, 2026-06-26:
    `/tmp/mgb64_playability_smoke_dam_review_1782427079`,
    `/tmp/mgb64_glass_material_review_1782427089`,
    `/tmp/mgb64_dam_portal_review_1782427109`, and
    `/tmp/mgb64_render_camera_clearance_review_1782427123` all passed without
    behavior edits. Dam startup/movement/render health stayed clean
    (`120` moving records, max horizontal delta `1038.63`). The glass material
    lane still proves default tinted render opacity `16`, legacy floor `96`,
    textured prop impacts by default, and one deterministic regular-glass
    shatter with `90` pieces, `max_active=90`, and zero large
    `effect=glass_shards` triangles. The portal lane keeps the pad-140 default
    room set tight (`5` rendered rooms) while the legacy projection/widening
    bundle over-admits (`40` rooms, `19.735%` image delta). The
    camera-clearance lane passed all `17` enabled/disabled contact cases with
    `position_delta=0.000` and
    `collision_delta=0.000`, including Dam wall, glass, and control-room aim,
    crouch, lean, look-sweep, weapon-switch, and soak variants.
  - Render-camera clearance refresh, 2026-06-26:
    `/tmp/mgb64_render_camera_clearance_xlu_coverage_1782436000` passed all
    `17` enabled/disabled contact cases after the XLU coverage diagnostic work.
    The Dam glass-area cases produced clearance in room `109` for forward
    contact (`210` hits), aim (`140`), crouch (`206`), aim+lean-left (`140`),
    and aim+lean-right (`140`) while disabled captures emitted zero clearance
    lines. Dam control-room cases stayed covered in room `114`, and every case
    preserved gameplay `pos_delta=0.000`, `col_delta=0.000`, and current room.
    The post-instrumentation rerun
    `/tmp/mgb64_render_camera_clearance_51727` again passes all `17`
    enabled/disabled cases: Dam exterior wall, long contact, 600-frame soak,
    glass area with aim/crouch/lean, control-room look/weapon-switch, Surface
    spawn wall, Runway tank contact, Facility closed/opening/closing door
    contact, and Facility tinted-glass contact. Every enabled case records
    clearance hits, every disabled case records zero, and every pair preserves
    gameplay `pos_delta=0.000` and `col_delta=0.000`.
  - Dam portal regression refresh, 2026-06-26:
    `/tmp/mgb64_dam_portal_regression_52176` passes after the renderer
    instrumentation changes. The pad-140 wall-contact default stays tight at
    `5` rendered rooms, the old legacy projection/widening A/B still reproduces
    over-admission at `40` rendered rooms with `19.735%` image delta, and default vs
    `GE007_PORTAL_BFS=0` remains `0.000%` changed at the repro frame.
  - Dam tunnel visibility fix refresh, 2026-06-26:
    `/tmp/mgb64_dam_tunnel_visibility_final_1782465190` passes the new pad-164
    service-tunnel guard. Default portal BFS now renders `6` tunnel rooms,
    includes room `98`, and has `0.000%` bright-blue cap pixels. The negative
    control `GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=0` reproduces the old
    under-admitted `3`-room view with `4.578%` bright-blue cap pixels, while
    `GE007_PORTAL_BFS=0` renders `54` rooms with `0.000%` bright-blue cap
    pixels. `/tmp/mgb64_dam_portal_after_tunnel_fix_1782464968` also passes the
    pad-140 over-admission guard after the default fallback change: default
    stays at `5` rooms, the legacy bundle stays at `40`, and default vs
    no-portal-BFS remains `0.000%` changed.
  - Post-tunnel-fix validation, 2026-06-26:
    `/tmp/mgb64_render_camera_after_tunnel_fix_1782464989`,
    `/tmp/mgb64_glass_material_after_tunnel_fix_1782465134`,
    `/tmp/mgb64_playability_dam_after_tunnel_fix_1782465174`,
    `/tmp/mgb64_impact_gate_after_tunnel_fix_1782465215`, and
    `/tmp/mgb64_dam_glass_visual_after_tunnel_fix_1782465264` all pass. The
    render-camera lane preserves gameplay `pos_delta=0.000` and
    `col_delta=0.000` across all covered cases; the glass material lane keeps
    deterministic regular-glass shatter at `90` pieces with zero large shard
    triangles; route contracts pass all 8 Dam routes; the stock-backed shatter
    route keeps the world-space impact gate at `2.140`; and the current Dam
    glass visual evidence route reports `80.9%` logical-viewport changed
    pixels with ROIs `center_glass=75.9%`, `left_room=57.8%`, and
    `pp7_hud=94.9%`.
  - Dam gameplay batch refresh, 2026-06-26:
    `/tmp/mgb64_dam_gameplay_batch_1782456988` passed the current native
    playability lanes without new behavior edits. `route_contract_smoke.sh`
    passed all eight Dam routes (`dam_forward_stop`, `dam_glass_visual_probe`,
    intro camera/swirl routes, the regular-glass shatter routes, and
    `dam_strafe_matrix`). `playability_smoke.sh --level 33` now has standalone
    proof for all cardinal and diagonal movement patterns: forward, right, back,
    left, forward-left, forward-right, back-left, and back-right each produced
    `120` moving records over `239` trace records, with max horizontal deltas
    ranging from `201.75` to `1407.05` and clean screenshot/render health.
    `damage_hud_smoke.sh --level 33` passed with `4684` warm pixels, `5817`
    cool pixels, `58` HUD triangles, and first active frame `305`.
    `scripted_look_smoke.sh --level 33` passed with pitch delta `0.395830`.
    `hidden_guard_contract_smoke.sh --level 33` passed both H2 and H1 modes:
    the guard reached peak visible `firecount=76`, hid at frame `402`, caused no
    phantom fire or damage while hidden, froze AI in H2 (`action=1`,
    `ai.offset=144`), and kept AI ticking while fire stayed blocked in H1.
    `knife_impact_smoke.sh --level 33` passed with one accepted Bond throwing
    knife hit at frame `191` on chr `1`, part `14`. Dam soak stability passed at
    both `1800` frames (`1799` trace records) and `5400` frames (`5399` trace
    records), with render health clean.
  - Current validation refresh, 2026-06-26:
    After the read-only shard material/stock comparison pass, `cmake --build
    build --target ge007 --parallel 4`, `git diff --check`, and
    `tools/route_contract_smoke.sh` all passed; route contract smoke validated
    all eight checked-in routes. The fresh stock/native RNG-isolated shatter run
    is `/tmp/mgb64_shard_stock_native_1782458680` and passed with exact first
    sample parity. Focused runtime gates also passed:
    `tools/glass_material_regression.sh`
    (`/tmp/mgb64_glass_material_regression_55937`),
    `tools/dam_portal_regression.sh`
    (`/tmp/mgb64_dam_portal_regression_55938`),
    `tools/render_camera_clearance_regression.sh`
    (`/tmp/mgb64_render_camera_clearance_55936`), and serial
    `tools/playability_smoke.sh --level 33`
    (`/tmp/mgb64_playability_smoke_57405`). The first parallel playability smoke
    attempt only timed out on the shared validation runtime lock while other
    gates were running; the serial rerun passed.
  - Active-shard visual A/B refresh:
    `/tmp/mgb64_dam_shard_compress_ab`,
    `/tmp/mgb64_dam_shard_fixed_mtx_ab`, and
    `/tmp/mgb64_dam_shard_near_clip_only` compare against the same stock
    active-shard frame. `GE007_GLASS_SHARD_FIXED_MTX=1` is effectively
    default-equivalent (`88.360%` logical-viewport diff, `92.909%`
    `shatter_pane`, `79.527%` `glass_corridor`, `93.778%` `hud_viewmodel`).
    `GE007_GLASS_SHARD_COMPRESS=1` slightly improves `shatter_pane`
    (`89.331%`) and whole viewport (`88.029%`) but worsens `glass_corridor`
    (`81.428%`) and introduces a large translucent diagonal shard shape, so it
    is not a promotable fix. `GE007_NEAR_CLIP_ONLY=1` is a clear negative
    control: it exposes outdoor/sky geometry and worsens the logical viewport
    to `94.693%` changed, with `glass_corridor=95.092%`.
  - Active-shard material-state trace, 2026-06-26:
    `/tmp/mgb64_shard_semantics_1782458590/native` passed the deterministic
    Dam pad-103 route with opt-in `GE007_EFFECT_TRI_TRACE_LABEL=glass_shards`
    and `GE007_TRACE_TEXGEN_MATERIALS=1`. The current build still emits bounded
    shard geometry: frame `108` shatters into `90` pieces, and frame `109`
    emits labelled `effect=glass_shards` triangles using `lighting=1`,
    `texgen=1`, `texgen_linear=0`, and `fog_geom=0`. The raw shard VTX bytes are
    normal/alpha data under `G_LIGHTING`, not RGB colors:
    `5,5,126,255`, `5,251,126,255`, and `251,251,126,255` decode as
    approximately `(+5,+5,+126)`, `(+5,-5,+126)`, and `(-5,-5,+126)` normals.
    Post-light shade is therefore grayscale in this material state
    (`150..209` in the sampled emitted rows). All sampled shard material rows
    share render mode `0x0C1849D8`, other-mode high `0x00992C60`, combiner
    `0x00F38E4F020A2D12`, geometry mode `0x00060205`, two texture inputs,
    `settex=0`, and live tile-1 mip binding. A fresh stock/native run using the
    local instrumented ares binary at
    `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`
    passed in `/tmp/mgb64_shard_stock_native_1782458680`: `max_active=90 -> 90`,
    exact first-sample parity (`max_delta=0.000`, `mismatches=0`), and
    `glass_props destroyed 1 -> 1, remove 1 -> 1`. A manual logical-viewport
    screenshot comparison for the same artifact
    (`visual_compare_rng_isolation_manual.json`) still reports `88.2%` changed
    pixels overall, but separates the signals: `glass_burst` is `84.6%` changed
    with bright pixels `544 -> 853`, `damage_arc` is stock-only HUD warmth
    (`6172 -> 0` warm pixels), and `hud_viewmodel` remains high presentation
    noise (`91.5%` changed). This narrows the remaining active-shard defect to
    lighting/texgen/combiner presentation, coverage/order, and HUD/presentation
    isolation, not pane lifecycle, random shard generation, or screen-spanning
    projection.
  - Active-shard visual actor guard refresh, 2026-06-26:
    `/tmp/mgb64_shard_visual_actor_guard_1782459927` ran the stock-backed
    `dam_regular_glass_shatter_visual_probe` with the local instrumented ares
    binary at
    `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`.
    Native render-health, native/stock screenshot-health, stock trace control,
    and logical-viewport screenshot comparison all completed. The route now
    intentionally fails afterward through `actor_compare_...json`: at first
    active glass frame stock chr `12` is `alive=1, action=3`, while native chr
    `12` is `alive=0, action=4`; both samples are hidden/rendered/onscreen with
    `hidden_bits=512`. The visual comparison in the same artifact remains noisy
    (`88.38%` changed overall, `glass_burst=88.13%`,
    `damage_arc=79.80%`, `hud_viewmodel=91.51%`). This proves the current
    active-shard visual route is an evidence route, not a promotable pixel gate,
    until chr12/HUD composition is isolated.
  - Active-shard visual actor guard cleanup, 2026-06-26:
    `/tmp/mgb64_visual_route_widefire_valid_1782461353` is the current checked-in
    visual route shape. Native applies `GE007_AUTO_AUTOAIM_SCRIPT=41:0,60:0,69:0`
    so the direct-boot shot does not autoaim into chr `12`; stock uses a wider
    gameplay fire window (`68:10`) to make the forced-pose shatter reliable. The
    route passes native render health, stock screenshot health, stock control
    audit, visual comparison, and the `last-active` chr `12` actor guard:
    `alive/hidden/hidden_bits/onscreen/rendered/action` all match
    (`action=8`, `hidden_bits=512`). The visual mismatch remains substantial
    (`87.3%` overall, `glass_burst=81.3%`, `damage_arc=77.3%`,
    `hud_viewmodel=91.2%`), so this is a clean evidence route, not a solved
    renderer gate.
  - Active-shard visual crosshair/impact-center guard refresh, 2026-06-26:
    `/tmp/mgb64_dam_visual_center_guard_1782495600` reran the stock-backed
    visual route after adding native `GE007_AUTO_CROSSHAIR_SCRIPT` and the
    route-level impact-center pre-pixel guard. The route passes native
    screenshot/config/render health, stock screenshot/control audit,
    glass/actor comparison, impact semantic match, and logical-viewport pixel
    comparison artifact generation. The impact center is aligned within the
    `5.0` gate (`3.707` world units), while the stricter all-vertex comparison
    still shows a small decal-quad delta up to `6.053`; keep that as impact
    decal presentation work. The pixel report remains an evidence artifact, not
    a solved renderer gate: masked changed pixels are `84.6%`,
    `glass_burst=65.4%`, `damage_arc=86.3%`, and `hud_viewmodel=87.1%`.
  - Bullet-impact state telemetry refresh, 2026-06-26:
    `/tmp/mgb64_impact_state_stock_native_1782462448` rebuilt the documented
    local ares binary and reran `dam_regular_glass_shatter_probe`. The existing
    shatter-state gate still passes (`max_active=90 -> 90`, same broken pane),
    and both stock/native traces now emit top-level `impact_state`.
  - Bullet-impact world-space gate fix, 2026-06-26:
    `/tmp/mgb64_impact_gate_route_1782463838` reran
    `dam_regular_glass_shatter_probe` after making impact-state comparison
    world-space aware, aligning native route facing to the stock forced pose,
    and removing the native-only `HIT_GLASS_XLU` override for the follow-on
    background impact after a glass prop hit. The route now hard-gates impact
    state: selected background impact fields match (`room=107`, `impact=7`,
    `model_pos=-1`, `clear=0`, `prop=0`) and world center delta is `2.140`
    within the route's `5.0` tolerance. The earlier strict failures
    (`107 -> 108` and large local/world deltas) were route/comparator artifacts;
    the real native defect was the oversized glass-crack classification on the
    background impact.
  - Native material/contact validation refresh, 2026-06-26:
    `/tmp/mgb64_glass_material_impact_state_1782462570` passes tinted opacity,
    textured prop-impact, and regular-glass shatter material checks after the
    impact-state instrumentation. `/tmp/mgb64_render_camera_clearance_impact_state_1782462593`
    passes all `17` render-camera clearance enabled/disabled contact cases,
    including Dam wall/glass/control-room aim/crouch/lean/look/weapon-switch,
    Surface wall, Runway tank, Facility closed/opening/closing doors, and
    Facility tinted glass, with gameplay `pos_delta=0.000` and
    `col_delta=0.000`.
  - Call-seeded shard-state isolation refresh, 2026-06-26:
    `/tmp/mgb64_callseed_material_trace_1782432857` applies the stock pre-shard
    seed at native `randomGetNext()` call `1237`, matching the first four active
    shard samples exactly against `/tmp/mgb64_dam_shard_rng_trace_1782430799`.
    Active shards begin at native frame `111` with `90` pieces; the focused
    `effect=glass_shards` trace emits bounded triangles over frames `111..114`
    (`52/53/53/6` emits) with no screen-spanning candidates. The stock screenshot's
    earlier red/orange mismatch is the HUD damage arc, not glass. Extending native
    capture to frame `160` reproduces that damage arc timing, but the stock/native
    screenshots still have too much presentation-state and active-bbox noise for
    a hard pixel gate. Keep the next oracle code-level: shard samples, material
    rows, texture payloads, and local burst coverage.
  - TMEM mip-chain alias fix, 2026-06-26:
    `/tmp/mgb64_dam_shard_loaded_tex_dump_1782479000` proved the shard overlay
    source is a full `4096` byte IA8 mip-chain load: base `54x54` rows padded to
    `56` bytes, then `27x27` at byte offset `3024`, then `13x13` at byte offset
    `3888`. Before the fix, tile 1/2 footprint logs had `addr=0x0`, so
    `TEXEL1` fell back to the base tile. The renderer now synthesizes live TMEM
    aliases for valid LOD subtiles and only uses `tile+1` when that endpoint has
    a loaded payload. `/tmp/mgb64_dam_shard_tmem_alias_trace_1782481000` confirms
    all `76` traced shard material rows now use `tile1={td=1}` with non-null
    `load1` at the expected offset. This is validated by
    `/tmp/mgb64_glass_material_tmem_alias_1782481000`,
    `/tmp/mgb64_dam_portal_tmem_alias_1782481000`,
    `/tmp/mgb64_camera_clearance_tmem_alias_1782481000`, and
    `/tmp/mgb64_playability_smoke_tmem_alias_1782481000`, all passing. It does
    not solve the stock color/presentation mismatch: stock visual diff changes
    from `88.360%` to `88.382%`, with no restored glass-specific feature signal;
    the red-dominant pixels from that comparison are now classified as HUD/presentation
    noise, not shard geometry.
  - Active-shard sample telemetry, 2026-06-26:
    `/tmp/mgb64_dam_shard_stock_sample_1782430294` refreshed the visual route
    after adding matching stock/native `glass.sample` arrays. The stock and
    native first active frames have the same first-piece position
    `[14358.84,-625.40,2137.89]` and the same `active=90`, but the per-piece
    scatter already differs before rendering: stock piece 0 is
    `rot=[2.50,0.00,3.50]`, `vel=[-0.40,2.72,0.83]`, vertices
    `[24,24,0] / [20,-22,0] / [-24,-24,0]`; native piece 0 is
    `rot=[2.50,0.00,5.97]`, `vel=[1.50,0.55,0.41]`, vertices
    `[7,8,0] / [6,-6,0] / [-8,-6,0]`. Stock piece 1 shows the same pattern
    (`~26x28` vertices) while native piece 1 is around `10x11`. This proves the
    active-shard burst size/pose mismatch is not yet isolated to the renderer;
    the current visual route still has stock/frontend vs native/direct-warp RNG
    and firing-timing differences. The colored viewport arc seen in the stock
    screenshot is tracked separately as HUD/damage presentation noise unless a
    trace proves it is shard-owned.
  - RNG transition telemetry, 2026-06-26:
    `/tmp/mgb64_dam_shard_rng_trace_1782430799` refreshed the same route after
    adding top-level stock/native `rng` trace state. The comparator now reports
    the pre-active and first-active seed transition. For this route, stock goes
    from `0x000000016539D61A` to `0x00000000C7904223` in `1272` RNG draws, while
    native goes from `0x00000001D46D6152` to `0x000000002DF67895` in `1274`
    draws. The pre-shatter seed mismatch and two-draw transition delta explain
    why active shard samples differ before rendering; this route must not become
    a hard active-shard pixel oracle until RNG/firing timing is aligned or
    explicitly compensated.
  - RNG-controlled shard-state isolation, 2026-06-26:
    `/tmp/mgb64_native_rng_calls_1782432034` adds a native forced RNG caller
    trace around the Dam shatter. Native performs 11 non-shard draws after the
    pre-active seed before the first `sub_GAME_7F0A1DA0` shard-size draw; stock's
    first shard sample starts one draw earlier in the same sequence. The
    call-seeded native probe `/tmp/mgb64_native_rng_call_seed_1782432176`
    applies `GE007_AUTO_RNG_CALL_SEED_SCRIPT=1237:0x00000001D8F3CC2B`
    immediately before native's first size draw. With that explicit renderer
    isolation, `tools/compare_glass_trace.py --require-sample-match` passes
    against the stock trace: first active position, active count, prop lifecycle,
    and the first four sampled shard pieces match exactly. The raw active-buffer
    hash still differs and should remain a coarse diagnostic, not the semantic
    stock/native gate. This compensation is now captured in
    `tools/rom_oracle_routes/dam_regular_glass_shatter_rng_isolation_probe.json`
    with `compare_first_sample_tolerance=0`, while the ordinary unseeded shatter
    route remains the gameplay RNG/timing signal.
    Comparing the active-stock screenshot from
    `/tmp/mgb64_dam_shard_rng_trace_1782430799` with the call-seeded native
    screenshot still leaves `88.2%` changed pixels overall, but the current
    feature metrics avoid misclassifying the HUD arc as glass: `glass_burst`
    has `544 -> 853` bright pixels, while `damage_arc` has `6172 -> 0` warm
    red/orange pixels for the native145 timing. The native160 timing probe adds
    the damage arc (`6172 -> 8661` warm pixels) but changes presentation
    brightness and allows slight native pose drift, so it is not a better hard
    gate by itself. With shard state controlled, the next glass work is material,
    draw ordering, burst coverage, and overlay/HUD isolation, not random shard
    generation.
  - Autoaim-clean shard-state isolation refresh, 2026-06-26:
    `/tmp/mgb64_rng_route_autoaimoff_valid_1782461215` updates the checked-in
    RNG-isolation route for the autoaim-clean pad-103 shot. Native now seeds at
    `GE007_AUTO_RNG_CALL_SEED_SCRIPT=1702:0x00000001D8F3CC2B`; the strict glass
    comparator passes exact first active sample parity (`max_delta=0.000`),
    `max_active=90 -> 90`, and destroyed/removed pane lifecycle parity.
  - Loaded-tile two-texture filter A/B, 2026-06-26:
    `/tmp/mgb64_loaded2tex_filter_diag_1782434110` applies
    `GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=0x00f38e4f020a2d12` on the same
    call-seeded Dam shatter route. All `56` traced `effect=glass_shards`
    material rows move to `opts=0x00003511` and `sampler_linear=(0,0)`,
    confirming shader-side N64 filtering is active for both LOD endpoints.
    The seeded native screenshot changes only `77 / 307200` pixels versus the
    default call-seeded capture, with `0.0%` changed in the `glass_burst` and
    `damage_arc` ROIs and only `0.1%` in `hud_viewmodel`. Treat loaded-tile
    two-texture N64 filtering as a checked negative control for active-shard
    presentation, not a promotable fix.
  - XLU coverage/A2C A/B, 2026-06-26:
    `/tmp/mgb64_xlu_coverage_a2c_1782436000` applies
    `GE007_DIAG_XLU_COVERAGE_A2C=0xC41049D8,0x0C1849D8` with `Video.MSAA=4` on
    the deterministic Dam regular-glass shot. The diagnostic reaches 200 traced
    XLU material rows and all 48 traced `effect=glass_shards` rows
    (`api_blend=alpha_coverage`), with clean render/screenshot health. Versus
    the MSAA4 baseline it changes `16794 / 307200` pixels overall, but only
    `168 / 19200` (`0.9%`) in the `glass_burst` ROI, `0.0%` in `damage_arc`,
    and `0.4%` in `hud_viewmodel`. Treat OpenGL alpha-to-coverage for these XLU
    modes as a checked weak/negative control, not a promotable default fix.
  - Blend/interpolation A/Bs, 2026-06-26:
    `/tmp/mgb64_alpha_depth_ab_1782454369` reruns broad
    `GE007_DIAG_ALPHA_BLEND=premult|add|inv_alpha|copy` and
    `GE007_DIAG_ZMODE_XLU_LESS=1` under the call-seeded active-shard route.
    Every alternate alpha factor worsens the stock comparison
    (`glass_burst` rises from `84.590%` changed to `97.236..99.479%`), while
    `ZMODE_XLU` `GL_LESS` is pixel-identical to default. Treat broad
    `GFX_BLEND_ALPHA` factor swaps and XLU less-vs-lequal as negative/no-op
    controls for active shards.
    `/tmp/mgb64_combiner_interp_ab_1782454713` then checks broad combiner and
    interpolation toggles. `GE007_TEX_ONLY=1` is a strong negative
    (`glass_burst=99.986%` changed), `GE007_DIAG_QUANTIZE_COMBINER=1` is a
    no-op, and loaded-tile 2tex N64 filtering remains effectively flat.
    Global `GE007_DIAG_NOPERSPECTIVE_INPUTS=1` improves the broad stock metric
    (`glass_burst=69.194%`), but `/tmp/mgb64_scoped_nopersp_ab_1782455251`
    proves this is not a shard-combiner fix: the new scoped
    `GE007_DIAG_NOPERSPECTIVE_CC_INPUTS=0x00f38e4f020a2d12` reaches all `76`
    traced shard rows (`opts=0x00008511`) yet leaves `glass_burst` unchanged at
    `84.590%`. Do not promote global noperspective interpolation as a glass fix;
    any future interpolation work must identify the exact non-shard material it
    improves.
  - Active-shard material/texture re-attack, 2026-06-27:
    `/tmp/mgb64_m12_shard_texgen_effect_filter` adds
    `GE007_TRACE_TEXGEN_MATERIALS_EFFECT=glass_shards` and proves the material
    budget is now reserved for active shards: `172 / 172` texgen rows are
    `effect=glass_shards`, with no room/pane rows. All rows use combiner
    `0x00f38e4f020a2d12`, raw render mode `0x0C1849D8`, other-mode high
    `0x00992C60`, geometry `0x00060205`, base IA8 texture key
    `0x800000000000028e`, tile-1 IA8 key `0x8000000000000e5e`, env color
    `(90,0,0,255)`, and fog color `(0,0,0,191)`. The combiner decodes to cycle 0
    trilerp over IA tile 0/1, then cycle 1 shade modulation after normal
    zero-constant handling; it is not the adjacent room-LUT oddball that uses
    tile 1 only for alpha. The texture dump shows the base shard payload as a
    broad dark/translucent IA8 sheet (`56x54`, alpha max `102`, nonzero
    `2916 / 3024`), which matches the current native symptom: correct projected
    envelope, wrong stock presentation.
    `/tmp/mgb64_m12_active_visual_k4k5` confirms `GE007_DIAG_CONVERT_K4K5=1`
    is still a strong negative control (`changed=99.945%`, bright pixels
    `410 -> 4927`, native mean luma about `117`). The scoped
    `GE007_DIAG_SWAP_IA8_NIBBLES=0x800000000000028e,0x8000000000000e5e` A/B is
    the first strong material lead: `/tmp/mgb64_m12_active_visual_ia8_swap`
    improves the projected visual metric slightly (`92.879% -> 92.457%`) and
    turns the native projected ROI from dark sheets into brighter glints
    (`bright=6 -> 930`), but it over-brightens/clusters and still fails the
    fixed visual ROI sanity gate (`masked=95.550%`, `glass_burst=99.806%`).
    Adding the loaded-tile N64 filter on top in
    `/tmp/mgb64_m12_active_visual_ia8_swap_n64filter` is effectively identical,
    so filtering is not the next lever. The next promotable fix must be
    stock-owned texture/material semantics for shard texture `654`, not a broad
    IA8 decode swap.
  - IA8 channel-semantics split, 2026-06-27:
    `GE007_DIAG_IA8_CHANNEL_MODE=mode[:key-list]` now separates the two IA8
    channel questions that the earlier nibble-swap diagnostic conflated. Scoped
    to shard keys `0x800000000000028e,0x8000000000000e5e`,
    `/tmp/mgb64_m13_active_visual_ia8_rgb_from_alpha` is a clear negative:
    projected visual changes worsen to `94.781%`, masked visual rises to
    `96.185%`, and the fixed `glass_burst` ROI reaches `99.896%`. In contrast,
    `/tmp/mgb64_m13_active_visual_ia8_alpha_from_intensity` is the best channel
    lead so far: projected visual changes improve slightly beyond the full swap
    (`92.457% -> 92.412%`), bright pixels move `410 -> 923`, and the projected ROI
    mean is `[44.52,46.57,41.62]`. It still fails the old fixed ROI sanity gate
    (`masked=95.457%`, `glass_burst=99.722%`), so it is not promotable. The
    matching texture dump
    `/tmp/mgb64_m13_shard_ia8_alpha_from_intensity_texdump` records
    `stage=alpha_from_intensity`, base alpha max `119`, and nonzero alpha
    `1956 / 3024`. This narrows the next fix attempt to active-shard
    alpha/coverage semantics driven by the high intensity nibble, not RGB
    brightening and not a global IA8 decode swap.
  - Shader-scoped shard alpha lead, 2026-06-27:
    `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=0x00f38e4f020a2d12` now tests the
    same high-nibble-alpha lead without changing IA8 texture upload. The shader
    samples keep their RGB and replace only `texVal0.a` / `texVal1.a` with the
    sampled intensity before the generated combiner consumes them. The active
    visual A/B at `/tmp/mgb64_m14_active_visual_alpha_from_tex_intensity_cc`
    passes route state, first active shard sample, destroyed pane, prop position,
    and projection (`active=90 -> 90`, onscreen `82 -> 82`, behind `0 -> 0`).
    Projected visual metrics exactly reproduce the prior best channel lead:
    `changed=92.412%`, bright `410 -> 923`, mean `[44.52,46.57,41.62]`. It still
    fails the older fixed ROI sanity gate (`masked=95.457%`,
    `glass_burst=99.722%`), so the lead is confirmed but not promotable by
    itself. Default-off safety is proven by
    `/tmp/mgb64_m14_glass_material_regression_verify`, and the env-enabled
    material gate `/tmp/mgb64_m14_glass_material_alpha_from_tex_intensity_cc`
    passes with shard rows showing `opts=0x00080511` while material identity
    remains `cc=00F38E4F020A2D12`, raw mode `0C1849D8`, OMH `00992C60`, geometry
    `00060205`, and `500` shard material rows.
  - Combined alpha-intensity plus XLU A2C, 2026-06-27:
    `/tmp/mgb64_m15_active_visual_alpha_intensity_a2c_msaa4_verified` proves the
    route actually ran with native `Video.MSAA=4` via
    `tools/glass_active_visual_isolation_regression.sh --native-config-override
    Video.MSAA=4`; the wrapper appends after route config and audits the saved
    `ge007.ini`. The earlier `/tmp/mgb64_m15_active_visual_alpha_intensity_a2c_msaa4`
    false start left `MSAA=0` and was byte-identical to M14. With real MSAA, the
    material path reaches `api_blend=alpha_coverage` and the state/projection
    gates still pass (`active=90 -> 90`, onscreen `82 -> 82`, behind `0 -> 0`),
    but pixels are still wrong: projected visual is only slightly better
    (`changed=92.324%`) while native bright pixels worsen `410 -> 943`, mean
    rises to `[44.92,47.09,41.91]`, and the old fixed ROI gate remains worse
    (`masked=95.484%`, `glass_burst=99.847%`). Treat simple GL A2C layered on
    the alpha lead as a negative, not a promotion.
  - Coverage-grid re-attack tool, 2026-06-27:
    `GE007_TRACE_GLASS_SHARD_COVERAGE=1` now emits one read-only
    `[SHARD-COVERAGE]` row per active `glass_shards` frame, with raw N64
    coverage flags, material/blend identity, and coarse `64x48` bbox-cell
    pressure. `/tmp/mgb64_m16_glass_material_coverage_trace` passes
    `tools/glass_material_regression.sh`: `25` coverage rows, stable
    `raw=0C1849D8`, `omh=00992C60`, `cc=00F38E4F020A2D12`, `geom=00060205`,
    `z=xlu`, `cvg=wrap`, `aa/imrd/clr_on_cvg/force_bl=1`,
    `cvg_x_alpha/alpha_cvg=0`, `api_blend=alpha`, zero material/blend
    mismatches, and clear overlap pressure (`max_cell=10`,
    `max_overlap_cells=1231`, `max_avg_hits=2.98`). This is not exact RDP
    coverage, but it proves the next milestone should model/trace
    coverage-write and color-on-coverage/overdraw behavior for this raw mode
    before changing projection, geometry, mips, or IA8 decode again.
  - Coverage-wrap thinning diagnostic, 2026-06-27:
    `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=0x00f38e4f020a2d12` adds a default-off
    shader A/B for matched loaded-tile alpha draws with the active shard raw
    `ZMODE_XLU`/`CVG_DST_WRAP`/`CLR_ON_CVG`/`IM_RD`/`FORCE_BL` state. The
    material proof `/tmp/mgb64_m17_glass_material_wrap_thin` passes with stable
    `opts=0x00180511`, unchanged `raw=0C1849D8`, `api_blend=alpha`, `25`
    coverage rows, and zero material/blend mismatches. The active visual result
    is negative: default rate `0.25`
    (`/tmp/mgb64_m17_active_visual_wrap_thin`) remains near the
    alpha-intensity/A2C overbright failure (`projected changed=92.332%`, bright
    `410 -> 943`, masked `95.488%`), and aggressive rate `0.05`
    (`/tmp/mgb64_m17_active_visual_wrap_thin_rate005`) still moves the wrong way
    (`projected changed=92.359%`, bright `410 -> 945`, masked `95.489%`). This
    rejects screen-space thinning as a promotable fix and points the next
    milestone at a real RDP coverage-memory/color-on-coverage model or a closer
    ares blender-port diagnostic.
  - Stencil coverage-memory diagnostic, 2026-06-27:
    `GE007_DIAG_XLU_COVERAGE_STENCIL_CC=0x00f38e4f020a2d12` adds a default-off
    backend A/B that creates a stencil-backed scene target and uses the lower
    stencil bits as an approximate per-pixel coverage counter before allowing
    color writes. `/tmp/mgb64_m18_glass_material_stencil_inc4` passes the
    material proof with stable `opts=0x00080511`, unchanged `raw=0C1849D8`,
    `api_blend=alpha_cvg_wrap_stencil`, `25` coverage rows, and zero
    material/blend mismatches. The full active visual wrapper was blocked twice
    by ares stock-cadence drift (`first gameplay global 1147` instead of `1146`),
    so the projected-visual sweep used the valid M17 stock capture against
    native-only M18 captures. Projection remains matched (`active/projected/
    onscreen/behind = 90/90/82/0`), but pixels do not improve: increment `1`
    gives `projected changed=92.390%`, bright `410 -> 945`; increment `4` gives
    `92.343%`, bright `410 -> 939`; increment `8` returns to the M14
    alpha-intensity baseline (`92.411%`, bright `410 -> 923`). This rejects the
    GL stencil coverage approximation and makes a closer ares-style
    RDP blender/memory-color path the next meaningful target.
  - RDP memory-color blender diagnostic, 2026-06-27:
    `GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=0x00f38e4f020a2d12` adds a default-off
    backend/shader A/B for the matched active-shard material. It snapshots the
    current framebuffer before each triangle, disables fixed GL alpha blending,
    and applies the decoded final-cycle RDP blend formula against sampled memory
    color. `/tmp/mgb64_m19_glass_material_rdp_memory` passes the material proof
    with stable `opts=0x00280511`, unchanged `raw=0C1849D8`,
    `api_blend=alpha_rdp_memory`, `25` coverage rows, and zero material/blend
    mismatches. Native visual capture `/tmp/mgb64_m19_native_rdp_memory` and
    manual compare `/tmp/mgb64_m19_manual_rdp_memory_compare` keep projection
    matched (`active/projected/onscreen/behind = 90/90/82/0`, max area
    `-0.029%`, union `-0.243%`), but pixels remain in the same high-error
    family (`projected changed=92.317%`, bright `410 -> 923`, near-white
    `190 -> 656`, warm `10 -> 0`). This rejects memory-color-only blending as a
    promotable fix and narrows the next target to exact coverage/color-on-coverage
    behavior combined with memory color, or a closer ares/Parallel-RDP
    coverage/blender port.
  - RDP coverage-plus-memory diagnostic, 2026-06-27:
    `GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=0x00f38e4f020a2d12` adds
    per-triangle NDC attributes, estimates an 8-sample RDP-style coverage count
    in the shader, stores synthetic coverage in framebuffer alpha, and applies
    the ares `CLR_ON_CVG && !coverage_wrap` memory-color rule. The material proof
    `/tmp/mgb64_m20_glass_material_rdp_cvg_memory` passes with stable
    `opts=0x00480511`, unchanged `raw=0C1849D8`,
    `api_blend=alpha_rdp_cvg_memory`, `25` coverage rows, and zero
    material/blend mismatches. Default rendering remains gated cleanly:
    `/tmp/mgb64_m20_default_material_recheck` passes with `api_blend=alpha`.
    Native visual capture `/tmp/mgb64_m20_native_rdp_cvg_memory` and manual
    compare `/tmp/mgb64_m20_manual_rdp_cvg_memory_compare` keep projection
    matched (`90/90/82/0`, max area `-0.029%`, union `-0.243%`), but pixels are
    effectively unchanged from M19 (`projected changed=92.317%`, bright
    `410 -> 923`, near-white `190 -> 656`, warm `10 -> 0`). This rejects
    framebuffer-alpha coverage memory as the fix and moves the next target to
    direct ares/Parallel-RDP pixel-output comparison for the shard material, or a
    true RDP path for this effect.
  - Projected-pixel classifier, 2026-06-27: added
    `tools/score_glass_projected_pixels.py` to classify the traced shard
    projection ROI after projection has already matched. The current default
    proof `/tmp/mgb64_m21_default_pixel_score_compare/projected_pixel_score.json`
    shows a gray/dim failure (`luma_delta_mean=5.899`,
    `sat_delta_mean=-0.151`, `abs_rgb_delta_mean=62.577`) where stock buckets
    `bright=220`, `near_white=190`, `gray=112536` become native `bright=6`,
    `near_white=0`, `gray=136708`. The M19 memory-color score
    `/tmp/mgb64_m19_manual_rdp_memory_compare/projected_pixel_score.json` and
    M20 coverage-plus-memory score
    `/tmp/mgb64_m20_manual_rdp_cvg_memory_compare/projected_pixel_score.json`
    both land in a brighter/desaturated failure cluster (`luma_delta_mean`
    about `11.90`, `sat_delta_mean=-0.144`, `abs_rgb_delta_mean=70.3`,
    `bright=267`, `near_white=656`, `gray=124458`). This proves the current
    GL-side approximations are not converging on stock output: default loses
    bright shard pixels, while memory-color diagnostics manufacture too many
    pale gray/white pixels.

## Remaining Backlog

### P0: Dam glass/control-room visual parity

The stock-backed visual route now exists and passes its capture/audit gates, and
the portal room set at the Dam glass checkpoint now matches stock. The force hook
preserves the ROM-maintained room-render basis, and stock screenshots now come
from the post-presentation ares viewport, so stock and native show the same
corridor/control-room composition at 640x480. The native black-bar pass is now
restored, removing the blue fog-clear guard bands. Focused filter A/B, raw
hand-state stock oracle traces, native weapon-projection A/B, and stock
viewmodel matrix evidence now rule out output sampling, wrong equipped-item
state, native weapon projection, and a gross first-person root-transform
mismatch as the primary remaining cause. Native `Input.ViewmodelSway=1` does
move the PP7 root away from stock, so the route pins `Input.ViewmodelSway=0`.
The route now applies the native force-player hook after the pad warp, matching
the stock force hook's absolute position, camera height, stan anchor, and
horizontal applied-view checkpoint. That removes the native-only pose drift and
`vv_verta=-4` camera pitch. The checked-in route now uses direct native gameplay
warp instead of authored-intro warmup, because
the chr `45` magic-travel fix holds without the warmup and the no-intro route
keeps chr `12` stock-like at the checkpoint. The route now uses the
logical-viewport comparator for the original 320x240 VI gameplay viewport
`[0,10,320,220]`; the current post-tunnel-fix evidence route reports `80.9%`
changed pixels, with `center_glass=75.9%`, `left_room=57.8%`, and
`pp7_hud=94.9%`. The promoted
clamped non-texture-edge `G_SETTEX` 3-point policy remains in place; the
logical-viewport pass proves presentation was inflating the old
active-normalized metric, but not enough to explain the glass/material mismatch.
Treat the remaining diff as foreground projection/screen mapping, translucent
material blend/coverage, depth/order, and fine draw-order parity backlog,
not as route, forced-camera, raw-VI dump, room-traversal, viewport guard-band,
output-filter, wrong-hand-item, missing room-glass distance-fog, native sway,
the corrected hand smoothing slot layout, or the now-promoted clamped
non-cutout `G_SETTEX` sampling policy. The previously exposed chr `45`
background-AI leak is fixed for normal direct-boot play by matching stock's
`WAYMODE_MAGIC` snapped-prop behavior, and the current visual route keeps chr
`45` unseen and in stock-equivalent `WAYMODE_MAGIC` state at the checkpoint.
The active-shard visual route now has a machine-enforced chr `12` actor guard
with a world-position tolerance. The older autoaim-clean route passed the
field-only `last-active` guard, but the current position-aware check rejects
that artifact because chr `12` is visibly in a different foreground position.
Chr `11` AK47 glass hits are now stock-compatible on the refreshed stock/native
trace, so do not classify them as a native-only glass regression; track chr `11`
only when a new visual capture shows composition/timing drift that would
contaminate pixel interpretation.
The route now records named visual ROIs for `center_glass`, `pp7_hud`, and
`left_room`; use those metrics to separate foreground movement from static room
and glass regressions before promoting any broad renderer change.
The active regular-glass shatter visual route now also has a tighter native
checkpoint: `GE007_AUTO_FORCE_PLAYER_SCRIPT=45-199:...:167.31:103` holds
camera, floor/height, and room anchor, while
`GE007_AUTO_CROSSHAIR_SCRIPT=45-199:158:114` is the current native
stock-equivalent for the stock forced `159:114` crosshair in this fixture. The
route's
pre-pixel impact guard now checks semantic impact identity plus the world center
before producing pixel metrics; the current passing artifact is
`/tmp/mgb64_dam_visual_center_guard_1782495600`. That still does not remove the
bright burst. The refreshed 2026-06-27 controls are stricter than the older
`0.4%` shards-off A/B: default native versus `GE007_GLASS_SHARDS=0` is now
byte-identical (`0/307200` pixels changed) and the shard-mask oracle reports
`0.000%` changed in
`/tmp/mgb64_shards_off_native_active/default_vs_shards_off_shard_pixel_oracle.json`.
Material tracing still identifies the visible world impact as
`bullet_impact_world` (`impact=7`, 32x32 `fmt=2`, `siz=1`, raw mode
`0x00504DD8`) rather than falling shard geometry. Disabling bullet impacts is
also not the fix: it changes `23983/281600` logical-viewport pixels (`8.5%`) in
`/tmp/mgb64_bullet_impacts_off_native_active_fullproj/default_vs_bullet_impacts_off_visual.json`,
but `glass_burst` remains wrong and bright pixels move `484 -> 574`. The new
repeatable ownership sweep
`/tmp/mgb64_glass_contributor_isolation_current/glass_contributor_isolation_summary.json`
confirms the same shape with `tools/glass_contributor_isolation_regression.sh`:
`shards_off=0.000%`, `bullet_impacts_off=8.517%` with `glass_burst` bright
`484 -> 574`, `weapon_render_off` only moves `hud_viewmodel` (`glass_burst=0`),
`no_fog` mostly moves `hud_viewmodel=75.492%`, and `flat_bullet_impacts` is only
`0.051%`. The next fix candidate should be stock-faithful bullet-impact/crack
decal presentation, pane-break presentation, HUD/viewmodel alignment, or broader
stock/native presentation normalization, not shard suppression, impact
suppression, fog disabling, or an unproven global alpha hack.
`/tmp/mgb64_bullet_impact_material_1782469060` adds a native
`GE007_TRACE_BULLET_IMPACT_MATERIALS=1` capture: the world impact renders as
two `bullet_impact_world` room triangles, `G_CC_MODULATEIA`, `blend=alpha`,
`texedge=0`, raw mode `0x00504DD8`, effective fog mode `0xC0504DD8`, and the
same 32x32 CI8 payload (`alpha_max=169`). That further downgrades "opaque
bullet-impact material" as a root cause and promotes presentation/viewport,
damage arc, and HUD/viewmodel alignment as the active visual backlog.
The 2026-06-27 step-back confirmed the refined instrumentation is working
better, with one important fix before continuing: effect label lookup now
prefers the narrowest overlapping display-list range, so broad `glass` labels no
longer hide `bullet_impact_prop_textured`. The focused capture
`/tmp/mgb64_prop_crack_material_probe_after_specificity` now exposes the
prop-attached crack material as two textured alpha room triangles per frame
(`raw=0x0C1849D8`, `eff=0xCC1849D8`, dual IA textures `48x48` and `24x24`,
fog on, stable NDC bounds). The stock-backed impact guard still passes in
`/tmp/mgb64_glass_impact_visual_after_label_specificity`, and
`/tmp/mgb64_glass_impact_flat_prop_after_label_specificity` proves
`GE007_FLAT_PROP_BULLET_IMPACTS=1` changes `0.000%` of the checkpoint viewport.
That makes the prop crack a useful lifecycle trace target, not the screenshot
owner. Keep the next fix on world bullet-impact/decal presentation, viewport and
composition normalization, and pane-break draw order.
`/tmp/mgb64_glass_impact_prop_creation_skip_crosshair_parity` then tested the
stronger creation-side diagnostic `GE007_DISABLE_PROP_BULLET_IMPACTS=1`. It
drops native full impact occupancy from `2` to `1`, proving the extra native
glass-prop impact is real, but stock parity gets worse (`stock_delta=+0.188`)
and `stock_glass_delta` stays `+0.000`. Do not promote prop-impact suppression;
keep it as an RNG/state isolation tool.
The strict all-vertex impact-quad comparison's earlier half-cell decal-shape
delta was a route-coordinate parity problem, not a global decal math problem.
The current focused route uses native `158:114` against stock `159:114` and now
matches the selected world-impact center and all four stored quad vertices
exactly. Keep any future decal size/orientation work behind this route-origin
guard rather than folding coordinate drift into renderer changes.
`/tmp/mgb64_impact_presentation_sweep_all` extends the impact contributor
harness with stock-vs-variant comparisons, so ownership and stock-direction are
now separate signals. The sweep rules out broad alpha/blend changes as the next
fix: `alpha_blend_copy`, `alpha_blend_premult`, and `alpha_blend_inv` worsen
overall stock diff by `+6.251`, `+6.547`, and `+5.693` points, and worsen
`glass_burst` by `+12.868`, `+13.271`, and `+8.632`. The
world-impact alpha-from-intensity diagnostic owns real pixels
(`3.413%` native delta; `glass_burst=27.326%`) but moves away from stock
(`stock_delta=+0.070`, `stock_glass_delta=+0.271`). The RDP memory and
coverage-memory approximations are glass-burst no-ops, while `zmode_dec_less` is
a no-op. Disabling DEC polygon offset is the only stock-direction improvement,
but at `stock_delta=-0.001` and `stock_glass_delta=-0.014` it is only a clue,
not a fix. Continue with exact decal footprint/order, stock-presented viewport
mapping, and localized DEC depth behavior rather than global blend-factor work.
The follow-up route-cadence audit on 2026-06-27 confirms the new pre-pixel
instrumentation is doing the right thing. A temporary relaxed route
(`/tmp/mgb64_glass_impact_relaxed_first_global_probe`) accepted the stock
`1147` branch but failed glass state: no active shards, no destroyed pane, and
the prop/world impact branch differed. A temporary global-timer fire route
(`/tmp/mgb64_glass_impact_global_fire_probe`) did shatter glass, but shifted the
stock shard age and moved impact center delta to `9.079`, so it was rejected.
The accepted hardening keeps `first_gameplay_global=1146`, defaults the focused
impact wrapper to `MGB64_ARES_VIDEO_BLOCKING=true` and
`MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES=8`, and preserves dirty attempts as
evidence. The current focused proof,
`/tmp/mgb64_glass_impact_checkpoint_search_focused`, fixes the previous
footprint mismatch and the follow-up screen projection regression: impact center
delta is `0.000`, quad max delta is `0.000`,
`rounding_report.exact_vertex_match=true`, and projected decal center delta is
`0.055px` under the wrapper's strict `<=1.0px` gate. The pre-fix projected proof
`/tmp/mgb64_glass_impact_projected_oracle_focused` measured the same exact
world quad but a `51.370px` native screen-space decal offset. The native shot and
bullet-impact creation both occur on frame `69`; the traced world ray is
`[0.010734,0.021102,0.999720]`; stock first observes the selected world impact
at trace frame `2437`, and native first observes it at trace frame `72`. The
same summary records `visual_oracle.status=dirty`,
`usable_for_production_pixel_fix=false`, stock visible actors `[10,12]`, native
visible actors `[10,12,45]`, chr `10` `onscreen` mismatch, chr `12`
`hidden_bits`/`action` mismatches, and chr `12` position delta `46.926`.
The new localized impact pixel oracle is also report-only:
`impact_pixel_oracle.status=masked_dirty`,
`usable_for_production_pixel_fix=false`,
`impact_focus.masked_excluded_pct=59.099`,
`impact_focus.masked_changed_pct=82.972`, and
`impact_left_unoccluded.masked_changed_pct=91.170` with bright pixels
`270 -> 73`. That proves the current screenshot is useful for ownership and
route/decal geometry, but it still cannot justify a production pixel-parity
change.
The latest contributor proof is
`/tmp/mgb64_glass_impact_contributors_crosshair_parity`: `shards_off=0.000%`,
`bullet_impacts_off=8.818%` (`glass_burst=14.688%`), `no_fog=23.257%` mostly in
`hud_viewmodel=75.513%`, `world_impact_alpha_from_intensity=3.413%` but
stock-worse, `zmode_dec_no_offset` only barely stock-positive
(`stock_delta=-0.005`, `stock_glass_delta=-0.090`), and RDP memory variants are
glass-burst no-ops.

The root cause was route-coordinate parity. A native-only crosshair sweep showed
that native `158:114` produces the same stored world-impact quad as stock
`159:114`; native `159:114` was responsible for the old `3.026` center delta and
`4.280` quad delta. This is a fixture correction, not a global decal math hack.
A negative forced-aim proof `/tmp/mgb64_glass_impact_visual_aimdir69` still
confirms that forcing `GE007_AUTO_AIM_DIR_SCRIPT=69:0:0:1` moves the native
impact to room `109` and breaks shard phase; the old frame-`70` aim script was
only a no-op. The follow-up umbrella proof
`/tmp/mgb64_dam_visual_suite_after_actor_probe` passes the full Dam suite
with the edited wrapper.

Critical stop/go assessment, 2026-06-28: continue using the new instrumentation
for route control, selected world-impact geometry, shard projection, and
ownership isolation, but stop treating this screenshot's broad visual diff as a
direct glass-rendering oracle. A visual inspection of the stock/native captures
shows stock has a guard body/head occluding the burst area while native does not.
The wrapper now makes that mismatch machine-readable:
`visual_oracle.status=dirty`, stock visible actors `[10,12]`, native visible
actors `[10,12,45]`, chr `10` `onscreen` mismatch, chr `12`
`hidden_bits`/`action` mismatches, and chr `12` position delta `46.926`. That
actor-composition mismatch pollutes `whole`, `masked`, and `glass_burst`
changed percentages. The localized impact oracle now quantifies the same
problem: masking the stock guard occluder excludes `59.099%` of `impact_focus`,
and the unoccluded-left ROI is still `91.170%` changed. The projected
world-impact/decal oracle is now present and strict for geometry. The new
`tools/score_impact_checkpoint_candidates.py` pass searched `744` active impact
pairs in this same trace and found `0` strict actor-clean candidates; the best
pair keeps projected decal parity at `0.055px` but is still actor-dirty with
native visible actor `45`. The next visual milestone should therefore be a
cleaner actor-free or strictly masked impact route/view, not another frame pick
from the current trace, before promoting any pixel-parity renderer fix.
Fresh current-build native-only scouting in
`/tmp/mgb64_dam_impact_native_fixture_scout` confirms the best next route target
is still pad `10092`, yaw `315`, distance `650`: among `20` pane/yaw candidates
it has the best score (`5609.623`), target destroyed, active shards present, and
only `2` visible/onscreen actors. The existing pad-`10092` actor-masked
stock/native route is not itself an impact oracle: the impact-aware checkpoint
search found `0` strict candidates, best impact center delta `21.400`, and
projected center delta `2.656px`. Use pad `10092`/yaw `315`/distance `650` as
the next stock-backed impact route seed, but retime/remask it specifically for
world-impact/decal parity. That route seed is now checked in as
`dam_regular_glass_shatter_pad10092_impact_visual_probe` and guarded by
`tools/glass_pad10092_impact_visual_regression.sh`; current proof
`/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean` passes with stock
crosshair `160:120`, native fire start `68`, native frame `124`, native
crosshair `159.00:122.50`, impact center delta `4.785 <= 5`, projected decal
center delta `0.949px <= 1.0px`, and full sampled world-impact sequence parity
(`[1,1,1,1] -> [1,1,1,1]`). It now also gates
checkpoint framing: camera/view/room-basis deltas are clean
(`cam_pos_delta=0.134`, `cam_target_delta=0.136`, `view_delta=0`,
`room_basis_delta=0`), while the room draw-set delta is reported separately
(`stock=[132,136]`, `native=[124,132,136]`). It also rejects the stock `1149`
branch and passes on the required `1147` route-control branch. It remains
report-only for pixels because strict actor candidates are still `0`, chr
`7`/`44` drift remains, and the projected-impact pixel ROI is still
composition-polluted.

Follow-up room ownership proof:
`/tmp/mgb64_glass_pad10092_room_draw_isolation` runs
`tools/glass_pad10092_room_draw_isolation.sh` against that same base case.
`GE007_FORCE_UNRENDERED_ROOMS=124` and `GE007_SKIP_BG_ROOM=124` both preserve
health/glass, projection, and framing gates, and both produce `0.000%`
native-default-vs-variant screenshot change. Room `124` is therefore
trace-visible but pixel-neutral for this checkpoint; do not spend the next pixel
pass trying to remove that room before actor/composition or material/presentation
is isolated.

Follow-up actor ownership proof:
`/tmp/mgb64_glass_pad10092_actor_ownership_isolation` runs
`tools/glass_pad10092_actor_ownership_isolation.sh` against the same base case
with native-only `GE007_SKIP_RENDER_CHRNUMS=7`, `44`, and `7,44` variants. The
route-state gates stay clean for health/glass, projection, and framing. The
probe has measurable signal (`skip_chr44` and `skip_chr7_44` change native
pixels by `0.309%`, mainly in `lower_actor_cluster`), but stock-vs-native
actor-masked mismatch and projected-impact mismatch change by `0.000%`.
Chr `7`/`44` draw ownership is therefore not the current pad-`10092` pixel
blocker. The next pixel pass should prioritize material/presentation or
stock/native blend/output behavior rather than more room-124 or actor-drift
experiments.

Follow-up contributor proof:
`/tmp/mgb64_glass_pad10092_contributors_refined_cc` runs
`tools/glass_contributor_isolation_regression.sh --fixture pad10092-impact`
against the same proven base case with the cleaned route's
`world_impact_cc=0x00f38e4f020a2d12`. This supersedes the
world-impact-specific rows from the earlier
`/tmp/mgb64_glass_pad10092_contributors` run, which used the older fixture's
combiner id for those variants. The broad ownership conclusions still hold:
falling shards, flat impact toggles, and weapon removal are not the pane/decal
owner. The corrected material-specific result is also negative:
alpha-from-intensity, mixed alpha-from-intensity, loaded-tile 2-texture
filtering, coverage wrap-thinning, and `ZMODE_DEC` no-offset are pixel-neutral;
stencil/RDP-memory variants move only `0.001%`; inverse-visibility-scale off
moves `0.011%`; fixed room matrices move `0.079%` with only tiny stock-direction
movement, and every refined variant leaves `projected_impact=0.000%`. The next
target needs to be stock/native presentation/output semantics or a localized
exact RDP/ares pixel oracle, not another room, actor, shard, stale
world-impact toggle, or broad blend/depth knob.

Follow-up presentation-alignment proof:
`/tmp/mgb64_glass_pad10092_presentation_alignment_probe` runs the read-only
`tools/glass_pad10092_presentation_alignment_probe.sh` against the same base
screenshots and scores `10428` crop/frame candidates per ROI. It confirms that
stock and native presentation boxes differ (`[8,2,625,474]` versus
`[0,19,640,442]`), but the broad pixel mismatch does not collapse under the best
alignment: `tower_pane` improves only `97.325% -> 94.067%`, and `impact_side`
improves `95.533% -> 90.233%`. The tiny `projected_impact` ROI improves more
(`100.000% -> 76.250%`) but remains heavily mismatched. Treat crop/presentation
alignment as a secondary measured factor. The next implementation pass should
target localized pane/decal pixel-output semantics, ideally with an exact
ares/Parallel-RDP oracle or a narrower native draw-output capture.

Follow-up pixel-semantics proof:
`/tmp/mgb64_glass_pad10092_pixel_semantics_sequence_clean` runs the native-only
`tools/glass_pad10092_pixel_semantics_probe.sh` against the cleaned base case.
The important correction is still that `bullet_impact_world` traces as
`drawclass=room`; filtering `GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect` explains
why the earlier manual capture produced material rows without matching
`EFFECT-TRI` rows. The passing summary now ties ownership, material state,
emitted triangles, viewport, projected region overlap, and impact-sequence
evidence together: `28` material rows, `28` emitted triangle rows, `56` effect
ranges, and one localized world-impact material signature. Signature
`0x00f38e4f020a2d12` owns the rows with textures `(64,32)/(32,16)`. The base
projected-impact stock/native mismatch is still `90.713%`, so this is not a
rendering fix; it proves the remaining work can now continue from a sequence-clean
world-impact/decal fixture rather than a route-state-polluted checkpoint.

Follow-up sequence cleanup:
`/tmp/mgb64_glass_pad10092_impact_sequence_scout_s68_aim` adds
`tools/glass_pad10092_impact_sequence_scout.sh` and
`tools/score_bullet_impact_sequence_candidates.py` to the same pad-`10092`
evidence lane. The older native frame `126` checkpoint had a hidden sequence
problem: stock frame `2541` was `[1,1,1,1]`, while native was `[1,1,1,7]`. The
promoted route now uses native fire start `68`, native frame `124`, and crosshair
`159.00:122.50`. It keeps the existing selected-impact geometry gate
(`impact_delta=4.785`, `projection_delta=0.949px`) and fixes the sampled
world-impact type sequence to `[1,1,1,1] -> [1,1,1,1]`.
`/tmp/mgb64_glass_pad10092_pixel_semantics_sequence_clean` confirms the native
type-`7` material pollution is gone: the localized world-impact material summary
now has one signature, `0x00f38e4f020a2d12`, with textures `(64,32)/(32,16)`.
The base projected-impact mismatch remains `90.713%`, so this still is not a
renderer fix, but it removes the fixture-state blocker before the next localized
world-impact/decal output pass.

Concrete tasks:

- Keep stock/native viewport presentation metadata explicit. The Dam route now
  uses the route-specific logical-viewport transform, and the checkpoint trace
  proves the native gameplay viewport is the expected 320x220 view scaled to
  `xywh=(0,20,640,440)`. Before adding hard pixel-fail thresholds, confirm the
  stock post-presentation active bbox remains stable enough for `active`-frame
  logical mapping, or replace it with a stronger stock VI/overscan source
  rectangle from the oracle.
- Keep the named `visual_regions` on the logical-viewport coordinate system.
  The comparator now records active bbox, margins, coverage, center offset, and
  logical-viewport crop metadata, plus bright-component and warm-pixel feature
  metrics. The current Dam shatter route uses `glass_burst`, `damage_arc`, and
  `hud_viewmodel` regions, and the wrapper records
  `combat_health_compare_<route>.json`, so HUD damage phase can be separated
  from shard/burst presentation before any threshold is promoted.
- Compare room material/shading state after the room set matches: sky/water
  composition, foreground weapon placement/lighting, grayscale corridor
  sampling, depth/alpha order, and glass pane detail. The room-glass env-alpha
  combiner replacement, simple env-alpha scale A/B, broad filter toggles, and
  clamped non-cutout `G_SETTEX` sampling policy are now checked; do not promote
  opacity scaling or global filter changes without stronger stock evidence.
- Keep using `tools/compare_viewmodel_projection.py` when the PP7/HUD ROI moves.
  Stock now exposes sampled render-pos matrix projections under
  `watch.hands.<hand>.vm.model.mtx`, and native exposes matching `wr_mtx` /
  `wl_mtx`. At the current checkpoint, fixed-50 projected anchors are close
  enough that the remaining foreground work should target weapon
  material/lighting/render state, mesh draw-state parity, presentation
  normalization, and one-frame timing noise before changing viewmodel root or
  projection policy.
- Treat the secondary-room env-alpha data and combiner rewrite as checked for
  rooms `109`, `108`, and `107`. The focused fog trace also checked that the
  probe panes resolve to zero distance fog with the current `G_RM_FOG_SHADE_A`
  inputs, and the clamped non-texture-edge `G_SETTEX` filter shortcut is fixed.
  The next static-glass targets are renderer semantics not covered by those
  facts: N64 blender/coverage/color behavior for XLU/DECAL modes and
  per-material presentation semantics. The current checkpoint already rules out
  `ZMODE_XLU`/`ZMODE_DEC` less-vs-lequal, `ZMODE_DEC` polygon-offset tuning,
  room draw-order mismatch, final-output RGB555 quantization, broad
  `GFX_BLEND_ALPHA` factor substitutions, broad texture-only combiner output,
  combiner intermediate quantization, and shard-combiner-scoped noperspective
  inputs/texcoords. It also rules out blanket LOD
  fraction forcing, `settex` texel1 mirroring, disabling `settex` UV
  linearization, disabling shader clamp, K4/K5 enum conversion, combiner
  8-bit quantization, and broad shade-scale changes. Traditional `G_LOADBLOCK`
  LOD mip endpoints are now populated through TMEM aliases, so do not chase
  missing `TEXEL1` payloads as the active-shard color explanation unless a new
  trace shows an invalid endpoint again.
- Investigate exact N64 blend, coverage, and depth semantics for the effective
  modes observed in the checkpoint (`0xC8104DD8`, `0xC81049D8`, and prop-glass
  `0xC41049D8`). Broad A/B removal, FOV changes, room-alpha scaling, and the
  current z-mode/depth probes worsen or no-op parity, so the next change should
  be a renderer-semantics fix with stock evidence, not a content skip.
- Keep actor-visibility parity guarded with `actors` plus
  `GE007_TRACE_CHRNUM`/`MGB64_ARES_TRACE_CHRNUM` when a visual route shows
  unexpected guard content. Route JSON can now declare `compare_actor_chrnums`
  so the wrapper emits `actor_compare_<route>.json` and fails dirty visual
  routes before their pixels are promoted. The current active-shard visual route
  passes chr `12` at `last-active`, while chr `45` remains unseen in
  stock-equivalent travel state. Chr `11` is no longer a native-only
  actor-state parity suspect for pad-`10004` `item=8` glass damage; use
  `GE007_TRACE_GUARD_OBJECT_SHOTS` only to prove shot ownership when a future
  capture looks composition-contaminated.
- Promote the covered native material/contact probes for basement tag-19 regular
  glass, tinted panes `10059`/`10060`, and the pad-140 control-room wall-contact
  view into stock/native visual parity routes.
- Persist route definitions only; keep ROM-derived traces/screenshots in `/tmp`.

Acceptance:

- Stock and native both prove they are in the same Dam area with matching route
  audit gates, including resolved stan room, BG room evidence, and portal room
  set evidence.
- Any visible guard/content mismatch at the glass checkpoint is backed by
  one-actor stock/native tracking before being classified as a material defect.
- Native render-health, screenshot-health, portal-room count, and camera
  clearance invariants stay clean.
- Pixel deltas become thresholded visual parity failures only after the
  logical-viewport stock active-bbox mapping is either proven stable across
  stock captures or replaced with a stronger stock VI/overscan source rectangle.

### P1: Glass shatter, crack, and pane-state parity

Native shards and material paths are stable and guarded, and there is now a
stock-backed shatter-state route. Stock and native both produce active shards
on the same pane sample, peak at the same 90 active shard count, and set the
same authored pane object to destroyed/remove-pending for the deterministic
pad-103 Dam regular-glass shot. There is also a stock-backed active-shard visual
route for the same pane. It proves the presentation gap is still real under a
same-pose, active-shard checkpoint, not just a lifecycle/state bug. Visual parity
is still not finished: first-active timing, active-state hash/distribution,
crack decal appearance, visible pane disappearance/render timing, shard
lifetime/alpha/sorting, N64 overlay/coverage treatment, and rendered shard
presentation remain open. The current stock frame has the distant pane burst
plus a colored viewport arc that current feature metrics classify as HUD/damage
presentation noise; default native renders the local burst but still differs in
burst brightness/coverage and has unrelated pickup-banner/HUD composition noise.
Fresh `glass.sample` telemetry proves the stock and native active-shard buffers
already differ in scatter, rotation, velocity, and local vertex extents at the
first active frame, despite matching the pane position and active count. The
current active-shard visual route should therefore stay an evidence route until
stock/native firing timing and RNG state are aligned or explicitly compensated.
Current read-only material tracing proves native is feeding the saturated shard
VTX bytes into the renderer but then converting the visible shade to grayscale
under `G_LIGHTING|G_TEXTURE_GEN` with the stock shard render state
(`0x0C1849D8` / `0x00992C60` / combiner `0x00F38E4F020A2D12`). That native state
is now machine-guarded by `tools/glass_material_regression.sh`, including the
`56x54,32x27` texture tuple, `00060205` geometry mode, depth tuple
`1,0,1,0,0x800`, texgen rows, and bounded material-triangle envelope. The
mip-chain TMEM alias fix proves `TEXEL1` is now a live lower mip for the shard
overlay, but that only changes the native active-shard screenshot by `0.42%`
versus the previous build and does not restore the remaining
stock burst/presentation delta.

The separate pad-`10004` `item=8` pane hits have now been reclassified as
stock-compatible chr `11` AK47 fire, proven by the guard-object trace and the
fresh stock/native chr `11` route. Do not fix that by blocking guard-owned
object damage; it belongs only in route-composition/timing review.

Concrete tasks:

- Keep `dam_regular_glass_shatter_probe` as the stock/native state guard for
  regular-glass shatter count and same-pane target. It currently requires
  active shards, exact peak active count (`90 -> 90`), and first active shard
  sample position within `1.0` world unit. It also requires both sides to mark
  the same authored pane object destroyed/remove-pending within `1.0` world
  unit. It does not gate first-active frame or hash because stock boot/menu
  capture and firing timing still differ.
- Use `glass.sample` as the next shard-state discriminator. Before pursuing more
  active-shard pixel fixes, make stock and native agree on the sampled piece
  rotations, velocities, angular velocities, and local vertices for the first
  active frame on the ordinary route. For renderer-owned visual work, use
  `dam_regular_glass_shatter_rng_isolation_probe`, which explicitly compensates
  native RNG at the first shard-size draw and requires exact first active
  `glass.sample` parity. The regular-glass shatter routes now pin
  `stock_speedframes=2`, and the rebuilt ares oracle treats
  `stock_gameplay_start_global` as a fixed origin for frame numbering. The
  checked-in visual route now uses a stock global-timer fire window and traced
  `159,114` crosshair, but later full-wrapper evidence supersedes the early
  clean repeats: the same route still fails on dirty stock startup cadences
  before pixel comparison because the shot lands on a nearby prop face instead
  of pane pad `10004`. Keep both the clean repeats and the global-origin miss
  controls as route-stability history, not renderer evidence.
- Tighten the shatter route only after stock/native firing timing and random
  state are proven equivalent. The next gates should be first-active timing,
  first/full active-state hash, and frame-by-frame active count/lifetime, not a
  broad visual-pixel threshold.
- Compare regular-glass bullet cracks against stock, not just prop-impact trace
  flags. This is now a hard state gate on
  `dam_regular_glass_shatter_probe`: read-only `impact_state` selects the first
  world-converted background impact, requires matching semantic fields, and
  accepts the current one-room-unit vertical quantization delta (`2.140` world
  units) under a `5.0` tolerance. Keep this gate green while working on the
  broader active-shard visual route.
- Use `dam_regular_glass_shatter_visual_probe` for pane disappearance,
  crack-art placement, and active-shard presentation evidence. On the successful
  stock path it captures active shards (`90 -> 90`) and the same destroyed pane,
  and the checked-in route keeps required health/HUD parity plus a
  screenshot-frame chr `12` actor guard over gameplay fields and world position.
  It now reports a masked aggregate that excludes the damage arc and viewmodel
  only after the pre-pixel guards pass, so use the masked warm/bright metrics
  plus the named ROIs when deciding whether a future change improves
  glass/material presentation. It still has high ROI and whole-frame diffs, so do
  not turn it into a hard pixel gate until actor composition, HUD/overlay noise,
  stock active-bbox mapping, and crack/impact-state parity are handled. The
  current fail summary shows both a health/shard phase mismatch
  (`Bond 0.8750 -> 0.9375`, shard timer `55 -> 91`) and meaningful chr `12`
  drift (`xz=36.145`, `dy=34.200`), so this route still needs a cleaner
  timing/composition solution before pixel evidence is authoritative. Run
  `tools/score_visual_checkpoints.py --require-active` on each candidate capture
  before changing the checked-in screenshot frames; the current best active pair
  is still not strict enough, and the attempted pre-damage retime has been
  checked negative.
- The "glass shatter across the whole screen" repro is now fixed at the renderer
  boundary. Focused trace
  `/tmp/mgb64_dam_glass_visual_postclip_guard_173538` shows the two
  frame-195 CPU-clipped `effect=glass_shards` triangles that previously emitted
  full-viewport `bbox=[-1,-1]-[1,1]` coverage are now rejected as
  `postclip_glass_shard`, while bounded active-shard triangles still emit. The
  guard is not Dam-specific; it rejects only labelled glass-shard triangles that
  expand to viewport-sized post-clip coverage.
- The remaining active-shard pixel gap is not proven to be the same defect.
  Current clean-stock comparison
  `/tmp/mgb64_dam_regular_glass_visual_guard_stock_173638` still shows stock
  `glass_burst` bright pixels `152 -> 0` at the checked-in native frame, and the
  refreshed comparator makes the screenshot phase mismatch explicit:
  `/tmp/mgb64_final_stock_visual_1782491250` reports stock screenshot
  `first_timer=55 rot_y=2.13` versus native `first_timer=91 rot_y=4.97`, with
  Bond health `0.8750 -> 0.9375` and first damage after glass at stock `12`
  frames versus native `48` frames. The comparator now reports checkpoint
  `move.clock` / `move.dt` and frame/global-timer deltas so this route-clock
  evidence is visible in the capture artifact instead of requiring raw JSONL
  inspection. A piece-aligned temp stock screenshot
  (`/tmp/mgb64_glass_visual_piece_aligned_76968`, stock frame `2517`) removes the
  misleading glass-burst bright mismatch but leaves high whole-frame and
  damage/HUD deltas, so keep this route evidence-only until shard phase and
  combat-damage phase are aligned well enough for a pixel gate.
- Current negative controls agree with that classification. Temporary native
  frame `163` (`/tmp/mgb64_dam_visual_native163_1782496900`) aligns the sampled
  shard timer (`55 -> 55`) but makes the screenshot substantially worse because
  native is in the damage-flash/HUD phase. Temporary native speedframe `2`
  (`/tmp/mgb64_dam_visual_native_sf2_1782497200`) fails the pre-pixel impact
  guard and over-advances shard age (`55 -> 147`). Do not fix Dam glass by
  changing gameplay damage cadence or globally speedframing native route ticks;
  isolate route timing first, then compare renderer presentation.
- The pre-damage route idea is also checked negative. Prototype
  `/tmp/mgb64_dam_visual_predamage_1782497344` aligns both health/HUD phase and
  shard timer before first Bond damage, but stock chr `12` crosses the
  foreground in action `15` while native chr `12` remains action `8`. The actor
  guard fails and manual screenshot metrics worsen, so a useful glass visual
  route needs actor/composition isolation, a different pane/view, or a
  stock/native actor-state control before a pre-damage pixel gate is meaningful.
- Use the `glass_props.sample` inventory before creating that route. Native
  inventory `/tmp/mgb64_dam_glass_actor_inventory_1782498378` shows pad `10001`
  as the best regular-glass candidate by guard distance and confirms the current
  pad `10004` target is intrinsically actor-contaminated. The next candidate
  route should start from a pane/view selected by this data, then require the
  same active-glass, impact, actor-field, and actor-position pre-pixel guards.
  Current pad-`10001` native scouting found a usable view but only crack damage,
  not active shards, so either solve its shot/damage path or move to the next
  inventory-ranked regular pane before replacing the pad-`10004` active-shard
  route. Native continuation scouts ruled out blindly promoting pads `10090`,
  `10092`, `10085`, or `10080`: they either put a guard in the foreground or
  require a cutaway/sky-through-room view. If one of those panes is revisited,
  stock/native actor-position tracking must be part of the route from the first
  capture, not added after pixel metrics look promising.
- Do not use direct native chr `12` forcing as the current route fix. It is now
  checked negative: early forcing blocks the native shatter path, and
  post-shatter forcing can activate a second pane while still missing the actor
  position tolerance. A useful normalization must either preserve exact glass
  state and actor composition together or select a different pane/view.
- Validate shard lifetime/alpha/sorting frame-by-frame after shatter, including
  multi-pane pass-through behavior from the native material regression
  (`shatter_count=2`, `shard_max_active=180`) with a stock-backed counterpart.
- Treat `GE007_GLASS_SHARD_FIXED_MTX=1`, `GE007_GLASS_SHARD_COMPRESS=1`,
  `GE007_GLASS_SHARD_BASIS_SCALE=1`, `GE007_GLASS_SHARD_NO_BASIS_SCALE=1`,
  `GE007_FIELD_10E0_SCALED=0`, and `GE007_NEAR_CLIP_ONLY=1` as checked A/Bs for
  the current route. Fixed-matrix is visually default-equivalent, full-matrix
  compression remains a negative/control path, legacy basis scaling recreates the
  undersized native shards, no-basis scaling recreates the too-large active-shard
  regression, unscaled `field_10E0` recreates the Surface/large-room projection
  regression, and near-clip-only reintroduces broad scene over-admission.
- After shard-state parity is proven, prioritize stock/native presentation
  semantics for the glass-shard effect rather than rechecking facts already
  guarded by `glass_material_regression.sh`. Use
  `GE007_EFFECT_TRI_TRACE=1 GE007_EFFECT_TRI_TRACE_LABEL=glass_shards` plus
  `GE007_TRACE_TEXSELECT=1` to keep raw VTX RGBA, post-light shade, texture id,
  render mode, other-mode high, combiner id, and geometry flags attached to the
  same triangles, and treat deviations from the guarded material tuple as a new
  regression. Add `GE007_DUMP_LOADED_TEXTURES='*'` only when validating texture
  payloads; the current expected shard state is base `56x54` IA8 plus a live
  tile-1 `27x27` mip. The loaded-tile two-texture N64-filter diagnostic is now
  checked negative for the active-shard visual route, broad alpha blend factors
  are worse, XLU less-vs-lequal is a no-op, and the OpenGL alpha-to-coverage XLU
  probe only weakly changes the burst ROI. The current scoped interpolation
  probe also proves `0x00F38E4F020A2D12` noperspective shader inputs reach the
  shard material without moving the glass-burst ROI. The shader-scoped
  alpha-from-intensity probe reaches the same shard combiner and reproduces the
  best IA8-channel metric, but it remains non-promotable by itself. The current
  combined alpha-intensity plus real-MSAA A2C probe is also non-promotable. The
  new `[SHARD-COVERAGE]` gate proves stable shard raw-mode identity and high
  overlapping bbox pressure, while the screen-space coverage-wrap thinning and
  stencil coverage-memory approximations are now checked negative. The
  memory-color-only RDP blender diagnostic and framebuffer-alpha coverage memory
  diagnostic are also checked negative. The current default-vs-`GE007_GLASS_SHARDS=0`
  control is stronger: it proves the normal falling-shard pass has no visible
  framebuffer contribution at the first-active checkpoint. The contributor
  isolation harness then proves the tested bullet-impact, fog, and weapon toggles
  move pixels but are not fixes for the burst. The next implementation target
  should therefore isolate pane break, crack/bullet-impact decal, HUD/viewmodel,
  and presentation-order contributors before direct ares/Parallel-RDP shard-pixel
  comparison or a true RDP path for `0C1849D8`.
  The 2026-06-27 step-back check confirms the refined instrumentation is useful
  but not final pixel evidence: `/tmp/mgb64_glass_active_visual_crosshair_parity`
  passes first-active shard state/projection/material containment and the
  native `158:114` / stock `159:114` crosshair-equivalent fixture, then
  explicitly reports
  `impact_lifecycle=status=dirty` (`checkpoint_occ=0->1`,
  `first_delta_from_glass=6->-36`, first world-impact center delta `0.000`).
  The older post-shatter visual route remains sidelined: the exact `1146`
  stock-startup branch did not reproduce in
  `/tmp/mgb64_post_shatter_visual_impact_recheck`, and a temporary branch probe
  without that precondition failed pre-pixel health, active-shard count, and
  chr `12` guards. The replacement clean fixture is now
  `tools/glass_impact_visual_isolation_regression.sh` /
  `dam_regular_glass_shatter_rng_impact_visual_probe`: current proof
  `/tmp/mgb64_glass_impact_checkpoint_search_focused` passes stock/native
  health/HUD phase, `90 -> 90` active shards, shard timer `6 -> 6`, world impact
  center delta `0.000`, quad max delta `0.000`, selected world-impact parity
  despite full-buffer occupancy `1 -> 2`, shard projection parity (`86 -> 86`
  onscreen, max area `8.123% -> 8.123%`), and projected decal center delta
  `0.055px` after fixing the native world-impact room-matrix visibility
  compensation. Its broad visual gap is still
  substantial (`whole=89.162%`,
  `masked=88.512%`, `glass_burst=86.292%`), but it is the correct route for pane break,
  crack/decal, bullet-impact, HUD/viewmodel, and presentation-order isolation
  only when paired with actor/composition-aware masks. The report-only
  `visual_oracle` records why: `status=dirty`,
  `usable_for_production_pixel_fix=false`, native-only visible chr `45`, chr
  `10` `onscreen` mismatch, and chr `12` `hidden_bits`/`action` plus position
  mismatches. The new `impact_pixel_oracle` records the localized pixel evidence
  without changing that conclusion: `status=masked_dirty`, focus masked changed
  `82.972%`, focus excluded `59.099%`, and unoccluded-left bright pixels
  `270 -> 73`. The follow-up checkpoint search scanned `744` active impact
  pairs in the same trace and found `0` strict clean candidates, so the next
  pixel-parity step needs a new route/view/mask rather than retiming this route.
  The next route seed is now
  `tools/glass_pad10092_impact_visual_regression.sh` /
  `dam_regular_glass_shatter_pad10092_impact_visual_probe`. It uses the
  actor-light pad `10092` view and a stock/native crosshair split
  (`160:120` -> `159.00:122.50`) to pass impact geometry
  (`4.785` world units, `0.949px` projected center), while requiring stock
  first-gameplay global `1147`. The route now also runs
  `tools/compare_visual_framing_trace.py`, which gates camera/view/room-basis
  alignment and reports native's extra drawn room `124`. The room-draw isolation
  proof then shows room `124` is native-pixel-neutral (`0.000%` default-vs-skip
  change), so pixels remain report-only because actor drift still prevents a
  strict clean candidate.
  The impact-fixture contributor sweep
  `/tmp/mgb64_glass_impact_contributors_crosshair_parity` then passed and gives
  the useful ownership split: `shards_off` changes `0.000%` of the logical
  viewport and projected shard masks, while `bullet_impacts_off` moves `8.818%`
  of the viewport and `14.688%` of `glass_burst` with bright pixels
  `1204 -> 742`; `weapon_render_off` and `no_fog` mostly affect the
  HUD/viewmodel, `world_impact_alpha_from_intensity` owns pixels but worsens
  stock, and `flat_bullet_impacts` is only `0.038%`. That is enough to keep
  executing on instrumentation, but it changes the target: first build a cleaner
  visual oracle, then inspect localized bullet-impact/decal presentation around
  pane break before another falling-shard blend/projection experiment.
  The sharper pad-`10092` contributor proof
  now has a corrected material-specific follow-up:
  `/tmp/mgb64_glass_pad10092_contributors_refined_cc`. The harness uses
  `0x00f38e4f020a2d12` for the cleaned pad-`10092` world-impact signature and
  records that target id in summaries; the one-variant smoke
  `/tmp/mgb64_glass_pad10092_contributor_summary_smoke` proves the summary path.
  The refined result confirms the same direction with less ambiguity:
  alpha-from-intensity, mixed alpha-from-intensity, loaded-tile 2-texture
  filtering, coverage wrap-thinning, and `ZMODE_DEC` no-offset are pixel-neutral;
  stencil/RDP-memory variants move only `0.001%`; inverse-visibility-scale off
  moves `0.011%`; fixed room matrices move `0.079%` with only tiny
  stock-direction movement, and every refined variant leaves
  `projected_impact=0.000%`.
  `/tmp/mgb64_glass_pad10092_pixel_semantics_effect_footprint` then narrows the
  pixel-ownership question further: the emitted `bullet_impact_world` footprint
  covers only `3.001%` of `projected_impact`; the full ROI is `90.461%` changed,
  and excluding that unpadded footprint leaves `91.627%` changed
  (`93.123%` after excluding a 2-logical-pixel padded footprint). This makes the
  small world-impact decal footprint an insufficient explanation for the broad
  visual mismatch.
  The presentation-alignment probe then confirms this is not primarily a viewport
  remap problem: best crop/frame search still leaves `tower_pane=94.067%` and
  `impact_side=90.233%` changed, with only partial projected-impact improvement.
  Do not pursue a blind shard-size/projection, active-shard color, missing-mip,
  broad filter-policy/interpolation change, raw alpha substitution, simple
  coverage-A2C promotion, random fragment thinning, or GL stencil wrap counting.
  Use `tools/score_glass_projected_pixels.py` for future active-shard color
  candidates and `tools/score_glass_projection_win.py` for projection containment
  candidates, so the next experiments are compared by pixel-family movement and
  by projection error rather than screenshot changed-pct alone.
- Keep `GE007_GLASS_SHARDS=0`, `GE007_GLASS_SHARD_FIXED_MTX=1`,
  `GE007_GLASS_SHARD_COMPRESS=1`, `GE007_GLASS_SHARD_BASIS_SCALE=1`,
  `GE007_GLASS_SHARD_NO_BASIS_SCALE=1`, `GE007_FIELD_10E0_SCALED=0`,
  `GE007_TINTED_GLASS_MIN_OPACITY=N`, `GE007_FLAT_PROP_BULLET_IMPACTS=1`, and
  `GE007_DISABLE_PROP_BULLET_IMPACTS=1` as A/B escape hatches only.

### P1: Surface-contact camera coverage

The render-eye clearance fix handles the known Dam wall/glass/control-room
repros without mutating gameplay position, including basic aim/crouch/peek,
weapon-switch, look-sweep, longer contact variants, object/pathblocker contact,
tank-object contact, moving-truck vehicle contact, closed-door contact, and
opening-door plus closing-door contact controls. The current lane also keeps the
pad-140 portal set tight and the pad-164 service-tunnel continuation visible.
Keep broadening coverage before calling the near-wall viewport problem
completely closed.

Concrete tasks:

- Keep the Dam moving-truck vehicle contact case green, and add more moving
  prop/contact cases if new independent-blocker repros appear.
- Add another non-Dam transparent-surface contact where collision metadata
  resolves to a prop, if one exists; Facility tinted pane `10098` is now covered
  through glass-trace plus room-edge/pathblocker evidence.
- Broaden soak-style drift detection beyond the current Dam exterior wall case
  if new near-surface repros appear.
- Keep the invariant strict: render camera may move; gameplay `pos`, collision
  `col`, room, and collision response must not diverge.

### P2: Full Dam mission playability pass

The automated lanes prove startup, short and long stability, render health,
all-direction movement contracts, damage HUD, scripted look input, hidden-guard
fire/damage gates, throwing-knife impact survival, camera clearance, portal
culling, glass material behavior, shard locality, objective-condition
progression, and a scripted mission-report success transition. A full
objective-complete mission pass is still missing as an organic route: the
current tool set proves Dam objective criteria and frontend end-state plumbing,
but does not yet drive Bond naturally from spawn through objectives and
bungee/end-state with stock-like navigation, combat, and timing.

Concrete tasks:

- Objectives across difficulties: keep `tools/dam_objective_progression_smoke.sh`
  as the narrow criteria/status guard and `tools/dam_mission_flow_smoke.sh` as
  the narrow scripted end-state guard. Add organic routes for
  modem/data/alarm/bungee variants, failure paths, and bungee/end-state
  transition timing.
- Guard behavior: patrol engagement, alarms, spawns, line of sight, door/cover
  interactions, weapon drops, and combat audio.
- World interactions: truck/barriers, doors, tower/gate geometry, pickups,
  explosions, bullet impacts, and tank-adjacent behavior where relevant.
- UI/audio: watch/pause, objective status, damage feedback, music/SFX balance,
  save/config behavior, and quit/restart flow.
- Display modes: 4:3 and widescreen, render scales, fullscreen/windowed, and
  at least one non-default FOV profile.

### P3: Dam visual polish beyond the glass report

These are not blockers for basic Dam playability, but they remain visible
quality targets:

- Character/prop drop shadows.
- Shoot-out-the-lights darkening.
- Bullet sparks/dust and other impact secondary effects.
- Sky/water horizon and aspect behavior on non-4:3 viewports.
- Any residual fog/alpha mismatch once stock reference captures are available.
