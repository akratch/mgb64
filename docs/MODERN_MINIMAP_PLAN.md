# Modern Minimap: Design, Architecture, and Game Hooks

> Status: **design / not yet implemented.** This is a code-grounded survey for a
> sharp, modern minimap with full level layout, objective pins, player heading,
> and temporary enemy guard reveals when guards fire. Code references were traced
> against the current tree; line numbers are approximate and will drift.

---

## 0. Executive Summary

The feature is feasible, but it should be implemented as a new native minimap
system rather than extending the existing multiplayer radar.

The existing radar in `src/game/radar.c:18` is a small multiplayer-only blip
renderer. It draws a textured radar disc and simple fill-rectangle dots from the
HUD display-list path, using only player-relative positions. That is useful as a
reference for player pose, viewport placement, and HUD order, but it is too
limited for a full map layout, objective markers, high-DPI anti-aliased shapes,
or modern icon treatment.

Recommended architecture:

1. Add a game-side minimap collector/cache in `src/game/minimap.c`.
2. Build a stage map cache from STAN collision geometry.
3. Resolve objective pins from objective records, live props, and setup pads.
4. Reveal enemy guards through guard firing events, not permanent all-guard
   polling.
5. Queue one minimap snapshot per active HUD pane during the existing HUD pass.
6. Render the queued snapshots with a native overlay after the PC renderer's
   retro output filter, before buffer swap, so the minimap stays crisp.

This gives the "modern" result: accurate map geometry, clean icons, smooth
alpha fades, sharp strokes, high-DPI scaling, and split-screen correctness.

---

## 1. Player-Facing Target

The target is a COD-like tactical minimap adapted to GoldenEye's mission design:

- **Full map layout**: the whole discovered/configured stage layout is visible
  inside the minimap by default, not only a tiny local radar circle.
- **Player marker**: a clear directional arrow based on `g_CurrentPlayer->vv_theta`.
- **Objective pins**: incomplete mission objectives appear at their live object,
  pad, or room location.
- **Enemy fire reveals**: guards appear as red pings for a short TTL when they
  fire an audible, unsuppressed weapon.
- **Current context**: current room/floor is highlighted; other floors are dimmed
  to handle overlapping vertical layouts.
- **Sharp overlay**: modern HUD-quality vector rendering, not N64-style filled
  rectangles unless a fallback path is needed.

Default recommendation:

- **Mode**: north-up full-stage fit.
- **Player heading**: arrow rotates, map does not.
- **Enemy layer**: reveal-on-fire only.
- **Objective layer**: incomplete objectives only.
- **Feature gate**: on by default if the port's remaster HUD defaults are meant
  to be modern, otherwise behind `Input.MinimapEnabled`.

Optional later mode:

- **Rotating local minimap**: player-centered, COD-style, clipped to local radius.
  This should be a secondary setting because the user specifically asked for the
  full map layout to be present.

---

## 2. Existing Radar and HUD Hook Survey

### Existing multiplayer radar

`src/game/radar.c:18` defines `display_red_blue_on_radar(Gfx *DL)`.

Important behavior:

- Returns immediately in single-player.
- Returns while the multiplayer menu is open or the player is dead.
- Honors `CHEAT_NO_RADAR_MP`.
- Draws near the top-right of the active player viewport using:
  - `viGetViewLeft()`
  - `viGetViewTop()`
  - `viGetViewWidth()`
  - `viGetViewHeight()`
- Uses `g_CurrentPlayer->prop->pos` and `g_CurrentPlayer->vv_theta`.
- Uses a fixed range/radius model, not real map geometry.
- Draws dots using `microcode_constructor_related_to_menus`.

This is not the right renderer for the modern minimap, but it proves the core
per-pane inputs are already available.

### HUD display order

`src/game/bondview.c:20285` defines `maybe_mp_interface(Gfx *arg0)`, the key HUD
path. Around `src/game/bondview.c:20458-20478`, the draw order is:

1. `gunDrawSight(&arg0)`
2. native hit marker
3. ammo total
4. countdown timer
5. `display_red_blue_on_radar(arg0)`
6. `currentPlayerDrawFade(arg0)`

For a display-list fallback, a minimap call could sit where radar is today:

```c
arg0 = display_modern_minimap(arg0);
```

However, for the preferred native overlay, this HUD location should instead
queue a `MinimapFrame` snapshot for the current pane. The actual drawing should
happen later in the platform renderer.

### Split-screen requirement

The render loop in `src/game/lvl.c:1566-1880` iterates players, calls
`set_cur_player(...)`, sets that player's viewport/FOV/aspect, renders the world,
then renders HUD/watch UI. Because `g_CurrentPlayer` changes per pane, the
minimap collector must run during the per-pane HUD pass, while viewport and
player state are still correct.

Native overlay rendering after the frame must **not** try to infer the current
player. It should only draw the snapshots queued earlier.

---

## 3. Renderer Architecture

### Recommended path: native overlay after output filter

The PC renderer path is in `src/platform/fast3d/gfx_pc.c`.

`gfx_run_dl`:

- starts the backend frame with `gfx_rapi->start_frame()` around
  `src/platform/fast3d/gfx_pc.c:22799`
- executes the display list
- flushes pending drawing
- calls `gfx_rapi->end_frame()` around `src/platform/fast3d/gfx_pc.c:22838`

The OpenGL backend resolves the scene and applies the output VI/retro filter in
`src/platform/fast3d/gfx_opengl.c:3271`:

```c
static void gfx_opengl_end_frame(void) {
    gfx_opengl_resolve_scene_target();
    gfx_opengl_apply_output_vi_filter();
}
```

The final swap happens in `gfx_end_frame()` at
`src/platform/fast3d/gfx_pc.c:23545`.

Preferred hook:

```c
gfx_rapi->end_frame();
minimap_overlay_draw_queued_frames();
```

or, less ideally:

```c
void gfx_end_frame(void) {
    minimap_overlay_draw_queued_frames();
    SDL_GL_SwapWindow(g_sdlWindow);
}
```

Drawing after `gfx_opengl_apply_output_vi_filter()` keeps the minimap sharp and
modern while the game world keeps its retro presentation.

### Why not pure display-list HUD

The existing UI helpers in `src/game/textrelated.c` are optimized for N64-style
rectangles and textured quads:

- `microcode_constructor(...)`
- `microcode_constructor_related_to_menus(...)`
- `combiner_bayer_lod_perspective(...)`

They are acceptable for a quick debug overlay or fallback. They are not ideal
for:

- anti-aliased clipped polygons
- rounded/circular clipping masks
- high-DPI icon geometry
- smooth alpha fades
- stroke joins
- crisp text or symbol rendering
- map polygon batching

### Renderer boundary

Keep game logic out of `gfx_opengl.c`.

Recommended separation:

- `src/game/minimap.c`: builds cache, resolves pins, tracks enemy reveal events,
  queues frame snapshots.
- `src/platform/minimap_overlay.c`: consumes queued snapshots and draws 2D
  shapes through OpenGL or a small immediate overlay API.

The platform renderer should not walk `objective_ptrs`, `g_ChrSlots`, STAN, or
`g_CurrentPlayer` directly.

---

## 4. World Geometry Source

### Use STAN, not visual room meshes

The full map layout should be built from STAN collision tiles. STAN gives the
walkable/collidable floor plan, with room IDs and stable top-down polygon data.
Visual room meshes are harder to classify, contain decorative surfaces, and are
loaded/rendered through visibility rules that are not the same as a tactical map.

Relevant types:

- `StandTilePoint` in `src/bondtypes.h:387`
- `StandTile` in `src/bondtypes.h:412`
- STAN macros in `src/game/stan.h`

Relevant globals/helpers:

- `standTileStart` in `src/game/stan.h:39`
- `firststaninroom[]` and `stan_room_bbox[][]` in `src/game/stan.c:45`
- `list_of_tilesizes[]` in `src/game/stan.c:120`
- `stanGetPositionYValue(...)` in `src/game/stan.c:6186`
- `getTileRoom(...)` in `src/game/stan.c:6844`

### Iteration pattern

`src/game/stan.c:348` already has the clean native walk used to compute room
bounds:

```c
StandTile *tile = stan_prefix.ptr_firstroom;
while (*(s32 *)tile != 0) {
    s32 pointCount = (tile->tail.half >> 12) & 0xF;
    ...
    tile = (StandTile *)((u8 *)tile + list_of_tilesizes[pointCount]);
}
```

The minimap cache should use the same walk.

### Coordinate scale

STAN tile points are stored in scaled integer coordinates. Live player/prop/pad
positions are world coordinates.

`setLevelScale` in `src/game/stan.c:6163` stores:

```c
level_scale = ls;
inv_level_scale = 1.0f / ls;
```

Many STAN helpers convert tile points to world space using `inv_level_scale`, for
example around `src/game/stan.c:1983` and `src/game/stan.c:4695`.

Therefore the minimap cache should store:

```c
world_x = tile->points[i].x * inv_level_scale;
world_y = tile->points[i].y * inv_level_scale;
world_z = tile->points[i].z * inv_level_scale;
```

Important lifecycle detail:

- `stanLoadFile(...)` calls `setLevelScale(1.0)`.
- `load_bg_file(...)` later calls `setLevelScale(levelinfotable[...].levelscale)`
  at `src/game/bg.c:3616`.

So do not build the final minimap geometry cache inside `stanLoadFile`. Build it
after `load_bg_file` applies the final level scale.

### Cache content

Recommended cache structure:

```c
#define MINIMAP_MAX_POLY_POINTS 12

typedef struct MinimapPoly {
    u16 tile_id;
    u8 room;
    u8 point_count;
    f32 y_avg;
    f32 x_min, z_min;
    f32 x_max, z_max;
    f32 x[MINIMAP_MAX_POLY_POINTS];
    f32 z[MINIMAP_MAX_POLY_POINTS];
} MinimapPoly;

typedef struct MinimapRoomInfo {
    u16 first_poly;
    u16 poly_count;
    f32 x_min, z_min;
    f32 x_max, z_max;
    f32 y_min, y_max;
} MinimapRoomInfo;

typedef struct MinimapLevelCache {
    MinimapPoly *polys;
    u32 poly_count;
    MinimapRoomInfo rooms[256];
    f32 x_min, z_min;
    f32 x_max, z_max;
    f32 y_min, y_max;
    u32 build_stage;
    u32 build_serial;
} MinimapLevelCache;
```

Use stage lifetime storage or native heap with a clear/free on stage reset. Do
not allocate per frame.

### Polygon drawing

Initial draw can render each STAN floor tile individually. STAN floor polygons
are already close to the tactical layout. Later polish can merge or simplify
adjacent coplanar polygons by room, but merging should not be required for an
accurate MVP.

For native rendering:

- triangulate polygons into a cached VBO, likely fan triangulation initially
- store per-room color/index metadata
- update only transform uniforms per frame
- draw selected room/floor brighter, other rooms dimmer

For display-list fallback:

- draw only outlines or coarse room rectangles
- this is useful for validation, not the final modern look

---

## 5. Objective Pin Source

### Objective state

Objective setup starts with `something_with_stage_objectives()` in
`src/game/objective.c:13`, which clears objective state and linked criteria
lists.

Objective entries are registered by `add_ptr_to_objective(...)` in
`src/game/objective.c:62`.

`proplvreset2(...)` in `src/game/prop.c:2399` converts setup data, walks
propdefs, and registers:

- watch briefing text around `src/game/prop.c:3438`
- objective entries around `src/game/prop.c:3451`
- subobjective/criteria entries in the same spawn pass

Objective status is evaluated by `get_status_of_objective(...)` in
`src/game/objective_status.c:255`.

### Types and records

The base objective record is `MissionObjectiveRecord` in
`src/bondtypes.h:3420`. It includes:

- type
- `ObjRefID`
- `TextID`
- `MinDificulty`
- `nextentry`

The objective entry wrapper is in `src/bondtypes.h:3927`.

Room/deposit/photo criteria structs are in `src/bondtypes.h:4161`:

- `criteria_roomentered`
- `criteria_deposit`
- `criteria_picture`

### Object-based objective pins

For objectives that reference an object tag:

- destroy object
- collect object
- deposit object
- photograph object
- possibly copy-item adjacent cases

Resolver:

1. Use `objFindByTagId(...)` from `src/game/objective_status.c:123`.
2. If the object has a live `obj->prop`, use `obj->prop->pos` and
   `obj->prop->stan`.
3. If no live prop is present, fall back to setup pad location.

Trace-only precedent exists in `src/platform/port_trace.c:2781` with
`traceMissionGateResolveObjectPos(...)`. That logic should be moved or mirrored
into a non-trace helper rather than called directly from minimap code.

Recommended helper:

```c
bool minimap_resolve_object_record_pos(
    const ObjectRecord *obj,
    coord3d *out_pos,
    s32 *out_room);
```

Resolution rules:

- doors use `g_CurrentSetup.boundpads[door->pad]`
- normal objects use `g_CurrentSetup.pads[obj->pad]`
- bound objects use `g_CurrentSetup.boundpads[getBoundPadNum(obj->pad)]`
- live `obj->prop` wins over setup pad if available

### Room objective pins

Room objectives are checked by:

- `objectivestatusCheckRoomEntered(...)` in `src/game/objective_status.c:491`
- `objectivestatusCheckDeposit(...)` in `src/game/objective_status.c:530`

Both resolve a pad or bound pad, then compare its STAN room to the entered room.

The minimap should use the same resolution:

```c
PadRecord *pad = isNotBoundPad(padid)
    ? &g_CurrentSetup.pads[padid]
    : (PadRecord *)&g_CurrentSetup.boundpads[getBoundPadNum(padid)];
```

Pin placement options:

1. place the pin at `pad->pos`
2. highlight all cached polygons in `pad->stan->room`
3. place a room-center pin using the cached room bounds

Recommendation:

- Use `pad->pos` for the icon.
- Also softly highlight the target room when the objective type is room-based.

### Objective filtering

Default filtering:

- skip null `objective_ptrs[i]`
- skip objectives above selected difficulty
- skip completed objectives
- style failed objectives separately or hide them depending on setting

Use existing status functions:

- `objectiveGetCount()`
- `get_difficulty_for_objective(i)`
- `get_status_of_objective(i)`

Do not parse localized objective text to infer locations. Use objective criteria
and object/pad data.

---

## 6. Enemy Guard Reveal Source

### Guard data

Relevant structures:

- `PropRecord` in `src/bondtypes.h:2273`
- `ChrRecord` in `src/bondtypes.h:2375`

Important fields:

- `ChrRecord::chrnum`
- `ChrRecord::firecount[2]`
- `ChrRecord::hidden`
- `ChrRecord::chrflags`
- `ChrRecord::prop`
- `ChrRecord::model`
- `ChrRecord::damage`
- `ChrRecord::maxdamage`
- `ChrRecord::alertness`
- `PropRecord::pos`
- `PropRecord::stan`
- `PropRecord::type`

Guard slots:

- `g_ChrSlots`
- `g_NumChrSlots`
- `get_numguards()` in `src/game/chr.c:1286`

Active guards:

- `g_ActiveChrs`
- `g_ActiveChrsCount`
- ticked by `chrlvAllChrTick()` in `src/game/chrlv.c:11458`

For all-guard debug views, iterate `g_ChrSlots`. For reveal-on-fire gameplay,
prefer event hooks.

### Fire event hook

The actual guard firing path is `chrlvFireWeaponRelated(...)` in
`src/game/chrlv.c:8204`.

Important points:

- It increments `self->firecount[hand]` around `src/game/chrlv.c:8273`.
- It validates muzzle/line state and may decrement firecount if invalid.
- It handles projectiles, hits, and beam/tracer length.
- It computes `play_sound = (sp27C != 0) || (sp278 != 0)`.
- It calls `sub_GAME_7F02BFE4(self, hand, play_sound)` around
  `src/game/chrlv.c:8613`.
- It calls `chrSetFiring(self, hand, sp27C)` around `src/game/chrlv.c:8616`.

Recommended minimap hook:

```c
if (sp27C != 0 || sp278 != 0) {
    minimap_note_guard_fired(
        self,
        hand,
        prop_selfchr->act_attack.attack_item,
        play_sound != 0);
}
```

Place it after `play_sound` is computed and before or after
`sub_GAME_7F02BFE4(...)`. This captures real firing attempts after the function's
main validation has run.

### Audible and suppressed weapons

`sub_GAME_7F02BFE4(...)` in `src/game/chrlv.c:7044` handles weapon sound
triggering with:

- `bondwalkItemGetSoundTriggerRate(...)`
- `bondwalkItemGetSound(...)`
- `sndPlaySfx(...)`
- `chrobjSndCreatePostEventDefault(...)`

For COD-like minimap behavior, a guard should reveal when they fire an audible,
unsuppressed weapon.

Suppress by default:

- `ITEM_WPPKSIL`
- `ITEM_MP5KSIL`

Those IDs are in `src/bondconstants.h:3349` and `src/bondconstants.h:3355`.

Recommended policy:

- unsuppressed audible shot: reveal
- suppressed shot: no reveal
- melee/no-sound/projectile edge cases: no reveal unless explicitly configured
- debug setting can force reveal all fire events

### Reveal table

Use a small fixed-size table keyed by `chrnum`, with a slot fallback by pointer
or index if needed.

```c
typedef struct MinimapEnemyReveal {
    s16 chrnum;
    s16 slot;
    f32 x, y, z;
    s16 room;
    s16 ttl60;
    s16 ttl60_max;
    u8 suppressed;
    u8 active;
} MinimapEnemyReveal;
```

Recommended defaults:

- TTL: 180 to 240 ticks, roughly 3 to 4 seconds at 60 Hz
- fade: alpha = `ttl60 / ttl60_max`
- position: update from live `chr->prop->pos` while active
- if guard dies: fade faster or remove immediately
- if prop disappears: keep last known position until TTL expires

Do not poll only `firecount[]` as the primary implementation. It is useful for
diagnostics, but an event hook is more accurate and avoids double-counting or
missing suppressed/invalid fire.

---

## 7. Player and Viewport Inputs

Player position:

```c
g_CurrentPlayer->prop->pos
```

Player STAN/current room:

```c
g_CurrentPlayer->prop->stan
g_CurrentPlayer->curRoomIndex
```

Player heading:

```c
g_CurrentPlayer->vv_theta
```

Viewport:

```c
viGetViewLeft()
viGetViewTop()
viGetViewWidth()
viGetViewHeight()
```

The existing multiplayer radar uses `prop->pos` and `vv_theta`, so these values
are already proven usable for minimap positioning. For native overlay, capture
them into the queued snapshot during the HUD pass.

---

## 8. Game-Side API Proposal

Add:

- `src/game/minimap.h`
- `src/game/minimap.c`

Public API:

```c
void minimap_stage_reset(void);
void minimap_build_level_cache(void);
void minimap_setup_ready(void);
void minimap_tick(void);

void minimap_note_guard_fired(
    ChrRecord *chr,
    s32 hand,
    s32 itemid,
    s32 audible);

void minimap_queue_current_player_snapshot(void);

const MinimapFrameQueue *minimap_get_frame_queue(void);
void minimap_clear_frame_queue(void);
```

Lifecycle:

1. `minimap_stage_reset()` during level reset before setup load.
2. `minimap_build_level_cache()` after `load_bg_file(...)` applies final
   `setLevelScale(...)`.
3. `minimap_setup_ready()` after `proplvreset2(...)` has converted pads,
   propdefs, objective entries, and criteria lists.
4. `minimap_tick()` once per gameplay frame to decrement reveal TTLs.
5. `minimap_queue_current_player_snapshot()` during each HUD pane.
6. platform overlay renders and clears the queue after the frame.

Potential hook points:

- `src/game/lvl.c:665`: `load_bg_file(g_CurrentStageToLoad)`
- `src/game/lvl.c:732`: objective state reset
- `src/game/lvl.c:742`: `proplvreset2(stage)`
- `src/game/lvl.c:756`: `init_path_table_links()`
- `src/game/bondview.c:20477`: existing radar HUD location
- `src/game/chrlv.c:8613`: guard fire event hook

Because `load_bg_file(...)` and `proplvreset2(...)` are distinct, geometry and
objective/pad readiness should be treated as separate readiness flags.

---

## 9. Snapshot Model

The renderer should consume immutable per-frame snapshots, not live game globals.

```c
#define MINIMAP_MAX_OBJECTIVE_PINS 16
#define MINIMAP_MAX_ENEMY_PINS 64
#define MINIMAP_MAX_SNAPSHOTS 4

typedef enum MinimapPinKind {
    MINIMAP_PIN_OBJECTIVE,
    MINIMAP_PIN_ENEMY_FIRE,
    MINIMAP_PIN_PLAYER,
} MinimapPinKind;

typedef struct MinimapPin {
    MinimapPinKind kind;
    f32 x, y, z;
    s16 room;
    u8 status;
    u8 alpha;
    u8 icon;
    u8 flags;
} MinimapPin;

typedef struct MinimapFrame {
    s32 player_num;
    s32 view_left;
    s32 view_top;
    s32 view_width;
    s32 view_height;

    f32 player_x;
    f32 player_y;
    f32 player_z;
    f32 player_theta_deg;
    s16 player_room;

    u8 enabled;
    u8 mode;
    u8 objective_count;
    u8 enemy_count;

    MinimapPin objectives[MINIMAP_MAX_OBJECTIVE_PINS];
    MinimapPin enemies[MINIMAP_MAX_ENEMY_PINS];
} MinimapFrame;
```

The level geometry cache can be shared by all snapshots. Pins are copied into
each snapshot because they can be player-specific once visibility, difficulty,
split-screen, or mission-state rules are applied.

---

## 10. Map Projection and Visual Rules

### Full-stage north-up projection

Use the cached stage bbox:

```c
map_w = cache.x_max - cache.x_min;
map_h = cache.z_max - cache.z_min;
scale = min((rect_w - padding * 2) / map_w,
            (rect_h - padding * 2) / map_h);

screen_x = rect_cx + (world_x - bbox_cx) * scale;
screen_y = rect_cy - (world_z - bbox_cz) * scale;
```

The negative Z mapping keeps north/up visually intuitive once validated against
player heading. If the first visual test shows the map mirrored, flip the sign
once in the projection layer, not in cached geometry.

### Player arrow

For north-up mode:

- position from `g_CurrentPlayer->prop->pos`
- rotation from `g_CurrentPlayer->vv_theta`
- validate angle by comparing forward movement and arrow direction

For rotating local mode:

- center player
- rotate all map points by `-vv_theta`
- arrow can stay upright

### Floor and room handling

Many GoldenEye maps overlap vertically. A full top-down projection will stack
floors unless the renderer communicates context.

Recommended MVP:

- bright: polygons in current room
- medium: rooms connected/nearby by Y range
- dim: all other rooms/floors
- objective room: accent fill or outline

Y grouping:

- store per-poly `y_avg`, room `y_min/y_max`
- compare against player Y and current room
- do not hide other floors by default because the user asked for full layout

### Dynamic map features

MVP should not attempt fully dynamic geometry. Doors and moving objects are pins
or overlays, not baked geometry.

Later possible additions:

- draw door bound pads from `DoorRecord`/`BoundPadRecord`
- dim locked/closed routes
- draw alarm/camera/autogun icons
- reveal rooms progressively if a discovery system is desired

---

## 11. Settings

Settings should follow the existing pattern in `src/platform/platform_sdl.c`,
near the modern input/HUD settings around `src/platform/platform_sdl.c:1662`.

Existing modern globals:

- `g_pcModernCrosshair`
- `g_pcHitMarkers`
- `g_pcReticleTargetFeedback`
- ADS settings

Suggested globals:

```c
s32 g_pcMinimapEnabled = 1;
s32 g_pcMinimapMode = 0;              /* 0 full north-up, 1 local rotating */
s32 g_pcMinimapObjectives = 1;
s32 g_pcMinimapEnemyFireReveal = 1;
s32 g_pcMinimapShowAllEnemies = 0;    /* debug/accessibility assist */
f32 g_pcMinimapOpacity = 0.85f;
f32 g_pcMinimapSize = 1.0f;
s32 g_pcMinimapSharpOverlay = 1;      /* 0 display-list fallback */
```

Suggested config keys:

| Key | Type / default | Range | Scope |
|---|---:|---:|---|
| `Input.MinimapEnabled` | int, 1 | 0..1 | live |
| `Input.MinimapMode` | int, 0 | 0..1 | live |
| `Input.MinimapObjectives` | int, 1 | 0..1 | live |
| `Input.MinimapEnemyFireReveal` | int, 1 | 0..1 | live |
| `Input.MinimapShowAllEnemies` | int, 0 | 0..1 | live |
| `Input.MinimapOpacity` | float, 0.85 | 0.0..1.0 | live |
| `Input.MinimapSize` | float, 1.0 | 0.5..2.0 | live |
| `Input.MinimapSharpOverlay` | int, 1 | 0..1 | live |

The exact category could be `Hud.*` instead of `Input.*`. Existing modern HUD
settings currently live under `Input.*`, so using `Input.Minimap*` keeps local
style consistent.

---

## 12. Build Integration

`CMakeLists.txt:275` uses:

```cmake
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
```

So adding `src/game/minimap.c` should enter the CMake target automatically.

Platform overlay files may need explicit integration. `CMakeLists.txt:276`
globs `src/platform/*.c`, but `src/platform/fast3d/gfx_opengl.c` is listed
explicitly around `CMakeLists.txt:287`. If the overlay lives at
`src/platform/minimap_overlay.c`, it should be picked up by the platform glob. If
it lives under `src/platform/fast3d/`, add it explicitly beside the other fast3d
backend files.

---

## 13. Validation Plan

### Geometry validation

Add a debug output mode:

```text
GE007_MINIMAP_DUMP=/path/to/minimap_stage.json
```

Dump:

- stage ID
- level scale
- STAN polygon count
- bbox
- room count
- room bboxes
- player position
- player current room
- objective pins
- active enemy reveals

Compare against existing trace patterns in `src/platform/port_trace.c`.

Visual checks:

1. Draw the player's current STAN tile highlighted.
2. Draw the player marker at `prop->pos`.
3. Move through Dam/Facility/Surface/Train/Control and confirm marker stays on
   walkable polygons.
4. Confirm objective pins land at known objects/pads.
5. Confirm enemy pings appear only on guard fire and fade out.

### Stage smoke list

Use stages that stress different aspects:

- Dam: simple outdoor/indoor route
- Facility: overlapping rooms and objective objects
- Surface I/II: large outdoor footprint
- Bunker: dense interior rooms
- Train: long thin map
- Control: vertical/overlapping spaces
- Frigate: multi-room objective/pad cases

### Regression checks

- Minimap disabled: no queued snapshots, no overlay draw, no behavior changes.
- Single-player: objective and enemy reveal layers work.
- Multiplayer: either disabled by default or one snapshot per split-screen pane.
- Death/menu/watch states: minimap should hide or dim consistently with HUD.
- Retro filter on/off: minimap remains sharp when `SharpOverlay=1`.
- Screenshot capture: decide whether screenshots should include overlay. If yes,
  draw before screenshot capture; if no, draw after.

Current `gfx_run_dl` screenshot capture occurs before `gfx_rapi->end_frame()`.
If overlay screenshots matter, move or duplicate capture after overlay draw.

---

## 14. Risks and Edge Cases

### Scale mismatch

Highest-risk bug. STAN points and prop positions live in different coordinate
spaces until `inv_level_scale` is applied. Build the map cache after final
`setLevelScale(...)` and verify by drawing the current tile under the player.

### Multi-floor overlap

Full top-down maps overlap vertically. Use current-room/floor highlighting and
dim other floors instead of hiding them entirely.

### Objective lifecycle

Objects can move, be collected, destroyed, or disappear. Resolve pins from live
props first, then setup pads. Every resolver must tolerate null prop/object/pad
state.

### Split-screen timing

Native overlay rendering happens after display-list execution, when
`g_CurrentPlayer` is no longer a reliable source for every pane. Queue snapshots
during each pane's HUD pass.

### Fade and menu ordering

If the overlay draws after `currentPlayerDrawFade(...)`, it may appear above
death fades or screen fades. Options:

1. accept modern HUD above fade
2. include fade alpha in each snapshot and apply it in overlay
3. draw minimap through display list when fade ordering must match vanilla

Recommendation: apply the current player's fade alpha to the overlay snapshot so
it respects death/transition fades while staying post-filter sharp.

### Gameplay spoilers

Full map and objective pins make missions easier. Keep layers separately
toggleable:

- map layout
- objectives
- enemy fire reveal
- all-enemy assist/debug

Do not enable permanent all-guard visibility by default.

### Determinism

The minimap should be read-only with respect to gameplay. It must not add RNG
draws, alter AI, alter objective state, or change prop/room visibility. Guard
fire hooks only record events into port-local minimap state.

---

## 15. Implementation Milestones

### MM-0: Scaffolding and feature gate

- Add `src/game/minimap.h`
- Add `src/game/minimap.c`
- Add setting globals and config registration
- Add no-op reset/tick/snapshot queue
- Add HUD queue call where radar is currently called

Acceptance:

- disabled state is a no-op
- builds cleanly
- no rendering yet

### MM-1: STAN geometry cache

- Walk STAN tiles after final level scale
- Convert tile points to world X/Z using `inv_level_scale`
- Build stage bbox and room metadata
- Add optional JSON/debug dump

Acceptance:

- nonzero polygon count on real stages
- sane bbox for Dam/Facility/Surface
- no per-frame allocations

### MM-2: Display-list or debug overlay

- Draw simple unfiltered or display-list map outlines
- Draw player marker
- Highlight current STAN tile/room

Acceptance:

- player marker aligns with walkable layout
- heading is not mirrored
- split-screen snapshots queue separately

### MM-3: Native sharp overlay

- Add platform overlay renderer
- Draw after output VI filter and before swap
- Render filled polygons, outlines, player arrow
- Support high-DPI scaling and per-pane clipping

Acceptance:

- minimap stays sharp with retro filter enabled
- no OpenGL state leaks into game rendering
- works at multiple window sizes

### MM-4: Objective pins

- Resolve object objectives from tag/live prop/setup pad
- Resolve room/deposit objectives from criteria pad/STAN room
- Filter by difficulty and completion status
- Draw objective icons and optional room highlight

Acceptance:

- objective pins appear on incomplete objectives
- completed objectives hide or change style
- missing object/prop state does not crash

### MM-5: Enemy fire reveals

- Hook guard fire event in `chrlvFireWeaponRelated`
- Track reveal TTL by `chrnum`
- Suppress silenced weapons by default
- Draw fading red pings

Acceptance:

- firing unsuppressed guards appear briefly
- silenced guard weapons do not reveal by default
- dead/removed guards are cleaned up safely

### MM-6: Polish and options

- Add size/opacity/mode settings
- Add north-up vs local-rotating mode
- Add objective/enemy layer toggles
- Add fade/menu/death behavior
- Add screenshot/test capture behavior

Acceptance:

- user-facing defaults are coherent
- feature can be disabled completely
- no material frame-time cost on disabled path

---

## 16. Minimal Hook Checklist

Geometry:

- `src/game/bg.c:3529` `load_bg_file(...)`
- `src/game/bg.c:3616` final `setLevelScale(...)`
- `src/game/stan.c:348` STAN tile walk pattern

Setup/objectives:

- `src/game/lvl.c:732` objective reset
- `src/game/lvl.c:742` setup load via `proplvreset2(stage)`
- `src/game/prop.c:3451` objective registration
- `src/game/objective_status.c:255` objective status
- `src/game/objective_status.c:491` room objective criteria
- `src/game/objective_status.c:530` deposit objective criteria

HUD/snapshots:

- `src/game/bondview.c:20477` existing radar call site
- `src/game/radar.c:18` old radar reference behavior

Enemies:

- `src/game/chrlv.c:8204` guard fire function
- `src/game/chrlv.c:8613` sound/firing call site
- `src/game/chrlv.c:8616` final firing state
- `src/game/chr.c:1286` guard count

Renderer:

- `src/platform/fast3d/gfx_pc.c:22838` backend end-frame
- `src/platform/fast3d/gfx_opengl.c:3271` output filter
- `src/platform/fast3d/gfx_pc.c:23545` buffer swap

Settings:

- `src/platform/platform_sdl.c:1010` modern setting globals
- `src/platform/platform_sdl.c:1662` modern setting registration block

---

## 17. Open Decisions

1. **Default enabled or opt-in?**
   Existing remaster-style HUD features default on, while ADS defaults off. The
   minimap changes mission information more than crosshair/hit markers, so a
   reasonable default is `Input.MinimapEnabled=1` but objective/enemy layers
   separately configurable. A purist preset should disable it.

2. **Should undiscovered rooms be hidden?**
   The user asked for full map layout present fully. Default should show the full
   layout. A later "discovery" mode could reveal rooms as visited.

3. **Should enemy pings require audible sound or only weapon fire?**
   COD-like behavior suggests audible unsuppressed fire. Use `audible` and
   suppressor checks by default, with debug override.

4. **Should the minimap appear in multiplayer?**
   Existing radar is multiplayer-only. This feature is primarily single-player
   mission modernization. For multiplayer, either keep old radar or use the same
   snapshot system with objective/enemy layers disabled.

5. **Should screenshots include the overlay?**
   Current screenshot capture happens before backend `end_frame`. If minimap
   screenshots matter, move capture after overlay draw.

---

## 18. Final Recommendation

Implement this as a port-local, snapshot-driven native overlay. Use STAN for
layout, objective records/pads for objectives, and guard fire events for enemy
reveals. Queue snapshots during the existing per-player HUD pass, then draw them
after the OpenGL output filter for a sharp modern presentation.

This approach is accurate because it uses the same collision, pad, objective,
and guard systems that gameplay already uses. It is sharp because it avoids the
N64 display-list HUD path for the final overlay. It is safe because the renderer
only consumes immutable snapshots and the gameplay hooks only record minimap
state.
