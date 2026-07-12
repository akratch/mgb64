# Post-boot & Launcher UX Review — 2026-07-12

**Method.** Two read-only discovery agents mapped the full user-facing surface —
(1) the launcher/app-shell (`src/app/`), (2) the in-game layers (app overlay +
the engine's diegetic watch/HUD/save/mission flow). Each derived the implied
**user story** per screen and ranked the gaps. This doc records what was fixed
this pass and what is deliberately deferred (with why), so the review is durable.

**Guardrail.** Everything landed here is **host-side** (app-shell / SDL / ImGui):
determinism is byte-identical (7 tapes green after each batch) and the config
lanes stay green. Engine-sim / diegetic-menu changes are determinism-sensitive
and are deferred to owner gameplay validation — see §3.

---

## 1. Fixed this pass (host-side, landed on `dev/post-v0.4.0-backlog`)

### Post-boot in-game UX (the "make it awesome" arc)
- **Rebindable menu/FPS toggles** — `Input.MenuToggleKey/Button`, `Input.FpsToggleKey`
  (read live; determinism-neutral). `809b9ee`
- **Settings staging layer** — mid-game edits go to a working copy; Apply commits +
  saves, Cancel reverts, Resume commits; FOV/sensitivity live Preview. The engine
  reads its globals directly, so it keeps running on last-applied values (safe in
  MP, which never pauses). New `test_config_staging` (13 checks). `f0aec90`, `da13330`
- **Device- & binding-aware control hints** in the overlay footer (pad glyphs vs
  keyboard keys; names the actual rebindable binding). `4297edc`

### Launcher
- **Quit button** in the nav footer — the only in-app exit on a handheld in
  borderless fullscreen; controller-reachable (the `Quit` action existed but no
  panel emitted it). `9c2fe5c`
- **Persisted launch options** (level/difficulty/MP/players) + load launch & mode
  selections unconditionally at startup — fixes an adjacent bug where a direct Play
  (without visiting Modes/Launch) silently used struct defaults and ignored saved
  preset/toggles. `9c2fe5c`
- **Colorblind ROM status** — validation card leads with a "Ready/Problem" word, not
  color alone. **No Play dead-end** — disabled Play offers "Go to Game ROM". `9c2fe5c`
- **Honest update status in About** — Checking / up-to-date / available / off /
  offline (new `UpdateCheck_didCheck()` distinguishes "checked & current" from
  "declined/offline"). `64e6adc`
- **First-run welcome** on the Game ROM panel (vanishes once a ROM is set). `b823e13`

### Controls
- **Rebind conflict detection** — duplicate key/button bindings flagged with a text
  suffix (not color alone) + tint + tooltip. `a59621c`
- **Tab → watch wired** (mirrors Esc→Start): the H-help and Controls panel both
  advertised "Tab = Watch/Start" but no handler existed; now truthful. `64e6adc`

### Honesty fixes
- **Windows "Return to Launcher" hidden** — its re-exec path isn't wired on Windows
  (it silently quit); Windows keeps a truthful "Quit to Desktop". `64e6adc`
- **Solo overlay cross-reference** — points solo players to the in-game watch for
  objectives/mission options/progress (the overlay deliberately doesn't duplicate
  them), reducing the two-menus confusion. `64e6adc`

---

## 2. The structural finding

MGB64 has **two parallel pause/menu systems that don't reconcile**, plus more
settings surfaces than a user can hold in their head:

1. **App-shell F1/Back overlay** — Resume / Settings / Return-to-Launcher / Quit.
2. **GoldenEye diegetic watch** (Start / Esc / Tab) — objectives, mission options,
   control style, save-via-progress.
3. **Front-end 007 Options** (`front.c:5145`) — retail sound/control, pre-mission.
4. **A fully-built but UNMOUNTED in-engine settings menu** (`pc_settings_menu.c`) —
   7 curated pages drawing from the *same* registry as the F1 overlay, reachable
   only via the `GE007_SETTINGS_MENU_FORCE` debug preview.

The host-side cross-reference (§1) is a bandaid. The real fix is convergence
(Phase 5 of the post-boot plan: Start → one hub that delegates to the watch), which
is engine-sim and gated on owner gameplay.

---

## 3. Deferred — engine-sim / determinism-sensitive (need owner gameplay)

Ranked by value. Each touches the decomp/sim or diegetic render path, so none were
changed blind.

1. **Unify the two pause menus** (Start → single hub delegating to the watch).
   Post-boot plan Phase 5; `mp_watch.c` + `ui_overlay.cpp` + `lvl.c:2094/2176`.
   Determinism guard = tape regression; UX needs both-device gameplay + `GE007_LEGACY_START_WATCH` A/B.
2. **Mount `pc_settings_menu.c`** — the best in-game settings UI exists and is invisible.
   Risky (DL/determinism render path). `pc_settings_menu.c:142-171`.
3. **Restart Mission / Retry / checkpoint** — "I died, try again" has no affordance.
   Overlay button is host-side-safe; the actual level reload is engine-sim. `front.c:9057`.
4. **Dialogue subtitles/captions** — accessibility gap; HUD text path `bondview.c:7185`.
5. **Settings-store unification** — invert/sound live in ≥3 stores (watch `game_options`,
   007 Options, settings registry) with no guaranteed sync. `watch.c:241-249`.
6. **Objectives on the HUD** — currently only in watch→Briefing→Objectives. `watch.c:11619`.
7. **Windows return-to-launcher re-exec** — implement via `GetModuleFileNameA`+`CreateProcess`;
   needs Windows build/test (why it's hidden today, not implemented blind). `ui_overlay.cpp:66`.
8. **Save success/failure + non-writable savedir warning** — "did my progress save?"
   savedir decision at `savedir.c:156` is stdout-only. The launcher could warn pre-boot (host-side).

## 4. Deferred — host-side, lower priority (safe to do later)

- Native file dialogs (NFD) aren't controller-navigable (the in-app browser is the pad path);
  the About claim was softened from "Fully" to "Controller-navigable".
- Multiplayer setup is thin (player count + "default stage" only) — no stage/mode/character.
- Advanced env textarea is an unvalidated "unsupported" box (silent-ignore on bad lines).
- `(unsupported type)` fall-through remains for unknown config kinds (`ui_settings.cpp`).
- No "recently played" ROM history (single remembered `last_rom`).
- ROM validation rejects legit ROM-hacks (requires literal "GOLDENEYE" title) with no override.

---

*Strengths worth preserving:* full keyboard+gamepad rebinding UI, thorough ROM
validation messaging, auto-scan + drag-drop + in-app browser fallbacks, and a
genuinely good diagnostics/bug-report flow.
