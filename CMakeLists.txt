cmake_minimum_required(VERSION 3.16)
project(ge007 C)

set(CMAKE_C_STANDARD 11)
include(CTest)

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
            "${CMAKE_SOURCE_DIR}/tools/compare_audio.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_audio_reference.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_screenshots.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_state.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_build_warnings.py"
            "${CMAKE_SOURCE_DIR}/tools/assert_stub_hits_zero.py"
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
            "${CMAKE_SOURCE_DIR}/tools/export_github_launch_items.py"
            "${CMAKE_SOURCE_DIR}/macos/Scripts/generate_app_icon.py"
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
    set_tests_properties(
        port_native_switch_guard
        port_release_ready_guard
        port_public_python_tool_syntax
        port_native_sdk_surface_guard
        port_native_stub_surface_guard
        port_public_shell_tool_syntax
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

    add_port_validation_smoke(port_frontend_watch_smoke frontend_watch_smoke.sh 120)
    add_port_validation_smoke(port_gameplay_room_origin_smoke gameplay_room_origin_smoke.sh 120)
    add_port_validation_smoke(port_room_render_matrix_smoke room_render_matrix_smoke.sh 180)
    add_port_validation_smoke(port_room_transition_matrix_smoke room_transition_matrix_smoke.sh 180)
    add_port_validation_smoke(port_route_room_render_matrix_smoke route_room_render_matrix_smoke.sh 600)
    add_port_validation_smoke(port_display_cast_oracle_routes route_contract_smoke.sh 180
        --tag display_cast_oracle
        --artifact-health
        --artifact-report-lane display_cast_oracle_routes
        --artifact-expected-route-label startup_display_cast_forced_zukovsky_state
        --artifact-expected-route-label startup_display_cast_forced_zukovsky_timer_state
        --artifact-expected-route-label startup_display_cast_forced_zukovsky_stock_follow_state
        --timeout 120
    )
    add_port_validation_smoke(port_weapon_viewmodel_smoke weapon_viewmodel_smoke.sh 180)
    add_port_validation_smoke(port_startup_weapon_viewmodel_smoke startup_weapon_viewmodel_smoke.sh 120)
    add_port_validation_smoke(port_visual_parity_smoke visual_parity_smoke.sh 180)
    add_port_validation_smoke(port_native_ramrom_playback_smoke native_ramrom_playback_smoke.sh 90)
    add_port_validation_smoke(port_laser_sfx_route_smoke laser_sfx_route_smoke.sh 90)
    add_port_validation_smoke(port_autogun_pose_smoke autogun_pose_smoke.sh 120)
    add_port_validation_smoke(port_runway_vehicle_smoke runway_vehicle_smoke.sh 120)
    add_port_validation_smoke(port_streets_tank_smoke streets_tank_smoke.sh 120)
    add_port_validation_smoke(port_tank_shell_damage_smoke tank_shell_damage_smoke.sh 180)
    add_port_validation_smoke(port_monitor_behavior_smoke monitor_behavior_smoke.sh 120)
    add_port_validation_smoke(port_setup_link_smoke setup_link_smoke.sh 180)
    add_port_validation_smoke(port_stage_mission_setup_smoke stage_mission_setup_smoke.sh 180)
    add_port_validation_smoke(port_dam_progression_smoke dam_progression_smoke.sh 180)
    add_port_validation_smoke(port_facility_progression_smoke facility_progression_smoke.sh 240)
    add_port_validation_smoke(port_runway_progression_smoke runway_progression_smoke.sh 240)
    add_port_validation_smoke(port_silo_progression_smoke silo_progression_smoke.sh 240)
    add_port_validation_smoke(port_statue_progression_smoke statue_progression_smoke.sh 240)
    add_port_validation_smoke(port_frigate_progression_smoke frigate_progression_smoke.sh 240)
    add_port_validation_smoke(port_surface1_progression_smoke surface1_progression_smoke.sh 180)
    add_port_validation_smoke(port_surface2_progression_smoke surface2_progression_smoke.sh 240)
    add_port_validation_smoke(port_archives_progression_smoke archives_progression_smoke.sh 180)
    add_port_validation_smoke(port_bunker2_progression_smoke bunker2_progression_smoke.sh 240)
    add_port_validation_smoke(port_depot_progression_smoke depot_progression_smoke.sh 180)
    add_port_validation_smoke(port_train_progression_smoke train_progression_smoke.sh 180)
    add_port_validation_smoke(port_jungle_progression_smoke jungle_progression_smoke.sh 180)
    add_port_validation_smoke(port_control_progression_smoke control_progression_smoke.sh 180)
    add_port_validation_smoke(port_caverns_progression_smoke caverns_progression_smoke.sh 180)
    add_port_validation_smoke(port_cradle_progression_smoke cradle_progression_smoke.sh 180)
    add_port_validation_smoke(port_door_render_smoke door_render_smoke.sh 120)
    add_port_validation_smoke(port_door_behavior_smoke door_behavior_smoke.sh 120)
    add_port_validation_smoke(port_door_motion_smoke door_motion_smoke.sh 120)
    add_port_validation_smoke(port_interaction_smoke interaction_smoke.sh 120)
    add_port_validation_smoke(port_spawn_health_smoke spawn_health_check.sh 120)
    add_port_validation_smoke(port_audio_churn_smoke audio_soak_test.sh 90
        --seconds 3
        --level "33 34 22 26 36 35"
        --repeat 2
    )
    add_port_validation_smoke(port_audio_mute_toggle_smoke audio_mute_toggle_smoke.sh 90)
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
)
# Note: TARGET_N64 is NOT defined — activates PC code paths

# --- Collect sources ---
file(GLOB GAME_SOURCES src/game/*.c)
file(GLOB PLATFORM_SOURCES src/platform/*.c)

# Exclude the old gfx_pc.c (replaced by fast3d)
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "gfx_pc\\.c$")
# Exclude an intentionally empty legacy audio-ring placeholder. The active
# native audio backend is audio_pc.c.
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "audio_ring\\.c$")

# Fast3D display list interpreter (replaces old gfx_pc.c)
set(FAST3D_SOURCES
    src/platform/fast3d/gfx_pc.c
    src/platform/fast3d/gfx_opengl.c
    src/platform/fast3d/gfx_cc.c
)

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
    )
else()
    target_link_libraries(ge007
        OpenGL::GL
        SDL2::SDL2
        m
    )
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
