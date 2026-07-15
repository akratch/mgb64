// mgb64-shell.js — static-page shell for the MGB64 wasm engine.
// Storage design: ROM bytes -> OPFS (persist once, reseed each visit);
// saves/config -> IDBFS mounted at /save (FS.syncfs on a timer + pagehide).
//
// ADAPTATION (documented, see docs/superpowers/plans/2026-07-15-web-port.md
// Task W5 vs. build reality): the plan's Step 2 loads the engine via
// `const { default: createMGB64 } = await import("./ge007_web.js")`, which
// assumes an ES module build. The actual CMakeLists.txt link options for
// `ge007_web` (-sMODULARIZE=1 -sEXPORT_NAME=createMGB64, no -sEXPORT_ES6) emit
// emscripten's default UMD-style factory: it ends with
// `if (typeof exports === 'object' ...) module.exports = createMGB64; ...`
// and otherwise falls through to a bare `var createMGB64 = ...` in whatever
// scope it is evaluated in. Verified directly against build-web/ge007_web.js
// and against the W3.6 proof-of-render harness
// (scratchpad/w3-smoke/harness_w36.html), which loads it as a plain classic
// `<script src="ge007_web.js">` and reads the resulting `window.createMGB64`
// global — there is no `export` statement for `import()` to bind to. This
// shell therefore injects ge007_web.js as a classic script tag and reads the
// global factory instead of using dynamic `import()`. Consequently
// index.html also loads *this* file as a classic script (no
// `type="module"`), since nothing else here needs ESM semantics.
const ROM_OPFS_NAME = "baserom.z64";
const ROM_SIZE = 12 * 1024 * 1024;

const $ = (id) => document.getElementById(id);

async function opfsRoot() { return await navigator.storage.getDirectory(); }

async function loadStoredRom() {
  try {
    const dir = await opfsRoot();
    const fh = await dir.getFileHandle(ROM_OPFS_NAME);
    const f = await fh.getFile();
    if (f.size !== ROM_SIZE) return null;
    return new Uint8Array(await f.arrayBuffer());
  } catch { return null; }
}

async function storeRom(bytes) {
  const dir = await opfsRoot();
  const fh = await dir.getFileHandle(ROM_OPFS_NAME, { create: true });
  const w = await fh.createWritable();
  await w.write(bytes); await w.close();
  try { await navigator.storage.persist(); } catch {}
}

async function forgetRom() {
  const dir = await opfsRoot();
  try { await dir.removeEntry(ROM_OPFS_NAME); } catch {}
}

function gate() {
  if (!("gpu" in navigator))
    return "This demo needs WebGPU (Chrome/Edge 113+, Safari 26+, Firefox on Windows). Your browser doesn't support it yet.";
  if (!("storage" in navigator) || !navigator.storage.getDirectory)
    return "This demo needs Origin-Private File System storage, which your browser doesn't support.";
  return null;
}

// Loads ge007_web.js as a classic script (see file-header ADAPTATION note)
// and returns the `createMGB64` factory it installs on `window`.
let _enginePromise = null;
function loadEngineFactory() {
  if (window.createMGB64) return Promise.resolve(window.createMGB64);
  if (_enginePromise) return _enginePromise;
  _enginePromise = new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = "ge007_web.js";
    s.onload = () => {
      if (window.createMGB64) resolve(window.createMGB64);
      else reject(new Error("ge007_web.js loaded but did not define createMGB64"));
    };
    s.onerror = () => reject(new Error("failed to load ge007_web.js"));
    document.head.appendChild(s);
  });
  return _enginePromise;
}

async function boot(romBytes) {
  $("gate").hidden = true;
  const canvas = $("mgb64-canvas"); canvas.hidden = false;
  // Non-streaming instantiate: GitHub Pages may serve .wasm with a wrong MIME.
  const wasmBinary = await (await fetch("ge007_web.wasm")).arrayBuffer();
  const createMGB64 = await loadEngineFactory();
  const m = await createMGB64({ canvas, wasmBinary, noInitialRun: true });
  m.FS.mkdir("/rom"); m.FS.writeFile("/rom/baserom.z64", romBytes);
  m.FS.mkdir("/save"); m.FS.mount(m.IDBFS, {}, "/save");
  await new Promise((res, rej) => m.FS.syncfs(true, (e) => e ? rej(e) : res()));
  const persist = () => m.FS.syncfs(false, () => { console.log("[shell] syncfs"); });
  setInterval(persist, 5000);
  addEventListener("pagehide", persist);
  m.callMain(["--rom", "/rom/baserom.z64", "--savedir", "/save"]);
}

(async () => {
  const err = gate();
  if (err) { $("gate-msg").textContent = err; return; }
  $("gate-msg").textContent = "";
  $("rom-ui").hidden = false;
  let rom = await loadStoredRom();
  const showReady = () => { $("rom-status").textContent = "ROM ready (stored in this browser)."; $("play").disabled = false; $("forget").hidden = false; };
  if (rom) showReady();
  $("rom-input").addEventListener("change", async (ev) => {
    const f = ev.target.files[0]; if (!f) return;
    if (f.size !== ROM_SIZE) { $("rom-status").textContent = `Wrong size (${f.size} bytes) — expected exactly 12 MB.`; return; }
    rom = new Uint8Array(await f.arrayBuffer());
    await storeRom(rom); showReady();
  });
  $("forget").addEventListener("click", async () => { await forgetRom(); location.reload(); });
  // Boot on click — the user gesture unlocks WebAudio.
  $("play").addEventListener("click", () => boot(rom).catch(e => { $("gate").hidden = false; $("gate-msg").textContent = "Boot failed: " + e; }));
})();
