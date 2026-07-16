# MGB64 in the browser

A static-page build of the same engine (`ge007`) that ships as the desktop
apps, compiled to a single wasm binary and served from GitHub Pages. Same
disclaimer as everywhere else in this repo: **you supply your own legally-owned
ROM.** The site ships engine + page only — no ROM or ROM-derived data is ever
built, packaged, or hosted. See [../DISCLAIMER.md](../DISCLAIMER.md).

---

## User flow

### Browser support

The demo requires **WebGPU**. There is deliberately no WebGL fallback — a
capability gate shows a friendly message instead of a degraded experience.

| Browser              | Supported | Notes                                   |
|----------------------|-----------|------------------------------------------|
| Chrome / Edge 113+   | Yes       | WebGPU shipped by default.               |
| Safari 26+           | Yes       | WebGPU shipped by default (macOS/iOS).   |
| Firefox (Windows) 141+ | Yes     | WebGPU behind default-on as of 141.      |
| Firefox (Linux/Android) | **No**  | WebGPU not yet shipped on these platforms. |
| Any browser without WebGPU | **No** | Gate message, no fallback renderer.   |

This combination reaches roughly **82% of global browser share** (per
caniuse WebGPU data at plan time). The `No` rows are not "degraded" — the
page detects `"gpu" in navigator` and `navigator.storage.getDirectory` up
front and refuses to boot with an explanatory message rather than attempting
a broken run.

### Your ROM never leaves your browser

1. You pick a GoldenEye 007 (U) `.z64` ROM file (exactly 12 MB) via the file
   input on the page.
2. The bytes are copied into your browser's **Origin-Private File System
   (OPFS)** — storage that is private to this site's origin and invisible to
   any server. **The ROM is never uploaded anywhere; there is no server-side
   component that could receive it.** Reloading the page reads the ROM back
   out of OPFS so you don't have to pick the file again.
3. A **"Forget stored ROM"** button removes the OPFS copy immediately.

### Storage and quota

- OPFS storage is subject to the browser's normal site-storage quota. The
  page calls `navigator.storage.persist()` (best-effort; not all browsers
  grant it) after a successful store to reduce the odds of eviction under
  disk pressure.
- **Store failure does not block play.** If the OPFS write fails (quota
  exceeded, private-browsing restrictions, etc.), the page still boots the
  game from the in-memory ROM bytes you just picked — you'll simply be asked
  to pick the file again on your next visit. This is a deliberate
  degrade-gracefully choice: a storage failure is never a play blocker.
- Saves auto-persist via **IDBFS** mounted at `/save`: the in-memory
  Emscripten filesystem is flushed to IndexedDB on a 5-second timer and again
  on the `pagehide` event (covers both "still open" and "tab closing"
  exits). There is no explicit "save now" button — it's automatic, like the
  desktop build's save-on-exit behavior, just more frequent because a browser
  tab can be discarded without warning.
- **Save round-trip has been owner-verified**: play, let the timer or a
  pagehide fire, reload the page, confirm progress is intact. This is a
  manual verification step on the owner's release checklist (see
  RELEASING.md) — it is not (yet) covered by an automated browser gate (see
  "Known items" below).

---

## Developer flow

### Install emsdk

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 4.0.10
~/emsdk/emsdk activate 4.0.10
```

**4.0.10 is a floor, not a pin** — it's the first emsdk release that bundles
the `emdawnwebgpu` port this build depends on for the WebGPU bindings.
`tools/web/build_web.sh` checks for `$EMSDK_DIR/emsdk_env.sh` and prints the
install commands above if it's missing.

### Build

```sh
tools/web/build_web.sh            # Release (default)
tools/web/build_web.sh --debug    # Debug (SAFE_HEAP=1 + ASSERTIONS=2 on)
```

This configures an Emscripten-toolchain build in `build-web/` (kept separate
from the native `build/` tree — different toolchain, different CMake cache),
builds the `ge007_web` target, and stages the deployable site into
`dist/web/`: `ge007_web.js`, `ge007_web.wasm`, plus the static shell
(`web/index.html`, `web/mgb64-shell.js`, `web/style.css`) copied alongside.
Both `build-web/` and `dist/` are gitignored — they're build output, not
source.

### Serve it locally

```sh
tools/web/serve_web.sh            # serves dist/web on http://127.0.0.1:8000
tools/web/serve_web.sh 9000        # optional port override
```

A plain static file server is sufficient — there is no backend. WebGPU
requires a secure context; `127.0.0.1` counts as one, so plain HTTP against
localhost works fine for local iteration.

### The compat-seam rule

**All WebGPU dialect divergence between native (`wgpu-native`) and browser
(`emdawnwebgpu`) goes through `src/platform/fast3d/gfx_webgpu_compat.h`.**

`gfx_webgpu.c` itself must never contain an inline `#ifdef __EMSCRIPTEN__`.
Instead, the compat header supplies the seam: event pumping, blocking waits,
and surface creation are each given one name that means the same thing on
both targets, with the `#ifdef` fork living entirely inside the header. This
keeps `gfx_webgpu.c` a single, readable code path shared by every platform
instead of accumulating divergent branches inline. If you find yourself about
to add `#ifdef __EMSCRIPTEN__` directly in `gfx_webgpu.c`, that's a signal the
seam is missing a primitive — add it to the compat header instead.

### ILP32 (wasm32) notes

wasm32 is an ILP32 target: pointers are 32 bits, same width as the N64's
native 32-bit segment-relative addresses used throughout the RDP display-list
decoding. That means a bare 32-bit value flowing through the renderer's
token-resolution paths is ambiguous between "N64 segment token" and "host
pointer" in a way it never was on a 64-bit native build (where a real host
pointer can't collide with a 32-bit token).

- **Registry-authoritative discrimination**: the fix is `gfx_ptr_resolve()` —
  a small open-addressed registry (`gfx_ptr_keys`/`gfx_ptr_vals`/
  `gfx_ptr_state`, declared in `gfx_ptr.h`) that authoritatively maps
  registered host pointers back to their 32-bit tokens. A 32-bit value is only
  ever treated as a bare N64 segment address as a *fallback*, after the
  registry lookup misses.
- **`[SEG_ADDR-ILP32]` telemetry** (`gfx_pc.c`, the registry-miss path): when a
  token misses the registry and gets resolved via segment-table fallback
  instead, the first 5 occurrences per boot log a diagnostic line. This is a
  coverage signal, not a hard failure — it means "this token was either a
  genuine N64 segment address, or a host pointer that never got registered
  (a gap in registry coverage)." It never gates the sim; it's a watch-item.
- **`gfx_ptr` longevity watch-item**: the registry is a fixed-size table
  (`GFX_PTR_TABLE_SIZE`) with insertion-coverage that depends on every host
  pointer that might be walked back through the RDP path being registered at
  creation time. If a future edit adds a new pointer-bearing structure to the
  hot path without registering it, the wasm build will silently fall back to
  segment-table resolution for it — functionally survivable (per the design)
  but worth checking the `[SEG_ADDR-ILP32]` log line count if wasm rendering
  ever looks subtly wrong after a renderer-adjacent change.
- **Fixed 2026-07-16 (`68afbc6`, review-hardened in `4494a63`): mission-load
  crash class.** Two ILP32-only faults surfaced when actually loading a
  mission (not just booting to menu):
  1. A dynAllocate-pool host pointer (matrix/vertex/light/lookat/viewport/
     sub-DL) missed the `gfx_ptr` registry and got mis-routed through a live
     N64 segment to a garbage address, faulting in `gfx_sp_matrix`. Fixed by
     resolving any address that falls inside the PC dynamic DL/VTX arenas
     (`gfx_addr_is_pc_host_pool`, ranges tracked by `dyn.c`) directly as a
     host pointer *before* segment-table lookup, closing the whole
     dynAllocate* class at once (this also silently blanked the animated
     menu/folder-select character models).
  2. Boot logos (Rare/RareWare/Nintendo/N64) went missing because
     `gfx_is_static_pc_dl`'s "near program base" 64MB window — valid on LP64,
     where static data sits far from the heap — false-positived on wasm
     (static rodata and heap share one linear memory), swallowing
     heap-resident N64-registered ROM logo DLs into the host-endian PC
     interpreter path. Fixed by treating any explicitly
     `gfx_register_n64_dl_region`'d address as never-a-static-PC-DL on
     ILP32 — the static-DL window guard now defers to the N64-DL registry
     first.
  Both fixes are width-guarded (LP64/native is untouched). `4494a63` also
  makes the registry-miss/unresolvable-token counter unconditional (log line
  stays `GE007_DEBUG`-gated) so a future coverage gap shows up in release
  telemetry even without debug logging on.

### wasm size budget

CI enforces a **40 MiB hard ceiling** on `dist/web/ge007_web.wasm` (GitHub
Pages' own per-file limit is 100 MiB; 40 MiB is this project's own
fail-closed budget, checked in `web-demo.yml`). Current actual size measured
from a clean build at this task's HEAD:

```
dist/web/ge007_web.wasm: 3,991,939 bytes (~3.81 MiB)
```

— comfortably under budget. Re-measure after any change that meaningfully
grows the linked code (`stat -f%z dist/web/ge007_web.wasm` on macOS,
`stat -c%s` on Linux — the CI step uses the latter).

---

## Deploy flow

Deploys are **owner-triggered only** — `web-demo.yml` is
`workflow_dispatch`-only, matching this repo's guarded-publish, no-hosted-
auto-triggers doctrine. Nothing pushes to `main` and auto-deploys the demo.

### One-time repo setup

**Settings → Pages → Source → GitHub Actions.** This must be selected once
per repo (or per fork) before the workflow can deploy — it tells GitHub Pages
to accept artifacts from a workflow run instead of serving a branch.

### Dispatching a deploy

The owner runs `web-demo.yml` via `workflow_dispatch` (Actions tab, or `gh
workflow run web-demo.yml`). The job builds with emsdk 4.0.10, checks the
wasm size budget, runs a ROM-absence guard over the exact artifact set (fails
closed if anything unexpected — or anything starting with the N64 ROM magic
bytes — shows up in `dist/web/`), then uploads and deploys via
`actions/deploy-pages`.

### `id-token` posture

The workflow requests `permissions: id-token: write`. This is **not** a
general-purpose credential — it's the standard OIDC token
`actions/deploy-pages` requires to authenticate its deployment to GitHub
Pages, scoped by GitHub to that single purpose for this workflow run. The job
otherwise only holds `contents: read` and `pages: write`; it has no push
access to the repo and no ability to mint tokens usable outside the Pages
deploy step.

---

## Known items

- **RenderScale blocky artifacting** (flagged during W3.6): under the browser
  WebGPU backend, the RenderScale-driven offscreen scene target can be
  clamped against the GPU's max-texture-dimension limit (observed:
  8192×6144), producing visible blocky artifacting relative to native. This
  is a backend polish item, not a correctness or sim-fidelity issue — content
  renders and is recognizable — and is left for future work rather than
  blocking the demo.
- **Firefox on Linux/Android is unsupported** — WebGPU isn't shipped there
  yet. The capability gate declines gracefully rather than attempting a
  broken run; there is no fallback renderer by design (see "Browser support"
  above).
- Automated browser gates (a headless Playwright smoke, for instance) are
  explicitly deferred (YAGNI) — see the plan's "Deferred" section. The W3/W5
  manual gates plus this task's native non-regression proof are the v1 bar;
  automate if/when the demo needs to be touched often enough to justify it.
- **Resolved 2026-07-16**: the wasm32 mission-load crash class (dyn-pool
  host-pointer seg-alias + static-DL window false-positive on boot logos) —
  see "ILP32 (wasm32) notes" above (`68afbc6`, `4494a63`). Not an open item;
  kept here for discoverability since it was the demo's only known
  correctness-blocking crash.
