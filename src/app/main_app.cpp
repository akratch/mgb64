// main_app.cpp — entry point for the portable MGB64 app shell.
//
// Task 1 (smoke): with no CLI args, open an ImGui window; with any args,
// delegate to the unchanged engine path. Task 3 replaces the crude "any args"
// check with a precise automation-flag allow-list (mgb_is_automation_invocation)
// so that non-automation flags (e.g. --rom) route into the launcher instead.
#include "app_config.h"
#include "app_host.h"
#include "arg_triage.h"
#include "config_schema.h"
#include "diag_log.h"
#include "engine_entry.h"
#include "ui_launcher.h"
#include "ui_overlay.h"
#include "update_check.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Headless validation harness for the update check: runs the real gated check
// (curl subprocess + tag parse + semver compare) once and prints the outcome,
// then exits — no window. Drives the mock via GE007_UPDATE_CHECK_URL and the
// negative controls (GE007_UPDATE_CHECK=0, empty PATH, automation flags). Mirrors
// the MGB64_APP_DUMP_SCHEMA self-check pattern below.
static int updateCheckSelfTest(int argc, char **argv) {
    mgb_config_init();  // registers Game.CheckForUpdates so the toggle gate reads it
    AppConfig::load();  // so a previously dismissed tag suppresses the banner
    UpdateCheck_start(argc, argv);
    for (int i = 0; i < 120 && !UpdateCheck_isDone(); ++i) {
        SDL_Delay(50);  // curl --max-time is 3s; wait a little past it
    }
    char tag[128];
    if (UpdateCheck_bannerTag(tag, sizeof(tag))) {
        std::printf("[selftest] banner: %s\n", tag);
    } else {
        std::printf("[selftest] no banner\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    // Headless update-check validation (before the automation gate so it can be
    // exercised alongside automation flags to prove the internal gate).
    if (std::getenv("MGB64_UPDATE_CHECK_SELFTEST")) {
        return updateCheckSelfTest(argc, argv);
    }

    // Automation/diagnostic invocations bypass the launcher and run the
    // unchanged engine path (byte-identical). Everything else opens the app.
    if (mgb_is_automation_invocation(argc, argv)) {
        return mgb64_headless_main(argc, argv);
    }

    // Tee stdout/stderr to the in-app console + mgb64.log BEFORE host.init, so
    // its fatal init diagnostics ([app] SDL_Init/CreateWindow/... failed) are
    // captured even under the GUI subsystem (-mwindows), where there is no
    // console to catch them. This is interactive-path only — the automation
    // path returned above, so it is never redirected — and the only code before
    // this point (mgb_is_automation_invocation) emits nothing. DiagLog_install
    // uses only SDL_GetPrefPath + a pipe, so it is safe before SDL_Init.
    DiagLog_install();

    AppHost host;
    if (!host.init("MGB64", 1280, 800)) {
        return 1;
    }

    // Register + load the engine config so the launcher's settings panel can
    // enumerate + edit it before a game boots. Idempotent with the engine boot.
    mgb_config_init();

    // Non-interactive schema self-check (validates config_schema enumeration).
    if (std::getenv("MGB64_APP_DUMP_SCHEMA")) {
        int n = mgb_config_count();
        std::printf("[app] config schema: %d settings\n", n);
        for (int i = 0; i < n; ++i) {
            MgbCfgEntry e;
            if (mgb_config_get(i, &e) && std::strcmp(e.key, "Video.VSync") == 0) {
                std::printf("[app] Video.VSync kind=%d live=%d enum_count=%d cur_idx=%d cur_token=%s\n",
                            (int)e.kind, e.is_live, e.enum_count, e.cur_enum_index,
                            mgb_config_enum_token(e.key, e.cur_enum_index));
            }
        }
        host.shutdown();
        return 0;
    }

    Launcher launcher;

    // Boot the engine into the shell's window: adopt the window, then run the
    // shared engine boot path. Blocks until the game exits.
    auto play = [&](const MgbBootConfig &cfg) {
        platformSetHostWindow(host.window(), host.glContext());
        Overlay_install(host.window(), argv[0]);  // in-game overlay (F1)
        mgb64_engine_boot(&cfg);
    };

    // Validation/CI: immediately boot into the shell window (with
    // MGB64_BOOT_SCREENSHOT_FRAME the engine screenshots + exits), proving the
    // launcher->engine handoff non-interactively.
    if (std::getenv("MGB64_APP_AUTOPLAY")) {
        // Isolate save state (MGB64_APP_SAVEDIR) so the validation boot never
        // pollutes the user's eeprom/ini or the byte-identity harness.
        MgbBootConfig cfg = {nullptr, std::getenv("MGB64_APP_SAVEDIR"), -1, -1, 0, 0, -1};
        cfg.level_slug = std::getenv("MGB64_APP_AUTOPLAY_LEVEL");  // exercises level boot
        play(cfg);
        host.shutdown();
        return 0;
    }

    // Headless smoke path (also used by CI in Task 11): render a fixed number
    // of frames regardless of window events, then exit with proof. Optionally
    // capture the final frame as a BMP (MGB64_APP_SMOKE_SHOT) for design review.
    const char *smoke = std::getenv("MGB64_APP_SMOKE_FRAMES");
    if (smoke && smoke[0]) {
        int frames = std::atoi(smoke);
        if (frames < 1) frames = 1;
        const char *shot = std::getenv("MGB64_APP_SMOKE_SHOT");
        bool sawQuit = false;
        for (int i = 0; i < frames; ++i) {
            if (host.pumpAndShouldQuit()) sawQuit = true;  // drain, don't exit
            host.beginFrame();
            launcher.draw(host);
            host.endFrame((i == frames - 1) ? shot : nullptr);
        }
        std::printf("[app] smoke: rendered %d frames, drawable %dx%d, sawQuit=%d\n",
                    frames, host.drawableWidth(), host.drawableHeight(), sawQuit ? 1 : 0);
        host.shutdown();
        return 0;
    }

    // Quiet, once-per-session check for a newer GitHub release on a background
    // thread (never blocks launch). Interactive path only — the automation/smoke/
    // autoplay/schema paths all returned above, so no test lane spawns curl. Also
    // internally gated on the automation flags + Game.CheckForUpdates + env.
    UpdateCheck_start(argc, argv);

    bool running = true;
    while (running) {
        if (host.pumpAndShouldQuit()) break;
        host.beginFrame();
        LauncherAction action = launcher.draw(host);
        host.endFrame();
        if (action.type == LauncherActionType::Quit) {
            running = false;
        } else if (action.type == LauncherActionType::Play) {
            play(action.boot);  // blocks; game renders into this window
            running = false;    // Task 9 adds return-to-launcher (re-exec)
        }
    }
    host.shutdown();
    return 0;
}
