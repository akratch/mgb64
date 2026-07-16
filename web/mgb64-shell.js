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

// WEB-003: `"gpu" in navigator` is NOT enough. Adapter-less configurations
// (Chrome on Linux defaults, blocklisted/disabled GPUs, some Android) expose
// navigator.gpu but resolve requestAdapter() to null — those users would pass a
// synchronous gate, store a ROM, hit Play, and boot to a permanently black
// canvas (sim/audio running against nothing). Requiring a real adapter here
// turns that trap into an up-front, honest refusal. Async as a result.
async function gate() {
  if (!("gpu" in navigator))
    return "This demo needs WebGPU (Chrome/Edge 113+, Safari 26+, Firefox 141+ on Windows). Your browser doesn't support it yet.";
  if (!("storage" in navigator) || !navigator.storage.getDirectory)
    return "This demo needs Origin-Private File System storage, which your browser doesn't support.";
  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter)
      return "Your browser exposes WebGPU but no compatible GPU is available (it may be blocklisted or disabled in your browser/OS settings). The game can't render here.";
  } catch (e) {
    return "WebGPU adapter request failed (" + e + "). The game can't render here.";
  }
  return null;
}

// WEB-003 / WEB-010 / WEB-025: single human-readable fatal-error surface. The C
// WebGPU bring-up failure path calls this via window.mgb64ShowError (EM_ASM in
// gfx_webgpu_compat.h), and the global crash handlers below route through it
// too. First message wins; it hides the canvas so the user sees text, not an
// inert black frame.
let _fatalShown = false;
function showFatal(msg) {
  if (_fatalShown) return;
  _fatalShown = true;
  const c = $("canvas"); if (c) c.hidden = true;
  const hint = $("overlay-hint"); if (hint) hint.hidden = true;
  let panel = $("fatal");
  if (!panel) {
    panel = document.createElement("div");
    panel.id = "fatal";
    panel.setAttribute("role", "alert");
    panel.style.cssText =
      "position:fixed;inset:0;display:flex;align-items:center;justify-content:center;" +
      "padding:2rem;text-align:center;font:500 1.1rem/1.5 system-ui,sans-serif;" +
      "color:#e6f2ea;background:#0b0f0d;z-index:9999;";
    document.body.appendChild(panel);
  }
  panel.textContent = msg;
  panel.hidden = false;
}
// WEB-009 interplay: a fatal shown during a FAILED boot (onAbort/bring-up
// failure) must not permanently cover the recovered gate UI — boot().catch
// calls this to drop the panel and re-arm the latch so a retry can surface a
// fresh failure.
function hideFatal() {
  _fatalShown = false;
  const panel = $("fatal"); if (panel) panel.hidden = true;
}
// Exposed for the C bring-up/device-lost path (see gfx_webgpu_compat.h).
window.mgb64ShowError = (msg) =>
  showFatal(msg || "The graphics device failed to start. Reload to try again.");

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
let _module = null;  // set once the engine module exists, for crash-path syncfs/audio cleanup

// WEB-010: crash surface. Asyncify makes callMain() return at the first stack
// unwind (the rAF yield), so a mid-game wasm trap NEVER reaches boot().catch —
// it surfaces as a global window error/rejection instead. These handlers are the
// only net once the game is running: show a message and make one last-ditch
// attempt to flush saves so the user knows a reload preserves their auto-save.
function handleCrash() {
  // A clean engine exit (ROM rejection) is surfaced by handleExit, not here —
  // don't mislabel it a crash if the ExitStatus also trips the global handlers.
  if (_exitHandled) return;
  // Honest messaging: only promise an auto-save once at least one persist
  // actually succeeded (_savedOnce) — a crash before the first syncfs has no
  // save to come back to.
  showFatal(_savedOnce
    ? "The game crashed — reload to continue from your last auto-save."
    : "The game crashed — reload the page to try again.");
  try { _module?.FS?.syncfs(false, () => {}); } catch {}
}
let _savedOnce = false;

// WEB-002: keep the last engine stderr lines so a nonzero exit (the engine
// refusing a wrong-content/PAL/J ROM in rom_io.c) can be shown to the user
// instead of vanishing into devtools. Bounded ring — we only surface the tail.
let _stderrTail = [];
function recordStderr(text) {
  console.error(text);
  _stderrTail.push(String(text));
  if (_stderrTail.length > 20) _stderrTail.shift();
}

// WEB-002: the engine's main() returns nonzero when rom_io.c refuses the ROM
// (bad header, wrong game, or — after the US offsets are patched onto a PAL/J
// cart that passed the title scan — a decompression fault at frame 0). Under
// Asyncify callMain() unwinds at the first yield, so we learn the outcome via
// onExit, not a return value. A nonzero exit is a REJECTION, not a crash: re-show
// the gate with the reason + the last stderr lines + the Forget-stored-ROM
// shortcut, because the bad file is already in OPFS and would re-poison every
// future visit (the exact trap WEB-002 targets).
let _exitHandled = false;
function handleExit(code) {
  if (code === 0 || _exitHandled) return;
  _exitHandled = true;
  booted = false;
  hideFatal();
  const canvas = $("canvas"); if (canvas) canvas.hidden = true;
  const hint = $("overlay-hint"); if (hint) hint.hidden = true;
  $("gate").hidden = false;
  $("rom-ui").hidden = false;
  const tail = _stderrTail.filter(Boolean).slice(-6).join("\n");
  const msg = $("gate-msg");
  msg.style.whiteSpace = "pre-wrap";
  msg.textContent =
    "The engine refused this ROM (exit " + code + "). It's likely the wrong " +
    "game or region — GoldenEye 007 (USA) is required." +
    (tail ? "\n\n" + tail : "");
  // The bad ROM is stored in OPFS; make its removal one click away.
  const forget = $("forget"); if (forget) forget.hidden = false;
  const play = $("play"); if (play) play.disabled = false;
}

// WEB-002: instant client-side content check at pick time (no canonical ROM
// hash exists in-repo to SHA-1 against, so we mirror rom_io.c's own header+title
// gate on the picked bytes). Catches the common "12 MB non-GoldenEye file" case
// up front — the black-screen-then-repoison trap — rather than after a full boot.
// Returns null when the bytes look like a GoldenEye N64 ROM, else a reason string.
function validateRomContent(bytes) {
  if (!bytes || bytes.length < 0x40) return "That file is too small to be an N64 ROM.";
  // Normalize just the header to big-endian (.z64) for the checks, mirroring
  // rom_io.c's byteswap for .v64 (swap 2) / .n64 (swap 4).
  const h = bytes.slice(0, 0x40);
  if (h[0] === 0x37 && h[1] === 0x80) {
    for (let i = 0; i + 1 < h.length; i += 2) { const t = h[i]; h[i] = h[i + 1]; h[i + 1] = t; }
  } else if (h[0] === 0x40 && h[1] === 0x12) {
    for (let i = 0; i + 3 < h.length; i += 4) {
      const a = h[i], b = h[i + 1], c = h[i + 2], d = h[i + 3];
      h[i] = d; h[i + 1] = c; h[i + 2] = b; h[i + 3] = a;
    }
  }
  if (h[0] !== 0x80 || h[1] !== 0x37 || h[2] !== 0x12 || h[3] !== 0x40)
    return "That file isn't an N64 ROM (bad header). Expected a GoldenEye 007 .z64/.v64/.n64 dump.";
  // Internal cartridge title must contain "GOLDENEYE" (scan 0x20..0x34 as rom_io.c does).
  const GE = [0x47, 0x4F, 0x4C, 0x44, 0x45, 0x4E, 0x45, 0x59, 0x45];
  let isGE = false;
  for (let i = 0x20; i + 9 <= 0x34 && !isGE; i++) {
    isGE = true;
    for (let j = 0; j < 9; j++) { if (h[i + j] !== GE[j]) { isGE = false; break; } }
  }
  if (!isGE)
    return "That ROM isn't GoldenEye 007 (internal title mismatch). Please supply the GoldenEye 007 cartridge dump.";
  return null;
}

async function boot(romBytes) {
  if (booted) return;
  booted = true;
  $("gate").hidden = true;
  const canvas = $("canvas"); canvas.hidden = false;
  // WEB-040: aim is right-click; without this the browser context menu pops on
  // every RMB whenever the pointer isn't locked.
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  // Additive UI only: reveal the unobtrusive in-game hint. It self-fades via CSS
  // and is purely cosmetic — no bearing on the boot contract.
  const hint = $("overlay-hint"); if (hint) hint.hidden = false;
  // Non-streaming instantiate: GitHub Pages may serve .wasm with a wrong MIME.
  const resp = await fetch("ge007_web.wasm");
  // WEB-033: without this an HTTP 404/500 becomes a cryptic
  // "CompileError: expected magic word" instead of a clear download failure.
  if (!resp.ok) throw new Error(`engine download failed (HTTP ${resp.status})`);
  const wasmBinary = await resp.arrayBuffer();
  const createMGB64 = await loadEngineFactory();
  // WEB-010: onAbort catches emscripten aborts (assertion/OOM) that don't throw
  // through JS; the window handlers catch the RuntimeError traps.
  const m = await createMGB64({
    canvas, wasmBinary, noInitialRun: true,
    onAbort: () => handleCrash(),
    // WEB-002: capture engine stderr and the process exit code so a ROM the
    // engine refuses surfaces to the user instead of a silent black canvas.
    printErr: (t) => recordStderr(t),
    onExit: (code) => handleExit(code),
  });
  _module = m;
  m.FS.mkdir("/rom"); m.FS.writeFile("/rom/baserom.z64", romBytes);
  m.FS.mkdir("/save"); m.FS.mount(m.IDBFS, {}, "/save");
  await new Promise((res, rej) => m.FS.syncfs(true, (e) => e ? rej(e) : res()));
  const persist = () => m.FS.syncfs(false, (e) => { if (!e) _savedOnce = true; });
  setInterval(persist, 5000);
  addEventListener("pagehide", persist);
  // Arm the global crash net only now that the engine is actually running, so
  // it can't fire on a boot-phase error already handled by boot().catch.
  addEventListener("error", handleCrash);
  addEventListener("unhandledrejection", handleCrash);
  // WEB-008: Ctrl+W (crouch-forward, the most common FPS chord) and Cmd+W close
  // the tab, and the browser won't let preventDefault stop it — the only lever
  // is a beforeunload confirm. Arm it once booted so a mis-hit chord asks before
  // discarding mission progress. (No custom string: modern browsers show their
  // own generic prompt, but any non-empty returnValue triggers it.)
  addEventListener("beforeunload", (e) => { e.preventDefault(); e.returnValue = ""; });
  // WEB-008: while fullscreen, best-effort Keyboard Lock so Ctrl+W / Cmd+W and
  // other system chords route to the game instead of the browser. Silently a
  // no-op where unsupported (Safari/Firefox) or not fullscreen — never throws.
  const applyKeyboardLock = () => {
    try {
      if (document.fullscreenElement && navigator.keyboard && navigator.keyboard.lock) {
        navigator.keyboard.lock(["KeyW", "KeyA", "KeyS", "KeyD",
                                 "ControlLeft", "ControlRight", "Escape"]).catch(() => {});
      } else if (navigator.keyboard && navigator.keyboard.unlock) {
        navigator.keyboard.unlock();
      }
    } catch {}
  };
  document.addEventListener("fullscreenchange", applyKeyboardLock);
  const args = ["--rom", "/rom/baserom.z64", "--savedir", "/save"];
  const unlockAll = $("unlock-all");
  if (unlockAll && unlockAll.checked) args.push("--unlock-all-levels");
  m.callMain(args);
}

(async () => {
  const err = await gate();
  if (err) { $("gate-msg").textContent = err; return; }
  // WEB-011: iOS/iPadOS Safari passes the WebGPU gate but the engine is
  // keyboard/mouse/gamepad only (no pointer lock on iOS; touch = fire-only), so
  // a phone user would invest a 12 MB upload before discovering they can't play.
  // Warn a coarse-pointer / touch-primary device up front — but still allow boot
  // (paired keyboards and controllers do exist). Not a gate refusal.
  const touchPrimary =
    (matchMedia && matchMedia("(pointer: coarse)").matches) &&
    (navigator.maxTouchPoints || 0) > 0;
  $("gate-msg").textContent = touchPrimary
    ? "Heads up: this game needs a keyboard & mouse (or a gamepad). On a touch-only device you can look around but not move — pair a keyboard/controller for full control."
    : "";
  $("rom-ui").hidden = false;
  let rom = await loadStoredRom();
  const showReady = () => { $("rom-status").textContent = "ROM ready (stored in this browser)."; $("play").disabled = false; $("forget").hidden = false; };
  if (rom) showReady();
  $("rom-input").addEventListener("change", async (ev) => {
    const f = ev.target.files[0]; if (!f) return;
    if (f.size !== ROM_SIZE) { $("rom-status").textContent = `Wrong size (${f.size} bytes) — expected exactly 12 MB.`; return; }
    const picked = new Uint8Array(await f.arrayBuffer());
    // WEB-002: reject wrong-content files at pick time so they never reach OPFS
    // (an unrecognized 12 MB file, stored, would re-poison every future visit).
    const bad = validateRomContent(picked);
    if (bad) { $("rom-status").textContent = bad; return; }
    rom = picked;
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
    boot(rom).catch(e => {
      // WEB-009: a transient boot failure (wasm fetch blip, syncfs rejection)
      // must not wedge the page. Reset all boot state so Play works again and
      // the gate returns, and close the AudioContext SDL opened during factory
      // init (a ScriptProcessorNode keeps burning CPU otherwise).
      booted = false;
      // If the failure surfaced as a fatal panel (onAbort/bring-up), drop it —
      // otherwise it sits at z-index 9999 over the re-shown gate and the Play
      // retry is unreachable; also re-arms the one-shot latch for the retry.
      hideFatal();
      $("canvas").hidden = true;
      const hint = $("overlay-hint"); if (hint) hint.hidden = true;
      try { _module?.SDL2?.audioContext?.close(); } catch {}
      $("gate").hidden = false;
      $("gate-msg").textContent = "Boot failed: " + e;
      $("play").disabled = false;
    });
  });
})();
