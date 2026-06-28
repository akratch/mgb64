# Glass shards (C1) — WIP state

Falling-glass shards (`sub_GAME_7F0A2C44` in `src/game/unk_0A1DA0.c`) were un-stubbed
(commits `f5b8f18`, `d8debf5`). The shatter crash is fixed, shards are bounded,
and the active-shard projection no longer screen-spans on the clean Dam glass route.
`GE007_GLASS_SHARDS=0` disables them. The current umbrella Dam visual gate is
`tools/dam_visual_regression_suite.sh`; latest proof
`/tmp/mgb64_dam_visual_suite_after_actor_probe` passes the Dam visual gates
and includes the native glass material gate, the pad-`10092` actor-masked
active-shard harness, the pad-`10004` impact-aligned pane/decal harness, and
stock/native `glass_projection` comparison.

Local stock-oracle work in this workspace should use the instrumented ares binary
at
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`.
Use `tools/prepare_ares_movement_oracle_build.sh` if that file is missing or if
new route/trace hooks are expected but absent.

## Tie-off / pause point (2026-06-28)

Glass work is intentionally paused here. Do not restart with another broad
stock/native screenshot sweep or a global alpha/brightness tweak; those lanes
have already produced negative controls and misleading center-pixel evidence.
The useful state at pause is:

- Native shard projection/material basics are guarded by
  `tools/glass_material_regression.sh`.
- Stock route/state parity is guarded by `tools/glass_route_parity_regression.sh`.
- The center pixel is near parity: stock `[24,24,24]` versus native
  `[25,25,25]` in `/tmp/mgb64_native_settex_pixel_probe_mapped_1782630417`.
- Off-center pixels still differ: `176,158` is stock `[32,32,32]` versus
  native `[22,22,22]`, and `188,170` is stock `[32,32,32]` versus native
  `[11,11,11]`.
- The current best lead is a per-pixel source/filter/raster/RDP state mismatch,
  not a uniform translucent-composition scalar.

The local ares pixel probe now emits raw RDP state (`other`, `combine`, `env`),
draw tile/tile state, decoded blend/depth/combiner state, and draw words. Resume
with one bounded recapture at stock/aligned `188,170` mapped to native `94,95`,
then compare raw stock state against the native `[SETTEX-PIXEL]` row. If that
does not explain the off-center delta, stop this thesis and pick a different
glass fixture or owner-isolated route before changing renderer behavior.

## Deterministic repro (no aiming)

Shooting glass headlessly is driven by scripted tag-damage, which calls
`maybe_detonate_object` → the shatter path:

```bash
# Dam glass prop is tag 19 (found by scanning GE007_AUTO_DAMAGE_TAG 0..40).
GE007_GLASS_SHARDS=1 GE007_TRACE_GLASS=1 \
GE007_AUTO_DAMAGE_TAG_FRAME=80 GE007_AUTO_DAMAGE_TAG=19 GE007_AUTO_DAMAGE_TAG_AMOUNT=80.0 \
./build/ge007 --rom baserom.u.z64 --level dam --deterministic \
  --screenshot-frame 110 --screenshot-exit
```

To see it close up, warp Bond to a pad next to the glass first (pad 100 ≈ 118 units
from the tag-19 glass; dump pads with `GE007_DUMP_STAGE_PADS=/tmp/pads.txt`):

```bash
... GE007_AUTO_WARP_FRAME=50 GE007_AUTO_WARP_PAD=100 ...
```

The tag-19 window is in a dark basement — too dark to judge quality. **A lit window
(interactive) is the real visual check.**

## Fixed

- **Crash:** `dynAllocateMatrix` returns `Mtx*` but had **no prototype** in
  `unk_0A1DA0.c` → implicit-`int` truncated its 64-bit pointer (`0x728705820` →
  `0x28705820`) → `matrix_4x4_f32_to_s32` wrote 64 bytes to a garbage address → SEGV.
  Fixed with the prototype. (Same class earlier: `currentPlayerGetMatrix10C8`.)
- **Blur/full-screen corruption:** the shard render now follows the native large-room
  matrix split. `field_5C` and native `field_10E0` both carry the level visibility
  scale for room/model rendering; the earlier stock-style unscaled `field_10E0`
  helped isolate glass projection but regressed Surface into a sky-dominant view.
  Falling shards now scale their full model matrix by
  `1.0 / bgGetLevelVisibilityScale()` before the scaled native `field_10E0`
  projection. This cancels the native-only large-room view compression for the
  isolated shard pass without changing global room rendering. `GE007_FIELD_10E0_SCALED=0`
  recreates the Surface/large-room negative control,
  `GE007_GLASS_SHARD_SQRT_BASIS=1` or `GE007_GLASS_SHARD_INV_VIS_SCALE=0`
  restores the previous bounded-but-misprojected path,
  `GE007_GLASS_SHARD_NO_BASIS_SCALE=1` recreates the too-large shard regression,
  `GE007_GLASS_SHARD_BASIS_SCALE=1` restores the older undersized shard basis,
  and `GE007_GLASS_SHARD_COMPRESS=1` remains a full-matrix compression A/B.
- **State/locality guard:** the deterministic Dam pad-103 regular-glass shot now
  produces a bounded 10x9 shard grid (`90` pieces, `max_active=90`) with zero large
  `effect=glass_shards` triangles in `tools/glass_material_regression.sh`.
- **First-active visual isolation guard:** `tools/glass_active_visual_isolation_regression.sh`
  runs `dam_regular_glass_shatter_rng_visual_probe`, a stock/native screenshot route
  with exact first-active shard-sample parity. Current post-fix proof
  `/tmp/mgb64_glass_active_visual_crosshair_parity` clears route control, health,
  shard-state, render-health, projected-visual, and strict stock/native
  projection checks after forcing equivalent stock/native crosshair coordinates
  (stock `159:114`, native `158:114`).
  The projection comparison now passes with `90 -> 90` active/projected shards,
  onscreen parity `82 -> 82`, `0 -> 0` behind-camera shards, max area
  `5.421% -> 5.392%`, and union area `65.245% -> 65.003%` under native
  `scale=inv_vis_full`. This closes the active-shard projection-parity
  milestone. The same proof now reports `impact_lifecycle=status=dirty`:
  checkpoint impact occupancy is `0 -> 1`, first impact relative to first glass
  is `+6 -> -36` frames, and the first world-impact center delta is `0.000`.
  Screenshot pixels still differ because impact/decal phase, presentation,
  material, and actor composition are not final parity.
- **Impact-aligned visual guard:** `tools/glass_impact_visual_isolation_regression.sh`
  runs `dam_regular_glass_shatter_rng_impact_visual_probe`, the same pad-`10004`
  shatter retimed to stock frame `2437` / native frame `113`. Current proof
  `/tmp/mgb64_glass_impact_checkpoint_search_focused` passes route control,
  health/HUD phase, first-active shard-sample parity, `90 -> 90` active shards,
  first shard timer `6 -> 6`, impact comparison, screenshot checkpoint impact
  selected world-impact parity, exact stock/native world-impact quad parity, and
  projection parity (`86 -> 86` onscreen, max area `8.123% -> 8.123%`). The full
  impact buffer is still `1 -> 2` because native also creates a nonvisible
  glass-prop impact. Its report-only `visual_oracle` is dirty rather than
  promotable: stock visible actors are `[10,12]`, native visible actors are
  `[10,12,45]`, chr `10` differs on `onscreen`, chr `12` differs on
  `hidden_bits` and `action`, and chr `12` position delta is `46.926`. This is
  now the better fixture for pane break, crack/decal, and presentation-order
  work; the first-active route remains the stricter timer-1
  shard-state/projection guard.
  The same proof writes a localized report-only `impact_pixel_oracle`, but it is
  also dirty: `status=masked_dirty`, `usable_for_production_pixel_fix=false`,
  `impact_focus.masked_excluded_pct=59.099`,
  `impact_focus.masked_changed_pct=82.972`, and the unoccluded-left ROI changes
  `91.170%` with bright pixels `270 -> 73`.
  The projected world-impact/decal oracle is now built and gated: the pre-fix
  proof `/tmp/mgb64_glass_impact_projected_oracle_focused` measured a
  `51.370px` selected-decal projection offset despite exact world-quad parity,
  and `/tmp/mgb64_glass_impact_checkpoint_search_focused` keeps that at `0.055px`
  under a strict `<=1.0px` gate. Its projected ROI is still report-only for
  pixels (`changed=75.632%`) because the actor/glass presentation remains dirty.
  The same proof now runs `tools/score_impact_checkpoint_candidates.py` across
  the current stock/native traces. It searched `744` active impact checkpoint
  pairs and found `0` strict clean matches; the best pair is still actor-dirty
  with native visible actor `45` present. Retiming within this route is therefore
  not enough to make the broad pixel oracle promotable.
- **LOD mip binding:** `texSelect()` shard and room-glass LOD textures now alias
  tile 1/2 TMEM offsets back into the full `G_LOADBLOCK` mip-chain source when
  those endpoints are live. This makes shard `TEXEL1` resolve to the real `27x27`
  IA8 mip instead of falling back to tile 0, while preserving the old base-tile
  fallback for invalid LOD endpoints. Regression coverage stays clean, but this
  is a correctness fix, not the remaining stock color/presentation fix.
- Hardening: `dynAllocateMatrix` overflow → scratch `Mtx`; VTX buffer 256→512KB;
  shard loop breaks before the GFX buffer overruns; NULL guards.

## Remaining work (stock presentation still mismatched)

1. **Stock active-shard presentation is not matched.** The stock
   `dam_regular_glass_shatter_visual_probe` frame shows the distant pane burst
   plus HUD/damage presentation noise around the viewport. Native default renders
   a stable local burst but still differs in burst brightness/coverage. Treat the
   colored viewport arc as HUD/presentation until a trace proves it is
   shard-owned. `/tmp/mgb64_shard_visual_actor_guard_1782459927` proved the old
   visual route was contaminated by native autoaim killing chr `12`. The current
   route disables native route-only autoaim before the shot and compares chr `12`
   at `last-active`; `/tmp/mgb64_visual_route_widefire_valid_1782461353` passes
   the actor guard (`alive/hidden/hidden_bits/onscreen/rendered/action` all
   match) while keeping the visual diff high (`87.3%` overall,
   `glass_burst=81.3%`, `damage_arc=77.3%`, `hud_viewmodel=91.2%`).
   The active renderer-isolation route is cleaner but still not pixel parity:
   `/tmp/mgb64_glass_active_visual_crosshair_parity` has exact first-sample and
   strict projection parity, then writes bounded visual metrics
   (`whole=92.362%`, `masked=92.036%`, `glass_burst=99.229%`,
   projected visual `91.735%`). Its new impact-lifecycle audit is dirty, so the
   impact-aligned route is the pane/decal fixture to use for the next visual
   pass, not another direct active-shard shader toggle.
   `/tmp/mgb64_glass_impact_checkpoint_search_focused` improves the route
   cleanliness and narrows the visual gap (`whole=89.162%`,
   `masked=88.512%`, `glass_burst=86.292%`) while matching impact center, all
   impact-quad vertices, and projected decal center (`0.055px <= 1.0px`)
   exactly enough for the geometry gate, but it still does not solve pixel
   parity.
   Actor composition remains intentionally reported rather than gated for the
   pad-`10004` route, and the current summary makes the risk machine-readable:
   `visual_oracle.status=dirty`, `usable_for_production_pixel_fix=false`, stock
   visible actors `[10,12]`, native visible actors `[10,12,45]`, chr `10`
   `onscreen` mismatch, chr `12` `hidden_bits`/`action` mismatches, and chr `12`
   position delta `46.926`. The localized `impact_pixel_oracle` also remains
   `masked_dirty`: the stock guard occluder excludes `59.099%` of
   `impact_focus`, and the unoccluded-left ROI still changes `91.170%` with
   bright pixels `270 -> 73`. Cleaner pane/view selection and
   actor-composition isolation remain part of that fixture work. The first pad-`10001`
   actor-clean re-attack is now a scoped negative rather than an open guess:
   `/tmp/mgb64_m5_actor_clean_scout_1782557026` tested six
   late-warp/late-fire variants against the stock active trace. The best organic
   route matches health, HUD timers, active count, shard timer, and visible actor
   set, but still fails on chr `10` action/position (`15 -> 8`, delta `108.542`)
   and chr `12` onscreen state. The actor-force diagnostic
   `/tmp/mgb64_m5_actor_force_probe_1782557347` also failed because it perturbs
   actor rendered state, so native actor-forcing is not a promotable fix.
   The broader Dam pane scout then found pad `10092` as a better actor-light but
   still not actor-clean branch. `/tmp/mgb64_m5_native_fixture_scout_roundrobin_1782558170`
   identified pad `10092` yaw `315` distance `650` as the best target-destroyed
   native candidate (`active=88`, visible `[44,7]`). The stock/native no-compare
   probe `/tmp/mgb64_m5_pad10092_stock_probe_1782558344` passes the important
   glass invariants (`max_active=88 -> 88`, first shard position delta `0.000`,
   prop-position delta `0.000`, destroyed/remove parity), but actor composition
   still differs (`chr7` onscreen and chr44 position). Retiming in
   `/tmp/mgb64_m5_pad10092_timing_scout_1782558492` reduced chr44 position delta
   to `345.862` but did not produce a strict actor match. The branch is now
   guarded by `tools/glass_actor_masked_visual_regression.sh` rather than left
   as an open scout.
   `/tmp/mgb64_shard_draw_join_dam_visual_suite2/glass_actor_masked/pad10092_actor_masked`
   passes the screenshot-checkpoint route
   `dam_regular_glass_shatter_pad10092_actor_masked_visual_probe`: full-health
   no-HUD state, pad `10092` destroyed, `active=88 -> 88`, first shard position
   delta `0.000`, and prop-position delta `0.000`. The actor caveat is explicit
   (`best_strict=0`, chr `7` onscreen drift, chr `44` position delta `574.977`).
   The route no longer requires one exact stock first-gameplay global because
   current ares startup can hit the same state-valid fixture through either the
   `1147` or `1149` branch; the strict timer-1 first-active proof remains
   `tools/glass_active_visual_isolation_regression.sh`. Masked visual metrics
   are still high (`full=91.634%`, `masked=92.092%`,
   `tower_pane_masked=97.325%`), so this is a catastrophic-regression harness,
   not final active-shard pixel parity. Current projection tracing in
   `/tmp/mgb64_shard_draw_join_dam_visual_suite2/glass_actor_masked/pad10092_actor_masked/projection_dam_regular_glass_shatter_pad10092_actor_masked_visual_probe.json`
   proves both stock and native project `88` active pieces with `88` onscreen
   and `0` behind-camera pieces under `scale=inv_vis_full`; max area is
   `0.206% -> 0.244%` and union area is `6.860% -> 6.119%`.
   Fresh current-build native-only scout
   `/tmp/mgb64_dam_impact_native_fixture_scout` reran the broader pane/yaw
   matrix against new stage pad/chr dumps from
   `/tmp/mgb64_dam_impact_fixture_scout_inputs`. It confirms pad `10092`,
   yaw `315`, distance `650` is still the best native actor-light target among
   the `20` tested candidates (`score=5609.623`, target destroyed, active
   shards present, visible/onscreen actors `2`). Pad `10093` yaw `315` is close
   (`score=5778.061`) but has `196` active shards, while the other destroyed
   panes carry `3` to `5` visible actors or worse. Reusing the existing
   pad-`10092` actor-masked stock/native route as an impact oracle does not
   work: `tools/score_impact_checkpoint_candidates.py` finds `0` strict
   candidates, with best impact center delta `21.400` world units and projected
   center delta `2.656px`. The next impact pixel route should therefore start
   from pad `10092`/yaw `315`/distance `650`, but needs dedicated stock-backed
   impact timing and/or a tighter mask rather than reusing the active-shard
   screenshot checkpoint. That route seed now exists as
   `dam_regular_glass_shatter_pad10092_impact_visual_probe`, guarded by
   `tools/glass_pad10092_impact_visual_regression.sh`. Current proof
   `/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean` uses stock crosshair
   `160:120`, native fire start `68`, and native crosshair `159.00:122.50`; it passes health/HUD,
   visual-framing parity, `active=88 -> 88`, first timer `1 -> 1`,
   prop-position delta `0`, shard projection, selected world-impact identity,
   impact center delta `4.785 <= 5`, and projected decal center delta
   `0.949px <= 1.0px`. It also matches the full sampled world-impact type
   sequence at stock frame `2541` / native frame `124` (`[1,1,1,1]` on both
   sides). The framing comparator proves camera/view/room-basis
   alignment (`cam_pos_delta=0.134`, `cam_target_delta=0.136`,
   `view_delta=0`, `room_basis_delta=0`) and reports the remaining room draw-set
   difference (`stock=[132,136]`, `native=[124,132,136]`) separately from pixel
   dirt. The proof also verifies stock-route retry behavior by rejecting a
   `1149` branch before passing on stock first-gameplay global `1147`. It is
   still not final pixel parity: strict actor candidates remain `0`, chr
   `7`/`44` drift remains, and the projected-impact pixel ROI is report-only.
   The room `124` lead is now explicitly isolated by
   `tools/glass_pad10092_room_draw_isolation.sh`. Current proof
   `/tmp/mgb64_glass_pad10092_room_draw_isolation` passes
   `GE007_FORCE_UNRENDERED_ROOMS=124` and `GE007_SKIP_BG_ROOM=124` variants with
   native default-vs-variant screenshot delta `0.000%` for both, while preserving
   health/glass, projection, and framing gates. Treat room `124` as
   trace-visible but pixel-neutral at this checkpoint; it is not the current
   pixel blocker.
   The chr `7`/`44` actor-ownership lead is now isolated by
   `tools/glass_pad10092_actor_ownership_isolation.sh`. Current proof
   `/tmp/mgb64_glass_pad10092_actor_ownership_isolation` passes the same
   health/glass, projection, and framing gates for native-only
   `GE007_SKIP_RENDER_CHRNUMS=7`, `44`, and `7,44` variants. The probe has real
   signal (`skip_chr44` changes native pixels by `0.309%`, concentrated in the
   lower actor cluster), but stock-vs-native actor-masked mismatch and
   projected-impact mismatch change by `0.000%` for every actor-skip variant.
   Treat chr `7`/`44` draw ownership as ruled out for the current pad-`10092`
   pixel blocker; the next pass should target material/presentation or
   stock/native blend/output differences instead of more actor or room chasing.
2. **Organic active shard state can still differ before rendering.** The fresh stock/native
   sample trace in `/tmp/mgb64_dam_shard_stock_sample_1782430294` adds bounded
   `glass.sample` records for the first four active pieces. At first active
   frame, stock piece 0 has `rot=[2.50,0.00,3.50]`, `vel=[-0.40,2.72,0.83]`,
   and vertices roughly `24x24`; native piece 0 starts at the same position but
   has `rot=[2.50,0.00,5.97]`, `vel=[1.50,0.55,0.41]`, and vertices around
   `8x8`. The current visual route is therefore not a hard renderer oracle for
   active shards until stock/native firing timing and RNG state are aligned.
   The follow-up RNG trace at `/tmp/mgb64_dam_shard_rng_trace_1782430799`
   confirms the pre-active seeds differ (`0x000000016539D61A` stock,
   `0x00000001D46D6152` native) and the transition draw counts differ by two
   (`1272` stock, `1274` native).
   A focused native caller trace at `/tmp/mgb64_native_rng_calls_1782432034`
   shows native performs 11 non-shard draws between the pre-active seed and the
   first `sub_GAME_7F0A1DA0` shard-size draw, then calls
   `sub_GAME_7F0A2160` for piece state. Stock's first piece state begins one
   draw earlier in the same RNG sequence. Seeding native immediately before the
   first size draw (`GE007_AUTO_RNG_CALL_SEED_SCRIPT=1237:0x00000001D8F3CC2B`)
   in `/tmp/mgb64_native_rng_call_seed_1782432176` makes
   `tools/compare_glass_trace.py --require-sample-match` pass against stock:
   the first four sampled active shard pieces match exactly. This is diagnostic
   evidence, not a gameplay fix; the remaining work is to align the real route
   timing/RNG path. The explicit renderer-isolation form is now encoded as
   `tools/rom_oracle_routes/dam_regular_glass_shatter_rng_isolation_probe.json`,
   which requires exact first-sample parity under the call-level seed.
   After the autoaim-clean route refresh, the equivalent native shard-generator
   entry is call `1702`; `/tmp/mgb64_rng_route_autoaimoff_valid_1782461215`
   proves the checked-in RNG-isolation route again passes exact first-sample
   parity with `GE007_AUTO_RNG_CALL_SEED_SCRIPT=1702:0x00000001D8F3CC2B`.
   The visual companion
   `tools/rom_oracle_routes/dam_regular_glass_shatter_rng_visual_probe.json`
   uses the same call-level seed under screenshot capture and passed the
   milestone-4 renderer-isolation gate; this is still route-only evidence, not a
   fix for unseeded gameplay RNG/cadence.
   `--require-hash-match` is still not a reliable cross-provider semantic gate
   because the current hash covers raw active-buffer words.
3. **Material/presentation state is still open after state parity.** A focused
   read-only trace at `/tmp/mgb64_shard_semantics_1782458590/native` shows the
   VTX buffer carries normal/alpha bytes under `G_LIGHTING`, not RGB color:
   `5,5,126,255`, `5,251,126,255`, and `251,251,126,255` are the shard normals
   approximately `(+5,+5,+126)`, `(+5,-5,+126)`, and `(-5,-5,+126)`. Every
   emitted triangle is then shaded through `G_LIGHTING|G_TEXTURE_GEN` into
   grayscale post-light colors (`150..209` in the sampled emitted rows). All
   sampled shard material rows use render mode `0x0C1849D8`, other-mode high
   `0x00992C60`, combiner `0x00F38E4F020A2D12`, geometry mode `0x00060205`,
   two texture inputs, `settex=0`, and live tile-1 mip binding. A fresh
   stock/native route run in `/tmp/mgb64_shard_stock_native_1782458680` passed
   exact first-sample parity and `max_active=90 -> 90` using the local ares
   binary at
   `build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`.
   A manual logical-viewport comparison on the same artifact keeps the remaining
   visual work scoped: overall changed pixels are still `88.2%`, `glass_burst`
   is `84.6%` changed with bright pixels `544 -> 853`, `damage_arc` is stock-only
   HUD warmth (`6172 -> 0` warm pixels), and `hud_viewmodel` is still high
   presentation noise (`91.5%` changed).
   A follow-up mip trace at
   `/tmp/mgb64_dam_shard_tmem_alias_trace_1782481000` proves `TEXEL1` now
   resolves to the live tile-1 mip (`27x27`, non-null source) for all traced
   shard material rows, but the stock visual diff remains effectively flat
   (`88.36%` before, `88.38%` after). Do not chase missing mip endpoints as the
   active-shard color fix unless a new trace shows an invalid endpoint again.
   After call-seeded first-sample parity, comparing the active-stock screenshot
   from `/tmp/mgb64_dam_shard_rng_trace_1782430799` against the call-seeded
   native screenshot from `/tmp/mgb64_native_rng_call_seed_1782432176` still
   leaves `88.2%` changed pixels overall, but the feature metrics separate the
   real glass burst from overlay noise: `glass_burst` has `544 -> 853` bright
   pixels while the stock-only red/orange pixels are concentrated in the damage
   HUD arc (`6172 -> 0` warm pixels for the native145 capture). That keeps
   material, ordering, burst coverage, and HUD/presentation isolation as the
   next real target after route-state parity, not the random shard generator.
4. **Matrix/clip A/Bs are now controls, not open fixes.** `GE007_GLASS_SHARD_FIXED_MTX=1`
   is default-equivalent for the current route. `GE007_GLASS_SHARD_INV_VIS_SCALE=0`
   or `GE007_GLASS_SHARD_SQRT_BASIS=1` restores the previous bounded-but-
   misprojected path (`82 -> 51` onscreen on pad `10004`), `GE007_GLASS_SHARD_COMPRESS=1`
   is a full-matrix compression negative/control path, `GE007_GLASS_SHARD_BASIS_SCALE=1`
   restores the older undersized native shard basis, and
   `GE007_GLASS_SHARD_NO_BASIS_SCALE=1` restores the too-large shard basis that
   produced max area `23.232%` on the pad-103 route. `GE007_NEAR_CLIP_ONLY=1`
   re-exposes outdoor/sky geometry and is a negative control.
5. **Projection parity is closed; pixel presentation is not.** The stock/native
   projection lane is now a hard guard. The current pad-103 proof in
   `/tmp/mgb64_invvis_active_gate2/active_rng_visual/projection_dam_regular_glass_shatter_rng_visual_probe.json`
   matches active/projected/onscreen/behind counts exactly. The all-piece
   diagnostic comparison
   `/tmp/mgb64_glass_projection_invvis_selected_1782580888/native_probe/projection_pieces_selected_vs_stock.json`
   compares all `90` sampled pieces with screen-center mean error `0.44` pixels,
   p90 `0.73`, max `3.99`, and median clip-w ratio `1.0006`.
   `tools/compare_glass_shard_pixel_oracle.py` is the current spatial re-attack
   tool for the remaining screenshot gap: it rasterizes traced stock/native
   projected shard pieces into the matched logical viewport and measures
   screenshot deltas under each piece mask. It is not, by itself, causal proof
   that the falling-shard draw pass wrote those pixels. It is now emitted by
   `tools/glass_active_visual_isolation_regression.sh` as
   `projected_shard_pixel_oracle_dam_regular_glass_shatter_rng_visual_probe.json`.
   The default wrapper records the compact first-16 sample and warns that it is
   partial; for full evidence, run the wrapper with both
   `GE007_TRACE_GLASS_PROJECTION_ALL=1` and
   `MGB64_ARES_TRACE_GLASS_PROJECTION_ALL=1`.
   `/tmp/mgb64_glass_shard_pixel_oracle_full` is the first full-sample spatial
   proof:
   the active gate passes with `90/90` sampled pieces, the triangle oracle
   rasterizes the `82` onscreen pieces, and the shard-covered union is still
   `89.980%` changed with mean luma delta `+11.35`, saturation delta `-0.108`,
   and mean abs RGB delta `56.56`. The important presentation signature is not a
   projection miss: stock buckets inside the triangle-covered shard union are
   `bright=88`, `near_white=78`, `gray=50482`, while native is `bright=223`,
   `near_white=1090`, `gray=45977`. The bbox oracle reports a broader envelope
   with `65.4%` overlapping coverage, `91.132%` changed pixels, and
   `near_white=190 -> 1533`. Pieces `68` and `69` dominate the over-bright
   cluster (`bright_or_white_delta=+1177` and `+195` in triangle mode), while
   high-overlap lower shards such as pieces `2`, `3`, and `13` become heavily
   desaturated.
   The 2026-06-27 native control changes the interpretation: comparing default
   native against `GE007_GLASS_SHARDS=0` is byte-identical at the screenshot level
   (`0/307200` pixels changed) and reports `0.000%` changed under the `82`
   rasterized shard masks in
   `/tmp/mgb64_shards_off_native_active/default_vs_shards_off_shard_pixel_oracle.json`.
   Therefore the stock/native shard-mask deltas are co-located with the projected
   shard envelope, but they are not evidence that the default falling-shard pass
   is visibly contributing to the framebuffer at this checkpoint. Pair every
   future pixel-oracle claim with this native default-vs-shards-off control before
   treating the result as shard-owned.
   The follow-up draw/material join is now repeatable with
   `tools/compare_glass_shard_draw_trace.py`: run a native trace with
   `GE007_EFFECT_TRI_TRACE=1`, `GE007_EFFECT_TRI_TRACE_LABEL=glass_shards`,
   `GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1`,
   `GE007_TRACE_TEXGEN_MATERIALS=1`, and
   `GE007_TRACE_TEXGEN_MATERIALS_EFFECT=glass_shards`, then join the native log
   against the pixel-oracle JSON. Current proof:
   `/tmp/mgb64_shard_draw_join_trace/shard_draw_trace_join.json` passes with
   `172` effect rows, `172` material rows, and all rows joined. The top
   over-bright pieces (`69`, `68`, plus desaturated pieces such as `3`, `2`,
   and `13`) are normal `glass_shards` effect triangles with material
   `cc=0x00F38E4F020A2D12`, raw mode `0x0C1849D8`, `cvg=wrap`,
   `clr_on_cvg=1`, the two expected IA texture tiles (`56x54` and `32x27`),
   and no CPU clipping. The stricter RDP formula join
   `/tmp/mgb64_shard_rdp_cvg_formula_trace/shard_draw_trace_join_formula.json`
   also passes: the decoded material matches the ares/Parallel-RDP formula for
   this mode, and the diagnostic coverage-memory shader was active for the joined
   rows. But the visual A/B in `/tmp/mgb64_shard_rdp_cvg_active_gate` changes only
   `82/307200` pixels versus default native and leaves the shard-mask stock/native
   delta at `89.980%`. Treat the GL RDP coverage-memory branch as a negative
   control, not the next fix.
   The closed facts are now narrower and stronger: active-shard projection,
   material identity, draw provenance, and final-cycle formula decoding are
   healthy, while the normal falling-shard pass has no visible contribution in
   the first-active default screenshot. The next re-attack should use
   `tools/glass_impact_visual_isolation_regression.sh` to isolate pane break,
   crack/decal, bullet-impact, HUD/viewmodel, and presentation ordering before
   spending more time on active-shard blend toggles.
   `tools/glass_contributor_isolation_regression.sh` is the repeatable ownership
   sweep for that next pass. Latest proof
   `/tmp/mgb64_glass_contributor_isolation_current` reuses the stock-backed
   first-active baseline and compares native-only A/Bs: `shards_off` is still
   exactly `0.000%` changed in the logical viewport and under the `82` shard
   masks; `bullet_impacts_off` moves `8.517%` of the viewport but makes
   `glass_burst` bright pixels worse (`484 -> 574`); `weapon_render_off` is
   HUD/viewmodel-only for this ROI (`glass_burst=0.000%`); `no_fog` mostly moves
   the HUD/viewmodel (`75.492%`) and only `1.243%` of `glass_burst`; and
   `flat_bullet_impacts` is a tiny `0.051%` frame delta. This makes pane/decal
   presentation and ordering the next actionable target, not suppression toggles.
   The impact-fixture sweep is now also clean:
   `/tmp/mgb64_glass_impact_contributors_crosshair_parity` passes the
   stock-backed impact baseline, then shows `shards_off=0.000%` logical viewport
   and shard-mask change, `bullet_impacts_off=8.818%` viewport movement with
   `glass_burst=14.688%` and bright pixels `1204 -> 742`,
   `weapon_render_off=2.321%` with `glass_burst=0.000%`, `no_fog=23.257%`
   mostly in `hud_viewmodel=75.513%`, `world_impact_alpha_from_intensity=3.413%`
   but stock-worse, and `flat_bullet_impacts=0.038%`. Because this checkpoint has
   exact selected world-impact center/quad parity and shard timer `6 -> 6`, it is
   the stronger proof that the next implementation target is bullet-impact/decal
   presentation around pane break, not falling-shard projection or blend toggles.
   The full impact buffer is still `1 -> 2` because native creates an additional
   nonvisible glass-prop impact on pad `10004`; keep comparing the selected world
   impact for route parity.
   The pad-`10092` contributor sweep needed one correction before its
   world-impact-specific variants were trustworthy. The harness now uses the
   fixture-specific cleaned-route combiner id `0x00f38e4f020a2d12` for
   `pad10092-impact` and records that id in the generated summary; the older
   active/impact fixtures keep `0x00f39e4f1f39e4f1`. The corrected proof
   `/tmp/mgb64_glass_pad10092_contributors_refined_cc` reuses the gated
   pad-`10092` base case
   `/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact`.
   Its result is a sharper negative: alpha-from-intensity, mixed
   alpha-from-intensity, loaded-tile 2-texture filtering, coverage wrap-thinning,
   and `ZMODE_DEC` no-offset are pixel-identical; stencil/RDP-memory variants
   move only `0.001%` of the viewport; disabling bullet-impact inverse
   visibility scale moves `0.011%`; fixed room matrices move `0.079%` with tiny
   stock-direction movement, but still `projected_impact=0.000%`. Treat this as
   evidence to stop chasing room `124`, chr `7`/`44`, shards, stale
   world-impact combiner toggles, or broad blend/depth knobs for pad `10092`;
   the next target is stock/native presentation/output semantics or an exact
   localized ares/Parallel-RDP pixel oracle.
   `/tmp/mgb64_glass_pad10092_presentation_alignment_probe` then checks the first
   of those next targets without changing code. It runs
   `tools/glass_pad10092_presentation_alignment_probe.sh` over the existing
   pad-`10092` base screenshots and enumerates `10428` crop/frame candidates per
   ROI. Stock and native presentation boxes do differ (`[8,2,625,474]` versus
   `[0,19,640,442]`), and the tiny `projected_impact` ROI is alignment-sensitive
   (`100.000% -> 76.250%`), but broad pane mismatch remains high after the best
   crop search (`tower_pane 97.325% -> 94.067%`, `impact_side 95.533% ->
   90.233%`). Presentation alignment is therefore a secondary measured factor,
   not the primary blocker. Move the next implementation work to localized
   pane/decal pixel-output semantics rather than another viewport remap.
   `/tmp/mgb64_glass_pad10092_pixel_semantics_sequence_clean` is the current
   positive localization artifact for that target. It runs
   `tools/glass_pad10092_pixel_semantics_probe.sh` and captures
   `bullet_impact_world` with `GE007_EFFECT_TRI_TRACE_DRAWCLASS=room`; using
   `effect` as the draw-class filter was the reason the earlier one-off capture
   had material rows but no `EFFECT-TRI` rows. The route has now been retimed to
   native fire start `68`, native frame `124`, and crosshair `159.00:122.50`.
   `tools/compare_bullet_impact_sequence.py` proves the sampled world-impact
   type sequence now matches stock (`[1,1,1,1] -> [1,1,1,1]`) while preserving
   the selected-impact geometry gate (`impact_delta=4.785`,
   `projection_delta=0.949px`). The native-only type-`7` pollution from the old
   frame `126` route is gone, and the material summary now records one localized
   world-impact signature, `0x00f38e4f020a2d12`, with textures
   `(64,32)/(32,16)`. The base projected-impact pixel mismatch remains
   `90.713%`, so this is not a rendering fix; it is the cleaner route needed
   before localized world-impact/decal output semantics can be interpreted. The
   focused material sweep `/tmp/mgb64_glass_pad10092_semantics_ab` found no clean
   default renderer knob, and `/tmp/mgb64_pad10092_fire_window_scout` showed
   shorter fire windows do not reach the glass lifecycle, so continue from this
   clean route rather than broad shader/material toggles.
   `/tmp/mgb64_glass_pad10092_pixel_semantics_effect_footprint` now adds the
   next ownership refinement. `tools/compare_effect_footprint_visual.py` scores
   the stock/native pixels inside the emitted `bullet_impact_world` effect bbox
   and inside the rest of the `projected_impact` ROI. The unpadded emitted
   footprint covers only `3.001%` of `projected_impact`; the whole ROI is still
   `90.461%` changed, and excluding the unpadded footprint leaves `91.627%`
   changed (`93.123%` after excluding a 2-logical-pixel padded footprint). That
   makes the tiny world-impact decal footprint an insufficient explanation for
   the broad pad-`10092` mismatch. The next faithful target is pane/background
   presentation or a stock/ares localized pixel-output oracle, not another
   isolated world-impact shader toggle.
   `/tmp/mgb64_glass_pad10092_texgen_roi_material_probe` then closes the
   material-bbox false-positive. `tools/glass_pad10092_texgen_roi_material_probe.sh`
   captures default and `GE007_GLASS_SHARDS=0` with full texgen material tracing
   and full shard projection sampling. The summarizer now maps NDC bboxes into
   the aligned screenshot crop before comparing them to visual ROIs; the older
   logical-viewport comparison was too small and misclassified ownership.
   Corrected proof emits `14` primary `projected_impact` rows, all room `glass`,
   and shard-off leaves those same `14` room-glass rows. The framebuffer still
   does not move: full-frame, `projected_impact`, and the `5062`-pixel
   full-sample shard mask all report `0.000%` changed. Therefore the current
   falling-shard draw pass should stay ruled out, but for the corrected reason:
   the primary ROI is not falling-shard owned at this checkpoint. The same
   corrected proof records `tower_pane` as `46` texgen rows (`38` room `glass`,
   `8` shard bboxes) and `impact_side` as `22` room `glass` rows. Room-glass
   material A/Bs in
   `/tmp/mgb64_glass_pad10092_room_glass_visibility` show those room-glass rows
   are pixel-visible in `tower_pane`, but the tested controls are not useful
   fixes: the only stock-directed change is `GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5`
   at `-0.222` percentage points, `1.5` and `GE007_ROOM_ALPHA_AS_TEXEDGE=1` are
   default-equivalent, and opaque/filter variants are broad side effects or
   negative controls. That run now also scores control-footprint coverage with
   `tools/compare_control_footprint_visual.py`. The largest `tower_pane`
   footprint is `GE007_DISABLE_N64_FILTER=1` at `44.802%`, but it worsens stock
   parity by `+0.080` percentage points. The only improving room-alpha control
   covers just `16.517%` of `tower_pane` and `0.000%` of `projected_impact`,
   while the broad point-filter control reaches only `8.750%` of
   `projected_impact`. This confirms the refined instrumentation is asking the
   right question--visible wrong-pixel ownership--and the simple room-glass and
   filter toggles are not the fix.
   `/tmp/mgb64_glass_pad10092_texgen_roi_pixel_correlation_current` now joins
   corrected aligned-crop material bboxes to the stock/native ROI pixel
   semantics. Latest traced native frame `123` shows `projected_impact` is
   `94.474%` changed and `100.000%` covered by two room `glass` bbox rows.
   `tower_pane` is `95.734%` changed but only `21.896%` covered by texgen bboxes
   (`21.186%` room `glass`, `0.710%` `glass_shards`), and `impact_side` is
   `93.702%` changed with `39.965%` room-glass bbox coverage. This makes the
   next target exact room-glass output semantics plus broader background or
   post-composite contributors, not a shard pass.
   `/tmp/mgb64_glass_pad10092_roi_pixel_semantics_current` then adds the direct
   stock/native color-semantics check. `tools/glass_pad10092_roi_pixel_semantics_probe.sh`
   reports both unmasked ROI semantics and route-masked semantics; this caught a
   masking trap where `projected_impact` route masking samples only `80 / 380`
   pixels because it overlaps `lower_actor_cluster`. The unmasked evidence says
   this is not a blue-sky leak: `tower_pane` is `95.734%` changed with mean luma
   delta `+16.28` and only `1.099%` native-bluer changed pixels; `impact_side` is
   `93.702%` changed with luma `+17.29` and only `1.666%` native-bluer; and
   `projected_impact` is `94.474%` changed with luma `+4.23` and `0.000%`
   native-bluer. The wrong pixels are mostly gray/luma/coverage differences, so
   the next pass should target pane/background blend, coverage, memory-color,
   fog/lighting, or post-composite semantics. A fresh presentation run,
   `/tmp/mgb64_glass_pad10092_presentation_alignment_current`, still shows that
   crop search cannot explain the broad mismatch (`tower_pane` best `94.067%`,
   `impact_side` best `90.233%`), so continue toward pane/background output
   semantics or a localized stock/ares pixel oracle instead of shard toggles,
   crop-only fixes, or simple room-alpha/filter toggles.
   `/tmp/mgb64_glass_pad10092_room_glass_output_sweep` is the first full
   refined-output falsification pass. It adds fog, alpha-blend, noperspective
   settex interpolation, targeted settex filter, and small color-scale variants
   for room-glass combiner `0x00738e4f020a2d12`. All captures pass health, but
   none are promotable: `alpha_blend_copy` covers the most `tower_pane` mismatch
   (`31.047%`) while worsening stock parity (`tower_pane=+0.783`,
   `projected_impact=+1.579`), and `premult` worsens `projected_impact` by
   `+3.158`. The repaired RDP memory-blend follow-up,
   `/tmp/mgb64_glass_pad10092_room_glass_rdp_memory_sweep`, closes the missing
   diagnostic class for `G_SETTEX`: both memory and coverage-memory variants
   activate cleanly, move the native room-glass pixels, and still worsen
   `projected_impact` by `+0.789` points with `0.000%` stock-mismatch footprint
   coverage. The refined approach is working, but it is now telling us to stop
   trying broad renderer knobs for pad-10092 room glass and build a truer
   stock/ares pixel-output or translucent-ordering oracle.
   `/tmp/mgb64_glass_pad10092_room_glass_pixel_oracle_current` is that next
   pixel-output ownership split. It consumes the existing clean stock/native
   screenshots plus the latest native TEXGEN trace and masks only
   `class=room`, `effect=glass`, `settex=1`,
   `effcc=0x00738e4f020a2d12`. `projected_impact` is now fully localized to
   that mask: `100.000%` ROI coverage, `100.000%` changed-pixel coverage,
   `94.474%` changed density, and mean luma delta `+4.231`. The broad ROIs are
   not: `tower_pane` room-glass mask covers only `21.186%` of the ROI and
   `20.209%` of changed pixels, while outside-mask pixels have higher changed
   density (`96.921%`) and luma error (`+17.876`) than in-mask pixels
   (`91.322%`, `+9.992`). `impact_side` is similar, with room glass covering
   `39.965%` of the ROI and `38.776%` of changed pixels. Split the backlog
   accordingly: exact room-glass output semantics for `projected_impact`; a
   separate background/post-composite/non-texgen contributor lane for the broad
   pane and side mismatch.
   `/tmp/mgb64_glass_pad10092_room_glass_settex_sample_current_v2` then proves
   the corrected material-state/sample lane. It parses `120`
   `[SETTEX-MATERIAL-CC]` rows from the focused trace, filters to `30` actual
   room-glass `G_SETTEX` rows (`class=room`, `texnum=654`, `wh=54x54`,
   `blend=alpha`, `alpha=1`, `fog=1`, `effcc=0x00738e4f020a2d12`,
   `opts=0x00043C13`, `oml_raw=0xC41049D8`), selects frame `122`, and finds
   four filtered rows covering `100.000%` of `projected_impact`. Their center
   samples use texture alpha `102` from both mip levels. The legacy
   `combL_float` helper still reports alpha `255`, but that helper does not
   evaluate the real two-cycle alpha combiner. The shader-mirrored fields now
   show the actual center output: `shaderL_comb` alpha is `102` for every row,
   and `shaderL_frag` remains alpha `102` after fog while luma moves from
   `0.0/12.75/30.0` to `0.0/11.0/23.0`; center fog alpha counts are
   `{0: 2, 60: 2}`. The refined approach is working and has narrowed the tiny
   impact ROI to exact room-glass texnum `654` blend/coverage/memory
   composition or stock-reference semantics, while broad pane/side work remains
   a separate contributor hunt.
   `/tmp/mgb64_glass_pad10092_room_glass_k4k5_sweep` closes the obvious
   combiner-constant hole for this target. `GE007_DIAG_CONVERT_K4K5=1` is a hard
   negative: it moves `100.000%` of `projected_impact`, `tower_pane`, and
   `impact_side` in the native A/B, but worsens stock parity. `projected_impact`
   rises from `94.474%` changed to `100.000%`, and mean luma error jumps from
   `+4.231` to `+94.803`. Do not promote K4/K5 conversion for room glass.
   `/tmp/mgb64_glass_pad10092_room_glass_skip_tex654` adds the pre-glass
   framebuffer negative control. `GE007_SKIP_TEX=654` is strongly visible in
   native (`projected_impact` moves `64.737%` versus default, `tower_pane`
   `14.908%`, `impact_side` `31.904%`), but it makes stock parity worse:
   `projected_impact` rises from `94.474%` to `95.526%` changed and luma error
   rises from `+4.231` to `+12.389`. The native underlay behind the room-glass
   draw is not the stock target; keep the next pass on exact composition of the
   texnum `654` glass draw.
   `/tmp/mgb64_glass_pad10092_room_glass_scalar_oracle_probe` then tests the
   opacity-scalar hypothesis directly by modeling `underlay + t * (default -
   underlay)`. It is not a robust fix path. Unmasked `projected_impact` improves
   only slightly at `t=1.490` (`mean_abs_rgb 12.689 -> 11.979`, changed remains
   `94.474%`), while the route-masked slice prefers `t=0.750` and improves only
   `9.700 -> 9.500` with changed still `100.000%`. Broad ROI preferences
   disagree too. Treat simple opacity/coverage scaling as a weak diagnostic
   clue, not a promotion candidate.
   `/tmp/mgb64_glass_pad10092_room_glass_required_source_probe` is the next
   read-only sanity check. It inverts fixed-alpha composition with alpha
   `102/255` from the corrected settex trace, asking what source color stock
   would require over the native skip-tex654 underlay. Unmasked
   `projected_impact` requires darker stock source luma (`9.63`) than native
   (`19.58`), not a brighter/stronger pass, but only `67.105%` of those inferred
   stock source pixels are in-gamut and `32.895%` require a negative channel.
   Only `40.526%` land inside the measured `shaderL_frag` luma band (`0..23`,
   tolerance `2`). The route-masked projected slice excludes `300/380` pixels
   due to the lower-actor mask, so keep the unmasked tiny ROI as the primary
   glass evidence. This pushes the next fix lane toward exact
   ordering/coverage/framebuffer semantics or per-pixel source/raster tracing,
   not opacity scaling.
   `/tmp/mgb64_glass_pad10092_room_glass_source_recon_sameframe_fb_final` completes
   that source/raster tracing milestone without renderer changes. The wrapper
   captures a fresh native pad-10092 route with `GE007_DUMP_SETTEX_TEXTURES=654`,
   captures a same-run `GE007_SKIP_TEX=654` underlay, captures same-frame
   pre/post framebuffer PPMs with `GE007_TRACE_SETTEX_FB_CAPTURE`, and
   reconstructs `projected_impact` from the tex654 dump plus the two owning
   `[SETTEX-MATERIAL-CC]` rows. The center self-check is exact
   (`uv` max delta `0.000004`, `t0l/t0p/shaderL/shaderP` max delta `0`) and
   native-aligned coverage is `380/380` pixels. The reconstructed source still
   does not explain either target under a simple fixed-alpha source-over model:
   best synthetic-vs-default is linear with `mean_abs_rgb=5.190`,
   `70.526%` changed; best synthetic-vs-stock is also linear with
   `mean_abs_rgb=12.332`, `92.895%` changed. The actual reconstructed source is
   darker than stock-required by luma mean `-14.919` with `mean_abs_rgb=30.840`;
   keep that stock-required metric as a hint because stock is resized into
   native space. This proves the same-run underlay, tex654 payload, UV
   transform, center shader samples, and same-frame native destination capture
   are no longer the blind spot. Two same-frame captures overlap the tiny ROI on
   frame `123` (`mean_abs_rgb` mean `2.699`, changed mean `19.480%`). The
   capture-to-screenshot join is reported but not thresholded because it crosses
   pre-output-filter PPMs with post-presentation route screenshots; current
   values are first-pre versus skip-underlay `mean_abs_rgb=5.636` and last-post
   versus native-final `mean_abs_rgb=3.982`. The remaining issue is exact
   translucent framebuffer/order/coverage semantics or stock-reference pixel
   output.
   The multihit extension in the same artifact closes the simple
   same-texture-overlap/order hypothesis: all `380` target pixels have exactly
   one owning room-glass row (`tri 788` owns `371`, `tri 789` owns `9`), and
   ordered-all/reverse-all composition is byte-equivalent to the old single-last
   model for this ROI. Best composition remains `linear/single_last`
   (`mean_abs_rgb=5.190` versus default, `12.332` versus stock). The local ares
   stock-material follow-up
   `/tmp/mgb64_glass_pad10092_stock_rdp_tex_probe_full_heap` now proves the
   missing stock side is not stock `G_SETTEX`: at stock frame `2541`, rendered
   rooms `[132,136]` have `settex=0` and `target_settex=0` on all
   primary/secondary room lists. The same checkpoint has complete ordinary RDP
   texture samples with no truncation: room `132` primary `135` samples / `12`
   `G_SETTIMG`, room `132` secondary `67` / `4`, room `136` primary `77` / `7`,
   and room `136` secondary `17` / `1`. The total sampled ops are `24`
   `settimg`, `137` `settile`, `24` `loadblock`, `11` `loadtlut`, and `100`
   `settilesize`. Re-attack the stock oracle by mapping native texnum `654`
   back to these stock `G_SETTIMG` image/tile candidates, not by hunting for
   stock room-glass `G_SETTEX` rows.
   `/tmp/mgb64_glass_pad10092_stock_rdp_tex_dump_probe` then closes the first
   mapping pass. `tools/analyze_stock_rdp_texture_candidates.py` validates that
   all `24/24` stock `G_SETTIMG` payload dumps exist for rooms `132/136` at
   frame `2541`, but finds `0` exact source/source-chain hash matches against
   native texnum `654`. The best naive decoder result is still weak
   (`room=136 primary idx=41 image=0x80137a88`, CI-index placeholder,
   `MAD=25.035`, `corr=0.118`), and the likely IA payload previews do not look
   like the native `54x54` IA8 tex654 artifact. The instrumentation is therefore
   answering the right question by rejecting direct payload identity; next work
   should trace stock draw-state/pixel output for the target pane pixels:
   current tile/TMEM interpretation, owning triangles/coverage, and framebuffer
   state at the draw boundary.
   `/tmp/mgb64_glass_pad10092_stock_draw_state_probe` adds the first draw-state
   association attempt with `MGB64_ARES_TRACE_RDP_DRAW_STATES=1`. It is a useful
   negative: the analyzer still captures `24/24` texture payloads and `0` exact
   native tex654 matches, but reports `draw-associated texture groups: 0/24`.
   Rooms `132/136` have no `G_TRI1`, `G_TRI2`, or raw RDP triangle commands in
   the `primary`/`secondary` texture streams. An explicit point-data scan in
   `/tmp/mgb64_glass_pad10092_stock_point_draw_state_probe` is also negative:
   treating room `point` data as a display list yields invalid texture-looking
	   values (`0x9e9e9eff`, `0x434343ff`). Leave
	   `MGB64_ARES_TRACE_ROOM_POINT_DL=1` opt-in only. The next stock oracle must
	   instrument actual RSP/RDP execution or framebuffer output, not just room-info
	   texture streams.
	   `/tmp/mgb64_glass_pad10092_stock_rdp_command_window_stateful_probe` is the
	   first positive RDP-execution proof. The injected ares `render.cpp` hook writes
	   `MGB64_ARES_TRACE_RDP_COMMANDS=1` sidecars from the actual RDP command stream,
	   and `tools/analyze_stock_rdp_command_stream.py` summarizes the output. The
	   wide frame `2500..2541` run captured `141174` records, `30558` draw ops, and
	   real draw-state use of the room texture candidates that the room-list trace
	   could not associate. The important glass-like candidate `0x80132c80` is drawn
	   under `fmt/siz=3/2`, tile `6`, combine `fc26a004/1f1493ff`, and other-mode
	   `ef992c6f/c81049d8`; `0x801504f8`, `0x8013b990`, and `0x8014a250` also appear
	   in real draw ops.
	   `/tmp/mgb64_glass_pad10092_stock_rdp_command_target_only_probe` then validates
	   the tightened hook after it was changed to maintain RDP state whenever command
	   tracing is enabled, while writing only the requested frame. A target-frame-only
	   capture at stock frame `2541` now reports `2578` command records, `690` draw
	   ops, `46` unique draw states, and zero truncation. Known image matches include
	   `0x80132c80` (`20` draws at `1x2`, `14` draws at `1x1`, plus a `2`-draw
	   alternate env/other state), `0x801504f8` (`18` draws), `0x8013b990`
	   (`16` draws), and `0x8014a250` (`8+2` draws). This proves the refined
	   approach is working: use RDP command-stream state for the next stock-material
	   step, not more primary/secondary/point room-list inference. It is still a
	   draw-state oracle, not the final pixel oracle; the next milestone should join
	   these draw states to the target pane ROI and capture/replicate shaded blended
	   output.
	   `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe` completes that ROI join
	   at bbox precision. With `MGB64_ARES_TRACE_RDP_DRAW_OPS=1`, the stock frame
	   `2541` sidecar records `690` draw ops, `688` valid conservative bboxes, zero
	   truncation, and a logical-screen draw union of `[1,10,319,230]`. The
	   `projected_impact` ROI is covered by `55` stock draw ops across `9` draw
	   states, with `380/380` unique ROI pixels covered. The highest-impact
	   overlapping states include `0x80149b28`, `0x8012f2f0`, `0x8012b150`,
	   `0x80132c80`, `0x8014a250`, `0x80141f60`, `0x801504f8`, and `0x80137a88`.
	   This is the first useful stock draw-stack shortlist for the impact pane, but
	   it is deliberately not a final visibility oracle. The analyzer now preserves
	   ordered ROI hits and an analyzer-side span model too. The span summary,
	   `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack.json`,
	   still covers `380/380` `projected_impact` pixels, but corrects the bbox
	   interpretation: the conservative final owners are `0x8012b150` for
	   `216/380` pixels and late `0x80132c80` for `134/380` pixels. The bbox-only
	   `0x80132c80` sequence `559` hit drops out under span coverage; the late
	   stock glass-like owners are sequences `552` and `553`. The next step must
	   either trace or reconstruct per-pixel shaded/blended output for that split
	   stock stack inside `projected_impact`.
	   The color-attached analyzer run
	   `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_color.json`
	   adds the first stock screenshot target for that stack: final-owner
	   `0x8012b150` pixels average RGB `27.171,27.072,27.174` / luma `27.113`,
	   while late `0x80132c80` pixels average RGB `36.948,36.201,36.146` / luma
	   `36.418`. These values use the route presentation frames, with stock
	   sampled through active bbox `[8,2,625,474]`; use them as the immediate
	   renderer/material parity target for `projected_impact`, and do not tune
	   against the bbox-only `0x80132c80` dominance.
	   The stock-owner masks sampled against the current native screenshot in
	   `/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_stock_native_color.json`
	   show the dominant native error is over-bright output: `0x8012b150` is
	   `56.632` luma versus stock `27.113` (`+29.519`), and late `0x80132c80` is
	   `56.459` versus stock `36.418` (`+20.041`). Treat those as
	   stock-mask/native-screenshot deltas, not proof of native draw ownership.
	   The next proof tool is the stock Parallel-RDP pixel probe in
	   `tools/analyze_stock_rdp_pixel_probe.py`; the route logical center is
	   `183,165`. Do not map this through the `640x480` screenshot or the
	   `source=640x240` presentation log; the probe reads the active RDP color
	   image. The repaired runtime proof
	   `/tmp/mgb64_rdp_pixel_probe_183.rjP13r` passed the route and recorded
	   `6387` successful readbacks, `3059` changed sample rows, and zero readback
	   failures. Late stock samples are on `fb=0x3da800/320/0/2`; the selected
	   authoritative center sample is `frame_context=2669`, `frame_draw_sequence=321`,
	   `texture_image=0x12f2f0`, bbox `[176,154,190,172]`, raw `0x000018c7`,
	   hidden `0x00000003`, decoded RGBA `[24,24,24,224]`. The handoff analyzer
	   compares native output against `stock_pixel.selected_sample`, which
	   defaults to the last changed stock pixel sample rather than an arbitrary
	   final emitted row.
	   The handoff join `/tmp/mgb64_glass_center_handoff_current` also catches a
	   useful caveat: the command-stream span/bbox region model lists five
	   `0x8012f2f0` hits but also three later different-texture hits covering the
	   same center target. Use the command stream as a draw-family shortlist and
	   the pixel probe as final-pixel authority. Native texnum `654` evidence is
	   still pre-blend fragment-source evidence (`shaderL_frag` luma
	   `0.0/11.0/23.0`, alpha `{102: 4}`), so the next fix remains exact
	   translucent composition against stock post-draw output.
	   `/tmp/mgb64_native_settex_pixel_probe_mapped_1782630417` adds the missing
	   native draw-boundary pixel evidence. The stock Parallel-RDP final target is
	   `183,165`, but the native SETTEX probe must use mapped logical target
	   `92,93` for this route (`640x440` aligned capture over native viewport
	   `[0,10,320,220]`). At that mapped target, the probe records `7`
	   target-covering changed texnum-`654` rows across frames `120..123`; the
	   complete selected material frame is `122`, where two covering rows compose
	   to native post `[25,25,25]` while stock final is `[24,24,24]`. This closes
	   the coordinate-space and incomplete-frame false leads. The center pixel is
	   a near-match, so the next renderer pass needs multi-pixel final-output
	   evidence across the ROI before changing room-glass composition.
	   That multi-pixel evidence now has two more points:
	   `/tmp/mgb64_pixel_handoff_176_158_1782631121` samples stock/aligned
	   `176,158` mapped to native `88,89` and reports stock `[32,32,32]` versus
	   native `[22,22,22]`; `/tmp/mgb64_pixel_handoff_188_170_1782631053`
	   samples stock/aligned `188,170` mapped to native `94,95` and reports
	   stock `[32,32,32]` versus native `[11,11,11]`. The source-capable
	   native-only rerun `/tmp/mgb64_native_pixel_source_94_95_rgba_1782631504`
	   confirms the target-source fields are usable: selected frame `122` has
	   `src_valid=1`, populated texel samples, `shaderL_frag=[0,0,0,102]`, and
	   framebuffer movement `[7,7,7] -> [11,11,11]`. Continue from
	   per-pixel source/filter/raster/RDP semantics, not broad alpha or
	   brightness scaling.
	   `/tmp/mgb64_glass_pad10092_room_glass_alpha_scale_probe` then adds targeted
	   `settex_alpha_scale_081` and `settex_alpha_scale_125` A/Bs for combiner
   `0x00738e4f020a2d12`, texsize `54x54`, texnum `654`. Both variants move
   room-glass pixels and pass route health, but target alpha scaling is a
   checked negative: `0.81` worsens `projected_impact` stock parity by `+2.368`
   percentage points, `1.25` improves it by only `-0.526` points, and the best
   tower-pane stock-mismatch coverage is just `0.043%`.
   `/tmp/mgb64_glass_pad10092_room_glass_order_probe` re-ran the two existing
   RDP memory-blend variants after that proof. Both variants move room-glass
   pixels but remain negative: `projected_impact` stock parity worsens by
   `+0.789` percentage points with luma delta `+4.223`, and tower-pane
   stock-mismatch footprint coverage is `0.000%`. Do not promote those toggles
   as-is.
   The 2026-06-27 refined-material pass also fixed an instrumentation blind spot:
   overlapping effect labels now prefer the narrowest range, so broad `glass`
   ranges no longer hide `bullet_impact_prop_textured`. The filtered capture
   `/tmp/mgb64_prop_crack_material_probe_after_specificity` now logs the
   prop-attached crack as two textured alpha room triangles per frame with
   `raw=0x0C1849D8`, `eff=0xCC1849D8`, dual IA textures `48x48` and `24x24`,
   and fog enabled. A follow-up native A/B,
   `/tmp/mgb64_glass_impact_flat_prop_after_label_specificity`, proves
   `GE007_FLAT_PROP_BULLET_IMPACTS=1` changes `0.000%` of the screenshot
   checkpoint. Treat prop crack state as break-lifecycle evidence, not as the
   current checkpoint's visible owner.
   `/tmp/mgb64_glass_impact_prop_creation_skip_crosshair_parity` adds the
   stronger prop-creation skip diagnostic. `GE007_DISABLE_PROP_BULLET_IMPACTS=1`
   drops native impact occupancy from `2` to `1`, but it worsens stock parity
   (`stock_delta=+0.188`) and leaves `stock_glass_delta=+0.000`; do not promote
   it as a fix.
   The follow-up impact presentation sweep
   `/tmp/mgb64_impact_presentation_sweep_all` adds stock-vs-variant deltas to
   the ownership harness. Broad alpha modes are strong negative controls
   (`stock_delta=+5.693..+6.547`, `stock_glass_delta=+8.632..+13.271`),
   world-impact alpha-from-intensity owns pixels but worsens stock
   (`stock_delta=+0.070`), and the RDP memory/coverage approximations are
   glass-burst no-ops. `GE007_DIAG_ZMODE_DEC_NO_POLY_OFFSET=1` is the only
   positive stock-direction clue, but the effect is tiny
   (`stock_glass_delta=-0.014`), so do not promote it without a more localized
   DEC/decal-order explanation.
   The route-cadence follow-up proves the stricter guards are useful, not
   busywork. `/tmp/mgb64_glass_impact_relaxed_first_global_probe` allowed the
   stock `1147` branch and immediately failed state guards: no active shards and
   no destroyed pane. `/tmp/mgb64_glass_impact_global_fire_probe` used a global
   fire window and did shatter, but failed fidelity with impact center delta
   `9.079` and shifted shard age. Keep the checked-in route strict at
   `first_gameplay_global=1146`; the accepted wrapper hardening is
   `MGB64_ARES_VIDEO_BLOCKING=true` plus `8` stock route-control retries.
   `/tmp/mgb64_glass_impact_visual_after_cadence_harden` and
   `/tmp/mgb64_dam_visual_suite_after_cadence_harden` are the cadence-hardening
   proofs. `/tmp/mgb64_glass_impact_checkpoint_search_focused` is the current
   focused proof and adds report-only full impact-quad metrics plus native shot
   and creation provenance: stock crosshair `159:114`, native stock-equivalent
   crosshair `158:114`, shot frame `69`, bullet-impact create frame `69`, traced
   world ray `[0.010734,0.021102,0.999720]`, selected world-impact
   `max_point_delta=0.000`, impact center delta `0.000`, exact stored-vertex
   parity, and projected decal center delta `0.055px`. Its checkpoint search
   scanned `744` active impact pairs and found `0` strict actor-clean matches,
   so this trace should stay a decal-footprint/order guard while pixel parity
   moves to a cleaner route, view, or mask. Do not re-add the old route
   `GE007_AUTO_AIM_DIR_SCRIPT`: frame `70` was a no-op for the frame-`69` shot,
   and `/tmp/mgb64_glass_impact_visual_aimdir69` proved forcing `+Z` on frame
   `69` moves the impact to room `109` and breaks shard phase. The umbrella
   rerun `/tmp/mgb64_dam_visual_suite_after_actor_probe` passes with the edited
   wrapper.
   The latest actor-composition guard stops the current screenshot from being
   treated as a direct glass pixel oracle: stock has a guard body/head occluding
   the burst area while native does not, and the summary records
   `visual_oracle.status=dirty` with native-only visible chr `45` plus chr `10`
   and chr `12` state/position mismatches. The route/impact/shard
   instrumentation is working, and the localized impact oracle confirms why
   it still must not drive renderer changes: `status=masked_dirty`, focus
   exclusion `59.099%`, focus masked changed `82.972%`, and unoccluded-left
   changed `91.170%`. The projected world-impact/decal oracle now covers
   geometry (`0.055px <= 1.0px`), so the next re-attack needs a cleaner
   actor-free/masked impact checkpoint before any production pixel-parity
   renderer change.
6. **Random orientation/scatter.** Check the per-piece rotation
   (`piece->field_0x10/0x14/0x18`, read by `matrix_4x4_set_position_and_rotation_around_xyz`)
   and the `update_broken_windows` dispersion against the N64 original. Spawn now
   clearly initializes the base rotations, so the remaining question is random-state
   parity and frame-by-frame integration, not uninitialized fields.

## Debug instrumentation (gated, strip once the visual lands)

- `GE007_TRACE_GLASS=1` → `[GLASS-SHATTER]` (centre, piece count, bondPos) and per-frame
  `[GLASS-SHARD]` (active count, `visScale`, `roomScale`, sample piece pos/rotation/vertex
  extents).
- `GE007_GLASS_SHARD_FIXED_MTX=1` → A/B the old fixed-point matrix emission path.
- `GE007_FIELD_10E0_SCALED=0` → A/B the stock-style unscaled `field_10E0` negative
  control. Default native keeps visibility-scaled `field_10E0`; Surface/Dam
  large-room rendering depends on it.
- `GE007_GLASS_SHARD_COMPRESS=1` → A/B full-matrix room-scale compression.
- `GE007_GLASS_SHARD_BASIS_SCALE=1` → A/B the older undersized native shard basis;
  default uses inverse visibility full-model scaling.
- `GE007_GLASS_SHARD_SQRT_BASIS=1` or `GE007_GLASS_SHARD_INV_VIS_SCALE=0` → A/B
  the previous bounded-but-misprojected `sqrt(get_room_data_float1())` basis
  compensation path.
- `GE007_GLASS_SHARD_NO_BASIS_SCALE=1` → A/B the too-large unscaled shard basis
  regression while keeping scaled `field_10E0`.
- State traces include top-level `glass_projection`, a compact per-frame
  stock/native projection summary for active shard count, projected/onscreen
  count, behind-camera count, viewport, union bbox, max screen area, and samples.
  Compare summary coverage with `tools/compare_glass_projection_trace.py`.
  Set `GE007_TRACE_GLASS_PROJECTION_ALL=1` and
  `MGB64_ARES_TRACE_GLASS_PROJECTION_ALL=1` to emit all active projection samples
  instead of the compact first-16 sample, then compare per-piece placement with
  `tools/compare_glass_projection_pieces.py` and per-piece projected pixels with
  `tools/compare_glass_shard_pixel_oracle.py`.
- `GE007_TRACE_SHARDS=1` → renderer-side large-triangle/pathological clipping trace,
  including labelled `effect=glass_shards` ranges.
- `GE007_EFFECT_TRI_TRACE=1 GE007_EFFECT_TRI_TRACE_LABEL=glass_shards` → per-triangle
  draw-state trace for active shards. The trace logs emit/reject status, render mode,
  other-mode high, combiner id, geometry flags, NDC bbox/area, UVs, raw VTX RGBA,
  post-light shade RGBA, and matrix/projection context.
- `GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect`, `GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1`,
  and `GE007_EFFECT_TRI_TRACE_UNLABELED=1` are budget/focus add-ons for noisy
  glass sessions. They were added after the Dam regular-glass visual route
  proved the old trace budget could be consumed by unrelated room rejects and
  screen-fill quads before the active shard pass.
- Native now rejects CPU-clipped `effect=glass_shards` triangles that expand to
  viewport-sized post-clip coverage. The current guard also catches near-viewport
  clipped shard triangles such as `bbox=[-1,-1]-[1,0.744]`, which appeared after
  restoring visibility-scaled `field_10E0`. `/tmp/mgb64_glass_material_final2`
  passes with bounded shard output and maximum shard material triangle
  `area2=0.29559`. This fixes the "glass across the whole screen" symptom
  without changing normal bounded shard output.
- Structured glass traces now include explicit `glass.first.timer`, `rot_y`, and
  `rot_z` fields. The legacy `age` key is `rot_y`, not lifetime; use `timer`
  when aligning stock/native active-shard screenshots.
- `GE007_TRACE_TEXGEN_MATERIALS=1` → per-texgen material trace. Rows now include
  `effect=glass_shards` when the current command is inside the registered shard
  display-list range, which keeps room glass and active shards separable. They
  also include `mode_decode={...}` so the shard render mode's N64 coverage and
  blender-cycle fields can be reviewed without decoding the raw hex by hand.
- `GE007_TRACE_TEXGEN_MATERIALS_EFFECT=glass_shards` → optional material-trace
  effect filter. Use it whenever the question is active shards; otherwise room
  and pane glass can consume the trace budget before the falling-shard display
  list. `/tmp/mgb64_m12_shard_texgen_effect_filter` is the focused proof: `172`
  of `172` `TEXGEN-MATERIAL` rows are `effect=glass_shards`, with no pane rows,
  all using combiner `0x00f38e4f020a2d12`, raw render mode `0x0C1849D8`,
  other-mode high `0x00992C60`, geometry `0x00060205`, base IA8 texture key
  `0x800000000000028e`, and tile-1 IA8 key `0x8000000000000e5e`. Decoding that
  combiner gives cycle 0 `TEXEL1,TEXEL0,LOD_FRACTION,TEXEL0` for color and alpha,
  then cycle 1 shade modulation after the translator's normal zero-constant
  handling; it is not the nearby "tile1 only for alpha" oddball LUT entry.
- `GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=0x00f38e4f020a2d12` → opt-in A/B for
  shard loaded-tile LOD filtering. It moves shard material rows from
  `opts=0x00000511` to `opts=0x00003511` and forces `sampler_linear=(0,0)`,
  but the call-seeded Dam active-shard screenshot changes only 77/307200
  pixels with no glass-burst ROI improvement. Keep it as a negative control, not
  a default fix.
- `GE007_DIAG_CONVERT_K4K5=1` remains a strong negative for the active-shard
  material path. `/tmp/mgb64_m12_active_visual_k4k5` reaches the projected visual
  gate but blows out the image (`changed=99.945%`, bright pixels `410 -> 4927`,
  native mean luma around `117`), so global K4/K5 conversion is not the glass
  answer.
- `GE007_DIAG_SWAP_IA8_NIBBLES=1|*|key-list` → default-off loaded-tile IA8
  decode A/B. Scoped to the two active-shard texture keys
  `0x800000000000028e,0x8000000000000e5e`, it is the strongest current material
  lead: `/tmp/mgb64_m12_active_visual_ia8_swap` improves the projected visual
  metric slightly (`92.879% -> 92.457%`) and changes native from dark sheets to
  bright glints (`bright=6 -> 930`), but it over-brightens/clusters and still
  fails the older fixed visual ROI gate (`masked=95.550%`,
  `glass_burst=99.806%`). Do not promote it as-is; use it to guide a stock-owned
  texture/material semantics check for shard texture `654`.
- `GE007_DIAG_IA8_CHANNEL_MODE=mode[:key-list]` → finer IA8 channel A/B for
  loaded tiles. `rgb_from_alpha` uses the low nibble for RGB but keeps the
  original low-nibble alpha; `alpha_from_intensity` keeps high-nibble RGB but uses
  the high nibble for alpha; `swap` is equivalent to the older full nibble swap.
  Scoped to the same two shard keys, `/tmp/mgb64_m13_active_visual_ia8_rgb_from_alpha`
  is a negative (`projected changed=94.781%`, masked `96.185%`), while
  `/tmp/mgb64_m13_active_visual_ia8_alpha_from_intensity` is the best channel
  lead so far (`projected changed=92.412%`, bright `410 -> 923`, masked
  `95.457%`). The texture dump
  `/tmp/mgb64_m13_shard_ia8_alpha_from_intensity_texdump` records the transformed
  base tile as `stage=alpha_from_intensity`, alpha max `119`, and nonzero alpha
  `1956 / 3024`. This argues for an alpha/coverage semantics fix around active
  shard IA8, not a broad IA8 asset decode swap.
- `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=1|*|cc-list` -> shader-side version of
  the same lead, scoped by effective combiner id and restricted to loaded-tile
  alpha materials. With `0x00f38e4f020a2d12`,
  `/tmp/mgb64_m14_active_visual_alpha_from_tex_intensity_cc` reproduces the best
  channel result without mutating texture uploads: projection/state pass,
  projected visual `changed=92.412%`, bright `410 -> 923`, mean
  `[44.52,46.57,41.62]`. It still fails the older fixed ROI sanity gate
  (`masked=95.457%`, `glass_burst=99.722%`), so this is a confirmed scoped
  diagnostic lead, not a promotable glass fix. The env-enabled material gate
  `/tmp/mgb64_m14_glass_material_alpha_from_tex_intensity_cc` passes and shows
  shard rows at `opts=0x00080511` with the guarded material tuple unchanged.
- `GE007_DIAG_XLU_COVERAGE_A2C=0xC41049D8,0x0C1849D8` plus `Video.MSAA=4` →
  opt-in A/B for Dam room-glass (`0xC41049D8`) and active-shard (`0x0C1849D8`)
  raw `ZMODE_XLU` coverage modes. It maps only matching `CLR_ON_CVG`/`FORCE_BL`
  alpha draws to an OpenGL alpha-to-coverage backend mode; use
  `GE007_TRACE_TEXGEN_MATERIALS=1` and the `api_blend=alpha_coverage` field to
  prove the diagnostic reached the draw before interpreting pixels. The
  `/tmp/mgb64_xlu_coverage_a2c_1782436000` A/B reached all 48 traced
  `effect=glass_shards` material rows and 200 traced XLU rows, but changed only
  0.9% of the `glass_burst` ROI and did not move the `damage_arc` ROI; keep this
  as a coverage negative control, not a default fix.
- Combined alpha-intensity plus XLU A2C was verified with a real native
  `Video.MSAA=4` override in
  `/tmp/mgb64_m15_active_visual_alpha_intensity_a2c_msaa4_verified`.
  `tools/glass_active_visual_isolation_regression.sh --native-config-override
  Video.MSAA=4` now appends after route config and audits `ge007.ini`, fixing
  the earlier false start where `GE007_MSAA=4` lost to the route's `MSAA=0`.
  The combined A/B reaches `api_blend=alpha_coverage` and still passes
  state/projection (`active=90 -> 90`, onscreen `82 -> 82`, behind `0 -> 0`),
  but it is not promotable: projected visual only moves to `changed=92.324%`
  while native bright pixels worsen `410 -> 943`, mean rises to
  `[44.92,47.09,41.91]`, and the old ROI sanity gate is still worse
  (`masked=95.484%`, `glass_burst=99.847%`).
- `GE007_TRACE_GLASS_SHARD_COVERAGE=1` is the first re-attack tool for the
  coverage/blender question. It emits read-only `[SHARD-COVERAGE]` frame rows
  for active `glass_shards`, using a coarse `64x48` bbox grid rather than exact
  RDP subpixel coverage. `/tmp/mgb64_m16_glass_material_coverage_trace` passes
  the material gate with `25` coverage rows, stable material identity
  (`raw=0C1849D8`, `omh=00992C60`, `cc=00F38E4F020A2D12`, `geom=00060205`),
  `z=xlu`, `cvg=wrap`, `aa/imrd/clr_on_cvg/force_bl=1`,
  `cvg_x_alpha/alpha_cvg=0`, `api_blend=alpha`, and zero material/blend
  mismatches. The same rows show strong overlapping bbox pressure
  (`max_cell=10`, `max_overlap_cells=1231`, `max_avg_hits=2.98`), which makes
  exact N64 coverage/color-on-coverage/overdraw behavior the next target instead
  of more projection, mip, or texture-channel edits.
- `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=0x00f38e4f020a2d12` plus optional
  `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_RATE=N` is the M17 coverage-wrap
  approximation. It is default-off and only applies to matched loaded-tile alpha
  draws whose raw mode has `ZMODE_XLU`, `CVG_DST_WRAP`, `CLR_ON_CVG`, `IM_RD`,
  and `FORCE_BL`. `/tmp/mgb64_m17_glass_material_wrap_thin` passes the material
  gate and proves stable `opts=0x00180511`, unchanged `raw=0C1849D8`,
  `api_blend=alpha`, `25` coverage rows, and zero material/blend mismatches.
  The visual A/B is a checked negative: default rate `0.25`
  (`/tmp/mgb64_m17_active_visual_wrap_thin`) remains over-bright
  (`projected changed=92.332%`, bright `410 -> 943`, masked `95.488%`), and
  aggressive rate `0.05`
  (`/tmp/mgb64_m17_active_visual_wrap_thin_rate005`) still moves the wrong way
  (`projected changed=92.359%`, bright `410 -> 945`, masked `95.489%`). This
  keeps the ares coverage/color-on-coverage finding alive, but rejects
  deterministic screen-space thinning as the fix.
- `GE007_DIAG_XLU_COVERAGE_STENCIL_CC=0x00f38e4f020a2d12` plus
  `GE007_DIAG_XLU_COVERAGE_STENCIL_INCREMENT=1..8` is the M18 framebuffer
  coverage-memory approximation. It forces a stencil-backed scene target, treats
  the lower stencil bits as an approximate per-pixel coverage count, and only
  emits color when the synthetic count would wrap. The material proof
  `/tmp/mgb64_m18_glass_material_stencil_inc4` passes with stable
  `opts=0x00080511`, unchanged `raw=0C1849D8`, `api_blend=alpha_cvg_wrap_stencil`,
  `25` coverage rows, and zero material/blend mismatches. The full active visual
  wrapper was blocked by two all-attempt stock cadence misses (`global 1147`
  versus expected `1146`), so the sweep used the valid M17 stock capture against
  native-only M18 captures. Projection still matches (`90/90/82/0`), but pixels
  do not improve: increment `1` gives `projected changed=92.390%`, bright
  `410 -> 945`; increment `4` gives `92.343%`, bright `410 -> 939`; increment
  `8` returns to the M14 alpha-intensity baseline (`92.411%`, bright
  `410 -> 923`). This rejects the GL stencil approximation as a fix and points
  the next pass at a closer ares-style RDP blender/memory-color implementation.
- `GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=0x00f38e4f020a2d12` is the M19
  memory-color blender diagnostic. It is default-off and only applies to the
  matched active-shard loaded-tile alpha draw; the backend snapshots the current
  framebuffer per triangle, disables fixed GL alpha blending, and the shader
  applies the decoded final-cycle RDP blend formula against sampled memory color.
  `/tmp/mgb64_m19_glass_material_rdp_memory` passes the material gate with stable
  `opts=0x00280511`, unchanged `raw=0C1849D8`,
  `api_blend=alpha_rdp_memory`, `25` coverage rows, and zero material/blend
  mismatches. Native visual proof `/tmp/mgb64_m19_native_rdp_memory` plus manual
  compare `/tmp/mgb64_m19_manual_rdp_memory_compare` keeps projection matched
  (`90/90/82/0`), but pixels are still wrong: `projected changed=92.317%`,
  bright `410 -> 923`, near-white `190 -> 656`, warm `10 -> 0`. This rejects
  memory-color-only blending as a fix; the next pass needs exact
  coverage/color-on-coverage behavior combined with memory color, or a closer
  port of the ares/Parallel-RDP coverage/blender path.
- `GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=0x00f38e4f020a2d12` is the M20
  coverage-plus-memory diagnostic. It adds per-triangle screen/NDC attributes,
  estimates an 8-sample RDP-style coverage count in the shader, stores synthetic
  coverage in framebuffer alpha, and applies the ares `CLR_ON_CVG` rule that
  returns memory color when coverage does not wrap. The material proof
  `/tmp/mgb64_m20_glass_material_rdp_cvg_memory` passes with stable
  `opts=0x00480511`, unchanged `raw=0C1849D8`,
  `api_blend=alpha_rdp_cvg_memory`, `25` coverage rows, and zero
  material/blend mismatches. The default recheck
  `/tmp/mgb64_m20_default_material_recheck` still passes with `api_blend=alpha`.
  Native visual proof `/tmp/mgb64_m20_native_rdp_cvg_memory` plus manual compare
  `/tmp/mgb64_m20_manual_rdp_cvg_memory_compare` keeps projection matched
  (`90/90/82/0`), but pixels are unchanged from M19: `projected changed=92.317%`,
  bright `410 -> 923`, near-white `190 -> 656`, warm `10 -> 0`. This rejects
  framebuffer-alpha coverage memory as a useful approximation; the next pass
  should compare ares/Parallel-RDP shaded/blended shard pixel outputs directly
  against native shader inputs, or route this effect through a true RDP path.
- `tools/score_glass_projected_pixels.py` is the M21 projected-pixel classifier
  for the matched active-shard ROI. The default renderer proof
  `/tmp/mgb64_m21_default_pixel_score_compare/projected_pixel_score.json` shows
  a gray/dim failure: `luma_delta_mean=5.899`, `sat_delta_mean=-0.151`,
  `abs_rgb_delta_mean=62.577`, and stock `bright=220`, `near_white=190`,
  `gray=112536` shift to native `bright=6`, `near_white=0`, `gray=136708`.
  The M19 and M20 diagnostic scores
  `/tmp/mgb64_m19_manual_rdp_memory_compare/projected_pixel_score.json` and
  `/tmp/mgb64_m20_manual_rdp_cvg_memory_compare/projected_pixel_score.json`
  instead land in a brighter/desaturated cluster, both around
  `luma_delta_mean=11.90`, `sat_delta_mean=-0.144`,
  `abs_rgb_delta_mean=70.3`, `bright=267`, `near_white=656`, `gray=124458`.
  This splits the remaining problem into wrong blended shard output rather than
  one missing GL coverage switch: default loses the stock highlights, while the
	  RDP-memory approximations create too many pale gray/white pixels.
- `tools/score_glass_projection_win.py` compares two projection-compare JSON
  artifacts and reports whether a candidate improved the projection error score.
  The historical containment proof showed `no_basis -> sqrt_basis` improved the
  projection error score by `82.40%`; the current `inv_vis_full` default is
  stricter and passes the active route's stock/native projection gate.
- `GE007_DIAG_ALPHA_BLEND=premult|add|inv_alpha|copy` and
  `GE007_DIAG_ZMODE_XLU_LESS=1` were rechecked on the call-seeded active-shard
  route in `/tmp/mgb64_alpha_depth_ab_1782454369`. Alternate alpha factors
  worsen the stock `glass_burst` ROI, and XLU `GL_LESS` is pixel-identical to
  default. Keep them as negative/no-op controls.
- `GE007_DIAG_NOPERSPECTIVE_CC_INPUTS=0x00f38e4f020a2d12` and
  `GE007_DIAG_NOPERSPECTIVE_CC=0x00f38e4f020a2d12` are scoped material
  interpolation A/Bs added after a broad global noperspective-input probe looked
  tempting. `/tmp/mgb64_scoped_nopersp_ab_1782455251` proves the scoped input
  flag reaches all 76 traced shard rows (`opts=0x00008511`) but leaves the
  `glass_burst` ROI unchanged. Do not promote global noperspective interpolation
  as an active-shard fix; it is moving other frame material.
- `tools/glass_contributor_isolation_regression.sh` → stock-backed first-active
  baseline plus native-only ownership variants (`GE007_GLASS_SHARDS=0`,
  `GE007_DISABLE_BULLET_IMPACTS=1`, `GE007_SKIP_FP_WEAPON_RENDER=1`,
  `GE007_NO_FOG=1`, `GE007_FLAT_BULLET_IMPACTS=1`). Use it before interpreting
  a shard-mask, impact, fog, or HUD/viewmodel pixel delta as renderer ownership.
- Route JSONL `glass.sample` → first four active shard pieces with position,
  three-axis rotation, velocity, angular velocity, and local triangle vertices.
  Use this before interpreting active-shard screenshot deltas as renderer bugs.
- Route JSONL top-level `rng` → current global seed, plus native call count.
  `tools/compare_glass_trace.py` reports pre-active and first-active seed
  transitions when both traces include the field.
- `tools/compare_glass_trace.py --require-sample-match` or
  `--first-sample-tolerance N` → gate the first active `glass.sample` records.
- `tools/compare_glass_trace.py --require-actor-match CHRNUM` plus route
  `compare_actor_chrnums` → fail a stock/native visual route when sampled actor
  state differs at the selected active glass frame. Use this before treating
  guard/HUD-contaminated screenshot deltas as renderer defects.
- `tools/score_actor_composition_checkpoints.py` → rank whole stock/native
  active-glass checkpoint pairs by visible actor set, required chr samples,
  actor fields/positions, health/HUD phase, active count, and shard timer.
- `tools/score_impact_checkpoint_candidates.py` → rank impact-aligned
  stock/native checkpoint pairs by selected world-impact identity, projected
  decal center, visible actor set, actor fields/positions, health/HUD phase,
  active count, and shard timer. The current pad-`10004` focused proof searched
  `744` active pairs and found no strict clean candidate, so the next impact
  pixel-parity pass needs a cleaner route/view/mask, not just a different frame
  from the same trace.
- `tools/glass_pad10092_impact_visual_regression.sh` →
  stock-backed pad-`10092` impact/decal route seed with strict selected-impact
  geometry (`4.785 <= 5` world units, `0.949px <= 1.0px` projected center) and
  report-only actor/pixel output. Use it to iterate route masks and localized
  decal presentation without reusing the active-shard screenshot checkpoint as a
  pixel oracle.
- `tools/glass_actor_clean_candidate_scout.sh` → generate temporary pad-`10001`
  or retargeted native route variants, run native-only captures, and score them
  against a known stock active trace. This is a scouting helper, not a regression
  gate.
- `tools/score_native_glass_fixture.py` and
  `tools/glass_native_fixture_scout.sh` → score single native/stock traces for
  target-pane destruction and first-active actor visibility, and generate broad
  native-only pane/yaw/distance candidate matrices before spending stock-oracle
  time.
- `tools/rom_oracle_routes/dam_regular_glass_shatter_rng_isolation_probe.json`
  → same-pose Dam shatter route with native call-level RNG compensation and
  exact first active `glass.sample` parity; use it for renderer isolation, not
  unseeded gameplay RNG parity.
- `tools/rom_oracle_routes/dam_regular_glass_shatter_rng_visual_probe.json` and
  `tools/glass_active_visual_isolation_regression.sh` → screenshot route and
  proof gate for exact first-active shard-state under visual capture. Use this
  before treating active-shard screenshot deltas as renderer defects, but do not
  use it as final pixel parity while actor composition is still divergent.
- `GE007_AUTO_RNG_SEED_SCRIPT=FRAME:SEED` and
  `GE007_AUTO_RNG_CALL_SEED_SCRIPT=CALL:SEED` → native route-only exact seed
  controls for frame-level and call-level isolation. Stock ares uses
  `MGB64_ARES_RNG_SEED_SCRIPT=GAMEPLAY_FRAME:SEED`.
- `GE007_TRACE_RNG_CALLS=1` + `GE007_TRACE_RNG_CALLS_FORCE=1` +
  `GE007_TRACE_RNG_CALLS_AFTER=N` → native caller trace around a non-ramrom
  auto route's RNG window.
- `GE007_DUMP_LOADED_TEXTURES='*' GE007_DUMP_LOADED_TEXTURES_AFTER_FRAME=108`
  with `GE007_DUMP_LOADED_TEXTURE_DIR=/tmp/...` → dump decoded TMEM imports. For
  the shard overlay, expect a low-intensity IA8 mask (`56x54`, alpha max `102`)
  plus live lower-mip aliases after the TMEM fix.
