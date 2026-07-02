# Remaster Phase 0 "First Light" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the gameplay-invariance enforcement rails (sim/render separation check, timing lock, RAMROM sim-state hash gate), then land a sampleable depth texture + SSAO — the first visibly-new remaster frame — proven byte-identical when flagged off.

**Architecture:** Three CI/local "rails" (R1 shell nm-check, R2 shell timing guard, R3 C sim-state hash + gate script) enforce that rendering never perturbs simulation. Then two renderer changes (A1 depth renderbuffer→texture, A2 SSAO in the existing inline output-pass GLSL) add the first modern-lighting cue, gated by `Video.Ssao`/`GE007_SSAO` and defaulting off.

**Tech Stack:** C11 (native port under `NATIVE_PORT`), OpenGL (fast3d backend), inline GLSL, POSIX shell + `nm`, Python validation tools, CMake/CTest, SDL2. Host: macOS arm64 (Apple Silicon M3).

## Global Constraints

- **Gameplay-invariant by construction** — no simulation TU may read render/material/FBO/GL state; the sim tick must never stretch. Every renderer change must leave the sim-state hash identical flags-on vs flags-off.
- **Default-identity** — with all Phase-0 flags at identity, every frame is byte-identical to the faithful port (screenshot hash + render-health counters unchanged).
- **Opt-in flags** — each runtime feature lands behind a `Video.*`/`Input.*` settings key **and** a `GE007_*` env A/B hatch, following the existing `settingsRegisterInt/Float` pattern.
- **Copyright** — no ROM-derived asset committed; contamination guard (`scripts/ci/check_no_rom_data.sh`) stays green; no tracked images/dumps.
- **macOS arm64** — no `-no-pie`; determinism achieved via in-hash pointer normalization, not fixed load addresses.
- **Anchors** verified against `robustness/remaster-hardening` @ 2026-07-01; re-verify exact line numbers before editing (they are function-level).
- **Per commit:** build clean (zero new warnings), ASan/UBSan clean on touched hot-draw code, `tools/playability_smoke.sh` relevant lane green.

---

## File Structure

**New files:**
- `scripts/ci/check_sim_render_separation.sh` — R1: nm-scans `src/game/*.c.o` for backend-symbol coupling.
- `scripts/ci/check_timing_lock.sh` — R2: asserts only `lvl.c`/`front.c` write `g_ClockTimer` and the clamp/RAMROM-bypass are intact.
- `src/platform/sim_state_hash.c` / `src/platform/sim_state_hash.h` — R3: region registry + pointer-normalizing FNV-1a hash + JSON emit.
- `tools/sim_invariance_gate.sh` — R3: local ROM gate (run `dam1` flags-off vs flags-on, compare hashes).
- `tests/test_sim_state_hash.c` — R3: ROM-free CTest for hash determinism + normalization + seeded divergence.

**Modified files:**
- `.github/workflows/ci.yml` — run R1 + R2 in the `no-rom-data`/hygiene job; run the R3 hash-tool CTest in `linux-build`.
- `src/platform/main_pc.c:~576` — add `--sim-state-hash-out <path>` CLI parse; call hash+emit at replay end.
- `src/boss.c:231-245` — expose the `s_pcPool` base+size via a getter for the region registry; optional `GE007_SIM_HASH` fixed-base pin.
- `src/platform/fast3d/gfx_opengl.c` — A1: depth RB→texture (`ensure_scene_target` ~:2237, globals ~:633); A2: SSAO gate (`~:2726`), inline GLSL SSAO (`apply_output_vi_filter` ~:3126), uniforms + depth-unit bind, `uProjMatrix`.
- `src/platform/platform_sdl.c:~195/~1543` — declare/register `Video.Ssao`,`Video.SsaoRadius`,`Video.SsaoIntensity` + `GE007_SSAO*`.
- `docs/REMASTER_ROADMAP.md` §6 + `docs/REMASTER_PHASE0_PLAN.md` — back-port the P0.1/P0.2/P0.3 corrections and the pointer-normalization R3 change.
- `CMakeLists.txt` — add `tests/test_sim_state_hash.c` CTest target; compile `sim_state_hash.c`.

**Interfaces produced (used across tasks):**
- `uint64_t sim_state_hash_compute(const SimHashRegion *regions, int n)` — FNV-1a over regions with intra-state pointer normalization.
- `int sim_state_hash_emit_json(const char *path, uint64_t hash, const SimHashRegion *regions, int n, int frame, const char *replay)` — writes the gate JSON.
- `void simHashRegistryBuild(SimHashRegion *out, int *n)` — fills the region set: `[0]` = pool, `[1..]` = curated globals.
- `const void *bossGetPcPoolBase(void); size_t bossGetPcPoolSize(void);` — pool region accessor (from `boss.c`).
- `struct { const char *name; const void *base; size_t size; } SimHashRegion;`

---

## Task Group R1 — Static sim/render separation check

### Task R1: nm-scan denylist check

**Files:**
- Create: `scripts/ci/check_sim_render_separation.sh`
- Modify: `.github/workflows/ci.yml` (hygiene job)

**Interfaces:**
- Consumes: built objects at `build/CMakeFiles/ge007.dir/src/game/*.c.o`.
- Produces: exit 0 (clean) / 1 (violation) with offending `symbol@object` lines.

- [ ] **Step 1: Write the failing test (seed a violation).** Create a throwaway object that references a denylisted symbol and confirm the (not-yet-written) script flags it. Temporary probe:

```bash
# tmp probe — a fake game TU that touches a backend symbol
cat > /tmp/r1probe.c <<'EOF'
extern int texture_pack_try_load(unsigned, void*);
int r1_probe(void){ return texture_pack_try_load(0,0); }
EOF
clang -c /tmp/r1probe.c -o /tmp/r1probe.c.o
nm -u /tmp/r1probe.c.o | grep -E '_texture_pack_'   # must print the symbol
```
Expected: prints `_texture_pack_try_load` — proving nm surfaces the coupling.

- [ ] **Step 2: Write the script.**

```bash
#!/usr/bin/env bash
# Fail if any simulation TU (src/game/*.c) references renderer-backend symbols.
# The display-list SUBMISSION API (gfx_run_dl, gfx_register_*, gfx_set_*, gfx_ptr_*,
# gfx_segment_table) is ALLOWED — that is how the sim draws. What is forbidden is
# reading render/material/FBO/GL *backend* state back into the simulation.
set -euo pipefail
OBJDIR="${1:-build/CMakeFiles/ge007.dir/src/game}"
[ -d "$OBJDIR" ] || { echo "R1: object dir $OBJDIR missing — build first"; exit 2; }

# macOS nm prefixes symbols with '_'. Denylist = renderer backend / material / GL.
DENY='(_gfx_opengl_|_texture_pack_|_g_pcTexturePack|_gl[A-Z]|_glad_gl)'
violations=0
for o in "$OBJDIR"/*.c.o; do
  hits=$(nm -u "$o" 2>/dev/null | awk '{print $NF}' | grep -E "$DENY" || true)
  if [ -n "$hits" ]; then
    echo "R1 VIOLATION in $(basename "$o"):"
    echo "$hits" | sed 's/^/    /'
    violations=$((violations+1))
  fi
done
if [ "$violations" -ne 0 ]; then
  echo "R1: $violations simulation TU(s) reference renderer-backend symbols."; exit 1
fi
echo "R1: clean — no simulation TU references renderer-backend state."
```

- [ ] **Step 3: Run against the real tree — expect clean.**

Run: `chmod +x scripts/ci/check_sim_render_separation.sh && ./scripts/ci/check_sim_render_separation.sh`
Expected: `R1: clean — ...`, exit 0.

- [ ] **Step 4: Negative test — seed a violation and expect failure.** Temporarily append a denylisted extern+call to a scratch copy, rebuild that one object, rerun. Then revert.

```bash
cp build/CMakeFiles/ge007.dir/src/game/trace.c.o /tmp/trace.bak.o
cp /tmp/r1probe.c.o build/CMakeFiles/ge007.dir/src/game/zzprobe.c.o
./scripts/ci/check_sim_render_separation.sh; echo "exit=$?"   # expect VIOLATION, exit 1
rm build/CMakeFiles/ge007.dir/src/game/zzprobe.c.o
```
Expected: prints VIOLATION, exit 1; after cleanup, clean again.

- [ ] **Step 5: Wire into CI.** In `.github/workflows/ci.yml`, add to the `linux-build` job (after "Build native port", since it needs objects):

```yaml
      - name: Sim/render separation (R1)
        run: ./scripts/ci/check_sim_render_separation.sh
```

- [ ] **Step 6: Commit.**

```bash
git add scripts/ci/check_sim_render_separation.sh .github/workflows/ci.yml
git commit -m "rails(R1): static sim/render separation nm-check (default-clean)"
```

---

## Task Group R2 — Timing lock

### Task R2: g_ClockTimer write-guard + clamp assertion

**Files:**
- Create: `scripts/ci/check_timing_lock.sh`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: source tree `src/`.
- Produces: exit 0/1; fails if a TU other than `lvl.c`/`front.c` writes `g_ClockTimer`, or if the clamp / RAMROM bypass is missing from `lvl.c`.

- [ ] **Step 1: Write the script.**

```bash
#!/usr/bin/env bash
# Timing lock: only lvl.c and front.c may WRITE g_ClockTimer, and lvl.c must retain
# the 1-4 tick clamp + RAMROM replay bypass (the determinism anchor).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ALLOWED='src/game/lvl.c src/game/front.c'
# find writes: assignment/increment to g_ClockTimer (exclude the declaration line).
rogue=0
while IFS= read -r hit; do
  f="${hit%%:*}"
  case " $ALLOWED " in *" $f "*) : ;; *) echo "TIMING VIOLATION: $hit"; rogue=1;; esac
done < <(grep -rnE 'g_ClockTimer[[:space:]]*(=|\+\+|--|[-+*/]=)' src \
          --include='*.c' | grep -vE 's32 g_ClockTimer = 0;')
# assert clamp + bypass survive in lvl.c
grep -q 'get_is_ramrom_flag' src/game/lvl.c || { echo "TIMING: RAMROM bypass missing in lvl.c"; rogue=1; }
grep -qE 'g_ClockTimer = 4;' src/game/lvl.c || { echo "TIMING: 4-tick clamp missing in lvl.c"; rogue=1; }
[ "$rogue" -eq 0 ] && echo "R2: timing lock intact." || exit 1
```

- [ ] **Step 2: Run — expect clean.**

Run: `chmod +x scripts/ci/check_timing_lock.sh && ./scripts/ci/check_timing_lock.sh`
Expected: `R2: timing lock intact.`, exit 0.

- [ ] **Step 3: Negative test.** Temporarily add `g_ClockTimer = 2;` to a scratch file and confirm failure, then revert.

```bash
echo 'void zz(void){ extern int g_ClockTimer; g_ClockTimer = 2; }' > src/game/zz_timing_probe.c
./scripts/ci/check_timing_lock.sh; echo "exit=$?"   # expect VIOLATION, exit 1
rm src/game/zz_timing_probe.c
```
Expected: VIOLATION, exit 1; clean after removal.

- [ ] **Step 4: Wire into CI** (`ci.yml`, hygiene job `no-rom-data`, ROM-free):

```yaml
      - name: Timing lock (R2)
        run: ./scripts/ci/check_timing_lock.sh
```

- [ ] **Step 5: Commit.**

```bash
git add scripts/ci/check_timing_lock.sh .github/workflows/ci.yml
git commit -m "rails(R2): timing-lock guard — g_ClockTimer writers + clamp assertion"
```

---

## Task Group R3 — RAMROM sim-state hash gate

### Task R3a: pool accessor + pointer-normalizing hash primitive

**Files:**
- Create: `src/platform/sim_state_hash.h`, `src/platform/sim_state_hash.c`
- Modify: `src/boss.c` (expose pool), `src/memp.h`/`boss.h` if a prototype is needed, `CMakeLists.txt`
- Test: `tests/test_sim_state_hash.c`

**Interfaces:**
- Produces: `SimHashRegion`, `sim_state_hash_compute`, `bossGetPcPoolBase/Size` (signatures in the File Structure block).

- [ ] **Step 1: Expose the pool.** In `src/boss.c`, lift `s_pcPool`/`PC_POOL_SIZE` to file scope (they are currently `static` inside the alloc block, `:231-245`) and add accessors:

```c
/* boss.c — near the PC pool definition */
static u8   *s_pcPool = NULL;
static size_t s_pcPoolSize = 0;
const void *bossGetPcPoolBase(void) { return s_pcPool; }
size_t      bossGetPcPoolSize(void) { return s_pcPoolSize; }
```
Set `s_pcPoolSize = PC_POOL_SIZE;` where the pool is allocated. Declare both in `src/boss.h` (guarded by `#ifdef NATIVE_PORT`).

- [ ] **Step 2: Write the failing test.**

```c
/* tests/test_sim_state_hash.c */
#include "sim_state_hash.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    /* 1. determinism: same bytes -> same hash */
    unsigned char buf[256]; for (int i=0;i<256;i++) buf[i]=(unsigned char)(i*7);
    SimHashRegion r1[1] = {{"buf", buf, sizeof buf}};
    uint64_t h1 = sim_state_hash_compute(r1, 1);
    uint64_t h2 = sim_state_hash_compute(r1, 1);
    assert(h1 == h2);

    /* 2. sensitivity: a one-byte change flips the hash */
    buf[100] ^= 0xFF;
    assert(sim_state_hash_compute(r1, 1) != h1);
    buf[100] ^= 0xFF;

    /* 3. pointer normalization: a word holding a pointer INTO the region hashes
       the same as the SAME logical layout at a different base address. */
    unsigned char a[64] = {0}, b[64] = {0};
    *(void**)(a+8) = (void*)(a+40);   /* a self-pointer at offset 8 -> offset 40 */
    *(void**)(b+8) = (void*)(b+40);   /* same logical layout, different base    */
    SimHashRegion ra[1] = {{"a", a, sizeof a}};
    SimHashRegion rb[1] = {{"b", b, sizeof b}};
    assert(sim_state_hash_compute(ra,1) == sim_state_hash_compute(rb,1));
    printf("test_sim_state_hash: OK\n");
    return 0;
}
```

- [ ] **Step 3: Run — verify it fails to link (no impl).**

Run: `clang tests/test_sim_state_hash.c -Isrc/platform -o /tmp/t && /tmp/t`
Expected: link error (undefined `sim_state_hash_compute`).

- [ ] **Step 4: Implement the primitive.**

```c
/* sim_state_hash.h */
#ifndef SIM_STATE_HASH_H
#define SIM_STATE_HASH_H
#include <stddef.h>
#include <stdint.h>
typedef struct { const char *name; const void *base; size_t size; } SimHashRegion;
uint64_t sim_state_hash_compute(const SimHashRegion *regions, int n);
int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *regions, int n,
                             int frame, const char *replay);
void simHashRegistryBuild(SimHashRegion *out, int *n);   /* defined in R3b */
#endif
```

```c
/* sim_state_hash.c */
#include "sim_state_hash.h"
#include <stdio.h>
#include <string.h>

#define FNV64_OFF 1469598103934665603ULL
#define FNV64_PRM 1099511628211ULL
static inline uint64_t fnv1a(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char*)p;
    for (size_t i=0;i<n;i++){ h ^= b[i]; h *= FNV64_PRM; }
    return h;
}
/* If word W points into region k, return a base-independent token; else pass W. */
static uint64_t canon_word(uintptr_t w, const SimHashRegion *rg, int n) {
    for (int k=0;k<n;k++){
        uintptr_t lo=(uintptr_t)rg[k].base, hi=lo+rg[k].size;
        if (w>=lo && w<hi) { return ((uint64_t)(k+1)<<48) ^ (uint64_t)(w-lo); }
    }
    return (uint64_t)w;   /* not an intra-state pointer: hash literally */
}
uint64_t sim_state_hash_compute(const SimHashRegion *rg, int n) {
    uint64_t h = FNV64_OFF;
    for (int k=0;k<n;k++){
        const unsigned char *b=(const unsigned char*)rg[k].base;
        size_t sz=rg[k].size, i=0;
        h = fnv1a(h, rg[k].name, strlen(rg[k].name));           /* order/identity */
        /* pointer-aligned words get canonicalized; trailing bytes hashed raw */
        for (; i+sizeof(uintptr_t)<=sz; i+=sizeof(uintptr_t)) {
            uintptr_t w; memcpy(&w, b+i, sizeof w);
            uint64_t c = canon_word(w, rg, n);
            h = fnv1a(h, &c, sizeof c);
        }
        if (i<sz) h = fnv1a(h, b+i, sz-i);
    }
    return h;
}
int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *rg, int n, int frame,
                             const char *replay) {
    FILE *f=fopen(path,"w"); if(!f) return -1;
    fprintf(f,"{\n  \"hash\": \"%016llx\",\n  \"frame\": %d,\n  \"replay\": \"%s\",\n  \"regions\": [",
            (unsigned long long)hash, frame, replay?replay:"");
    for(int k=0;k<n;k++) fprintf(f,"%s{\"name\":\"%s\",\"size\":%zu}",
                                 k?", ":"", rg[k].name, rg[k].size);
    fprintf(f,"]\n}\n"); fclose(f); return 0;
}
```

- [ ] **Step 5: Run the test — expect pass.**

Run: `clang tests/test_sim_state_hash.c src/platform/sim_state_hash.c -Isrc/platform -o /tmp/t && /tmp/t`
Expected: `test_sim_state_hash: OK`.

- [ ] **Step 6: Add CTest target.** In `CMakeLists.txt`, near the other tests:

```cmake
add_executable(test_sim_state_hash tests/test_sim_state_hash.c src/platform/sim_state_hash.c)
target_include_directories(test_sim_state_hash PRIVATE src/platform)
add_test(NAME sim_state_hash COMMAND test_sim_state_hash)
```
Add `src/platform/sim_state_hash.c` to the ge007 build (it is under `src/platform`, already globbed — confirm it links).

- [ ] **Step 7: Run CTest — expect pass.**

Run: `cmake --build build --target test_sim_state_hash && ctest --test-dir build -R sim_state_hash --output-on-failure`
Expected: `1/1 Test #… sim_state_hash … Passed`.

- [ ] **Step 8: Commit.**

```bash
git add src/platform/sim_state_hash.* src/boss.c src/boss.h tests/test_sim_state_hash.c CMakeLists.txt
git commit -m "rails(R3a): pointer-normalizing sim-state hash primitive + pool accessor"
```

### Task R3b: region registry (pool + curated globals)

**Files:** Modify: `src/platform/sim_state_hash.c` (add `simHashRegistryBuild`).

**Interfaces:** Produces `simHashRegistryBuild(SimHashRegion*, int*)` filling `[0]`=pool + curated globals.

- [ ] **Step 1: Resolve the curated globals.** Grep the actual symbol names/types before writing (do NOT guess):

```bash
grep -rnE 'extern .*(g_playerPlayerData|g_playerData|g_vars|g_Vars|random|Rand|Seed|g_ClockTimer|g_MissionConfig|g_CurrentLevel)' src/game/*.h | head -40
```
Pick the RNG seed global(s), the player-data array, mission/level state, and `g_ClockTimer`. Record chosen symbols in a comment block.

- [ ] **Step 2: Implement the registry** (example — replace with resolved symbols from Step 1):

```c
/* sim_state_hash.c — append */
#include "boss.h"          /* bossGetPcPoolBase/Size */
extern int g_ClockTimer;   /* + the resolved globals from Step 1 */
/* extern struct player_data g_playerPlayerData[4]; ... */
void simHashRegistryBuild(SimHashRegion *out, int *n) {
    int i = 0;
    out[i].name="pool"; out[i].base=bossGetPcPoolBase(); out[i].size=bossGetPcPoolSize(); i++;
    out[i].name="g_ClockTimer"; out[i].base=&g_ClockTimer; out[i].size=sizeof g_ClockTimer; i++;
    /* out[i]=... one entry per resolved global ... */
    *n = i;
}
```
Note: keep the array bound (`SimHashRegion regs[32]`) generous; `simHashRegistryBuild` must not exceed it.

- [ ] **Step 3: Build the port — verify it links** (registry references real globals):

Run: `cmake --build build --parallel 8 2>&1 | tail -3`
Expected: `Built target ge007`, no undefined-symbol errors.

- [ ] **Step 4: Commit.**

```bash
git add src/platform/sim_state_hash.c
git commit -m "rails(R3b): sim-state region registry (pool + curated game globals)"
```

### Task R3c: `--sim-state-hash-out` CLI + replay-end emit

**Files:** Modify: `src/platform/main_pc.c` (arg parse ~:576; emit at replay end).

**Interfaces:** Consumes registry + primitive; produces a JSON file at the requested path after the deterministic replay reaches its final frame.

- [ ] **Step 1: Parse the flag.** Near the `--trace-state` handling (`main_pc.c:581`):

```c
static const char *g_simHashOut = NULL;
/* in the arg loop: */
else if (strcmp(argv[i], "--sim-state-hash-out") == 0 && i+1 < argc) {
    g_simHashOut = argv[++i];
}
```

- [ ] **Step 2: Emit at replay end.** At the deterministic shutdown/last-frame point (where `--trace-state` finalizes; follow that exact call site), add:

```c
if (g_simHashOut) {
    SimHashRegion regs[32]; int n=0;
    simHashRegistryBuild(regs, &n);
    uint64_t h = sim_state_hash_compute(regs, n);
    sim_state_hash_emit_json(g_simHashOut, h, regs, n,
                             /*frame*/ g_deterministicFinalFrame, /*replay*/ g_replayName);
}
```
Include `sim_state_hash.h`. Use the existing final-frame + replay-name variables (resolve their real names near the trace finalize).

- [ ] **Step 3: Build + smoke** (ROM present locally):

Run:
```bash
cmake --build build --parallel 8 2>&1 | tail -2
SDL_AUDIODRIVER=dummy GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build/ge007 --deterministic --replay dam1 --frames 600 \
  --sim-state-hash-out /tmp/h_off.json
cat /tmp/h_off.json
```
Expected: a JSON file with a nonzero `hash` and the region list. (Resolve the exact replay/frames CLI from `ramromreplay.c` / `main_pc.c`; adjust flag names to the real ones.)

- [ ] **Step 4: Commit.**

```bash
git add src/platform/main_pc.c
git commit -m "rails(R3c): --sim-state-hash-out emits sim-state hash at replay end"
```

### Task R3d: invariance gate script + seeded-divergence proof

**Files:** Create: `tools/sim_invariance_gate.sh`.

- [ ] **Step 1: Write the gate.**

```bash
#!/usr/bin/env bash
# Run the same deterministic replay flags-OFF vs flags-ON; hashes must match.
set -euo pipefail
BIN=./build/ge007; REPLAY="${1:-dam1}"; FR="${2:-3600}"
common="SDL_AUDIODRIVER=dummy GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1"
env $common Video.RemasterFX=0 Video.RenderScale=1 Video.Ssao=0 \
    $BIN --deterministic --replay "$REPLAY" --frames "$FR" --sim-state-hash-out /tmp/sim_off.json
env $common Video.RemasterFX=1 Video.RenderScale=4 Video.Ssao=1 \
    $BIN --deterministic --replay "$REPLAY" --frames "$FR" --sim-state-hash-out /tmp/sim_on.json
off=$(grep -o '"hash": "[0-9a-f]*"' /tmp/sim_off.json)
on=$( grep -o '"hash": "[0-9a-f]*"' /tmp/sim_on.json)
echo "OFF $off"; echo "ON  $on"
[ "$off" = "$on" ] && echo "INVARIANCE GATE: PASS" || { echo "INVARIANCE GATE: FAIL"; exit 1; }
```
(Config-override syntax: use the real `--config-override Video.X=Y` form if env-style keys are not honored — confirm against `platform_sdl.c` settings parsing.)

- [ ] **Step 2: Run — expect PASS.**

Run: `chmod +x tools/sim_invariance_gate.sh && ./tools/sim_invariance_gate.sh dam1 3600`
Expected: `INVARIANCE GATE: PASS`. If FAIL, use `tools/compare_state.py` on `--trace-state` JSONL (off vs on) to localize — then fix the leak (a real bug) or the registry (a false-positive region).

- [ ] **Step 3: Seeded-divergence negative proof.** Temporarily perturb a sim value under a render flag (e.g. guard `g_ClockTimer`-neutral test: add `if (g_pcSsao) someAiCounter++;` in a sim tick), rebuild, run the gate → expect FAIL; revert. This proves the gate has teeth.

Expected: `INVARIANCE GATE: FAIL` while seeded; PASS after revert.

- [ ] **Step 4: Document the gate as a local preflight** in `docs/REMASTER_PHASE0_PLAN.md` §4 (CI is ROM-free; only the R3a CTest runs in CI). Commit.

```bash
git add tools/sim_invariance_gate.sh docs/REMASTER_PHASE0_PLAN.md
git commit -m "rails(R3d): local RAMROM sim-invariance gate + seeded-divergence proof"
```

---

## Task Group A1 — Sampleable depth texture

### Task A1: depth renderbuffer → texture

**Files:** Modify: `src/platform/fast3d/gfx_opengl.c` (globals ~:633-638; `gfx_opengl_ensure_scene_target` ~:2237-2390; MSAA resolve ~:3477; output pass depth-bind).

**Interfaces:** Produces a sampleable `g_scene_depth_tex` bound for the output pass; `rsp.P_matrix` reachable as `uProjMatrix`.

- [ ] **Step 1: Baseline the identity frame** (must stay byte-identical after this task):

```bash
SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 \
  GE007_NO_INPUT_GRAB=1 ./build/ge007 --level 33 --deterministic --screenshot-frame 180 \
  --config-override Video.RemasterFX=0 --config-override Video.RenderScale=1 \
  --screenshot-out /tmp/a1_base.png
shasum /tmp/a1_base.png
```

- [ ] **Step 2: Convert depth RB→texture.** In `gfx_opengl.c`: rename `g_scene_depth_rb`→`g_scene_depth_tex` (glGenTextures), replace `glRenderbufferStorage(...GL_DEPTH_COMPONENT24...)` with `glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,w,h,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL)` + `GL_NEAREST` filters + `GL_CLAMP_TO_EDGE`, and `glFramebufferRenderbuffer(...DEPTH...)`→`glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,g_scene_depth_tex,0)`. Keep the `need_stencil` path as `GL_DEPTH24_STENCIL8` texture + `GL_DEPTH_STENCIL_ATTACHMENT` if stencil is required; else drop stencil. Mirror in the resize path. For the MSAA FBO keep a multisample depth RB and **blit** MSAA depth → the single-sample depth texture after the color resolve (`glBlitFramebuffer(..., GL_DEPTH_BUFFER_BIT, GL_NEAREST)`).

- [ ] **Step 3: Build — expect clean.**

Run: `cmake --build build --parallel 8 2>&1 | tail -3`
Expected: `Built target ge007`, no new warnings.

- [ ] **Step 4: Identity check — frame must be byte-identical** (nothing samples depth yet):

```bash
SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 \
  GE007_NO_INPUT_GRAB=1 ./build/ge007 --level 33 --deterministic --screenshot-frame 180 \
  --config-override Video.RemasterFX=0 --config-override Video.RenderScale=1 \
  --screenshot-out /tmp/a1_after.png
shasum /tmp/a1_base.png /tmp/a1_after.png   # hashes MUST match
python3 tools/audit_screenshot_health.py /tmp/a1_after.png
```
Expected: identical shasums; render-health OK.

- [ ] **Step 5: R3 invariance holds.**

Run: `./tools/sim_invariance_gate.sh dam1 3600`
Expected: `INVARIANCE GATE: PASS`.

- [ ] **Step 6: Commit.**

```bash
git add src/platform/fast3d/gfx_opengl.c
git commit -m "renderer(A1): sampleable scene depth texture (identity-preserving)"
```

---

## Task Group A2 — SSAO

### Task A2a: Video.Ssao flag plumbing

**Files:** Modify: `src/platform/platform_sdl.c` (~:195 decl, ~:1543 register), `src/platform/fast3d/gfx_opengl.c` (extern ~:47).

- [ ] **Step 1: Declare + register** (mirror `Video.Bloom`), default OFF:

```c
/* platform_sdl.c */
s32 g_pcSsao = 0;            /* identity-first: default off */
f32 g_pcSsaoRadius = 0.5f;
f32 g_pcSsaoIntensity = 1.0f;
/* in the register block: */
settingsRegisterInt("Video.Ssao", &g_pcSsao, 0, 0, 1, SETTING_SCOPE_LIVE,
    "GE007_SSAO", "--config-override Video.Ssao=VALUE", "SSAO",
    "Screen-space ambient occlusion (depth-based contact shading). 0 = off.");
settingsRegisterFloat("Video.SsaoRadius", &g_pcSsaoRadius, 0.5f, 0.1f, 2.0f,
    SETTING_SCOPE_LIVE, "GE007_SSAO_RADIUS", "--config-override Video.SsaoRadius=VALUE",
    "SSAO radius", "Sample radius (screen fraction).");
settingsRegisterFloat("Video.SsaoIntensity", &g_pcSsaoIntensity, 1.0f, 0.0f, 2.0f,
    SETTING_SCOPE_LIVE, "GE007_SSAO_INTENSITY", "--config-override Video.SsaoIntensity=VALUE",
    "SSAO intensity", "Occlusion darkening strength.");
```

```c
/* gfx_opengl.c externs */
extern int   g_pcSsao;
extern float g_pcSsaoRadius, g_pcSsaoIntensity;
```

- [ ] **Step 2: Build + confirm the key exists.**

Run: `cmake --build build --parallel 8 2>&1 | tail -2 && ./build/ge007 --list-config 2>/dev/null | grep -i ssao || echo "(check settings dump path)"`
Expected: build clean; `Video.Ssao` present in the settings registry.

- [ ] **Step 3: Commit.**

```bash
git add src/platform/platform_sdl.c src/platform/fast3d/gfx_opengl.c
git commit -m "renderer(A2a): Video.Ssao/GE007_SSAO settings plumbing (default off)"
```

### Task A2b: SSAO gate + inline GLSL + uniforms

**Files:** Modify: `src/platform/fast3d/gfx_opengl.c` (gate ~:2726; fragment GLSL ~:2792-2991; uniform upload ~:3064-3119; depth-unit bind + `uProjMatrix`).

- [ ] **Step 1: Add the gate.**

```c
static bool gfx_opengl_output_ssao_active(void) {
    return g_pcRemasterFX && g_pcSsao != 0;
}
```
Fold `|| gfx_opengl_output_ssao_active()` into `gfx_opengl_output_color_adjust_active()` so the output pass runs when only SSAO is on.

- [ ] **Step 2: Force the scene FBO on when SSAO is active.** At the scene-target decision (where the FBO is skipped for identity RenderScale=1/MSAA=0), add `|| gfx_opengl_output_ssao_active()` to the "need scene target" condition so a depth texture exists to sample.

- [ ] **Step 3: Add uniforms + inline SSAO GLSL.** In the fragment shader string, add `uniform sampler2D uDepthTex; uniform vec2 uDepthSize; uniform mat4 uProjMatrix; uniform int uSsao; uniform float uSsaoRadius, uSsaoIntensity;` and, in the effect chain (after FXAA, before bloom), a horizon/hemisphere AO:

```glsl
if (uApplyPost == 1 && uSsao == 1) {
    float d = texture(uDepthTex, vTexCoord).r;          // [0,1] window depth
    // linearize with projection (perspective): zN = 2d-1 (GL [-1,1] convention)
    float A = uProjMatrix[2][2], B = uProjMatrix[3][2];
    float zeye = B / (A + (2.0*d - 1.0));               // negative view-space z
    float occ = 0.0; const int K = 8;
    for (int i=0;i<K;i++){
        float ang = 6.2831853 * float(i)/float(K);
        vec2 off = vec2(cos(ang),sin(ang)) * uSsaoRadius * uDepthSize.y / uDepthSize; // aspect-safe
        float ds = texture(uDepthTex, vTexCoord + off).r;
        float zs = B / (A + (2.0*ds - 1.0));
        occ += step(zs, zeye - 0.02*abs(zeye));         // neighbor closer => occluder
    }
    occ = 1.0 - uSsaoIntensity * (occ/float(K));
    color.rgb *= clamp(occ, 0.0, 1.0);
}
```
(Tune bias/`0.02`, radius scaling, and the `A/B` extraction to the actual `guPerspective` matrix layout — verify sign against a depth capture; a 4-tap blur pass over `occ` can be added if it reads noisy.)

- [ ] **Step 4: Upload uniforms + bind depth to unit 1** in `gfx_opengl_draw_output_filter_texture` (mirror the bloom uniform block):

```c
glUniform1i(glGetUniformLocation(prog,"uSsao"), (apply_post && g_pcSsao) ? 1:0);
glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_scene_depth_tex);
glActiveTexture(GL_TEXTURE0);
glUniform1i(glGetUniformLocation(prog,"uDepthTex"), 1);
glUniform2f(glGetUniformLocation(prog,"uDepthSize"),
            (float)gfx_current_dimensions.width, (float)gfx_current_dimensions.height);
glUniform1f(glGetUniformLocation(prog,"uSsaoRadius"), g_pcSsaoRadius);
glUniform1f(glGetUniformLocation(prog,"uSsaoIntensity"), g_pcSsaoIntensity);
glUniformMatrix4fv(glGetUniformLocation(prog,"uProjMatrix"),1,GL_FALSE,(const float*)rsp.P_matrix);
```
Expose `rsp.P_matrix` to the backend (extern the RSP struct or add a `gfx_pc_get_projection(float*)` getter in `gfx_pc.c` — prefer the getter to avoid leaking the struct).

- [ ] **Step 5: Build — clean.**

Run: `cmake --build build --parallel 8 2>&1 | tail -3`
Expected: `Built target ge007`, no new warnings; shader compiles at runtime (no GL log errors).

- [ ] **Step 6: Identity-OFF byte-identical** (SSAO off must not change anything):

```bash
SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 \
  GE007_NO_INPUT_GRAB=1 ./build/ge007 --level 33 --deterministic --screenshot-frame 180 \
  --config-override Video.RemasterFX=0 --config-override Video.Ssao=0 \
  --config-override Video.RenderScale=1 --screenshot-out /tmp/a2_off.png
shasum /tmp/a1_base.png /tmp/a2_off.png     # MUST match the A1 baseline
```
Expected: identical to the A1 baseline hash.

- [ ] **Step 7: SSAO-ON produces a visible, correct change.**

```bash
SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 \
  GE007_NO_INPUT_GRAB=1 ./build/ge007 --level 33 --deterministic --screenshot-frame 180 \
  --config-override Video.RemasterFX=1 --config-override Video.Ssao=1 \
  --config-override Video.RenderScale=2 --screenshot-out /tmp/a2_on.png
python3 tools/audit_screenshot_health.py /tmp/a2_on.png
python3 tools/compare_screenshots.py /tmp/a2_off.png /tmp/a2_on.png --max-changed-pct 60 || true
```
Expected: render-health OK; a **visible** darkening in corners/under geometry (changed-pct clearly nonzero but not whole-screen); manual A/B confirms contact shadows read correctly (not haloing/inverted). Capture a second interior-heavy level for confirmation.

- [ ] **Step 8: R3 invariance — SSAO must not perturb the sim.**

Run: `./tools/sim_invariance_gate.sh dam1 3600`
Expected: `INVARIANCE GATE: PASS` (the payoff — proves the whole rails→feature pipeline).

- [ ] **Step 9: ASan/UBSan on the hot draw path.** Build the sanitizer config and smoke a few frames:

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan --parallel 8 2>&1 | tail -3
SDL_AUDIODRIVER=dummy GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build-asan/ge007 --level 33 --deterministic --screenshot-frame 120 \
  --config-override Video.Ssao=1 --config-override Video.RenderScale=2 --screenshot-out /tmp/a2_asan.png
```
Expected: no ASan/UBSan reports.

- [ ] **Step 10: Perf budget.** Run the census with SSAO on to confirm no regression past budget:

```bash
tools/perf_census.sh --level 33 --config-override Video.Ssao=1 || true
```
Expected: within the documented budget (M1/M2 headroom present); record the delta.

- [ ] **Step 11: Commit.**

```bash
git add src/platform/fast3d/gfx_opengl.c src/platform/fast3d/gfx_pc.c
git commit -m "renderer(A2): depth-based SSAO in output pass (Video.Ssao, default off)"
```

---

## Task Group DOC — Roadmap back-port + checkpoint

### Task DOC: fold corrections into the master plan

**Files:** Modify: `docs/REMASTER_ROADMAP.md` §6; `docs/REMASTER_PHASE0_PLAN.md` (R3 pointer-normalization).

- [ ] **Step 1: Update `REMASTER_ROADMAP.md §6`** P0.1 (denylist reframe: submission API allowed, backend symbols denied), P0.2 (native `s_pcPool`+curated-globals design with **pointer normalization**, not `-no-pie`/segment-region), P0.3 (drifted anchors: `lvl.c:2060-2083`, only `lvl.c`+`front.c` write `g_ClockTimer`). Add "P1.1 depth + P3.1 SSAO shipped" to the §2 status table.

- [ ] **Step 2: Update `docs/REMASTER_PHASE0_PLAN.md`** R3 section to describe pointer normalization as the primary determinism mechanism (replacing the `MAP_FIXED`+`-no-pie` text, which is arm64-infeasible).

- [ ] **Step 3: Commit.**

```bash
git add docs/REMASTER_ROADMAP.md docs/REMASTER_PHASE0_PLAN.md
git commit -m "docs: back-port Phase-0 P0.1/P0.2/P0.3 corrections into master roadmap"
```

- [ ] **Step 4: ◆ Checkpoint.** Review the SSAO look; decide default-on-under-`RemasterFX` vs keep opt-in; pick the Phase-1 artifact (smooth env normals §4 T1.3 or sun shadow map T1.4). Record the decision.

---

## Self-Review

**Spec coverage:** R1↔P0.1, R2↔P0.3, R3↔P0.2, A1↔P1.1, A2↔P3.1/T1.2, DOC↔"back-port corrections". All spec deliverables mapped. ✔

**Placeholder scan:** Curated globals (R3b) and exact CLI flag names (R3c) are resolved by explicit grep steps, not left as TODO. GLSL constants (bias/radius) are flagged for tuning against a real depth capture (genuine runtime tuning, not a placeholder). ✔

**Type consistency:** `SimHashRegion`, `sim_state_hash_compute`, `simHashRegistryBuild`, `bossGetPcPoolBase/Size`, `g_pcSsao`/`g_pcSsaoRadius`/`g_pcSsaoIntensity` used consistently across R3a/R3b/R3c and A2a/A2b. `g_scene_depth_tex` introduced in A1, consumed in A2b. ✔

**Known runtime-resolve points (not placeholders — verify during execution):** exact `--replay/--frames/--screenshot-out` CLI spelling (`main_pc.c`/`ramromreplay.c`); env-vs-`--config-override` settings syntax; final-frame/replay-name variable names at the trace-finalize site; `guPerspective` matrix layout for depth linearization sign.
