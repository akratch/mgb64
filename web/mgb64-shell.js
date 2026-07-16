// mgb64-shell.js — static-page shell for the MGB64 wasm engine.
// Storage design: ROM bytes -> OPFS (persist once, reseed each visit);
// saves/config -> IDBFS mounted at /save (FS.syncfs on a timer + pagehide).
//
// Engine load: the `ge007_web` link options (-sMODULARIZE=1
// -sEXPORT_NAME=createMGB64, no -sEXPORT_ES6) emit emscripten's default
// UMD-style factory — it ends with
// `if (typeof exports === 'object' ...) module.exports = createMGB64; ...`
// and otherwise falls through to a bare `var createMGB64 = ...` in whatever
// scope it is evaluated in. There is no `export` statement for `import()` to
// bind to, so this shell injects ge007_web.js as a classic script tag and
// reads the resulting `window.createMGB64` global rather than using dynamic
// `import()`. index.html therefore also loads *this* file as a classic script
// (no `type="module"`), since nothing else here needs ESM semantics.
const ROM_OPFS_NAME = "baserom.z64";
const ROM_SIZE = 12 * 1024 * 1024;

// Favicon (trademark-free crosshair in the page accent), generated at runtime
// as a Blob URL. Deliberately NOT a tracked data: URI (contamination guard
// forbids embedded data-URI payloads) and NOT a 6th dist file (the web-demo
// workflow pins the Pages artifact to exactly 5 files). Kills /favicon.ico 404.
(() => {
  const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">'
    + '<g stroke="#5ce09a" stroke-width="2" fill="none">'
    + '<circle cx="16" cy="16" r="9"/>'
    + '<line x1="16" y1="2" x2="16" y2="9"/><line x1="16" y1="23" x2="16" y2="30"/>'
    + '<line x1="2" y1="16" x2="9" y2="16"/><line x1="23" y1="16" x2="30" y2="16"/>'
    + '</g><circle cx="16" cy="16" r="2.5" fill="#5ce09a"/></svg>';
  const link = document.createElement("link");
  link.rel = "icon";
  link.href = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
  document.head.appendChild(link);
})();

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

// Returns true on success. Storing is a convenience (persist across visits),
// not a requirement — callers must still let the user boot from the in-memory
// bytes even when this fails (e.g. OPFS quota exceeded).
async function storeRom(bytes) {
  try {
    const dir = await opfsRoot();
    const fh = await dir.getFileHandle(ROM_OPFS_NAME, { create: true });
    const w = await fh.createWritable();
    await w.write(bytes); await w.close();
    try { await navigator.storage.persist(); } catch {}
    return true;
  } catch (e) {
    console.error("[shell] storeRom failed:", e);
    return false;
  }
}

async function forgetRom() {
  const dir = await opfsRoot();
  try { await dir.removeEntry(ROM_OPFS_NAME); } catch {}
}

function gate() {
  if (!("gpu" in navigator))
    return "This demo needs WebGPU (Chrome/Edge 113+, Safari 26+, Firefox 141+ on Windows). Your browser doesn't support it yet.";
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

let booted = false;
async function boot(romBytes) {
  if (booted) return;
  booted = true;
  $("gate").hidden = true;
  const canvas = $("mgb64-canvas"); canvas.hidden = false;
  // Additive UI only: reveal the unobtrusive in-game hint (F1/F10/Esc). It
  // self-fades via CSS and is purely cosmetic — no bearing on the boot contract.
  const hint = $("overlay-hint"); if (hint) hint.hidden = false;
  // Non-streaming instantiate: GitHub Pages may serve .wasm with a wrong MIME.
  const wasmBinary = await (await fetch("ge007_web.wasm")).arrayBuffer();
  const createMGB64 = await loadEngineFactory();
  const m = await createMGB64({ canvas, wasmBinary, noInitialRun: true });
  m.FS.mkdir("/rom"); m.FS.writeFile("/rom/baserom.z64", romBytes);
  m.FS.mkdir("/save"); m.FS.mount(m.IDBFS, {}, "/save");
  await new Promise((res, rej) => m.FS.syncfs(true, (e) => e ? rej(e) : res()));
  const persist = () => m.FS.syncfs(false, () => {});
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
    const stored = await storeRom(rom);
    if (stored) {
      showReady();
    } else {
      // Persistence failed (e.g. OPFS quota) — booting from the in-memory
      // bytes still works, so still enable Play.
      $("rom-status").textContent = "Couldn't store the ROM in browser storage (quota?). You can still pick it again next visit.";
      $("play").disabled = false;
    }
  });
  $("forget").addEventListener("click", async () => { await forgetRom(); location.reload(); });
  // Boot on click — the user gesture unlocks WebAudio.
  $("play").addEventListener("click", () => {
    $("play").disabled = true;
    boot(rom).catch(e => { $("gate").hidden = false; $("gate-msg").textContent = "Boot failed: " + e; });
  });
})();
