# Renderer and Simulation Deep Dive - 2026-07-07

Scope: Metal backend, Fast3D command translation, native simulation/render interaction, Bond level-intro rendering, and 2D HUD texture allocation/loading.

Position: this is a native port. The goal is excellent player-facing gameplay and rendering, not bug-for-bug preservation when the original behavior produces broken visuals, missing UI, unstable geometry, or hard-to-debug native corruption.

## Executive priorities

1. Treat the Bond level-intro body as a release-blocking visual system, not a compatibility experiment. The red shard failure has a known aliasing root cause that is mostly fixed, but the current code still has routes back into the broken path.
2. Fix intro pose/grounding by making one system authoritative for the viewer body transform before `chrTickBeams` builds matrices. Current-player intro state is corrected after render prep, which is too late for robust visual output.
3. Port the minimap overlay to the renderer abstraction. It is explicitly skipped on Metal today.
4. Harden dynamic allocators so buffer exhaustion skips rendering instead of aliasing live geometry and matrices. Aliased matrix/vertex memory is exactly the sort of failure that becomes shards.
5. Harden HUD texture/image loading with bounds checks, dimension checks, and visible fallback assets. The ammo HUD path is mostly sane now, but it still trusts compiled image metadata too much.
6. Close Fast3D interpreter parity gaps that can become graphical bugs: N64 binary display-list recursion limits, RDP framebuffer-read approximation, and false-advertised primitive depth support.

## What looks healthy

- The Metal backend has several good guardrails: command-buffer throttling via semaphore, command-buffer failure cleanup, target allocation cleanup, depth clamp enablement, scissor/viewport Y-flip handling, post-process target isolation, and explicit mid-frame readback handling. See `src/platform/fast3d/gfx_metal.mm:960`, `src/platform/fast3d/gfx_metal.mm:1189`, `src/platform/fast3d/gfx_metal.mm:1587`, `src/platform/fast3d/gfx_metal.mm:2653`, and `src/platform/fast3d/gfx_metal.mm:3148`.
- The native Bond body aliasing bug has been identified in unusually clear comments, and the default path allocates a dedicated body header and body buffer instead of reusing the right-hand weapon slot. See `src/game/bondview.c:2871`, `src/game/bondview.c:2900`, `src/game/bondview.c:2919`, and `src/game/bondview.c:3012`.
- The main texture arena is now growable and preserves old chunks so low-32 texture pointers can still be resolved after growth. See `src/game/initmttex.c:28`, `src/game/image.c:178`, and `src/game/image.c:218`.
- The ammo HUD no longer depends on invalid N64 raw GBI emission. It maps ammo image globals to real compiled image table entries, then uses `texSelect` plus `display_image_at_position`. See `src/game/image_bank.c:270`, `src/game/image_bank.c:290`, `src/game/gun.c:31133`, and `src/game/gun.c:31800`.

## Findings

### 1. High - Bond intro red shards have a known aliasing root cause, and broken routes still exist

Evidence:

- `src/game/bondview.c:2871` explains the core bug: the original native path assembled Bond's body into the `GUNRIGHT` weapon-header slot, then the weapon loader overwrote it. That dropped required render positions from about 21 to 6 and left joints 6 through 20 reading uninitialized matrices.
- `src/game/bondview.c:2919` explains the matching data-buffer bug: the body/head converted `RootNode`, vertices, and display lists lived in the right-hand weapon data buffer, and the first-person weapon loader overwrote them, causing the body mesh to fan into degenerate spikes.
- `src/game/bondview.c:2889` makes the fix default-on, but `GE007_NO_BOND_BODY_FIX` can restore the broken aliasing path.
- `src/game/bondview.c:2900` and `src/game/bondview.c:2930` fall back to the aliased weapon header/buffer if allocation fails. That means low-memory behavior returns to the exact visual corruption the fix was meant to prevent.
- `docs/CINEMATICS.md:55` and `docs/CINEMATICS.md:56` document environment hatches for the intro character timing fix and the body fix, including the pre-fix invisible/spiky Bond behavior.

Impact:

This is the strongest code-level match for the reported "odd red shards around the character." Red shards are not likely to be a Metal blending problem first; they are consistent with corrupted joint matrices or corrupted body display-list data sending triangles across the screen with valid red-tinted material state.

Recommended fix:

- Remove the allocation-failure fallback to the aliased `GUNRIGHT` header/buffer in normal builds. If the dedicated body header or body buffer cannot be allocated, skip the Bond body render for that intro and log a hard render-health error.
- Keep `GE007_NO_BOND_BODY_FIX` only as a developer regression knob, not as a route used by automated player-quality runs.
- Add a runtime assertion in native builds that the Bond body header, right-hand weapon header, body buffer, and weapon buffer are not the same allocation during intro setup.
- Add a focused regression route that fails if the intro body render position count is below the body model's required joint count.

### 2. High - Bond intro floating/pose likely comes from transform-authority mismatch

Evidence:

- The native intro character is created in `bondviewPrepareNativeBondIntroChr`, which loads the solo character body, applies the selected intro animation, sets `ACT_BONDINTRO`, clears sleep, and clears the room pointer. See `src/game/bondview.c:5688`, `src/game/bondview.c:5716`, `src/game/bondview.c:5755`, and `src/game/bondview.c:5768`.
- Swirl camera mode intentionally defers character loading by one per-tick pass unless `GE007_NO_INTRO_CHR_TIMING_FIX` is set. See `src/game/bondview.c:5944`.
- Frozen intro camera rendering changes the camera-relative render origin to the live camera position, not the gameplay model position. See `src/game/bondview.c:15629` and `src/game/bondview.c:15664`.
- The current-player beam path calls `chrTickBeams` first, then, for frozen intro/first-person camera modes, snaps `player->prop->pos` back to `field_488.collision_position` and returns. See `src/game/bondview.c:24140`, `src/game/bondview.c:24144`, and `src/game/bondview.c:24158`.
- The comment at `src/game/bondview.c:24144` says this correction exists because the model prop can otherwise lag behind the collision/player position or inherit intro-animation transforms.

Impact:

For the player character in an intro, `chrTickBeams` is the code that advances animation, resolves visibility, allocates render positions, and builds render matrices. If the current-player prop/model state is corrected only after that work, the visible body can be built from stale, animation-shifted, or camera-relative state while the traced collision/player state looks corrected. That explains a body that is animated but appears floating or disconnected from the intended pad.

Recommended fix:

- For frozen intro modes, align the current-player prop/model root to the selected intro pad/collision position before `chrTickBeams`, not after it.
- Make the viewer-body intro transform explicit: source pad, room, animation root offset, model suboffset, and camera-relative render origin should be logged as one record before matrix generation.
- Add a visual/trace assertion for the intro body root: the body pelvis/root render position should stay within a small tolerance of the selected intro pad plus the known animation root offset.
- Do not rely on the post-`chrTickBeams` snap at `src/game/bondview.c:24158` as the visual correction. It should be only a final consistency guard.

### 3. High - Intro validation can pass while the visual body is still bad

Evidence:

- `tools/audit_intro_trace.py:30` tracks fields such as active, present, onscreen, model matrix, rendered, animation, frame, and body parts.
- `tools/audit_intro_trace.py:225` counts `bond_rendered`, animation presence, and model matrix presence for the intro.
- `docs/ROM_COMPARISON.md:660` describes the desired Dam intro route: Bond should appear, render, animate, and carry the silenced PP7.
- `docs/ROM_COMPARISON.md:669` compares camera vectors, setup fingerprint, selected camera, actor state, animation identity/hash, and animation frames.

Impact:

These checks are necessary but not sufficient. A shredded body can still have `bond_rendered=1`, a valid animation hash, and advancing frames. A floating body can still be active, onscreen, and animated. Current diagnostics are actor-state checks more than image-quality checks.

Recommended fix:

- Add a screenshot/pixel validator for the Dam intro route. It should inspect the Bond bounding box, non-background silhouette coverage, and red/warm shard outliers around the body.
- Add a ground-contact metric to the trace: root/pelvis/foot Y relative to the selected intro pad floor. Fail on large persistent offset.
- Add an intro-specific "shard score": count triangles or pixels far outside the expected body bounding box after projected joint bounds are known.

### 4. Medium - Bond body load guards the head sub-buffer but not the weapon sub-buffer

Evidence:

- `src/game/bondview.c:3147` guards the head load against `totalsize > bodyBufSize`.
- The right-hand weapon still loads at `bodyBuffer + totalsize` with `bodyBufSize - totalsize`. See `src/game/bondview.c:3249`.
- There is no equivalent explicit guard immediately before that weapon load.

Impact:

If body/head data consumes more of the dedicated buffer than expected, the weapon load can receive an invalid pointer/size pair. Even if this only happens with corrupted data, bad HD asset metadata, or future model changes, the failure mode is high-risk because it affects the same memory region used for the intro body and weapon display data.

Recommended fix:

- Add an explicit checked-offset guard before the right-hand weapon load.
- If there is not enough remaining space, skip weapon attachment for the intro body and log a render-health failure. Do not reuse the body buffer past bounds.
- Consider splitting intro weapon data into its own dedicated buffer, matching the body de-aliasing strategy.

### 5. Medium - `chrTickBeams` is a minimal native implementation with broad visibility bypasses

Evidence:

- `src/game/chr.c:5064` labels `chrTickBeams` as a minimal native implementation.
- It advances character action and animation at `src/game/chr.c:5086` and `src/game/chr.c:5115`.
- It resolves stan/room data at `src/game/chr.c:5130`.
- By default, visibility uses a room-rendered bypass; the more direct frustum path is gated behind `GE007_CHRBEAMS_FRUSTUM`. See `src/game/chr.c:5204`.
- It allocates render positions and calculates matrices at `src/game/chr.c:5350` and `src/game/chr.c:5367`.

Impact:

For intro Bond, the broad visibility bypass can hide actual camera/room mistakes by rendering the character anyway. For other characters, it can render models that should be clipped or culled differently. This is useful while bootstrapping the native port, but it is not final-quality simulation/render behavior.

Recommended fix:

- Keep the bypass only where it is intentionally needed, such as known first-person viewer body cases.
- Move toward real per-camera frustum/room visibility for normal characters and intros.
- For intro Bond specifically, validate both visibility reasons: "room says rendered" and "projected body bounds are sane and onscreen."

### 6. Medium - Character render recalculates guarded bone matrices immediately before draw

Evidence:

- `chrRenderProp` skips rendering if `chr->field_20` is null, then prepares model render data and draw state. See `src/game/chr.c:8224` and `src/game/chr.c:8285`.
- Immediately before `drawjointlist`, native code refreshes guarded bone matrices with the current `camGetWorldToScreenMtxf()`. See `src/game/chr.c:8362`.
- It then registers level visibility and traces Bond intro render at `src/game/chr.c:8422` and `src/game/chr.c:8424`.

Impact:

This is a reasonable defense against stale guarded matrices, but it means intro Bond's final matrices depend on the camera state at draw time as well as the state used earlier in `chrTickBeams`. If those camera/model states diverge in frozen intro modes, the final draw can disagree with the earlier actor trace and render-prep assumptions.

Recommended fix:

- Trace the matrix source used for the viewer body in `chrTickBeams` and the one used in `chrRenderProp`.
- For the current-player intro body, prefer one explicit matrix-generation path and make the draw-time refresh verify consistency instead of silently becoming a second authority.

### 7. High - Metal still drops the minimap overlay

Evidence:

- `src/platform/fast3d/gfx_pc.c:24142` ends the frame through the active rendering API.
- Immediately after that, `src/platform/fast3d/gfx_pc.c:24143` comments that the minimap overlay currently uses direct OpenGL calls.
- `src/platform/fast3d/gfx_pc.c:24148` skips `minimap_overlay_draw_queued_frames()` when `gfx_backend_use_metal()` is true.

Impact:

This is a direct player-facing Metal gap. Any level or mode relying on the queued minimap overlay will be missing UI under Metal.

Recommended fix:

- Move the minimap overlay behind the renderer abstraction instead of calling OpenGL directly.
- Add a Metal path that draws through Fast3D/renderer API primitives or a small backend-neutral overlay API.
- Add a route-level screenshot test that asserts the overlay appears under Metal and OpenGL.

### 8. Medium - Metal framebuffer-read emulation is batch-level, which can be visually wrong for RDP memory effects

Evidence:

- The architecture document says RDP memory color-combiner inputs require a snapshot path. See `docs/RENDERING_ARCHITECTURE.md:61`.
- Metal handles the snapshot by ending the scene encoder, blitting a rectangle from scene color into `s_snapshot_tex`, reopening the scene encoder, and binding the snapshot as texture index 2. See `src/platform/fast3d/gfx_metal.mm:2989`, `src/platform/fast3d/gfx_metal.mm:3004`, `src/platform/fast3d/gfx_metal.mm:3027`, and `src/platform/fast3d/gfx_metal.mm:3122`.

Impact:

This is much better than ignoring framebuffer reads, but it is still an approximation if the original effect depends on per-primitive ordering, overlapping translucent surfaces, or self-read behavior inside a batch. It can produce subtle or obvious errors in glass, scopes, fades, and HUD-like effects.

Recommended fix:

- Add material/command instrumentation to identify all uses of RDP memory inputs under Metal.
- For overlapping framebuffer-read materials, force smaller flush boundaries or per-triangle snapshots where correctness matters.
- Build screenshot routes for the known RDP-memory effects and compare Metal/OpenGL outputs.

### 9. Medium - N64 binary display-list recursion lacks the PC interpreter depth cap

Evidence:

- The PC display-list interpreter caps recursion depth above 32 and returns. See `src/platform/fast3d/gfx_pc.c:22075`.
- The N64 binary display-list interpreter increments `dl_depth` without the same depth cap. See `src/platform/fast3d/gfx_pc.c:22755`.
- Recursive `G_DL` processing calls `gfx_process_n64_dl` after only a plausibility check. See `src/platform/fast3d/gfx_pc.c:23271`.
- There is a command-count guard at 50,000 commands. See `src/platform/fast3d/gfx_pc.c:22810`.

Impact:

The command-count guard prevents infinite command loops, but it does not protect the native call stack or preserve clean render state under deeply nested display lists. Corrupt or malformed display-list pointers can still create unstable renderer behavior.

Recommended fix:

- Apply the same depth cap to `gfx_process_n64_dl` that `gfx_run_dl_pc` uses.
- When the cap is hit, restore geometry state and record the display-list pointer that caused it.
- Make this a render-health counter surfaced in diagnostics.

### 10. High - Dynamic allocators avoid out-of-bounds writes by aliasing live memory, which can still corrupt geometry

Evidence:

- Native display-list and vertex buffers are fixed at 512 KB each. See `src/game/dyn.c:68` and `src/game/dyn.c:83`.
- `dynAllocate7F0BD6C4` logs overflow and returns the current pointer without advancing. See `src/game/dyn.c:129`.
- `dynAllocateMatrix` logs overflow and returns a static scratch matrix. See `src/game/dyn.c:151`.
- `dynAllocate7F0BD6F8` has the same return-current-pointer overflow behavior. See `src/game/dyn.c:176`.
- Generic `dynAllocate` also logs overflow and returns the current pointer. See `src/game/dyn.c:195`.

Impact:

This avoids direct out-of-bounds writes, but it is still dangerous. Callers can write new vertices, display-list commands, or matrices over memory that another render object expects to remain stable. For models, that can become warped limbs, shards, bad billboards, or flickering HUD elements.

Recommended fix:

- Change native overflow behavior to fail closed: return null/sentinel, and make callers skip the affected draw.
- Add checked multiplication and addition for allocator sizes.
- Add a per-frame dyn-allocation failure counter and make visual test routes fail if it is nonzero.
- Consider growing native dyn arenas or using frame arenas with explicit high-water telemetry instead of fixed 512 KB caps.

### 11. Medium - Low-32 pointer registry can silently evict or hide mappings

Evidence:

- `src/platform/gfx_ptr.h:29` uses a four-slot linear probe for each low-32 hash bucket.
- If all four slots are occupied, `gfx_pc_register_pointer_range` silently evicts slot zero. See `src/platform/gfx_ptr.h:43`.
- `gfx_pc_invalidate_pointer_range` clears matching entries. See `src/platform/gfx_ptr.h:53`.
- `gfx_pc_resolve_pointer_full` stops probing at the first empty slot. See `src/platform/gfx_ptr.h:66`.

Impact:

Silent eviction and stop-at-empty probing are acceptable for best-effort diagnostics, but risky for render data resolution. A later texture or display-list pointer can fail to resolve even though an entry exists later in the probe window, or because a busy bucket evicted a still-live mapping.

Recommended fix:

- Use tombstones for invalidated entries, or probe all four slots before giving up.
- Increase associativity or move to a small chained table for native pointer ranges.
- Add collision/eviction counters and fail tests when live render pointers depend on evicted mappings.

### 12. Medium - Texture load path still trusts texture numbers too much

Evidence:

- `texLoad` takes the texture number from the low 16 bits of `*updateword`. See `src/game/image.c:3332`.
- It indexes `g_Textures[g_TexNumToLoad + 1]` to compute the compressed source range. See `src/game/image.c:3349`.
- On memory shortage, the growable main pool attempts to grow, but the failure path falls back to the pool start/blank texture. See `src/game/image.c:3379`, `src/game/image.c:3385`, and `src/game/image.c:3391`.
- `texSelect` resolves native image indices and loads textures only when the resolved index is below `NUM_TEXTURES`. See `src/game/othermodemicrocode.c:586`, `src/game/othermodemicrocode.c:596`, and `src/game/othermodemicrocode.c:607`.

Impact:

Most callers now constrain texture IDs, but `texLoad` itself is still a sharp edge. A bad texture number can read adjacent texture metadata before the fallback path has a chance to help. For player-facing robustness, texture loading should defend itself at the lowest level.

Recommended fix:

- Add an explicit `g_TexNumToLoad + 1 < NUM_TEXTURES` check inside `texLoad`.
- Return the blank texture and log a render-health error on invalid texture numbers.
- Apply the same validation to texture dump/debug helpers so diagnostics do not crash on bad IDs.

### 13. Medium - Fast3D texture decode allocates RGBA buffers with unchecked integer multiplication

Evidence:

- `gfx_handle_settex` loads/fetches the decoded texture metadata. See `src/platform/fast3d/gfx_pc.c:21555`.
- It computes `int texel_count = w * h` and allocates `texel_count * 4`. See `src/platform/fast3d/gfx_pc.c:21569`.
- The Metal upload path rejects null, nonpositive, and greater-than-4096 dimensions. See `src/platform/fast3d/gfx_metal.mm:2823`.

Impact:

The backend upload has a useful dimension guard, but decode allocation happens earlier. Corrupt texture metadata, bad HD pack metadata, or future asset changes can overflow `int`, under-allocate, and then decode past the heap buffer.

Recommended fix:

- Use `size_t` checked multiplication for `w * h * 4` before allocation.
- Apply the same maximum-dimension policy before decode that Metal applies before upload.
- On invalid dimensions, bind the fallback texture and record a texture-health error.

### 14. Medium - Ammo HUD icon path is much better, but missing/bad images fail too quietly

Evidence:

- Ammo image globals are assigned to compiled image table entries in native builds. See `src/game/image_bank.c:270` and `src/game/image_bank.c:290`.
- `portGetAmmoImage` maps ammo types to those image globals. See `src/game/gun.c:31752`.
- `portDrawHandAmmo` falls back to width 5 if the image pointer is null, draws no icon if `image == NULL`, and still draws ammo numbers. See `src/game/gun.c:31809`, `src/game/gun.c:31811`, and `src/game/gun.c:31830`.
- `microcode_generation_ammo_related` trusts `img->width` and `img->height` before calling `texSelect` and `display_image_at_position`. See `src/game/gun.c:31142`, `src/game/gun.c:31148`, `src/game/gun.c:31164`, and `src/game/gun.c:31165`.
- `draw_textured_rectangle` clips negative X/Y and avoids zero-size division, but does not clamp right/bottom against the screen before issuing the texture rectangle. See `src/game/bondwalk2.c:34`, `src/game/bondwalk2.c:42`, `src/game/bondwalk2.c:73`, and `src/game/bondwalk2.c:99`.

Impact:

The specific ammo-icon allocation path no longer looks like a raw memory-allocation failure. The bigger issue is quality: a missing or malformed icon can disappear silently, and bad dimensions are trusted until much later. Players see a broken HUD, while tests may still pass because the ammo numbers render.

Recommended fix:

- Add a visible fallback ammo icon for missing images.
- Validate ammo image dimensions before drawing. Reject zero, negative, or unreasonable dimensions before `display_image_at_position`.
- Add a HUD screenshot route that cycles every ammo type and confirms both icon and digits render.
- Clamp texture-rectangle right/bottom coordinates or document why the backend/N64 command clipping is sufficient.

### 15. Medium - Metal texture upload is correct but leaves performance and lifetime robustness on the table

Evidence:

- `mtl_upload_texture` creates an RGBA8 texture with `MTLStorageModeShared`, writes it with `replaceRegion`, and stores it in the texture dictionary. See `src/platform/fast3d/gfx_metal.mm:2831`, `src/platform/fast3d/gfx_metal.mm:2839`, and `src/platform/fast3d/gfx_metal.mm:2844`.
- Texture IDs are monotonic and deletion removes the dictionary entry. See `src/platform/fast3d/gfx_metal.mm:2806` and `src/platform/fast3d/gfx_metal.mm:2812`.
- The Fast3D texture cache evicts old textures, deletes backend textures, and frees CPU samples. See `src/platform/fast3d/gfx_pc.c:21966`.

Impact:

Shared textures are simple and functional, but for mostly static game textures they are not Metal best practice. Private textures uploaded through staging buffers usually give the driver more room to optimize. This is less urgent than correctness bugs, but it matters for stutter and frame pacing.

Recommended fix:

- Keep the current path as a fallback, but add a private-texture upload path for static cached textures.
- Batch texture uploads where possible and avoid creating/destroying Metal texture objects during gameplay-critical frames.
- Add counters for texture uploads per frame and cache evictions per frame.

### 16. Low - Primitive depth source is wired through Fast3D but ignored by backends

Evidence:

- Fast3D computes `depth_source_prim` from `G_ZS_PRIM`. See `src/platform/fast3d/gfx_pc.c:17562` and `src/platform/fast3d/gfx_pc.c:17572`.
- It passes that flag to the backend `set_depth_mode`. See `src/platform/fast3d/gfx_pc.c:17583`.
- Metal explicitly discards it. See `src/platform/fast3d/gfx_metal.mm:2854`.
- The OpenGL implementation comments that it respects primitive depth source, but the implementation does not reference `depth_source_prim`. See `src/platform/fast3d/gfx_opengl.c:1757` and `src/platform/fast3d/gfx_opengl.c:1763`.

Impact:

This appears low-risk for normal gameplay today, but it is a false contract in the renderer abstraction. Any future material or effect that relies on primitive depth will be wrong in both backends.

Recommended fix:

- Either implement primitive-depth behavior properly or remove the misleading backend contract/comment and add a diagnostic if `G_ZS_PRIM` appears in non-debug display lists.

### 17. Medium - Depth-update behavior should be audited instead of globally approximated

Evidence:

- Fast3D only enables backend depth testing when geometry mode has `G_ZBUFFER` or sky backdrop depth and render mode has `Z_CMP`. See `src/platform/fast3d/gfx_pc.c:17562`.
- Depth writes are based on `Z_UPD`, but they are passed alongside the depth-test decision. See `src/platform/fast3d/gfx_pc.c:17568` and `src/platform/fast3d/gfx_pc.c:17583`.
- The OpenGL backend can only write depth when depth testing is enabled because `glDepthMask` is set inside the depth-test branch. See `src/platform/fast3d/gfx_opengl.c:1785` and `src/platform/fast3d/gfx_opengl.c:1801`.
- Metal's depth state similarly combines compare/write state in `mtl_depth_state_for`. See `src/platform/fast3d/gfx_metal.mm:1125`.

Impact:

This may be intentional to avoid native over-occlusion from N64 depth-priming display lists, but it is still a broad approximation. Some original effects may expect depth update without compare, while others should not write native depth. A global rule is fragile.

Recommended fix:

- Instrument every material/render-mode combination that has `Z_UPD` without `Z_CMP`.
- Decide per case whether it should write native depth, be ignored, or use a special path.
- Add route screenshots for known skyline/backdrop, weapon, and intro cases before changing default behavior.

### 18. Medium - Mid-frame framebuffer readback is functional but expensive and disruptive

Evidence:

- `mtl_read_framebuffer_rgb` closes the active encoder, commits the command buffer, waits for completion, creates a new command buffer, and reopens the scene encoder. See `src/platform/fast3d/gfx_metal.mm:3159` and `src/platform/fast3d/gfx_metal.mm:3167`.
- It then blits to a shared buffer and waits again. See `src/platform/fast3d/gfx_metal.mm:3185` and `src/platform/fast3d/gfx_metal.mm:3195`.

Impact:

This is correct for synchronous readback, but it is a frame-pacing hazard if used outside diagnostics or rare effects. It also makes command-buffer lifetime more complex during the same frame.

Recommended fix:

- Keep synchronous readback only for diagnostics and unavoidable compatibility paths.
- Add call-site counters and warnings if gameplay frames trigger mid-frame readback.
- Prefer asynchronous readback or delayed validation for screenshot/audit tooling.

### 19. Low - Metal target allocation failure clears all targets, but there is no degraded rendering mode

Evidence:

- `mtl_ensure_targets` allocates scene color, depth, snapshot, final, filter, and readback source targets. See `src/platform/fast3d/gfx_metal.mm:966`, `src/platform/fast3d/gfx_metal.mm:975`, `src/platform/fast3d/gfx_metal.mm:984`, and `src/platform/fast3d/gfx_metal.mm:992`.
- If any allocation fails, it logs once, clears all targets, and returns false. See `src/platform/fast3d/gfx_metal.mm:1002`.
- `mtl_start_frame` handles target failure by clearing shadow readiness and returning. See `src/platform/fast3d/gfx_metal.mm:1622`.

Impact:

This is safe, but it means one failed optional target can drop the entire frame. For player quality, optional effects should degrade before core scene rendering disappears.

Recommended fix:

- Classify targets as required or optional.
- If post-filter/readback/snapshot targets fail, disable the dependent effect and continue rendering the scene where possible.
- Keep scene color/depth failure as a hard frame failure.

## Specific Bond intro action plan

1. Add a trace record before `chrTickBeams` for current-player intro: selected intro pad, prop position, collision position, model suboffset, animation frame, root joint position, current room, and camera render origin.
2. Move frozen-intro current-player prop/model alignment before `chrTickBeams`, then keep the existing post-call snap only as a consistency assertion.
3. Fail the route if body render position count is below the model's required joint count.
4. Hard-disable aliased body header/buffer fallbacks in normal builds.
5. Add screenshot validation for the Dam intro with three checks: body present, body grounded, and no high-area red shard outliers.
6. Keep `GE007_NO_BOND_BODY_FIX` and `GE007_NO_INTRO_CHR_TIMING_FIX` as dev-only toggles to prove the validators catch the old broken behavior.

## Specific HUD/ammo action plan

1. Add `texLoad` texture-number bounds checks before reading `g_Textures[g_TexNumToLoad + 1]`.
2. Add checked `size_t` multiplication in `gfx_handle_settex` before RGBA allocation.
3. Add `portValidateImageEntry` for HUD images: non-null, positive dimensions, reasonable max dimensions, valid texture index.
4. Give `portDrawHandAmmo` a visible fallback icon instead of silently drawing only numbers.
5. Add a HUD screenshot route that equips weapons covering every ammo icon path and verifies icon pixels plus digits.

## Specific Metal/Fast3D action plan

1. Port minimap overlay off direct OpenGL and into a backend-neutral or Metal-capable path.
2. Add N64 binary display-list recursion depth cap matching the PC interpreter.
3. Instrument RDP memory snapshot users and split batches where framebuffer-read ordering matters.
4. Decide what to do with primitive depth source: implement, assert unused, or remove the false backend claim.
5. Add per-frame health counters: dyn allocator failures, texture load fallbacks, Metal target failures, mid-frame readbacks, pointer-table evictions, and display-list recursion aborts.

## Execution context for new engineers

The active build defines `NONMATCHING`, `NATIVE_PORT`, and `PORT_FIXME_STUBS`, so the C native implementations in this report are live code, not dead fallback branches. See `CMakeLists.txt:445`.

The high-level render flow is:

1. Game/simulation code builds N64-style display lists.
2. `gfx_run_dl` interprets PC display-list commands and dispatches nested N64 binary display lists.
3. `gfx_process_n64_dl` decodes big-endian N64 room/model display-list bytes.
4. Fast3D state changes and triangles are batched in `gfx_pc.c`.
5. `gfx_flush` calls the active backend vtable, usually OpenGL or Metal.
6. The backend uploads the batch and draws it.

This is documented in `docs/RENDERING_ARCHITECTURE.md:26`. The important design consequence is that many "Metal bugs" are actually upstream data bugs. If the simulation builds corrupted matrices, aliased vertex buffers, or wrong texture metadata, Metal can draw the corrupted data faithfully.

Use this impact scale:

- P0: primary gameplay or a major cinematic/HUD path is visibly broken, unstable, or corrupt.
- P1: frequent player-visible rendering/UI issue, but not usually a hard blocker.
- P2: narrower visual correctness issue, route-specific effect, or diagnostic risk.
- P3: performance, maintainability, or future-proofing issue.

Use this feasibility scale:

- F1: local guard or bounds check, low regression risk.
- F2: localized code change plus focused route/screenshot validation.
- F3: cross-module change, needs careful backend or simulation validation.
- F4: systemic behavior change, needs multiple routes and A/B diagnostics.
- F5: architectural rewrite.

## Ranked execution order

This ordering considers gameplay impact first and feasibility second. A P0/F4 issue can still outrank a P1/F1 issue if players are staring at the bug every level intro.

| Rank | Issue | Gameplay impact | Feasibility | Why this rank |
|------|-------|-----------------|-------------|---------------|
| 1 | Bond body alias fallbacks | P0 | F2 | Direct match for red shards/spiky Bond; fix is localized if fallbacks become fail-closed. |
| 2 | Bond intro transform authority | P0 | F4 | Direct match for floating/ungrounded Bond; harder because it touches player, camera, and model matrix timing. |
| 3 | Dyn allocator aliasing on overflow | P0 | F4 | Can create arbitrary geometry/matrix corruption anywhere; needs caller contracts, not just allocator edits. |
| 4 | `texLoad` texture-number bounds | P1 | F1 | Simple defensive guard before table indexing; protects HUD and world textures. |
| 5 | Fast3D RGBA decode allocation overflow | P1 | F1 | Simple checked multiplication before decode; prevents heap overwrite from bad metadata. |
| 6 | Ammo HUD fallback and image validation | P1 | F2 | Player-facing HUD quality; localized to image/HUD draw path. |
| 7 | Metal minimap overlay skipped | P1 | F3 | Known Metal-only missing UI; needs renderer abstraction work. |
| 8 | Intro visual validation gap | P1 | F2 | Not a runtime fix, but it prevents regressions and proves the Bond fixes work. |
| 9 | Bond intro weapon sub-buffer guard | P1 | F2 | Same memory neighborhood as the red-shard bug; localized defensive fix. |
| 10 | N64 binary display-list recursion cap | P1 | F1 | Straight parity fix with PC interpreter; prevents pathological renderer stack/state failures. |
| 11 | RDP memory snapshot ordering | P1 | F4 | Real effect correctness risk; needs material census and screenshot comparisons. |
| 12 | Low-32 pointer registry robustness | P2 | F2 | Can cause missing pointer resolutions; fix is small but touches core pointer lookup. |
| 13 | `chrTickBeams` visibility bypass | P2 | F4 | Broad simulation/render behavior; important but needs route coverage. |
| 14 | Draw-time character matrix refresh authority | P2 | F3 | Suspect for transform mismatches; needs instrumentation before behavior changes. |
| 15 | Depth-update behavior audit | P2 | F3 | Could affect backdrop/weapon/glass correctness; must be material-specific. |
| 16 | Metal target degraded mode | P2 | F3 | Robustness improvement for low-resource states; not likely the current Bond bug. |
| 17 | Mid-frame Metal readback stalls | P2 | F2 | Frame-pacing risk mostly for diagnostics; easy to count and gate. |
| 18 | Metal shared texture upload path | P3 | F3 | Best-practice/performance work after correctness issues. |
| 19 | Primitive depth-source false contract | P3 | F2 | Mostly future-proofing unless a live material starts using `G_ZS_PRIM`. |

Status update, 2026-07-07 follow-up:

- R1 fail-closed allocation behavior is implemented in the working tree, and the red-shard root cause has been corrected to the shared body/head/weapon buffer-packing offset in `solo_char_load`.
- R4 is implemented in the working tree: `texLoad` now rejects invalid native texture IDs before reading `g_Textures[id + 1]`.
- R5 is implemented in the working tree: `G_SETTEX` decode now validates dimensions and uses checked allocation sizing before writing the RGBA decode buffer.
- R7 is done: the Metal skip is removed and the minimap overlay now draws natively on Metal via `gfx_metal_draw_minimap_overlay`, with GL and Metal overlay draw summaries matching in the minimap smoke.
- Next ready item: R10, the N64 binary display-list recursion cap. It is the next best F1 task because the PC display-list interpreter already has the correct depth limit, and the N64 binary interpreter can adopt the same guard without changing valid render output.
- Not next: R2/R3. They remain high impact, but they need trace/screenshot instrumentation before behavior changes. R6 is player-facing and worthwhile after R10, but it is slightly broader because it needs a fallback icon policy and HUD screenshot route.

## Issue execution packets

### R1 - Bond body alias fallbacks

Impact/feasibility: P0/F2.

Trace:

1. Level intro setup calls `bondviewPrepareNativeBondIntroChr`. It starts the viewer-body fade, sets `s_nativeLoadingBondIntroChr`, calls `solo_char_load`, then assigns the intro animation and marks the chr as `ACT_BONDINTRO`. See `src/game/bondview.c:5688`, `src/game/bondview.c:5716`, `src/game/bondview.c:5755`, and `src/game/bondview.c:5768`.
2. `solo_char_load` builds the current-player viewer body. The comments at `src/game/bondview.c:2871` explain the old failure: body header data lived in the right-hand weapon slot, the first-person weapon loader overwrote it, and the body display list then referenced uninitialized joint matrices.
3. The native fix allocates a dedicated body header and body buffer. See `src/game/bondview.c:2900`, `src/game/bondview.c:2930`, and the dedicated-buffer substitution at `src/game/bondview.c:3012`.
4. The fix is not fail-closed. If `GE007_NO_BOND_BODY_FIX` is set, or if `calloc` fails, `solo_char_load` falls back to the aliased weapon slot. See `src/game/bondview.c:2889`, `src/game/bondview.c:2907`, `src/game/bondview.c:2937`, and `src/game/bondview.c:3116`.
5. The same code later clears `lock_hand_model`, allowing normal first-person weapon loading to continue. See `src/game/bondview.c:3264`. That is correct only if body memory is no longer aliased with weapon memory.

Why it matters:

The user-visible symptom, red shards around Bond, is exactly what corrupted body vertices or corrupted joint matrices look like. Metal is not the first suspect here. It is probably drawing bad body data produced by the native body loader.

Fix steps:

1. Change `portBondBodyHeader` and `portBondBodyBuffer` failure behavior for normal native builds: return a typed failure state instead of silently allowing fallback to `GUNRIGHT`.
2. In `solo_char_load`, if the dedicated body header or body buffer is unavailable in single-player intro/viewer mode, skip body construction and emit one hard render-health error. Do not write into `get_ptr_itemheader_in_hand(GUNRIGHT)` as a fallback.
3. Keep `GE007_NO_BOND_BODY_FIX` only as a dev-only A/B knob. Tests should never run with it unless the goal is to prove the validator catches the broken path.
4. Add assertions/logging that body header address, body buffer address, right-hand weapon header, and right-hand weapon buffer are distinct during intro load.

Validation:

1. Run the Dam intro route with `GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5`.
2. Run once normally and once with `GE007_NO_BOND_BODY_FIX=1`. The normal run must render stable Bond. The disabled-fix run should fail the visual validator.
3. Confirm `modelGetRenderPosCount` for the body is at least the body model's required joint count before `chrTickBeams` builds the render list.

Correction (2026-07-07): finding #1 misattributed the red shards. Empirically,
the header/buffer **aliasing** leaves Bond **invisible** (render count collapses
to ~6), not sharded — `GE007_NO_BOND_BODY_FIX=1` produces a clean, Bond-less
scene with **no** shards. The actual red shards come from a **separate buffer
bug** in `solo_char_load` (same family as R4/R9): the intro body, head, and
right-hand weapon share one load buffer, and the native path failed to advance
`totalsize` past the head mesh before the weapon load (retail's
`totalsize = ALIGN64_V3(bufferSizeRemain + 0xFB)` sat in the `#else` and was
skipped). Measured on Dam: weapon loaded at offset 28480 inside the head mesh
[27776, 35200), overwriting ~90% of Bond's head → dark-red degenerate shards in
the swirl/outro (only *visible* once the de-alias keeps render count ~21). FIXED
2026-07-07 by running that advancement in the native path (weapon offset now
36096, past head_end 35200; `GE007_TRACE_BOND_BUF=1` reports `OVERLAP=0`).
Verified: shards gone at the previously-sharded Dam swirl frames, Bond renders
intact, FP weapon still correct, Bond trace counts unchanged
(present=201/rendered=148/render_count=289). This makes R4/R9 higher-value than
their original Medium rank — they are the neighborhood of the real shard bug.

Status (2026-07-07): the allocation-failure
fallback to the aliased `GUNRIGHT` header/buffer is removed: when the body fix
is enabled (default) but a dedicated header/buffer cannot be allocated,
`solo_char_load` now fails closed — it skips viewer-body construction, leaves
`ptr_char_objectinstance` NULL (every caller guards on it), clears the hand
locks so normal FP weapon loading still arms the player, and emits one
`[BONDVIEW][RENDER-HEALTH]` error. The aliased path is reachable only with the
dev A/B knob `GE007_NO_BOND_BODY_FIX`. A distinctness guard
(`portBondBodyAssertDistinct`) asserts the body header/buffer never coincide
with the `GUNRIGHT` weapon header/buffer. A new fault-injection hook
`GE007_BOND_BODY_ALLOC_FAIL=header|buffer|both` exercises the fail-closed path
without real OOM. Verified on the Dam intro route via the per-frame Bond trace
(`--trace-state`): fix-on renders Bond identically (present=201/rendered=148/
render_count=289 over 339 frames); forced alloc-fail emits **zero** Bond
geometry (present=0/rendered=0/render_count=0) and exits cleanly — no shards
are structurally possible; `GE007_NO_BOND_BODY_FIX` is unchanged.

### R2 - Bond intro transform authority

Impact/feasibility: P0/F4.

Trace:

1. The intro character is loaded and animated through `bondviewPrepareNativeBondIntroChr`. See `src/game/bondview.c:5688`.
2. The swirl setup intentionally defers the native chr load by one tick to match stock timing, unless `GE007_NO_INTRO_CHR_TIMING_FIX` is set. See `src/game/bondview.c:5944`.
3. Frozen intro camera setup changes the camera-relative render origin: if `playerHasFrozenIntroCamera` is true, `current_model_pos` becomes the camera position. See `src/game/bondview.c:15629` and `src/game/bondview.c:15647`.
4. `chrTickBeams` advances the chr animation, resolves visibility, allocates model render positions, and calls `subcalcmatrices`. See `src/game/chr.c:5115`, `src/game/chr.c:5204`, `src/game/chr.c:5357`, and `src/game/chr.c:5369`.
5. The current-player path calls `chrTickBeams` first. Only after that does native code copy `field_488.collision_position` back into `player->prop->pos` and call `setsuboffset`. See `src/game/bondview.c:24139` and `src/game/bondview.c:24144`.

Why it matters:

The code comment at `src/game/bondview.c:24146` says `field_488` is authoritative because the model prop can lag or inherit intro-animation transforms. But that correction currently happens after render matrices and the display-list chain have already been generated. That can leave the visible body floating or offset even though the player/collision state is corrected by the time diagnostics look at it.

Fix steps:

1. Add a helper like `bondviewAlignCurrentPlayerIntroPropBeforeChrTick(struct player *)`.
2. Call it before `chrTickBeams` in the current-player path when `playerHasFrozenIntroCamera(player)` is true.
3. The helper should copy `field_488.collision_position` to `prop->pos`, copy `field_488.current_tile_ptr` to `prop->stan`, and call `setsuboffset` on `ptr_char_objectinstance`.
4. Keep the existing post-`chrTickBeams` copy as a debug assertion/final consistency repair, but log if it changes any coordinate by more than a small epsilon.
5. Do not apply this blindly to non-intro gameplay until the first-person hand/body path is validated.

Validation:

1. Add trace fields before and after `chrTickBeams`: prop pos, collision pos, model suboffset, root render position, camera mode, current_model_pos, and selected intro pad.
2. Dam intro must show stable root/pelvis height relative to the selected pad.
3. Screenshot validation should fail if Bond's projected body center drifts far from the authored intro location.

Status (2026-07-08, `73afbfb`): re-scoped after D43 (`9acba24`) and D31
(`510e181`) landed and were oracle-validated (Bond Y matches stock, median delta
0.00 through the whole swirl). Those fixes restructured the post-`chrTickBeams`
block so that on animated frozen-intro frames the animation root motion drives
`prop->pos` and the block copies it **outward** to the collision anchor and
returns — it no longer snaps the anchor **into** the rendered prop. Measured with
the new `GE007_TRACE_INTRO_AUTHORITY` diagnostic (Dam intro, 557 current-player
intro ticks): the animation inside `chrTickBeams` is already the single authority
for the swirl body, and every inward-snap frame is a no-op (zero health
warnings). Per charter rule 10 the prescribed pre-`chrTickBeams` alignment
refactor was **not** forced onto an already-stock-correct scene (zero visual
upside, would fight D31/D43); instead the post-tick block is demoted to a logged
consistency check — a one-shot `[BONDVIEW][RENDER-HEALTH]` warning fires if the
inward snap ever moves the already-rendered body by >0.01u in the frozen intro.
Log-only: `sim_state_hash` / renderer parity green, oracle-facing counters
byte-identical. Backlog M1.3 carries the full decision.

### R3 - Dynamic allocator aliasing on overflow

Impact/feasibility: P0/F4.

Trace:

1. Native dyn memory uses 512 KB VTX and GFX halves per active buffer. See `src/game/dyn.c:68`.
2. Several allocators detect overflow but return the current pointer without advancing. See `dynAllocate7F0BD6C4` at `src/game/dyn.c:129`, `dynAllocate7F0BD6F8` at `src/game/dyn.c:176`, and generic `dynAllocate` at `src/game/dyn.c:195`.
3. Matrix overflow returns one static scratch matrix for every overflow allocation. See `src/game/dyn.c:151`.
4. Callers usually assume allocation succeeded and write vertices, lights, display-list commands, or matrices into the returned storage.

Why it matters:

This prevents immediate out-of-bounds writes, but it does not prevent corruption. Returning the same pointer means unrelated render objects can share and overwrite one another's vertices or matrices. Static matrix fallback means many objects can receive the same transform. Both failure modes produce visible shards, warped models, bad billboards, or flickering overlays.

Fix steps:

1. Introduce an explicit native failure sentinel for dyn allocation, preferably `NULL`.
2. Add checked arithmetic before `count * 0x10` and before aligned size addition.
3. Update high-risk callers first: model render-position allocation, glass shards, effects billboards, HUD rectangles, and matrix-producing camera/render code.
4. If a caller gets `NULL`, it must skip the specific draw/effect and increment a per-frame render-health counter.
5. Keep a temporary compatibility mode if needed, but tests should run fail-closed.

Validation:

1. Add `GE007_DYN_STRESS_LIMIT=<bytes>` or similar to force low dyn budgets.
2. Under stress, the game should drop effects or objects cleanly, not create triangles spanning the screen.
3. Normal gameplay routes should report zero dyn allocation failures.

### R4 - `texLoad` texture-number bounds

Impact/feasibility: P1/F1.

Trace:

1. HUD and world texture paths eventually call `texLoad` or `texLoadFromTextureNum`.
2. `texLoad` takes the texture ID from `*updateword & 0xffff`. See `src/game/image.c:3332`.
3. It then reads `g_Textures[g_TexNumToLoad]` and `g_Textures[g_TexNumToLoad + 1]` to get compressed ROM offsets. See `src/game/image.c:3349`.
4. Some callers validate IDs first, for example `texSelect` checks against `NUM_TEXTURES`. See `src/game/othermodemicrocode.c:596`.
5. `texLoad` itself does not contain the lowest-level guard.

Why it matters:

Texture loading is foundational. A bad texture ID should produce a blank/fallback texture, not read adjacent metadata. This affects ammo icons, menus, world textures, and any future HD or generated asset metadata.

Fix steps:

1. At the top of `texLoad`, after `g_TexNumToLoad` is assigned, check `g_TexNumToLoad >= NUM_TEXTURES - 1`.
2. If invalid, set `*updateword` to the blank texture/pool start if available, log a texture-health error, and return.
3. Audit debug helpers that index `g_Textures[texturenum + 1]` and add the same guard.

Validation:

1. Add a small unit/debug route that calls `texLoadFromTextureNum(NUM_TEXTURES, NULL)` and confirms no crash.
2. Confirm normal levels and HUD routes report zero invalid texture IDs.

### R5 - Fast3D RGBA decode allocation overflow

Impact/feasibility: P1/F1.

Trace:

1. `G_SETTEX` handling decodes Rare texture-by-number assets into RGBA32 before backend upload. See `src/platform/fast3d/gfx_pc.c:21505`.
2. It loads the texture through the game texture pool, then reads `tex->width` and `tex->height`. See `src/platform/fast3d/gfx_pc.c:21555` and `src/platform/fast3d/gfx_pc.c:21569`.
3. It computes `int texel_count = w * h` and `malloc(texel_count * 4)`. See `src/platform/fast3d/gfx_pc.c:21578`.
4. Metal upload later rejects nonpositive or greater-than-4096 dimensions. See `src/platform/fast3d/gfx_metal.mm:2823`. That is too late for decode allocation.

Why it matters:

If texture metadata is corrupt or an HD asset path feeds unexpected dimensions, the allocation can under-allocate and the decode loops can write past the heap buffer.

Fix steps:

1. Before allocation, validate `w > 0`, `h > 0`, `w <= 4096`, `h <= 4096`.
2. Use checked `size_t` multiplication for `(size_t)w * (size_t)h` and then `* 4`.
3. On failure, clear `settex_active`, bind/keep fallback texture state, log `SETTEX_DIM_INVALID`, and return.
4. Apply the same checked size to diagnostic readback buffers near `src/platform/fast3d/gfx_pc.c:21940`.

Validation:

1. Add a debug-only test path with forced bad dimensions.
2. Confirm existing muzzle/texture diagnostics still dump correctly.
3. Run HUD and title/menu routes, because they exercise many small UI textures.

### R6 - Ammo HUD fallback and image validation

Impact/feasibility: P1/F2.

Trace:

1. `texReset` points ammo image globals at compiled image table arrays in native builds. See `src/game/image_bank.c:269` and `src/game/image_bank.c:290`.
2. `portGetAmmoImage` maps ammo enum values to those globals. See `src/game/gun.c:31752`.
3. `portDrawHandAmmo` gets the current weapon, resolves ammo type, chooses the image, and draws the icon if `image != NULL`. See `src/game/gun.c:31800` and `src/game/gun.c:31830`.
4. If the image is null, it silently uses width 5 for number spacing and draws only digits. See `src/game/gun.c:31810`.
5. The draw helper trusts `img->width` and `img->height` before calling `texSelect` and `display_image_at_position`. See `src/game/gun.c:31142` and `src/game/gun.c:31164`.
6. `display_image_at_position` delegates to `draw_textured_rectangle`, which only checks positive half-size and clips negative X/Y. See `src/game/bondwalk2.c:136` and `src/game/bondwalk2.c:15`.

Why it matters:

Ammo icons are constant gameplay UI. Missing icons or malformed dimensions should be visible to developers and should degrade to a recognizable fallback for players.

Fix steps:

1. Add `portValidateImageEntry(const struct sImageTableEntry *img, const char *label)`.
2. Validate non-null, positive width/height, reasonable max dimensions, and texture index in range.
3. Add a small fallback ammo icon entry or draw a simple backend-neutral rectangle/glyph if validation fails.
4. Count missing/invalid ammo icons as HUD health errors.
5. Clamp texture rectangle right/bottom or confirm the Fast3D texture rectangle path already clamps safely in the backend.

Validation:

1. Add a route or scripted inventory cycle that displays every ammo type.
2. Take screenshots under OpenGL and Metal.
3. Check for both icon pixels and digit pixels, not just the presence of text.

### R7 - Metal minimap overlay skipped

Impact/feasibility: P1/F3.

Trace:

1. The main frame finishes through `gfx_rapi->end_frame`. See `src/platform/fast3d/gfx_pc.c:24142`.
2. The minimap overlay is drawn afterward through direct OpenGL calls, outside the renderer vtable.
3. Native Apple builds skip the overlay when `gfx_backend_use_metal()` is true. See `src/platform/fast3d/gfx_pc.c:24143`.

Why it matters:

This is a confirmed missing UI feature on Metal. It is not theoretical and not dependent on corrupted input data.

Fix steps:

1. Locate the minimap overlay draw implementation and identify the primitive set it needs: textured quads, lines, points, scissor, blend, or simple colored triangles.
2. Add a backend-neutral overlay API if the existing Fast3D path is too heavyweight.
3. Implement the Metal overlay path using the same target and presentation timing as the main frame.
4. Remove the `gfx_backend_use_metal()` skip only after screenshot validation passes.

Validation:

1. Use a level/mode where the minimap overlay is expected.
2. Compare OpenGL and Metal screenshots.
3. Confirm no OpenGL symbols are called while Metal is active.

### R8 - Intro visual validation gap

Impact/feasibility: P1/F2.

Trace:

1. Existing intro trace tooling counts actor state: present, onscreen, model matrix, rendered, animation, item match, render count. See `tools/audit_intro_trace.py:30`.
2. It increments these counters based on JSON fields like `bond_rendered` and `bond_anim`. See `tools/audit_intro_trace.py:225`.
3. Existing ROM comparison routes require Bond to appear, render, animate, and carry the silenced PP7. See `docs/ROM_COMPARISON.md:660`.
4. The existing cinematic docs already include screenshot commands for Dam intro. See `docs/CINEMATICS.md:70`.

Why it matters:

A shredded Bond can still be "rendered" and "animated." A floating Bond can still be onscreen with the correct animation hash. The tests need to inspect pixels and projected body position, not only actor state.

Fix steps:

1. Extend the intro trace with projected root/pelvis/feet coordinates and model render-position count.
2. Add a screenshot analyzer for the Dam intro frame window.
3. Compute a Bond bounding region from projected joints or a known route-specific box.
4. Fail if silhouette coverage is too small, if red/warm outlier pixels extend far outside the body box, or if root/feet are far above the pad.

Validation:

1. Normal run should pass.
2. `GE007_NO_BOND_BODY_FIX=1` should fail shard/silhouette checks.
3. A deliberately offset intro prop should fail grounding checks.

Status (2026-07-08, `dd36bad` + `3161b7a`): done. The trace gained an
`intro.bond_body` record (world root, floor Y beneath Bond, model height, joint
render-position count, projected screen bbox / root+head points). A new Dam-intro
screenshot analyzer (`tools/analyze_intro_body.py`) runs presence (warm
skin/tan silhouette coverage), grounding (median `world_root.y - floor_y`, from
the trace, projection-independent), and a dark-red shard-outlier score, wired
into `tools/intro_visual_regression.sh` with the negative controls baked in:
`GE007_NO_BOND_BODY_FIX=1` fails presence (and render_pos_count 6<18);
`GE007_INTRO_BODY_Y_OFFSET=300` fails grounding (median 409.6>250); a dark-red
injection self-test fails shards; `GE007_NO_INTRO_PHASE3`/`_ROOTMOTION` both pass.
Caveat: the port's frozen-intro camera does not project actor world positions to
screen reliably, so the Bond region is an empirically-measured Dam-route fixture
rather than an engine joint projection (point 3 above adjusted accordingly);
grounding stays exact via world-space trace values.

### R9 - Bond intro weapon sub-buffer guard

Impact/feasibility: P1/F2.

Trace:

1. `solo_char_load` loads the body into `bodyBuffer`, then places the head into the same buffer at an aligned offset. See `src/game/bondview.c:3132` and `src/game/bondview.c:3138`.
2. Native code guards the head sub-buffer offset before loading the head. See `src/game/bondview.c:3147`.
3. Later, the right-hand weapon header is copied and `load_object_fill_header` loads weapon data into `bodyBuffer + totalsize` with `bodyBufSize - totalsize`. See `src/game/bondview.c:3249`.
4. There is no equivalent guard before the weapon load.

Why it matters:

This is in the same memory region as the Bond body corruption fix. A malformed body/head size or future model change can corrupt the intro body or weapon model and produce exactly the same class of player-visible shards.

Fix steps:

1. Before the weapon load, check `totalsize >= 0 && totalsize < bodyBufSize`.
2. Ensure the remaining bytes are enough for `PitemZ_entries[rhandPropID].filename` based on `get_pc_buffer_remaining_value`.
3. If insufficient, skip weapon attachment and log a one-shot Bond intro health error.
4. Prefer a dedicated intro weapon buffer in the longer term.

Validation:

1. Dam intro still shows Bond with the silenced PP7.
2. A forced-small body buffer skips the weapon without corrupting the body.
3. Existing route requirement for `ITEM_WPPKSIL` should be updated to allow a forced-failure test mode but remain strict normally.

### R10 - N64 binary display-list recursion cap

Impact/feasibility: P1/F1.

Trace:

1. The PC display-list interpreter increments `dl_depth` and returns if depth exceeds 32. See `src/platform/fast3d/gfx_pc.c:22075`.
2. The N64 binary interpreter tracks `entry_depth`, pushes display-list context, and increments `dl_depth`. See `src/platform/fast3d/gfx_pc.c:22717` and `src/platform/fast3d/gfx_pc.c:22755`.
3. Recursive N64 `G_DL` calls enter `gfx_process_n64_dl` again. See `src/platform/fast3d/gfx_pc.c:23271`.
4. There is a command-count guard at 50,000 commands, but no matching recursion-depth cap. See `src/platform/fast3d/gfx_pc.c:22810`.

Why it matters:

Malformed or misresolved N64 display-list pointers can grow the native stack and leave renderer state in a bad nested context. This can turn one bad asset pointer into widespread frame corruption.

Fix steps:

1. Add a depth check immediately after validating the N64 display-list pointer and before incrementing `dl_depth`.
2. Match the PC interpreter limit of 32.
3. On abort, restore `g_executing_weapon_dl`, `g_executing_guard_dl`, inherited effect labels, and any stack state exactly as normal exits do.
4. Add a diagnostic counter and log the offending display-list pointer once per route.

Validation:

1. Existing levels should report zero depth aborts.
2. A synthetic nested DL test should abort cleanly at depth 33 without crashing.

### R11 - RDP memory snapshot ordering

Impact/feasibility: P1/F4.

Trace:

1. Some N64 translucent effects read current framebuffer memory as part of blending. See `docs/RENDERING_ARCHITECTURE.md:61`.
2. Metal cannot sample the render attachment being written, so it ends the encoder, copies a rectangle from scene color to `s_snapshot_tex`, reopens the encoder, and binds the snapshot as texture index 2. See `src/platform/fast3d/gfx_metal.mm:2989`, `src/platform/fast3d/gfx_metal.mm:3017`, `src/platform/fast3d/gfx_metal.mm:3026`, and `src/platform/fast3d/gfx_metal.mm:3122`.
3. The architecture doc explains that the default snapshot is once per batch, with a per-triangle A/B mode for exactness. See `docs/RENDERING_ARCHITECTURE.md:71`.

Why it matters:

Batch-level snapshots are fast and often correct, but any overlapping framebuffer-read material can need more precise ordering. If the wrong ordering happens on glass, water, foliage, scopes, or HUD effects, players see wrong translucency, stale color, or missing coverage behavior.

Fix steps:

1. Instrument every draw that uses `diagRdpMemory` or `diagRdpCvgMemory`, including material, draw class, rect, and triangle overlap.
2. Build a list of live materials/routes that actually depend on RDP memory.
3. For overlapping batches, split before draw or use per-triangle snapshots only for those materials.
4. Preserve the current batch path for non-overlapping panes/cards.

Validation:

1. Compare OpenGL and Metal screenshots for Dam glass, water, foliage, and any scope/HUD framebuffer-read effects.
2. A/B with per-triangle snapshot mode where available.
3. Track frame time because this path can stall the GPU.

### R12 - Low-32 pointer registry robustness

Impact/feasibility: P2/F2.

Trace:

1. N64-style display-list commands can carry truncated 32-bit pointers on a 64-bit host.
2. `gfx_ptr_store` hashes the low 32 bits and probes four slots. See `src/platform/gfx_ptr.h:29`.
3. If all four slots are occupied, it silently evicts the first slot. See `src/platform/gfx_ptr.h:43`.
4. Invalidation zeroes slots. See `src/platform/gfx_ptr.h:53`.
5. Resolution stops at the first empty slot. See `src/platform/gfx_ptr.h:66`.

Why it matters:

This is usually a benign miss when another resolver can recover, but it is risky for arbitrary display-list, texture, or matrix pointer resolution. Silent eviction also hides high-collision bugs.

Fix steps:

1. Replace stop-at-empty resolution with full-window probing, or add tombstones for invalidated entries.
2. Add counters for collisions, evictions, invalidations, and failed resolves.
3. Consider increasing associativity from 4 to 8 if collision counts are nontrivial.
4. Log the pointer kind when possible: texture, DL, matrix, image, or unknown.

Validation:

1. Run normal routes and confirm no live pointer resolutions depend on evicted entries.
2. Add a stress test that registers colliding low-32 pointers and confirms later entries still resolve.

### R13 - `chrTickBeams` visibility bypass

Impact/feasibility: P2/F4.

Trace:

1. `chrTickBeams` is explicitly a minimal native implementation. See `src/game/chr.c:5064`.
2. It advances AI/animation, fixes stan, disposes the previous render chain, computes visibility, allocates render positions, and generates a new display-list chain. See `src/game/chr.c:5086`, `src/game/chr.c:5130`, `src/game/chr.c:5198`, `src/game/chr.c:5204`, and `src/game/chr.c:5350`.
3. The real frustum test is gated behind `GE007_CHRBEAMS_FRUSTUM`; default behavior uses room-rendered visibility. See `src/game/chr.c:5204`.
4. Hidden guards are forced invisible later, which fixes one class of false positives. See `src/game/chr.c:5298`.

Why it matters:

The room-rendered bypass can make an actor "visible" because its room is rendered, even if the camera/projection relationship is wrong. That can hide intro Bond transform bugs and can render other characters in situations where stock would cull them.

Fix steps:

1. Do not globally flip `GE007_CHRBEAMS_FRUSTUM` on without split-screen validation.
2. For single-player intro routes, add a special validation mode that records both room-rendered and frustum visibility.
3. If they disagree for viewer Bond, inspect camera origin, prop pos, room ids, and projected bounds before changing runtime behavior.
4. Gradually replace the bypass for normal single-player characters once route screenshots prove parity.

Validation:

1. Use `GE007_TRACE_VISIBILITY` to collect disagreement records.
2. Cross-check disagreements against screenshots, not only logs.
3. Validate split-screen separately before enabling any global behavior.

### R14 - Draw-time character matrix refresh authority

Impact/feasibility: P2/F3.

Trace:

1. `chrTickBeams` builds `model->render_pos` using `camGetWorldToScreenMtxf`. See `src/game/chr.c:5353` and `src/game/chr.c:5369`.
2. `chrRenderProp` later requires `chr->field_20` and prepares final draw state. See `src/game/chr.c:8224` and `src/game/chr.c:8285`.
3. Native code then refreshes guarded bone matrices immediately before `drawjointlist`, again using `camGetWorldToScreenMtxf`. See `src/game/chr.c:8362`.

Why it matters:

This refresh was added to fix stale guard matrices, but it creates two matrix-generation points. If the camera/model state differs between `chrTickBeams` and `chrRenderProp`, the final draw can disagree with the earlier trace and visibility logic.

Fix steps:

1. Instrument both matrix passes with frame number, camera mode, camera matrix pointer/hash, model root position, and render_pos count.
2. For viewer Bond intro, verify both passes see the same intended camera/model state.
3. If they differ, either skip the draw-time refresh for current-player intro bodies or make `chrTickBeams` and `chrRenderProp` consume an explicit frozen-intro matrix snapshot.
4. Keep the guard fix for ordinary guards unless a route proves it is safe to remove.

Validation:

1. Dam intro should show identical matrix hashes or an intentional documented difference.
2. Guard routes that previously had stale matrix seams must remain fixed.

### R15 - Depth-update behavior audit

Impact/feasibility: P2/F3.

Trace:

1. Fast3D only enables depth testing when geometry mode has depth and render mode has `Z_CMP`. See `src/platform/fast3d/gfx_pc.c:17562`.
2. It separately computes `depth_update` from `Z_UPD`. See `src/platform/fast3d/gfx_pc.c:17569`.
3. OpenGL can express write-without-compare only inside the `depth_test` branch. See `src/platform/fast3d/gfx_opengl.c:1801`.
4. Metal's depth state similarly writes only when `test && update`. See `src/platform/fast3d/gfx_metal.mm:1125`.

Why it matters:

The current conservative rule may be intentional, but it is broad. Some N64 materials may expect depth priming without compare; others would create native over-occlusion if allowed. The right answer is material-specific, not a permanent global approximation.

Fix steps:

1. Log every draw with `Z_UPD` and no `Z_CMP`: draw class, material, geometry mode, render mode, room/effect label, and route frame.
2. Classify the results: should write native depth, should not write depth, or unknown.
3. Add targeted overrides only for known-correct classes.
4. Validate sky, weapon, decal, glass, and intro routes after any change.

Validation:

1. No broad behavior change should ship without screenshot comparisons.
2. Any newly enabled write-only depth path must be route-gated until proven.

### R16 - Metal target degraded mode

Impact/feasibility: P2/F3.

Trace:

1. `mtl_ensure_targets` allocates scene color, scene depth, snapshot, final color, and low-filter targets. See `src/platform/fast3d/gfx_metal.mm:960`.
2. If any allocation fails, it clears all targets and returns. See `src/platform/fast3d/gfx_metal.mm:1002`.
3. `mtl_start_frame` then skips the frame if targets are missing. See `src/platform/fast3d/gfx_metal.mm:1622`.

Why it matters:

The current behavior is safe, but optional effects can take down the entire frame. A player would prefer no post-filter or no snapshot effect over a missing frame.

Fix steps:

1. Mark targets as required or optional.
2. Required: scene color and depth.
3. Optional: snapshot, final/post-filter, low-filter/readback depending on active features.
4. If an optional target fails, disable the dependent effect for that frame and render the base scene.

Validation:

1. Add a debug env var to force individual target allocation failures.
2. Scene color/depth failure should skip the frame cleanly.
3. Optional target failure should still present a base scene and log degraded mode.

### R17 - Mid-frame Metal readback stalls

Impact/feasibility: P2/F2.

Trace:

1. `mtl_read_framebuffer_rgb` supports mid-frame and between-frame reads. See `src/platform/fast3d/gfx_metal.mm:3148`.
2. If called mid-frame, it ends the render encoder, commits the command buffer, waits for completion, creates a new command buffer, and reopens the scene encoder. See `src/platform/fast3d/gfx_metal.mm:3159`.
3. It then creates a shared buffer, blits, commits another command buffer, and waits again. See `src/platform/fast3d/gfx_metal.mm:3185`.

Why it matters:

This is correct for diagnostics, but it serializes CPU and GPU work. If gameplay paths accidentally use it, frame pacing will suffer.

Fix steps:

1. Add counters for mid-frame readbacks and between-frame readbacks.
2. Print a warning if mid-frame readback happens outside screenshot/diagnostic modes.
3. Prefer delayed/asynchronous readback for validation tooling where possible.

Validation:

1. Normal gameplay should report zero mid-frame readbacks.
2. Screenshot and pixel-probe routes should report expected readbacks and no command-buffer errors.

### R18 - Metal shared texture upload path

Impact/feasibility: P3/F3.

Trace:

1. Fast3D decodes textures to RGBA and calls the backend upload hook. See `src/platform/fast3d/gfx_pc.c:21885`.
2. Metal creates `MTLStorageModeShared` RGBA8 textures and writes them with `replaceRegion`. See `src/platform/fast3d/gfx_metal.mm:2823`.
3. Texture cache eviction deletes backend textures through the vtable. See `src/platform/fast3d/gfx_pc.c:21966`.

Why it matters:

This is functionally fine, but static textures are better uploaded to private GPU textures through staging resources. The benefit is smoother frame pacing and better driver optimization, not immediate correctness.

Fix steps:

1. Add per-frame counters for texture uploads, texture bytes uploaded, and cache evictions.
2. Confirm whether uploads happen during gameplay-critical frames.
3. Add a private-texture upload path for static cached textures using a staging buffer/blit.
4. Keep the shared path as fallback for simplicity or small dynamic textures.

Validation:

1. Texture pixels must match the current path.
2. No increase in upload failures or missing textures.
3. Compare frame-time spikes before and after on texture-heavy routes.

### R19 - Primitive depth-source false contract

Impact/feasibility: P3/F2.

Trace:

1. Fast3D computes `depth_source_prim` from `G_ZS_PRIM` and includes it in `depth_mode`. See `src/platform/fast3d/gfx_pc.c:17572`.
2. It passes the flag to the backend. See `src/platform/fast3d/gfx_pc.c:17583`.
3. Metal discards the flag. See `src/platform/fast3d/gfx_metal.mm:2854`.
4. OpenGL claims to respect primitive depth source, but the function never uses `depth_source_prim`. See `src/platform/fast3d/gfx_opengl.c:1757`.

Why it matters:

This is not currently the likely Bond or HUD bug. It is a renderer abstraction lie. Future effects or debugging code can assume primitive depth works when neither backend implements it.

Fix steps:

1. Add instrumentation for any live non-debug draw using `G_ZS_PRIM`.
2. If none exist, update comments and add a warning if it appears unexpectedly.
3. If live materials use it, design an implementation or a material-specific fallback.

Validation:

1. Normal routes should report either zero `G_ZS_PRIM` uses or an explicit known allowlist.
2. Backend comments and behavior should match.

## Bottom line

The most player-visible problems are not isolated to Metal. The Bond intro red shards strongly point at native body/model memory ownership and matrix generation. Metal can faithfully draw corrupted triangles, so the fix should start with the intro body allocation/transform pipeline, then validate with screenshots. The HUD ammo path is largely on the right track, but it needs defensive bounds checks and visible fallback behavior. Metal itself has a solid base, but the missing minimap overlay, framebuffer-read approximation, and a few abstraction gaps must be closed before it can be treated as the primary player-quality renderer.

The practical first sprint should be R1, R4, R5, R8, R9, and R10. Those are high-signal, mostly localized changes that reduce corruption risk and make the Bond intro bug measurable. The second sprint should take on R2 and R3 because they are likely to remove the remaining floating/shard-class failures but need more route validation.
