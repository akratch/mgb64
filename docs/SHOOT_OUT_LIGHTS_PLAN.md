<!--
  Provenance: scoped 2026-06-28 via an independent 6-agent investigation workflow
  (D1 table-population, D2 hit-record, D3 room geometry/segments, D4 F3D bit layout,
  D5 draw-time visibility, D6 flag/build conventions) + synthesis, then spot-verified
  by hand: bg.c:7987-7995 (native loader skips population — D1 blocker CONFIRMED),
  include/PR/gbi.h + include/gbi_extension.h (G_VTX=4 / G_TRI1=0xBF / G_TRI4=0xB1 CONFIRMED),
  bg.c:188 bgReadBe32 + bg.c:10130-10254 collision-DL walker (decode idiom + the
  vtx_base_offset subtraction behind Risk #1 CONFIRMED).
  Status: PLAN ONLY — not yet implemented. Branch: feat/dam-hd-remaster.
-->

# Shoot-Out-The-Lights — Native Port Implementation Plan

## 1. Summary

"Shoot out the lights" is the original GoldenEye 007 behavior where firing at a ceiling/wall light fixture (lamps, neon, panel lights) **darkens that surface** (and its neighbouring triangles within a radius) and **spawns glass/debris particles** along the broken triangle edges. The darkening persists across room reloads. The player-facing effect: shoot a lamp, the lit surface visibly dims (its vertex shade is divided by 4), shards fly, and the room gets darker.

The detection plumbing, the persistence tables, the per-vertex darkening, and the particle/shard system are **all already ported and live on native**. Only **three** functions in `src/game/lightfixture.c` are stubbed under `#ifdef NATIVE_PORT`:

- `return_ptr_vertex_of_entry_room` (`lightfixture.c:111`) → returns `NULL`
- `extract_vertex_indices_from_triangle` (`lightfixture.c:136`) → zeroes outputs
- `sub_GAME_7F0BBE0C` (`lightfixture.c:341`) → empty body (this is the live parent called from `chrprop.c:3069`)

The correct N64 `#else` reference bodies exist (`lightfixture.c:117-131`, `142-172`, `346-491`) but are inert and **cannot be copied verbatim** — they use N64 big-endian/32-bit `Gfx`-union accessors that do not exist on the LE 64-bit native build.

**Verdict: NOT a clean three-function port.** Implementing the three stubs is *necessary but not sufficient*. There is one **MAJOR blocker**: on native, `light_fixture_table` is **never populated for bg-room geometry**, so a fully-ported `sub_GAME_7F0BBE0C` iterates an empty table and the feature is **silently invisible**. A native room-load population pass must be added alongside the three stubs. Treat all four pieces as **one combined work item**.

---

## 2. Feasibility & blockers

### MAJOR — D1: `light_fixture_table` is never populated on native (feature is a guaranteed no-op without extra work)

**What it is.** `sub_GAME_7F0BBE0C` only acts on entries in `light_fixture_table[]`. Each entry is created by `add_entry_to_init_lightfixture_table` / `save_ptrDL_enpoint_to_current_init_lightfixture_table` (`lightfixture.c:67-84`), whose **only** callers are inside `texLoadFromGdl` (`unk_0CC4C0.c:1615, 1752, 1810`). The room index stamped onto entries comes solely from `clear_light_fixturetable_in_room` (`lightfixture.c:495-505`, sets `index_of_cur_entry_lightfixture_table = room_index` at :505).

**Why it matters.** The native bg room-DL loaders **skip both calls**:
- Primary loader `sub_GAME_7F0B609C`: `#ifdef NATIVE_PORT` returns the raw N64 DL ("PC: skip texture processing", `bg.c:7986-7992`); `clear_light_fixturetable_in_room(roomID)` + `texLoadFromGdl(...)` live only in the `#else` (`bg.c:7996-8006`).
- Secondary loader `sub_GAME_7F0B61DC`: identical native skip; `texLoadFromGdl` only in `#else` (`bg.c:8220-8239`).

So every `light_fixture_table[i].room_index` stays `0` (from `init_lightfixture_tables`, `lightfixture.c:34-49`, invoked `bg.c:3554`). The guards at `lightfixture.c:378` (room match) and `:380-381` (gfx in `[ptr_start, ptr_end)`) **never pass**. Even a perfectly-ported parent is a no-op, and `redarken_lights_in_room` (`bg.c:8587`, `lvl.c:275`) re-darkens an empty table.

> The object-model loader (`objecthandler_2.c:82`) *does* call `texLoadFromGdl` on native, but that populates entries for **prop geometry** pointing into `texLoadFromGdl`'s rewritten `dst` — not the bg-room fixtures the shoot path (`bgPopulateHitRecord`) targets, and not the raw room DL `tricmd` points into. It does not help here.

**Mitigation (required additional work).** Add a native room-load population pass that fills `light_fixture_table` from the **same raw N64 DL the renderer and hit-test use** (`g_BgRoomInfo[room].ptr_expanded_mapping_info`). In the native room-load wrappers (`lvl.c` ~223-276, `bg.c` ~8522-8587), after `gfx_register_n64_dl_region` and around `redarken_lights_in_room(room)`:
1. `clear_light_fixturetable_in_room(room)` — resets stale entries **and** sets `index_of_cur_entry_lightfixture_table = room`.
2. Walk the raw primary (and secondary) room DL byte-wise; for each texture-select command, extract `texturenum = (w1 & 0xFFF)`; when `check_if_imageID_is_light(texturenum)` is true (`lightfixture.c:87-107`), call `add_entry_to_init_lightfixture_table(<ptr into RAW DL>)` at the start of the light's triangle run and `save_ptrDL_enpoint_to_current_init_lightfixture_table(<ptr into RAW DL>)` at the run end — mirroring `texLoadFromGdl`'s `isLight` bookkeeping (`unk_0CC4C0.c:1609-1617, 1750-1753, 1809-1811`) but storing **pointers into the raw DL, not a rewritten `dst`**.

**Critical gotcha:** the stored `ptr_start`/`ptr_end` must be in the *exact* buffer `hit_output.tricmd` points into, or the range test at `lightfixture.c:380-381` can never match. Verify the hit-test walks `ptr_expanded_mapping_info` (it does: `bg.c:10051`).

### minor — D2/D3/D4: reference bodies use N64 accessors; native must read raw big-endian DL bytes

**What it is.** The `#else` reference bodies dereference `gfx->dma.addr`, `gfx->dma.cmd`, `gfx->tri.tri.v[k]`. Those union members are gated behind `#if !defined(F3D_OLD) && IS_BIG_ENDIAN && !IS_64_BIT` (`gbi.h:1363-1385`); on native `IS_BIG_ENDIAN=0` and `IS_64_BIT=1` (`platform_info.h:8-10`), so **only `words.w0`/`words.w1` exist, and they are 64-bit**. Worse, `tricmd` points into **raw big-endian N64 8-byte-stride F3DEX command bytes**, so even `words.w0/w1` (64-bit, host-endian) are the wrong width/endianness to read them.

**Why it matters.** A verbatim port (a) fails to compile (`->dma`/`->tri` absent) and (b) if forced to compile via `->words`, reads garbage. Also `sizeof(Gfx)==16` on native, so the backward G_VTX walk must step by **literal 8**, not `sizeof(Gfx)`.

**Mitigation.** Read the DL as raw bytes using `bg.c`'s already-proven idiom (`bgReadBe32`, `bg.c:188-191`; the collision walker `bg.c:10109-10254`). Opcodes for this **base-GBI** build (not F3DEX2): `G_VTX = 0x04`, `G_TRI1 = 0xBF`, the GE007 "G_TRI2" is actually **Rare's G_TRI4 = 0xB1** (`-0x4f & 0xFF`; `gbi_extension.h:39`). Index math: **G_TRI1 indices are `/10`** (base-GBI DMEM stride), **G_TRI4 indices are raw 4-bit (no `/10`)**. Full sketches in §4. Segment handling: the G_VTX `addr` is still a raw segment-`0x0E` address (`SPSEGMENT_BG_VTX=14`) resolved at draw time by `gfx_pc.c` — so the `(addr & 0xFF000000)==0x0E000000` test is **TRUE on native and the rebase to `ptr_point_index + (addr & 0xFFFFFF)` is required** (`bg.c:10111-10113` does the identical low-24-bit rebase). Use `uintptr_t`, not the reference's truncating `(s32)` cast (`lightfixture.c:127` is a 64-bit-pointer bug).

### none — D5: visibility/effect

No blocker. The active renderer (`src/platform/fast3d/gfx_pc.c`) re-interprets the whole DL **every frame** and reads `Vtx.v.cn` **fresh from live game-RAM** at G_VTX execution (`gfx_pc.c:21900-21907`, `16176-16178`/`16230`; N64-decode path reads `cn` bytes 12-15 at `gfx_pc.c:22043-22053`). So mutating `cn >>= 2` darkens the surface on the next frame, exactly like N64. The particle spawner `sub_GAME_7F0A2160` (`unk_0A1DA0.c:500-559`) and shard renderer `sub_GAME_7F0A2C44` (un-stubbed, default-on, wired into `lvlRender` at `lvl.c:1812/1817`) are live, as are `get_room_data_float1/2` (`bg.c:4932-4938`) and `getRoomPositionScaledByIndex` (`unk_0BC530.c:447-452`). Cosmetic caveat only: glass-shard *look* is WIP (`docs/GLASS_SHARDS_WIP.md`) — shards appear but may be too large/random.

### none — D6: flag/build/validation

No blocker. Standard default-on GE007 escape-hatch pattern applies (see §5).

---

## 3. How the system works (data flow)

**Precondition — table population (per-room, at load):**
- N64/reference: `sub_GAME_7F0B609C` calls `clear_light_fixturetable_in_room(roomID)` then `texLoadFromGdl(...)` (`bg.c:7999-8000`). `texLoadFromGdl` (`unk_0CC4C0.c:1546`, real native C under `NONMATCHING`) walks the DL, reads `texturenum = src->words.w1 & 0xFFF` (`:1619`), and on `check_if_imageID_is_light(texturenum)` (`:1750`) records the light's DL span via `add_entry_to_init_lightfixture_table(gdl)` (`:1752`) / `save_ptrDL_enpoint_...(gdl)` (`:1615, 1810`).
- **Native today: this call is skipped (D1 blocker).** A new population pass must reproduce it against the raw DL (§2, §6).

**Per shot (live, native):**
1. Weapon fires → `chraiDefaultWeaponFireHandler` (`chrprop.c:1868` native branch) computes a hit via `bgTestBulletHitBackground` / portal-walk fallbacks, all sharing one `BgHitRecord hit_output` (`chrprop.c:2550, 2671-2699`). `best_room` is set and `hit_something=1` only when `best_room > 0` (`chrprop.c:2701-2702`).
2. The winning triangle's record is filled by `bgPopulateHitRecord` (`bg.c:165-179`): `dst->tricmd = (Gfx*)cmd` (pointer into `g_BgRoomInfo[room].ptr_expanded_mapping_info`, `bg.c:10051/10096/10125`), `dst->tri_index` = `0` for G_TRI1 (`bg.c:10230`) or `triIndex+1` for G_TRI4 sub-tris (`bg.c:10351`), `dst->texturenum`. Struct types: `Gfx *tricmd; s16 tri_index; s16 texturenum;` (`bg.h:122-134`).
3. Gated by hit success and material: `if (check_if_imageID_is_light(hit_output.texturenum)) sub_GAME_7F0BBE0C(hit_output.tricmd, hit_output.tri_index, best_room);` (`chrprop.c:3068-3069`; 2nd caller is the inert `#else` body at `chrprop.c:3465-3467`).
4. `sub_GAME_7F0BBE0C` (`lightfixture.c:346-491`) scans `light_fixture_table[0..0x64)`; for the entry whose `room_index==room` and `ptr_start <= gfx < ptr_end` (`:378-381`): if `darkened_light_table_contains_triangle` already (`:383`) return; else `darken_triangle_in_room` (`:385`), compute the triangle's three edge lengths, walk each edge spawning `sub_GAME_7F0A2160(&coord, 0, 10)` particles (`:411-433`), then scan the whole light DL `[ptr_start, ptr_end)` (`:435-488`) darkening any neighbouring G_TRI1 (`0xBF`) / G_TRI4 (`0xB1`) triangle whose any vertex is within `get_room_data_float1()*100` of an already-darkened vertex (`sub_GAME_7F0BBCCC`, `:306-337`).
5. `darken_triangle_in_room` (`:268-284`) resolves indices via `extract_vertex_indices_from_triangle`, the vertex base via `return_ptr_vertex_of_entry_room`, then `darken_vertex_in_room` (`:222-245`) records `(room_index, vtx_index)` in `darkened_light_table` (`vtx_index = (vertex - ptr_point_index) >> 4`, `:229`) and applies `cn[0..3] >>= 2`.

**Per room reload (native, live):** `redarken_lights_in_room(room)` (`lightfixture.c:200-219`, called `bg.c:8587`, `lvl.c:275`) re-applies `cn >>= 2` to every `darkened_light_table` entry for that room, so darkness persists across re-entry. (No-op while the table is empty.)

**Why the mutation is seen:** `cn` is read per-frame from the same RAM (§2/D5). Darkening touches **only `cn`** (color/shade), never `ob`/positions — so collision/LOS/AI (which read positions via `bgTestLineIntersectionInRoom`, `bg.c:9750`) are unaffected: gameplay-invariant by construction.

---

## 4. The three functions to implement

All three live in `lightfixture.c` under `#ifdef NATIVE_PORT`. **Do not copy the `#else` bodies verbatim** — replace their `gfx->dma`/`gfx->tri` accessors with raw big-endian byte reads. Reuse `bgReadBe32` (currently `static` in `bg.c:188`; either expose it via a header or add a local equivalent in `lightfixture.c`).

### 4.1 `return_ptr_vertex_of_entry_room(Gfx *gfx, s32 room_index)` — `lightfixture.c:111`

**Must do:** walk the DL backward from `gfx` to the governing `G_VTX` (`0x04`) command, read its segment address, and return the absolute `Vtx*` base for the triangle's indices.

**Reference (`:117-131`) is algorithmically correct but uses `gfx->dma.cmd`/`gfx->dma.addr` and a truncating `(s32)` cast.** Native adaptations: opcode = high byte of the 8-byte command; step **8 bytes**; `addr` = big-endian word at command+4; keep the `0x0E` test and rebase but in `uintptr_t`. Mirror `bg.c:10111-10113`.

```c
Vtx *return_ptr_vertex_of_entry_room(Gfx *gfx, s32 room_index) {
    const u8 *cmd = (const u8 *)gfx;
    const u8 *dl_start = (const u8 *)g_BgRoomInfo[room_index].ptr_expanded_mapping_info;
    while (cmd[0] != 0x04 /* G_VTX */) {           /* step 8, NOT sizeof(Gfx)==16 */
        cmd -= 8;
        if (cmd < dl_start) return NULL;            /* guard: malformed/leading non-G_VTX DL */
    }
    u32 addr = bgReadBe32(cmd + 4);                 /* the N64 dma.addr field */
    if ((addr & 0xFF000000u) == 0x0E000000u)        /* segment 0x0E = SPSEGMENT_BG_VTX */
        return (Vtx *)((uintptr_t)g_BgRoomInfo[room_index].ptr_point_index + (addr & 0x00FFFFFFu));
    return (Vtx *)(uintptr_t)addr;
}
```

> **Index/base consistency (see §8 risk #1):** the hit-test resolves vertices as `vtx_pool_base + (raw_idx - vtx_base_offset)*0x10`, where `vtx_pool_base = ptr_point_index + (header_word4 & 0xFFFFFF)` and `vtx_base_offset = coll_dl[1] & 0xF` (`bg.c:10109-10113, 10132-10134, 10252-10254`). The lightfixture path resolves the base via the preceding G_VTX instead. These must converge on the **same absolute `Vtx*`** (and the same `>>4` index relative to `ptr_point_index` used by `darken_vertex_in_room`/`redarken`). Verify empirically (assert that `return_ptr_vertex_of_entry_room(tricmd,room)[idx]` equals the `Vertex*` the hit record carried, `bg.c:10226-10229`); if they diverge by `vtx_base_offset`, either subtract `vtx_base_offset` in §4.2 or bias the base here. Do **not** double-apply the offset.

### 4.2 `extract_vertex_indices_from_triangle(Gfx *gfx, u32 tri_type, s32 *idx1, s32 *idx2, s32 *idx3)` — `lightfixture.c:136`

**Must do:** for `tri_type==0` (G_TRI1, `0xBF`) read the three index bytes `/10`; for `tri_type 1..4` (G_TRI4, `0xB1`, sub-triangle `tri_type-1`) unpack raw 4-bit indices (no `/10`).

**Reference (`:142-172`):** case 0 is conceptually right (`v[k]/10`) but via `gfx->tri.tri.v[k]`; cases 1-4 are byte/bit poking of the host pointer that is wrong on LE 64-bit. Replace cases 1-4 with the **authoritative** decode from `bg.c:10252-10254` / `gfx_pc.c:22329-22332`.

```c
void extract_vertex_indices_from_triangle(Gfx *gfx, u32 tri_type,
                                          s32 *idx1, s32 *idx2, s32 *idx3) {
    const u8 *cmd = (const u8 *)gfx;
    if (tri_type == 0) {                 /* G_TRI1 (0xBF): indices /10, bytes 5,6,7 */
        *idx1 = cmd[5] / 10;
        *idx2 = cmd[6] / 10;
        *idx3 = cmd[7] / 10;
    } else {                             /* G_TRI4 (0xB1): raw 4-bit, k = tri_type-1 */
        s32 k  = (s32)tri_type - 1;      /* 0..3 */
        u32 w0 = bgReadBe32(cmd + 0);
        u32 w1 = bgReadBe32(cmd + 4);
        *idx1 = (w1 >> (8 * k))     & 0xF;
        *idx2 = (w1 >> (8 * k + 4)) & 0xF;
        *idx3 = (w0 >> (4 * k))     & 0xF;
    }
}
```

**Key asymmetry:** G_TRI1 = `/10`; G_TRI4 = raw 4-bit. Do **not** divide the G_TRI4 indices.

> **Verified caveat (ties into §8 risk #1):** the authoritative collision walker subtracts the per-section `vtx_base_offset` (`coll_dl[1] & 0xF`) from *every* index — G_TRI1 at `bg.c:10131-10134`, G_TRI4 at `bg.c:10252-10254`. The sketch above omits that subtraction because `return_ptr_vertex_of_entry_room` (§4.1) resolves the vertex base from the G_VTX command itself rather than from the section header. **Apply the `vtx_base_offset` correction in exactly one of §4.1 or §4.2 — never both, never neither.** Decide which by the step-7 assertion before finalizing.

### 4.3 `sub_GAME_7F0BBE0C(Gfx *gfx, u32 tri_type, s32 room_index)` — `lightfixture.c:341`

**Must do:** the full reference body (`:346-491`) — table match, dedup, darken hit triangle, spawn edge particles, scan-and-darken neighbours. The body is **almost portable as-is**: it calls the already-ported helpers (`darken_triangle_in_room`, `extract_vertex_coords_from_triangle`, `sub_GAME_7F0BBCCC`, `darken_vertex_in_room`), the live `sub_GAME_7F0A2160`/`getRoomPositionScaledByIndex`/`get_room_data_float2`, and `sqrtf`. Two native adaptations:

1. **The neighbour-scan loop opcode reads** at `:437` and `:461` use `gfx2->dma.cmd` — replace with `((const u8 *)gfx2)[0]` compared to `0xBF` (G_TRI1) and `0xB1` (G_TRI4).
2. **The loop pointer step** `gfx2++` at `:435` advances by `sizeof(Gfx)==16` on native, but DL commands are **8 bytes**. Walk with a `const u8 *` stepping `+= 8` (and keep the `< ptr_end` bound).

```c
void sub_GAME_7F0BBE0C(Gfx *gfx, u32 tri_type, s32 room_index) {
    if (!ge007_shoot_out_lights_enabled()) return;   /* A/B off: byte-identical to old stub */

    for (s32 i = 0; i < LIGHTFIXTURE_TABLE_MAX; i++) {
        if (room_index != light_fixture_table[i].room_index) continue;
        if (gfx <  light_fixture_table[i].ptr_start_pertinent_DL) continue;
        if (gfx >= light_fixture_table[i].ptr_end_pertinent_DL)   continue;

        if (darkened_light_table_contains_triangle(gfx, tri_type, room_index) != 0) return;

        darken_triangle_in_room(gfx, tri_type, room_index);
        /* ... edge-length math + 3 edge particle loops: copy lines 386-433 verbatim ... */

        const u8 *p   = (const u8 *)light_fixture_table[i].ptr_start_pertinent_DL;
        const u8 *end = (const u8 *)light_fixture_table[i].ptr_end_pertinent_DL;
        for (; p < end; p += 8) {                      /* 8-byte stride, NOT gfx2++ */
            u8 op = p[0];
            if (op == 0xBF) {                          /* G_TRI1 */
                /* extract_vertex_coords_from_triangle((Gfx*)p, 0, ...); proximity test; darken */
            } else if (op == 0xB1) {                   /* G_TRI4 (reference -0x4f) */
                for (s32 j = 0; j < 4; j++) {
                    /* extract_vertex_coords_from_triangle((Gfx*)p, j+1, ...); test; darken */
                }
            }
        }
        return;
    }
}
```

(Fill the elided sections with `lightfixture.c:386-433` and `:441-486` verbatim — those call only ported/live helpers and need no change.)

---

## 5. Flag, gating & defaults

**Flag:** `GE007_SHOOT_OUT_LIGHTS`, **default ON.**

**Justification (project rails).** This is a faithfulness/unfinished-port restoration (class C3, `docs/RENDER_PORT_SURVEY.md:11, 28, 117-119`), same bucket as the portal fixes and falling glass shards — all default-ON with the flag as an A/B/debug escape hatch. Rail 1c (`docs/REMASTER_ROADMAP.md:127-134`) requires an all-off state byte-identical to the faithful port; here OFF reproduces the current stub exactly. It is gameplay-invariant (darkening touches only `Vtx.cn`; rail 1a, `docs/REMASTER_ROADMAP.md:46-60`).

**Helper idiom** (mirror `ge007_glass_shards_enabled`, `unk_0A1DA0.c:1121-1132`; and `bgPortalOrderingEnabled`, `bg.c:668-678`), add under `#ifdef NATIVE_PORT` in `lightfixture.c`:

```c
#ifdef NATIVE_PORT
static int s_ge007_shoot_lights = -1;
static int ge007_shoot_out_lights_enabled(void) {
    if (s_ge007_shoot_lights < 0) {
        const char *e = getenv("GE007_SHOOT_OUT_LIGHTS");
        s_ge007_shoot_lights = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    return s_ge007_shoot_lights;
}
#endif
```

**What to gate:**
- **Gate ONLY** the parent `sub_GAME_7F0BBE0C` — first statement: `if (!ge007_shoot_out_lights_enabled()) return;` (gate at the body top, the glass-shards shape, `unk_0A1DA0.c:1149-1152`).
- **Gate the new population pass** (§6 step 4) too — so OFF leaves the table empty and `redarken_lights_in_room` (`bg.c:8587`, `lvl.c:275`) stays a no-op, guaranteeing byte-identical OFF.
- **Do NOT** gate the two `chrprop.c` call sites (`3069`/`3466`) — they are already material-gated by `check_if_imageID_is_light` (mirror glass-shards, which gates inside the effect fn, not its callers).
- The two leaf helpers have **no external callers** (only reachable via the gated parent), so they need no gate; a defensive early-return in each is harmless but unnecessary.

With the parent + population both gated off, `darkened_light_table` and `light_fixture_table` stay empty ⇒ OFF is byte-identical to HEAD.

---

## 6. Step-by-step implementation plan

Land all of the following **in one change** (the three stubs alone render the feature invisible — D1):

1. **`lightfixture.c`** — add the `ge007_shoot_out_lights_enabled()` helper (§5) under `#ifdef NATIVE_PORT`. Make `bgReadBe32` available: either change `bg.c:188` from `static` and declare it in `bg.h`, or add a private `static u32 lf_read_be32(const u8*)` in `lightfixture.c`.
2. **`lightfixture.c:111-115`** — replace the `return_ptr_vertex_of_entry_room` stub with §4.1 (backward 8-byte walk, `uintptr_t` rebase, DL-start guard).
3. **`lightfixture.c:136-140`** — replace the `extract_vertex_indices_from_triangle` stub with §4.2 (G_TRI1 `/10`; G_TRI4 raw 4-bit).
4. **`lightfixture.c:341-344`** — replace the `sub_GAME_7F0BBE0C` stub with §4.3: copy `#else` body `:346-491`, add the flag gate as first statement, and convert the neighbour-scan (`:435-488`) to a `const u8*` 8-byte-stride walk with `p[0]` opcode compares (`0xBF`/`0xB1`).
5. **Population pass (the MAJOR-blocker fix).** Add a native helper, e.g. `void lf_populate_room_lightfixtures(s32 room)`, that:
   - `if (!ge007_shoot_out_lights_enabled()) return;`
   - `clear_light_fixturetable_in_room(room);`
   - walks `g_BgRoomInfo[room].ptr_expanded_mapping_info` (and the secondary DL) byte-wise in 8-byte steps; tracks the most recent texture-select command's `texturenum = (bgReadBe32(cmd+4) & 0xFFF)`; on transition into a light triangle-run (`check_if_imageID_is_light(texturenum)`), call `add_entry_to_init_lightfixture_table((Gfx*)<run start in raw DL>)`, and at the run end call `save_ptrDL_enpoint_to_current_init_lightfixture_table((Gfx*)<run end in raw DL>)`. Mirror `texLoadFromGdl`'s isLight bookkeeping (`unk_0CC4C0.c:1609-1617, 1750-1753, 1809-1811`) but store **raw-DL pointers**, not `dst`.
   - **Verify the texture-command opcode/`w1` mask** against `texLoadFromGdl` (`unk_0CC4C0.c:1586` `cmdByte`, `:1619` `texturenum = w1 & 0xFFF`) — see §8 risk #2.
6. **`bg.c` (~8514-8590) and `lvl.c` (~216-276)** — call `lf_populate_room_lightfixtures(room)` after `gfx_register_n64_dl_region(...)` (`bg.c:8522`, `lvl.c:223`) and before/with `redarken_lights_in_room(room)` (`bg.c:8587`, `lvl.c:275`), on **both** native room-load wrappers.
7. **Consistency assertion (debug-only, temporary).** In `darken_triangle_in_room` or a one-shot check, assert that, for the live shot, `return_ptr_vertex_of_entry_room(tricmd,room)[idx]` matches the absolute `Vertex*` the hit record carried (`bg.c:10226-10229`). Resolve §8 risk #1 before removing.
8. Build, run the full validation recipe (§7), and confirm in-game.

---

## 7. Validation & testing

**Build** (`tools/validation_common.sh:24-25, 115-137`):
```
cmake -S . -B build && cmake --build build --parallel
```

**Flag-OFF byte-identical proof** (the 0.000%-changed control, `docs/INSTRUMENTATION.md:2077-2080, 3325`; `tools/compare_screenshots.py`):
```
./build/ge007 --level 33 --deterministic --background --no-input-grab --screenshot-frame 80 --screenshot-exit   # default
GE007_SHOOT_OUT_LIGHTS=0 ./build/ge007 --level 33 --deterministic --background --no-input-grab --screenshot-frame 80 --screenshot-exit
python3 tools/compare_screenshots.py <off.bmp> <default-or-HEAD.bmp>   # expect 0.000% changed
```
Confirm OFF vs the **pre-change HEAD** binary is also 0.000%.

**ASan/UBSan** (`tools/asan_smoke.sh:1-26`, levels 33/41): `tools/asan_smoke.sh --gate` (must be clean — the new DL walks and `>>=2` writes are the prime suspects for OOB).

**Regression lanes** (this is the Dam HD branch): `tools/dam_visual_regression_suite.sh` (tunnel_visibility / dam_palette / effect_texture, `:128-134`) and `tools/bunker_brightness_regression.sh`. Stub-surface guards: `tools/check_native_stub_surface.py` (must not flag the filled bodies as no-op fallbacks) and `tools/validate_quick.sh`.

**In-game repro** (`docs/RENDER_PORT_SURVEY.md:28, 117-119`): start a level with shootable fixtures — **Facility** or **Bunker** (lamps/panel/neon; the material list is `check_if_imageID_is_light`, `lightfixture.c:89-98`). Shoot a lamp and observe: (a) the surface visibly dims (~¼ shade), (b) glass/debris shards spawn along the triangle edges, (c) neighbouring triangles within radius also dim. Then **leave the room and re-enter** — it must stay dark (exercises `redarken_lights_in_room` and proves the population pass + `darkened_light_table` persistence). A/B with `GE007_SHOOT_OUT_LIGHTS=0` (no darkening, identical to current build) and `GE007_GLASS_SHARDS=0` to isolate the shard cosmetic.

---

## 8. Risks & open questions

1. **(Highest) Vertex base/index consistency between the two decode strategies.** The hit-test resolves vertices as `vtx_pool_base + (raw_idx − vtx_base_offset)*0x10`, with `vtx_pool_base = ptr_point_index + (header_word4 & 0xFFFFFF)` and `vtx_base_offset = coll_dl[1] & 0xF` (`bg.c:10109-10113, 10132-10134, 10252-10254`). The lightfixture path instead resolves the base by walking back to the governing `G_VTX` and adds raw indices (no offset). If the G_VTX-derived base differs from `vtx_pool_base` by `vtx_base_offset*0x10`, every darkened vertex is off by a fixed stride and the wrong surfaces dim (or `darken_vertex_in_room`'s `>>4` index, `lightfixture.c:229`, mismatches `redarken`). **Verify first** (step 7 assertion). If they diverge, either subtract `vtx_base_offset` in §4.2 or bias the base in §4.1 — never both. A robust alternative is to thread the hit record's already-absolute `Vertex* vtx0/vtx1/vtx2` (`bg.c:10226-10229`) into `sub_GAME_7F0BBE0C` for the **initial** triangle (signature change), and use the helpers only for the neighbour scan.

2. **Texture-command opcode/mask in the population pass.** `texLoadFromGdl` reads `cmdByte = *(u8*)src` (`unk_0CC4C0.c:1586`) and `texturenum = src->words.w1 & 0xFFF` (`:1619`). Confirm which command byte(s) carry the imageID in the **raw** room DL (the prompt's D1 recommendation guessed `G_SETTILESIZE/0xC0`; this is **not confirmed** — derive it from `texLoadFromGdl`'s actual `cmdByte` switch and from `bgFindTextureForTriangleCommand`, `bg.c:10197`, which already maps a triangle command to its `texturenum` on native). The cleanest, lowest-risk option may be to reuse `bgFindTextureForTriangleCommand` per-triangle rather than re-implementing the texture-state walk.

3. **G_TRI4 sub-triangle bit layout (the reference "case 1..4").** §4.2 uses the authoritative `bg.c:10252-10254` / `gfx_pc.c:22329-22332` decode, **not** the reference's `:151-170` byte poking (which is wrong on LE 64-bit). The `bg.c` decode is exercised live by the hit-test, so it is trustworthy — but spot-check that `tri_type` (1..4) maps to `triIndex = tri_type-1` consistently (`bg.c:10351` passes `triIndex+1`). Confirm there is exactly one governing `G_VTX` per triangle run (GE collision DLs front-load a G_VTX then a tri run, and the walker treats `0x04` as a section boundary, `bg.c:10117-10127`); the backward-walk guard in §4.1 protects against the malformed case.

4. **Secondary room DL.** Confirm whether light fixtures appear in the secondary DL (`ptr_secondary_expanded_mapping_info`) as well as the primary; if so, the population pass must cover both (the secondary loader `sub_GAME_7F0B61DC` also skips on native, `bg.c:8220-8239`).

5. **Endianness of `ob[]`/`tc[]` reads.** Darkening (`cn >>= 2`) is byte-safe. But `extract_vertex_coords_from_triangle` (`lightfixture.c:186-196`) reads `vertices[idx].v.ob[*]` as native `s16` — the raw vtx buffer stores **big-endian** `ob[]` (`gfx_pc.c:22045`). If the spawned edge particles land in the wrong place, byte-swap `ob[]` on read (this affects only particle placement and `sub_GAME_7F0BBCCC` proximity, not the darkening). Verify particle positions in-game.

6. **Cosmetic:** glass-shard visual is WIP (`docs/GLASS_SHARDS_WIP.md`); shards appear but may look too large/random (`GE007_GLASS_SHARDS=0` to A/B). Not a blocker for this feature.
