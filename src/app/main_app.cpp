// main_app.cpp — entry point for the portable MGB64 app shell.
//
// Task 1 (smoke): with no CLI args, open an ImGui window; with any args,
// delegate to the unchanged engine path. Task 3 replaces the crude "any args"
// check with a precise automation-flag allow-list (mgb_is_automation_invocation)
// so that non-automation flags (e.g. --rom) route into the launcher instead.
#include "app_host.h"
#include "arg_triage.h"
#include "config_schema.h"
#include "diag_log.h"
#include "engine_entry.h"
#include "ui_launcher.h"
#include "ui_overlay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
    // Automation/diagnostic invocations bypass the launcher and run the
    // unchanged engine path (byte-identical). Everything else opens the app.
    if (mgb_is_automation_invocation(argc, argv)) {
        return mgb64_headless_main(argc, argv);
    }

    AppHost host;
    if (!host.init("MGB64", 1280, 800)) {
        return 1;
    }

    // Tee engine output to the in-app console + mgb64.log (interactive path
    // only; the automation path returned above, so it is never redirected).
    DiagLog_install();

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
