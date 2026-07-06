cmake_minimum_required(VERSION 3.16)
project(ge007 C)

# macOS-only native Metal rendering backend (opt-in; GL stays default). The
# backend TU is Objective-C++ against the system Metal/QuartzCore frameworks —
# no metal-cpp vendoring. Guarded so Linux/Windows are untouched.
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
endif()

# Windows executable icon (windows/mgb64.rc -> branding/appicon-windows.ico).
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
include(CTest)

# The portable in-process app shell (Dear ImGui launcher + in-game overlay).
# Default ON: the shipping `ge007` binary boots the launcher and delegates
# automation/CLI invocations to the unchanged engine path. -DMGB64_APP=OFF
# builds the bare engine CLI (useful for minimal/headless builds).
option(MGB64_APP "Build the in-process ImGui app shell (portable launcher + overlay)" ON)
if(MGB64_APP)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(APPLE)
        enable_language(OBJC)  # nfd_cocoa.m (native file dialog)
    endif()
endif()

# The build uses GCC/Clang-style flags throughout (-Wall, -Wno-*,
# -fms-extensions); MSVC (cl.exe) rejects them. On Windows, build with
# MSYS2 MinGW64 as documented in docs/BUILDING.md.
if(MSVC)
    message(FATAL_ERROR
        "MSVC is not supported yet. On Windows, use the MSYS2 MinGW 64-bit "
        "toolchain (see docs/BUILDING.md).")
endif()

# --- Platform detection ---
if(APPLE)
    find_library(OPENGL_LIB OpenGL)
    # SDL2 via Homebrew or framework
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        # Fallback: try pkg-config
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 REQUIRED sdl2)
        endif()
    endif()
elseif(WIN32)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
else()
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

option(PORT_VALIDATION_TESTS "Register runtime validation smoke tests with CTest" OFF)

if(BUILD_TESTING)
    add_test(
        NAME port_native_switch_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_switch_access.py"
    )
    add_test(
        NAME port_release_ready_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/ci/check_release_ready.sh"
    )
    add_test(
        NAME port_public_python_tool_syntax
        COMMAND python3 -m py_compile
            "${CMAKE_SOURCE_DIR}/tools/audit_oracle_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_intro_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_minimap_dump.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_render_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_screenshot_health.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_audio.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_audio_reference.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_intro_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_movement_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_screenshots.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_state.py"
            "${CMAKE_SOURCE_DIR}/tools/intro_trace_summary.py"
            "${CMAKE_SOURCE_DIR}/tools/rom_oracle_route.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_intro_census.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_build_warnings.py"
            "${CMAKE_SOURCE_DIR}/tools/assert_stub_hits_zero.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_hidden_guard_contract.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_knife_impact.py"
            "${CMAKE_SOURCE_DIR}/tools/campaign_route_smoke.py"
            "${CMAKE_SOURCE_DIR}/tools/route_target_reach.py"
            "${CMAKE_SOURCE_DIR}/tools/check_markdown_links.py"
            "${CMAKE_SOURCE_DIR}/tools/check_shell_syntax.py"
            "${CMAKE_SOURCE_DIR}/tools/check_github_branch_protection.py"
            "${CMAKE_SOURCE_DIR}/tools/check_github_public_commit_refs.py"
            "${CMAKE_SOURCE_DIR}/tools/check_public_history_paths.py"
            "${CMAKE_SOURCE_DIR}/tools/check_third_party_notices.py"
            "${CMAKE_SOURCE_DIR}/tools/check_sdk_inventory.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_switch_access.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_sdk_surface.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_stub_surface.py"
            "${CMAKE_SOURCE_DIR}/tools/check_bg_visibility_opcode_table.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_stock_rdp_command_stream.py"
            "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_command_stream_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_stock_rdp_pixel_probe.py"
            "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_pixel_probe_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_glass_center_handoff.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_center_handoff_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_glass_handoff_points.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_points_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_glass_handoff_runs.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_run_compare_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_native_tri_pixel_probe.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_tri_pixel_probe_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_room_glass_source_reconstruction.py"
            "${CMAKE_SOURCE_DIR}/tools/check_room_glass_source_reconstruction_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/check_rdp_coverage_memory_backend_guard.py"
            "${CMAKE_SOURCE_DIR}/tools/check_room_xlu_cvg_memory_classifier_guard.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_rdp_render_mode_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/check_rdp_render_mode_trace_summary_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/select_glass_shard_pixel_targets.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_pixel_target_selection_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/export_github_launch_items.py"
            "${CMAKE_SOURCE_DIR}/tools/check_validation_smoke_registry.py"
            "${CMAKE_SOURCE_DIR}/tools/gen_env_reference.py"
            "${CMAKE_SOURCE_DIR}/macos/Scripts/generate_app_icon.py"
    )
    add_test(
        NAME port_stock_rdp_command_stream_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_command_stream_regression.py"
    )
    add_test(
        NAME port_stock_rdp_pixel_probe_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_pixel_probe_regression.py"
    )
    add_test(
        NAME port_glass_center_handoff_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_center_handoff_regression.py"
    )
    add_test(
        NAME port_glass_handoff_points_summary_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_points_regression.py"
    )
    add_test(
        NAME port_glass_handoff_run_compare_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_run_compare_regression.py"
    )
    add_test(
        NAME port_bg_visibility_opcode_table_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_bg_visibility_opcode_table.py"
                --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_native_tri_pixel_probe_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_tri_pixel_probe_regression.py"
    )
    add_test(
        NAME port_room_glass_source_reconstruction_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_room_glass_source_reconstruction_regression.py"
    )
    add_test(
        NAME port_rdp_coverage_memory_backend_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_rdp_coverage_memory_backend_guard.py"
    )
    add_test(
        NAME port_room_xlu_cvg_memory_classifier_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_room_xlu_cvg_memory_classifier_guard.py"
    )
    add_test(
        NAME port_rdp_render_mode_trace_summary_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_rdp_render_mode_trace_summary_regression.py"
    )
    add_test(
        NAME port_glass_shard_pixel_target_selection_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_pixel_target_selection_regression.py"
    )
    add_test(
        NAME port_native_sdk_surface_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_sdk_surface.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_native_stub_surface_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_stub_surface.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_public_shell_tool_syntax
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_shell_syntax.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # Settings-menu curation table vs. live settings registry.
    add_test(
        NAME settings_menu_model
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_settings_menu_model.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # Registry honesty: every add_port_validation_smoke(...) below must
    # reference a script that actually ships in tools/, so the advertised
    # validation-lane coverage cannot exceed what a fresh checkout can run.
    add_test(
        NAME port_validation_smoke_registry
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_validation_smoke_registry.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # T5: tools/tests/ unit tests —
    # ROM-free, pure Python, never skips. Closes the "CI only py_compiles the
    # tools" gap at the ctest layer for the intro/oracle tooling this plan
    # relies on. Kept in this always-registered block (unlike the intro_*
    # ROM-gated smokes below, which live in their own always-registered but
    # runtime-skippable block near add_port_validation_smoke).
    add_test(
        NAME intro_tools_unittests
        COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/tests" -p "test_*.py"
    )
    set_tests_properties(
        intro_tools_unittests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30
    )
    set_tests_properties(
        port_native_switch_guard
        port_release_ready_guard
        port_public_python_tool_syntax
        port_native_sdk_surface_guard
        port_native_stub_surface_guard
        port_public_shell_tool_syntax
        settings_menu_model
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    if(APPLE)
        add_test(
            NAME macos_app_asset_free_verifier
            COMMAND bash "${CMAKE_SOURCE_DIR}/macos/Tests/test_asset_free_verifier.sh"
        )
        set_tests_properties(
            macos_app_asset_free_verifier
            PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()

    # Sim-state invariance hash primitive — ROM-free unit test (remaster P0.2 rail).
    add_executable(test_sim_state_hash
        "${CMAKE_SOURCE_DIR}/tests/test_sim_state_hash.c"
        "${CMAKE_SOURCE_DIR}/src/platform/sim_state_hash.c"
    )
    target_include_directories(test_sim_state_hash PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME sim_state_hash COMMAND test_sim_state_hash)

    # Smooth env-normals builder — ROM-free unit test (remaster W1.E1.T1).
    # Compiles only the pure builder core (game-facing wrapper excluded via the
    # GFX_ROOM_NORMALS_UNIT_TEST guard, so no g_BgRoomInfo dependency).
    add_executable(test_room_normals
        "${CMAKE_SOURCE_DIR}/tests/test_room_normals.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d/gfx_room_normals.c"
    )
    target_compile_definitions(test_room_normals PRIVATE GFX_ROOM_NORMALS_UNIT_TEST)
    target_include_directories(test_room_normals PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    if(NOT WIN32)
        # glibc keeps libm separate; needed for lround() (no-op on macOS).
        target_link_libraries(test_room_normals m)
    endif()
    add_test(NAME room_normals COMMAND test_room_normals)

    # Registering GE007_* env-flag accessors — ROM-free unit test.
    add_executable(test_port_env
        "${CMAKE_SOURCE_DIR}/tests/test_port_env.c"
        "${CMAKE_SOURCE_DIR}/src/platform/port_env.c"
    )
    target_include_directories(test_port_env PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME port_env COMMAND test_port_env)

    # Generated GE007_* flag reference must match the source flag surface, so
    # docs/ENV_FLAGS.md cannot drift from the code (regenerate with
    # tools/gen_env_reference.py --out docs/ENV_FLAGS.md).
    add_test(
        NAME env_reference_current
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/gen_env_reference.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
            --check docs/ENV_FLAGS.md
    )
    set_tests_properties(env_reference_current PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(BUILD_TESTING AND PORT_VALIDATION_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/baserom.u.z64")
    function(add_port_validation_smoke test_name script_name timeout_seconds)
        set(_port_validation_script "${CMAKE_SOURCE_DIR}/tools/${script_name}")
        if(NOT EXISTS "${_port_validation_script}")
            message(STATUS "Skipping ${test_name}: missing ${_port_validation_script}")
            return()
        endif()
        add_test(
            NAME "${test_name}"
            COMMAND bash "${_port_validation_script}"
                --build-dir "${CMAKE_BINARY_DIR}"
                --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
                --no-build
                ${ARGN}
        )
        set_tests_properties("${test_name}" PROPERTIES
            RUN_SERIAL TRUE
            RESOURCE_LOCK ge007_runtime
            PROCESSORS 1
            TIMEOUT "${timeout_seconds}"
        )
    endfunction()

    # Runtime validation smokes. Registered only when PORT_VALIDATION_TESTS is
    # ON and a baserom is present. Every script referenced here must ship in
    # tools/ — enforced by the always-on port_validation_smoke_registry guard
    # above — so this list can never advertise a lane whose script was never
    # committed. (Historically ~38 registrations pointed at scripts that never
    # shipped; those silently skipped, inflating apparent coverage. Removed.)
    add_port_validation_smoke(port_rom_oracle_route_contract route_contract_smoke.sh 900
        --native-smoke
        --all
        --timeout 120
    )
    add_port_validation_smoke(port_campaign_route_smoke campaign_route_smoke.sh 900)
    add_port_validation_smoke(port_dam_progression_smoke dam_progression_smoke.sh 300)
    add_port_validation_smoke(port_surface2_final_flow_smoke surface2_final_flow_smoke.sh 180)
    add_port_validation_smoke(port_hidden_guard_contract_smoke hidden_guard_contract_smoke.sh 240)
    add_port_validation_smoke(port_knife_impact_smoke knife_impact_smoke.sh 180)
    add_port_validation_smoke(port_knife_throw_sfx_smoke knife_throw_sfx_smoke.sh 240)
    add_port_validation_smoke(port_minimap_smoke minimap_smoke.sh 900)
    add_port_validation_smoke(port_playability_smoke playability_smoke.sh 180)
    add_port_validation_smoke(port_renderer_parity_smoke renderer_parity_capture.sh 300)
    add_port_validation_smoke(port_surface_xlu_cvg_memory_regression surface_xlu_cvg_memory_regression.sh 360
        --msaa-values 0
    )
    add_port_validation_smoke(port_jungle_xlu_cvg_memory_regression jungle_xlu_cvg_memory_regression.sh 360)
    add_port_validation_smoke(port_spawn_health_smoke spawn_health_check.sh 120)
    # Per-level frame-time budget gate (docs/design/PERFORMANCE_PLAN.md M0). Reference-
    # hardware sensitive; opt-in and serialized like the other runtime lanes.
    add_port_validation_smoke(port_perf_budget_smoke perf_budget_smoke.sh 900)
endif()

# T5: cheap intro-path regression
# gates. Unlike add_port_validation_smoke above (opt-in via
# PORT_VALIDATION_TESTS, and invisible entirely without baserom.u.z64 at
# CMake-configure time), these three are ALWAYS registered so a ROM-less
# checkout (CI) still enumerates them and reports a genuine SKIPPED result
# instead of the test simply not existing. Each entry runs through
# tools/ctest_require_prereqs.sh, which checks the ROM/binary/ares
# prerequisite itself and exits SKIP_RETURN_CODE (125) before invoking the
# real script — the underlying scripts' own missing-prereq paths already
# exit 2 (see e.g. intro_parse_digest_gate.sh's header comment), but that
# exit 2 is shared with unrelated argument/validation failures in the same
# scripts, so it cannot be claimed directly as SKIP_RETURN_CODE without
# turning real failures into silent skips.
if(BUILD_TESTING)
    function(add_intro_gate_smoke)
        set(oneValueArgs NAME TIMEOUT)
        set(multiValueArgs REQUIRES COMMAND)
        cmake_parse_arguments(AIG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        set(_require_args "")
        foreach(_req IN LISTS AIG_REQUIRES)
            list(APPEND _require_args --require "${_req}")
        endforeach()

        add_test(
            NAME "${AIG_NAME}"
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/ctest_require_prereqs.sh"
                ${_require_args}
                --
                ${AIG_COMMAND}
        )
        set_tests_properties("${AIG_NAME}" PROPERTIES
            SKIP_RETURN_CODE 125
            TIMEOUT "${AIG_TIMEOUT}"
            RUN_SERIAL TRUE
            RESOURCE_LOCK ge007_runtime
            PROCESSORS 1
        )
    endfunction()

    # T3 parse-digest gate — 3-stage subset for a sub-90s ctest lane. The
    # full 20-stage sweep stays a manual/release-lane invocation
    # (tools/intro_parse_digest_gate.sh with no --stages override).
    add_intro_gate_smoke(
        NAME intro_parse_digest_smoke
        TIMEOUT 90
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/intro_parse_digest_gate.sh"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --binary "${CMAKE_BINARY_DIR}/ge007"
            --stages "33 34 22"
    )

    # T2/T4-hardened Dam intro camera-path oracle route: full native capture
    # + ares stock capture + comparator run. HEAVYWEIGHT (~4-6 min) — LABELed
    # "oracle" so `ctest -L oracle` selects it independent of the `intro_`
    # name filter used for the other two. SKIPs (not fails) when the
    # instrumented ares binary is absent, via the same prerequisite-wrapper
    # mechanism as the other two entries.
    add_intro_gate_smoke(
        NAME intro_oracle_dam_route
        TIMEOUT 900
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
            "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/movement_oracle_capture.sh"
            --route dam_intro_camera_path
            --build-dir "${CMAKE_BINARY_DIR}"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --no-build
            --ares-bin "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
            --timeout 600
    )
    set_tests_properties(intro_oracle_dam_route PROPERTIES LABELS oracle)

    # Single-stage (Dam) census smoke, reduced frame count for a sub-60s
    # ctest lane; tools/intro_census_capture.sh --all (20 stages) is the
    # manual/release-lane invocation.
    add_intro_gate_smoke(
        NAME intro_census_dam_smoke
        TIMEOUT 60
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/intro_census_capture.sh"
            --level 33
            --frames 400
            --build-dir "${CMAKE_BINARY_DIR}"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --no-build
    )
endif()

# --- Compile definitions ---
add_definitions(
    -DNONMATCHING
    -DNATIVE_PORT
    -DPORT_FIXME_STUBS
    -D_LANGUAGE_C
    -DVERSION_US
    -DLANG_US
    -DREFRESH_NTSC
    -DLEFTOVERDEBUG
    -DLEFTOVERSPECTRUM
    -DBUGFIX_R0
    -DBYTEMATCH
    # We provide our own main() (main_pc.c) and call SDL_SetMainReady() first
    # thing in it, rather than letting SDL_main.h's macro rename it to
    # SDL_main and require linking SDL2main. This already worked "by
    # accident" because main_pc.c never included SDL.h; this makes it an
    # officially supported configuration instead.
    -DSDL_MAIN_HANDLED
)
# Note: TARGET_N64 is NOT defined — activates PC code paths

# --- Collect sources ---
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/*.c)

# Exclude an intentionally empty legacy audio-ring placeholder. The active
# native audio backend is audio_pc.c.
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "audio_ring\\.c$")

# Fast3D display list interpreter (replaces old gfx_pc.c)
set(FAST3D_SOURCES
    src/platform/fast3d/gfx_pc.c
    src/platform/fast3d/gfx_opengl.c
    src/platform/fast3d/gfx_cc.c
    src/platform/fast3d/gfx_backend.c
    src/platform/fast3d/gfx_room_normals.c
)
# Native Metal backend (macOS-only, opt-in at runtime). GL TU stays on all platforms.
if(APPLE)
    list(APPEND FAST3D_SOURCES src/platform/fast3d/gfx_metal.mm)
    # ARC for the Metal TU only (manages id<MTL...>/CAMetalLayer/drawable lifetimes).
    set_source_files_properties(src/platform/fast3d/gfx_metal.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# glad OpenGL loader (not needed on macOS — uses framework directly)
if(NOT APPLE)
    set(GLAD_SOURCES lib/glad/src/glad.c)
else()
    set(GLAD_SOURCES "")
endif()

# System sources - select only what we need
set(SYSTEM_SOURCES
    src/boss.c
    src/init.c
    src/memp.c
    src/mema.c
    src/sched.c
    src/fr.c
    src/vi.c
    src/joy.c
    src/music.c
    src/snd.c
    src/deb.c
    src/debugmenu.c
    src/speed_graph.c
    src/random.c
    src/token.c
    src/ramrom.c
    src/crash.c
    src/cfb.c
    src/stacks.c
    src/rmon.c
    src/pi.c
    src/tlb_manage.c
    src/tlb_random.c
    src/c_data_filler.c
)

# Native builds must not compile SDK-derived libultra audio source. Keep this
# explicit so SDK audio files cannot silently enter the target through a glob.
set(LIBULTRA_AUDIO_SOURCES
)
# These compile with NATIVE_PORT; libaudio.h is now ungated so types are available
set(LIBULTRARE_AUDIO_SOURCES
)
# libultrare also compiles with NATIVE_PORT

# Exclude files that are N64-only or need special handling
# (boot.c, motor.c, str.c, sprintf.c, usb.c)

# Asset sources.
#
# These are first-party / code-coupled source (display lists, descriptor tables,
# offset metadata) that contain NO ROM media payload, so they always ship:
#   font_dl.c          - basic-geometry display lists
#   GlobalImageTable.c - image display lists + symbolic descriptor tables
set(ASSET_SOURCES
    assets/font_dl.c
    assets/GlobalImageTable.c)
#
# These are ROM-DERIVED BULK DATA (animation frames, the Rareware logo image) and
# are intentionally NOT distributed (see DISCLAIMER.md / docs/STATUS.md). The port
# loads this data from YOUR ROM at runtime, so it builds and links without them.
# They are compiled in automatically only if you have extracted them into assets/.
foreach(_romdata
        assets/animationtable_data.c
        assets/animationtable_entries.c
        assets/rarewarelogo.c)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_romdata}")
        list(APPEND ASSET_SOURCES ${_romdata})
    else()
        message(STATUS "MGB64: ROM-derived asset data not present (not distributed): ${_romdata} -- see docs/STATUS.md")
    endif()
endforeach()

# --- Build executable ---
option(PORT_SOUNDPLAYER_REAL "Compile snd.c's real ALSndPlayer path while keeping global port stubs enabled" ON)

add_executable(ge007
    ${GAME_SOURCES}
    ${SYSTEM_SOURCES}
    ${ASSET_SOURCES}
    ${PLATFORM_SOURCES}
    ${FAST3D_SOURCES}
    ${GLAD_SOURCES}
    ${LIBULTRA_AUDIO_SOURCES}
    ${LIBULTRARE_AUDIO_SOURCES}
)

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/branding/appicon-windows.ico")
    target_sources(ge007 PRIVATE ${CMAKE_SOURCE_DIR}/windows/mgb64.rc)
endif()

if(PORT_SOUNDPLAYER_REAL)
    target_compile_definitions(ge007 PRIVATE PORT_SOUNDPLAYER_REAL)
endif()

target_include_directories(ge007 PRIVATE
    include
    include/PR
    src
    src/game
    src/platform
    src/platform/fast3d
    src/libultra/audio
    assets
    lib/glad/include
    lib/stb
    .
)

option(PORT_STRICT "Enable strict pointer safety warnings as errors (audit tool)" OFF)

# --- Diagnostic configuration ---
# The default build suppresses 12+ diagnostic categories to allow compilation
# of NONMATCHING decompiled functions that use N64-era pointer/integer casts.
# This is intentional for development velocity — the suppressed warnings are
# real type issues but only matter on 64-bit (arm64/x86_64) targets.
#
# PORT_STRICT re-enables a subset of these as -Werror to audit pointer safety:
#   cmake -B build-strict -DPORT_STRICT=ON .
# Expect errors in files with active NONMATCHING code that hasn't been
# ported yet (bg.c, bondhead.c, bondinv.c, blood_decrypt.c, etc.).
# Errors inside PORT_FIXME_STUBS blocks are expected and harmless.
target_compile_options(ge007 PRIVATE
    -Wall
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-missing-braces
    -Wno-parentheses
    -Wno-unknown-pragmas
    -Wno-implicit-function-declaration
    -Wno-missing-declarations
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-empty-body
    -Wno-switch
    # -Wno-return-type  — REMOVED: all return paths verified
    # -Wno-return-mismatch  — REMOVED: all return types verified
    -fms-extensions
    # Force the SysV/Itanium bit-field layout on EVERY platform. This decomp
    # port relies on the original N64 struct layouts (see -fno-strict-aliasing
    # below), but MinGW/GCC on Windows DEFAULTS to -mms-bitfields (MSVC-compatible
    # bit-field packing), which changes the size and field offsets of the game's
    # mixed bit-field structs — e.g. `struct {u8 a; unsigned x:8; u16 y:12; u32 addr;}`
    # goes from 8 bytes / addr@4 to 16 bytes / addr@12. That desyncs every struct
    # read over ROM/model/setup data from the bytes actually there, which is the
    # Windows-only immediate-crash-on-load. macOS/Linux already default to this
    # layout, so the flag is a no-op there and keeps all three targets in sync.
    -mno-ms-bitfields
    # This is a decomp port: pointer punning between the original N64 struct
    # layouts and PC-native types (ROM_OFFSET in platform_os.h, bg.c/gun.c/
    # chrobjhandler.c/bondview.c reinterpret casts) is pervasive and relied
    # upon for byte-faithful behavior. gcc -O3 acts on strict-aliasing UB
    # (92 warnings on gcc-13); clang/macOS is unaffected either way.
    -fno-strict-aliasing
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(ge007 PRIVATE
        -Wno-macro-redefined
        -Wno-initializer-overrides
        -Wno-flexible-array-extensions
        -Wno-gnu-flexible-array-initializer
        -Wno-invalid-token-paste
        -Wno-microsoft-anon-tag
        -ferror-limit=0
    )
endif()

option(SANITIZE "Enable UBSan + ASan for crash diagnosis" OFF)
if(SANITIZE)
    target_compile_options(ge007 PRIVATE -fsanitize=undefined,address -fno-omit-frame-pointer -O1 -g)
    target_link_options(ge007 PRIVATE -fsanitize=undefined,address)
endif()

if(PORT_STRICT)
    target_compile_options(ge007 PRIVATE
        -Werror=int-conversion
        -Werror=incompatible-pointer-types
        -Werror=pointer-to-int-cast
        -Werror=int-to-pointer-cast
        -Werror=implicit-function-declaration
        -Werror=return-type
        -Werror=pointer-integer-compare
        -Werror=conditional-type-mismatch
    )
endif()

if(APPLE AND (PORT_STRICT OR SANITIZE))
    target_compile_options(ge007 PRIVATE -Werror=deprecated-declarations)
endif()

if(APPLE)
    target_link_libraries(ge007
        ${OPENGL_LIB}
        m
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
    )
    if(SDL2_FOUND)
        target_link_libraries(ge007 SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(ge007 PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ge007 ${SDL2_LIBRARIES})
    endif()
elseif(WIN32)
    target_link_libraries(ge007
        OpenGL::GL
        SDL2::SDL2
        # clock_gettime(CLOCK_MONOTONIC) (stubs.c, gfx_room_normals.c) is a
        # winpthread symbol on mingw-w64. It's already pulled in by the
        # posix-threads variant's default libs, but the (also common)
        # win32-threads variant needs it explicit on the link line or it's
        # an undefined reference (probe-verified both ways under Docker +
        # x86_64-w64-mingw32-gcc-win32/-posix). Harmless on posix-threads.
        winpthread
    )
else()
    target_link_libraries(ge007
        OpenGL::GL
        SDL2::SDL2
        m
        # dladdr() in src/random.c; folded into libc on glibc >= 2.34 but a
        # separate libdl on older distros.
        ${CMAKE_DL_LIBS}
    )
endif()

# ===========================================================================
# MGB64 App Shell (portable in-process ImGui launcher + overlay)
# Built as a separate C++/ObjC static library with clean flags (kept free of
# the C-only decomp flags on the ge007 target), linked into ge007. main() comes
# from src/app/main_app.cpp; main_pc.c renames its main to mgb64_headless_main.
# ===========================================================================
if(MGB64_APP)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
    set(NFD_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/lib/nfd)

    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    if(APPLE)
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_cocoa.m)
    elseif(WIN32)
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_win.cpp)
    else()
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_portal.cpp)
    endif()

    file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)

    add_library(mgb64_app STATIC
        ${IMGUI_SOURCES}
        ${NFD_PLATFORM_SOURCE}
        ${APP_SOURCES}
    )
    target_compile_features(mgb64_app PRIVATE cxx_std_17)
    # GL_SILENCE_DEPRECATION: macOS deprecates the GL API but still ships it.
    # NOTE: do NOT add the engine's `include/` dir here — it contains a `math.h`
    # shim for the C decomp that shadows the system <math.h> and breaks C++/ObjC
    # system headers (ImGui stb_truetype `acos`, nfd `double_t`). The app lib
    # only needs the seam headers under src/platform + src/app.
    target_compile_definitions(mgb64_app PRIVATE MGB64_APP GL_SILENCE_DEPRECATION)
    # Keep the engine's broad source dirs OFF the app lib's include path: src/
    # contains headers (sched.h, byteswap.h) and include/ a math.h shim that
    # shadow the system angle-includes SDL/pthread/ImGui pull in. App<->engine
    # seam headers therefore live in the collision-free src/app/ dir.
    target_include_directories(mgb64_app PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${NFD_DIR}/include
        src/app
        lib/glad/include
    )
    if(SDL2_FOUND)
        target_link_libraries(mgb64_app PUBLIC SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(mgb64_app PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(mgb64_app PUBLIC ${SDL2_LIBRARIES})
    endif()
    if(APPLE)
        target_link_libraries(mgb64_app PUBLIC
            ${OPENGL_LIB}
            "-framework AppKit"
            "-framework UniformTypeIdentifiers"
        )
    elseif(WIN32)
        # nfd_win.cpp uses COM + the Shell IFileOpenDialog; MinGW does not
        # auto-link these, so they must be explicit or the link fails.
        target_link_libraries(mgb64_app PUBLIC OpenGL::GL ole32 shell32 uuid)
    else()
        # Linux: GL + the nfd xdg-desktop-portal backend. D-Bus is REQUIRED --
        # nfd_portal.cpp hard-includes <dbus/dbus.h>, so fail loudly at configure
        # (install libdbus-1-dev) rather than with a confusing compile error.
        target_link_libraries(mgb64_app PUBLIC OpenGL::GL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DBUS REQUIRED dbus-1)
        target_include_directories(mgb64_app PRIVATE ${DBUS_INCLUDE_DIRS})
        target_link_libraries(mgb64_app PUBLIC ${DBUS_LIBRARIES})
    endif()

    # ge007's entry point now comes from the app shell.
    target_compile_definitions(ge007 PRIVATE MGB64_APP)
    target_link_libraries(ge007 mgb64_app)

    if(BUILD_TESTING)
        add_executable(test_arg_triage
            tests/test_arg_triage.cpp
            src/app/arg_triage.cpp)
        target_include_directories(test_arg_triage PRIVATE src/app)
        target_compile_features(test_arg_triage PRIVATE cxx_std_17)
        add_test(NAME arg_triage COMMAND test_arg_triage)

        add_executable(test_rom_validate
            tests/test_rom_validate.cpp
            src/app/rom_validate.cpp)
        target_include_directories(test_rom_validate PRIVATE src/app)
        target_compile_features(test_rom_validate PRIVATE cxx_std_17)
        add_test(NAME rom_validate COMMAND test_rom_validate)
    endif()
endif()

# ===========================================================================
# macOS App Bundle (optional — builds game as static library for Swift shell)
# Usage: cmake -B build-macos -DMACOS_APP_BUNDLE=ON
# ===========================================================================
option(MACOS_APP_BUNDLE "Build as macOS .app bundle (static library + Swift shell)" OFF)

if(MACOS_APP_BUNDLE AND APPLE)
    # Set architecture and deployment target BEFORE creating the library target
    # so CMake applies them at generation time.
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()

    # Collect all C sources for the static library (same as executable, minus main)
    set(BRIDGE_SOURCES
        macos/Sources/GameBridge.c
    )

    add_library(ge007_lib STATIC
        ${GAME_SOURCES}
        ${SYSTEM_SOURCES}
        ${ASSET_SOURCES}
        ${PLATFORM_SOURCES}
        ${FAST3D_SOURCES}
        ${GLAD_SOURCES}
        ${LIBULTRA_AUDIO_SOURCES}
        ${LIBULTRARE_AUDIO_SOURCES}
        ${BRIDGE_SOURCES}
    )

    # The global add_definitions() (NONMATCHING, NATIVE_PORT, etc.) apply
    # automatically. We only need to add the app bundle flag.
    target_compile_definitions(ge007_lib PRIVATE
        MACOS_APP_BUNDLE
    )
    if(PORT_SOUNDPLAYER_REAL)
        target_compile_definitions(ge007_lib PRIVATE PORT_SOUNDPLAYER_REAL)
    endif()

    target_include_directories(ge007_lib PRIVATE
        include
        include/PR
        src
        src/game
        src/platform
        src/platform/fast3d
        src/libultra/audio
        assets
        lib/glad/include
        lib/stb
        macos/Sources
        .
    )

    # Same compiler flags as the main executable
    target_compile_options(ge007_lib PRIVATE
        -Wall
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-missing-braces
        -Wno-parentheses
        -Wno-unknown-pragmas
        -Wno-implicit-function-declaration
        -Wno-missing-declarations
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wno-empty-body
        -Wno-switch
        -fms-extensions
        # See the ge007 target above for both of these: N64-matching bit-field
        # layout on every platform (Windows/MinGW defaults to -mms-bitfields),
        # and decomp-port pointer punning.
        -mno-ms-bitfields
        -fno-strict-aliasing
    )

    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(ge007_lib PRIVATE
            -Wno-macro-redefined
            -Wno-initializer-overrides
            -Wno-flexible-array-extensions
            -Wno-gnu-flexible-array-initializer
            -Wno-invalid-token-paste
            -Wno-microsoft-anon-tag
            -ferror-limit=0
        )
    endif()

    target_link_libraries(ge007_lib
        ${OPENGL_LIB}
        m
    )
    if(SDL2_FOUND)
        target_link_libraries(ge007_lib SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(ge007_lib PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ge007_lib ${SDL2_LIBRARIES})
    endif()

    message(STATUS "macOS App Bundle: ON")
    message(STATUS "  Library target: ge007_lib")
    message(STATUS "  Architectures: ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  Deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    message(STATUS "  Link the Swift app against libge007_lib.a")
endif()
