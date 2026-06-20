#!/bin/bash
#
# prepare_ares_movement_oracle_build.sh -- Build local ares movement oracle.
#
# This clones ares into ignored local build space, applies a small N64 movement
# trace and controller-route hook, and builds the desktop frontend. It does not
# vendor ares source and does not create redistributable ROM-derived captures.
#
set -euo pipefail
cd "$(dirname "$0")/.."

ARES_REPO_URL="${ARES_REPO_URL:-https://github.com/ares-emulator/ares.git}"
ARES_REF="${ARES_REF:-91b112279ab2ce89c5fc9bff5dbb81e29af51a68}"
WORK_DIR="${MGB64_ARES_MOVEMENT_ORACLE_DIR:-$PWD/build/ares-movement-oracle}"
JOBS="${JOBS:-}"
FORCE=0
NO_BUILD=0

usage() {
    cat <<'USAGE'
Usage: tools/prepare_ares_movement_oracle_build.sh [options]

Options:
  --work-dir DIR       ignored local workspace (default: build/ares-movement-oracle)
  --ares-ref REF       ares commit/tag/branch (default: pinned known-good commit)
  --jobs N             parallel build jobs (default: host CPU count)
  --no-build           clone/patch only
  --force              delete and recreate the local ares checkout

The checkout and build are local-only. Do not commit ares source, ROM-derived
traces, screenshots, saves, or generated captures.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --ares-ref) ARES_REF="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$JOBS" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
    if [[ -z "$JOBS" ]] && command -v getconf >/dev/null 2>&1; then
        JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi
    JOBS="${JOBS:-4}"
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --jobs must be a positive integer" >&2
    exit 2
fi

for required in git cmake python3; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "FAIL: $required is required" >&2
        exit 2
    fi
done

WORK_DIR="$(python3 - "$WORK_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"
SRC_DIR="$WORK_DIR/ares"
BUILD_DIR="$SRC_DIR/build-movement-oracle"

if [[ "$FORCE" -eq 1 && -e "$SRC_DIR" ]]; then
    rm -rf "$SRC_DIR"
fi

mkdir -p "$WORK_DIR"
if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --depth 1 "$ARES_REPO_URL" "$SRC_DIR"
fi

if ! git -C "$SRC_DIR" diff --quiet || ! git -C "$SRC_DIR" diff --cached --quiet; then
    echo "FAIL: local ares checkout has uncommitted changes: $SRC_DIR" >&2
    echo "Use --force to recreate it, or clean that checkout manually." >&2
    exit 1
fi

git -C "$SRC_DIR" fetch origin "$ARES_REF" --depth 1 2>/dev/null || git -C "$SRC_DIR" fetch origin "$ARES_REF"
git -C "$SRC_DIR" checkout --detach "$ARES_REF"

python3 - "$SRC_DIR" <<'PY'
from pathlib import Path
import sys

src = Path(sys.argv[1])

oracle_cpp = r'''#include <n64/n64.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ares::Nintendo64 {

namespace {

struct OracleInputEvent {
  u64 start = 0;
  u64 end = 0;
  u16 buttons = 0;
  int stickX = 0;
  int stickY = 0;
  u8 phase = 0;
};

struct OracleState {
  FILE* trace = nullptr;
  bool configured = false;
  bool hasInputScript = false;
  bool complete = false;
  bool gameplayTimelineStarted = false;
  u64 videoFrame = 0;
  u64 inputFrame = 0;
  u64 gameplayOriginInputFrame = 0;
  s32 gameplayOriginGlobal = 0;
  s32 gameplayStartGlobal = 0;
  u32 gameplaySpeedFrames = 0;
  u64 frameLimit = 0;
  u64 lastInputFrame = 0;
  u64 lastInputGameplayFrame = 0;
  u16 lastInputButtons = 0;
  int lastInputStickX = 0;
  int lastInputStickY = 0;
  u32 lastInputMenuEvents = 0;
  u32 lastInputGameplayEvents = 0;
  u32 lastInputSuppressedMenuEvents = 0;
  u32 lastInputSuppressedGameplayEvents = 0;
  bool lastInputMenuClosed = false;
  OracleInputEvent events[1024];
  u32 eventCount = 0;
  const char* symbolLayout = "us";
  u32 currentPlayerAddress = 0x8008a0b0;
  u32 playerPointersAddress = 0x80079ee0;
  u32 currentMenuAddress = 0;
  u32 currentStageToLoadAddress = 0x80048364;
  u32 clockTimerAddress = 0x80048374;
  u32 globalTimerDeltaAddress = 0x80048378;
  u32 globalTimerAddress = 0x8004837c;
  u32 cameraModeAddress = 0x80036494;
  u32 cameraAfterCinemaAddress = 0x80036498;
  u32 cameraTransitionTimerAddress = 0x800364a4;
  u32 introCameraIndexAddress = 0x800364a8;
  u32 introSwirlAddress = 0x800364ac;
  u32 selectedIntroCameraAddress = 0x800364c0;
  u32 introAnimationIndexAddress = 0x80036514;
  u32 animationTablePtrAddress = 0x80069538;
  s32 targetStage = -1;
  bool closeMenuOnPlayer = true;

  enum : u32 {
    PlayerViewMode = 0x0000,
    PlayerIntroPos = 0x0004,
    PlayerIntroTarget = 0x0010,
    PlayerIntroUp = 0x001c,
    PlayerIntroFloor = 0x0028,
    PlayerCurrentModelPos = 0x0038,
    PlayerField70 = 0x0070,
    PlayerStanHeight = 0x0074,
    PlayerVvTheta = 0x0148,
    PlayerProp = 0x00a8,
    PlayerSpeedTheta = 0x014c,
    PlayerSpeedVerta = 0x0160,
    PlayerSpeedSideways = 0x016c,
    PlayerSpeedStrafe = 0x0170,
    PlayerSpeedForwards = 0x0174,
    PlayerSpeedBoost = 0x0178,
    PlayerSpeedMaxTime60 = 0x017c,
    PlayerBondPrevPos = 0x0408,
    PlayerCollisionPos = 0x048c,
    PlayerHeadPos = 0x04fc,
    PlayerSpeedGo = 0x2a4c,

    PlayerWatchPauseTime = 0x01c0,
    PlayerWatchAnimationState = 0x01c8,
    PlayerOutsideWatchMenu = 0x01cc,
    PlayerOpenCloseSoloWatchMenu = 0x01d0,
    PlayerPausingFlag = 0x0200,
    PlayerColourFadeTimeMax60 = 0x03e4,
    PlayerField488 = 0x0488,

    PropPos = 0x0008,
    PropFlags = 0x0001,
    PropChr = 0x0004,

    ChrChrnum = 0x0000,
    ChrActiontype = 0x0007,
    ChrSleep = 0x0008,
    ChrChrflags = 0x0014,
    ChrProp = 0x0018,
    ChrModel = 0x001c,
    ChrField20 = 0x0020,

    ModelRenderPos = 0x000c,
    ModelAnim = 0x0020,
    ModelGunhand = 0x0024,
    ModelAnimLooping = 0x0026,
    ModelFrame = 0x0028,
    ModelEndFrame = 0x003c,
    ModelSpeed = 0x0040,

    ModelAnimationFrameCount = 0x0004,

    CollisionCurrentTile = 0x0000,
    CollisionPosition = 0x0004,
    CollisionTheta = 0x0010,
    CollisionFloor = 0x0020,
    CollisionRadius = 0x002c,
    CollisionCameraPos = 0x0030,
    CollisionAppliedView = 0x003c,
    CollisionAppliedUp = 0x0048,
    CollisionPortalTile = 0x0054,
  };

  enum : s32 {
    MenuUnknown = -9999,
    MenuRunStage = 11,
  };

  enum : u8 {
    PhaseGameplay = 0,
    PhaseMenu = 1,
  };

  auto configure() -> void {
    if(configured) return;
    configured = true;
    configureSymbols();

    const char* limitEnv = getenv("MGB64_ARES_FRAME_LIMIT");
    if(!limitEnv || !*limitEnv) limitEnv = getenv("MGB64_ARES_EXIT_AFTER_FRAMES");
    if(limitEnv && *limitEnv) {
      char* end = nullptr;
      auto parsed = strtoull(limitEnv, &end, 10);
      if(end && *end == 0) frameLimit = parsed;
    }

    if(auto script = getenv("MGB64_ARES_INPUT_SCRIPT")) {
      if(*script) loadInputScript(script);
    }

    auto path = getenv("MGB64_ARES_ORACLE_TRACE");
    if(!path || !*path) path = getenv("MGB64_ARES_MOVEMENT_TRACE");
    if(!path || !*path) {
      complete = true;
      return;
    }

    trace = fopen(path, "wb");
    if(!trace) {
      fprintf(stderr, "mgb64 oracle: failed to open %s\n", path);
      complete = true;
      return;
    }

    setvbuf(trace, nullptr, _IONBF, 0);
    fprintf(stderr, "mgb64 oracle: writing JSONL to %s\n", path);
  }

  auto loadInputScript(const char* path) -> void {
    FILE* file = fopen(path, "rb");
    if(!file) {
      fprintf(stderr, "mgb64 oracle: failed to open input script %s\n", path);
      return;
    }

    hasInputScript = true;
    char line[512];
    while(fgets(line, sizeof(line), file) && eventCount < 1024) {
      char* cursor = line;
      while(*cursor == ' ' || *cursor == '\t') cursor++;
      if(*cursor == '\0' || *cursor == '\n' || *cursor == '#') continue;

      unsigned long long start = 0;
      unsigned long long length = 0;
      unsigned int buttons = 0;
      int stickX = 0;
      int stickY = 0;
      char phase[32] = "gameplay";
      int parsed = sscanf(cursor, "%llu %llu %x %d %d %31s", &start, &length, &buttons, &stickX, &stickY, phase);
      if(parsed < 5) {
        fprintf(stderr, "mgb64 oracle: ignored malformed input line: %s", line);
        continue;
      }
      if(start == 0 || length == 0) continue;

      auto& event = events[eventCount++];
      event.start = start;
      event.end = start + length - 1;
      event.buttons = buttons & 0xffff;
      event.stickX = clampAxis(stickX);
      event.stickY = clampAxis(stickY);
      if(parsed >= 6 && (strcmp(phase, "menu") == 0 || strcmp(phase, "boot") == 0 || strcmp(phase, "frontend") == 0)) {
        event.phase = PhaseMenu;
      } else {
        event.phase = PhaseGameplay;
      }
    }

    fclose(file);
    fprintf(stderr, "mgb64 oracle: loaded %u input event(s) from %s\n", eventCount, path);
  }

  static auto clampAxis(int value) -> int {
    if(value > 80) return 80;
    if(value < -80) return -80;
    return value;
  }

  auto configureSymbols() -> void {
    if(auto layout = getenv("MGB64_ARES_SYMBOL_LAYOUT")) {
      if(strcmp(layout, "jp") == 0 || strcmp(layout, "jp_bugfix") == 0) {
        symbolLayout = layout;
        currentPlayerAddress = 0x80078bc0;
        playerPointersAddress = 0x800789f0;
        currentMenuAddress = 0;
        currentStageToLoadAddress = 0x80040fe4;
        clockTimerAddress = 0x80040ff4;
        globalTimerAddress = 0x80040ffc;
        globalTimerDeltaAddress = 0x80041004;
        cameraModeAddress = 0;
        cameraAfterCinemaAddress = 0;
        cameraTransitionTimerAddress = 0;
        introCameraIndexAddress = 0;
        introSwirlAddress = 0;
        selectedIntroCameraAddress = 0;
        introAnimationIndexAddress = 0;
        animationTablePtrAddress = 0x80058478;
      } else if(strcmp(layout, "us") == 0 || strcmp(layout, "usa") == 0) {
        symbolLayout = "us";
      } else {
        symbolLayout = layout;
      }
    }

    currentPlayerAddress = parseAddressEnv("MGB64_ARES_CURRENT_PLAYER", currentPlayerAddress);
    playerPointersAddress = parseAddressEnv("MGB64_ARES_PLAYER_POINTERS", playerPointersAddress);
    currentMenuAddress = parseAddressEnv("MGB64_ARES_CURRENT_MENU", currentMenuAddress);
    currentStageToLoadAddress = parseAddressEnv("MGB64_ARES_CURRENT_STAGE", currentStageToLoadAddress);
    clockTimerAddress = parseAddressEnv("MGB64_ARES_CLOCK_TIMER", clockTimerAddress);
    globalTimerDeltaAddress = parseAddressEnv("MGB64_ARES_GLOBAL_TIMER_DELTA", globalTimerDeltaAddress);
    globalTimerAddress = parseAddressEnv("MGB64_ARES_GLOBAL_TIMER", globalTimerAddress);
    cameraModeAddress = parseAddressEnv("MGB64_ARES_CAMERA_MODE", cameraModeAddress);
    cameraAfterCinemaAddress = parseAddressEnv("MGB64_ARES_CAMERA_AFTER_CINEMA", cameraAfterCinemaAddress);
    cameraTransitionTimerAddress = parseAddressEnv("MGB64_ARES_CAMERA_TRANSITION_TIMER", cameraTransitionTimerAddress);
    introCameraIndexAddress = parseAddressEnv("MGB64_ARES_INTRO_CAMERA_INDEX", introCameraIndexAddress);
    introSwirlAddress = parseAddressEnv("MGB64_ARES_INTRO_SWIRL", introSwirlAddress);
    selectedIntroCameraAddress = parseAddressEnv("MGB64_ARES_SELECTED_INTRO_CAMERA", selectedIntroCameraAddress);
    introAnimationIndexAddress = parseAddressEnv("MGB64_ARES_INTRO_ANIM_INDEX", introAnimationIndexAddress);
    animationTablePtrAddress = parseAddressEnv("MGB64_ARES_ANIMATION_TABLE_PTR", animationTablePtrAddress);
    targetStage = parseS32Env("MGB64_ARES_TARGET_STAGE", targetStage);
    gameplayStartGlobal = parseS32Env("MGB64_ARES_GAMEPLAY_START_GLOBAL", gameplayStartGlobal);
    gameplaySpeedFrames = parseU32Env("MGB64_ARES_GAMEPLAY_SPEEDFRAMES", gameplaySpeedFrames);
    closeMenuOnPlayer = parseBoolEnv("MGB64_ARES_CLOSE_MENU_ON_PLAYER", closeMenuOnPlayer);
  }

  static auto parseAddressEnv(const char* name, u32 fallback) -> u32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtoul(value, &end, 0);
    if(end && *end == 0) return (u32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseS32Env(const char* name, s32 fallback) -> s32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtol(value, &end, 0);
    if(end && *end == 0) return (s32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseU32Env(const char* name, u32 fallback) -> u32 {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    char* end = nullptr;
    auto parsed = strtoul(value, &end, 0);
    if(end && *end == 0) return (u32)parsed;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto parseBoolEnv(const char* name, bool fallback) -> bool {
    auto value = getenv(name);
    if(!value || !*value) return fallback;
    if(strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) return true;
    if(strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "off") == 0) return false;
    fprintf(stderr, "mgb64 oracle: ignored invalid %s=%s\n", name, value);
    return fallback;
  }

  static auto hashU32(u64 hash, u32 value) -> u64 {
    hash ^= (u64)value;
    hash *= 1099511628211ull;
    return hash;
  }

  auto scriptedController(n32 data) -> n32 {
    configure();
    inputFrame++;
    if(!hasInputScript) return data;

    u16 buttons = 0;
    int stickX = 0;
    int stickY = 0;
    const bool hasPlayer = hasGameplayPlayer();
    const u64 gameplayFrame = currentGameplayFrame();
    const bool menuClosed = stockMenuInputClosed(hasPlayer);
    u32 menuEvents = 0;
    u32 gameplayEvents = 0;
    u32 suppressedMenuEvents = 0;
    u32 suppressedGameplayEvents = 0;

    for(u32 n = 0; n < eventCount; n++) {
      const auto& event = events[n];
      if(event.phase == PhaseMenu) {
        if(inputFrame < event.start || inputFrame > event.end) continue;
        if(menuClosed) {
          suppressedMenuEvents++;
          continue;
        }
        menuEvents++;
      } else {
        if(gameplayFrame == 0 || gameplayFrame < event.start || gameplayFrame > event.end) {
          if(gameplayFrame == 0) suppressedGameplayEvents++;
          continue;
        }
        gameplayEvents++;
      }
      buttons |= event.buttons;
      stickX += event.stickX;
      stickY += event.stickY;
    }

    stickX = clampAxis(stickX);
    stickY = clampAxis(stickY);
    lastInputFrame = inputFrame;
    lastInputGameplayFrame = gameplayFrame;
    lastInputButtons = buttons;
    lastInputStickX = stickX;
    lastInputStickY = stickY;
    lastInputMenuEvents = menuEvents;
    lastInputGameplayEvents = gameplayEvents;
    lastInputSuppressedMenuEvents = suppressedMenuEvents;
    lastInputSuppressedGameplayEvents = suppressedGameplayEvents;
    lastInputMenuClosed = menuClosed;

    n32 out = ((u32)buttons << 16) | ((u32)(stickX & 0xff) << 8) | (u32)(stickY & 0xff);
    return out;
  }

  auto validRdram(u32 address, u32 bytes = 4) -> bool {
    if(address == 0) return false;
    u32 offset = physicalOffset(address);
    return offset < rdram.ram.size && bytes <= rdram.ram.size - offset;
  }

  auto physicalOffset(u32 address) -> u32 {
    if((address & 0xe0000000) == 0x80000000 || (address & 0xe0000000) == 0xa0000000) {
      return address & 0x1fffffff;
    }
    return address;
  }

  auto readU32(u32 address) -> u32 {
    if(!validRdram(address, 4)) return 0;
    return rdram.ram.read<Word>(physicalOffset(address), RBusDevice::ARES_DEBUGGER);
  }

  auto readU8(u32 address) -> u8 {
    u32 aligned = address & ~3u;
    if(!validRdram(aligned, 4)) return 0;
    u32 value = readU32(aligned);
    return (u8)((value >> ((3 - (address & 3u)) * 8)) & 0xff);
  }

  auto readS8(u32 address) -> s8 {
    return (s8)readU8(address);
  }

  auto readU16(u32 address) -> u16 {
    return (u16)(((u16)readU8(address) << 8) | readU8(address + 1));
  }

  auto readS16(u32 address) -> s16 {
    return (s16)readU16(address);
  }

  auto readS32(u32 address) -> s32 {
    return (s32)readU32(address);
  }

  auto animationTableBase() -> u32 {
    if(animationTablePtrAddress == 0 || !validRdram(animationTablePtrAddress, 4)) return 0;
    u32 base = readU32(animationTablePtrAddress);
    return validRdram(base, 0x14) ? base : 0;
  }

  auto logicalAnimationOffset(u32 value) -> u32 {
    u32 base = animationTableBase();
    if(base != 0 && value >= base) {
      u32 offset = value - base;
      if(offset < 0x20000u) return offset;
    }
    return value;
  }

  auto readF32(u32 address) -> f32 {
    u32 bits = readU32(address);
    f32 value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if(!std::isfinite(value)) return 0.0f;
    return value;
  }

  auto validPlayerPointer(u32 player) -> bool {
    return player != 0 && validRdram(player, PlayerSpeedGo + 4);
  }

  auto targetStageMatches(s32 currentStage) -> bool {
    return targetStage < 0 || currentStage == targetStage;
  }

  auto selectedPlayer(const char*& source) -> u32 {
    u32 player = readU32(currentPlayerAddress);
    source = "current";
    if(validPlayerPointer(player)) return player;

    for(u32 index = 0; index < 4; index++) {
      player = readU32(playerPointersAddress + index * 4);
      if(!validPlayerPointer(player)) continue;
      switch(index) {
      case 0: source = "players[0]"; break;
      case 1: source = "players[1]"; break;
      case 2: source = "players[2]"; break;
      default: source = "players[3]"; break;
      }
      return player;
    }

    source = "none";
    return readU32(currentPlayerAddress);
  }

  auto hasGameplayPlayer() -> bool {
    const char* source = nullptr;
    return targetStageMatches(readS32(currentStageToLoadAddress)) && validPlayerPointer(selectedPlayer(source));
  }

  auto currentMenu() -> s32 {
    if(currentMenuAddress == 0 || !validRdram(currentMenuAddress, 4)) return MenuUnknown;
    return readS32(currentMenuAddress);
  }

  auto stockMenuInputClosed(bool hasPlayer) -> bool {
    if(gameplayTimelineStarted) return true;

    s32 menu = currentMenu();
    if(menu == MenuRunStage) return true;

    s32 currentStage = readS32(currentStageToLoadAddress);
    if(closeMenuOnPlayer && hasPlayer && targetStageMatches(currentStage)) {
      return true;
    }

    s32 global = readS32(globalTimerAddress);
    if(gameplayStartGlobal > 0 && targetStageMatches(currentStage) && global >= gameplayStartGlobal) {
      return true;
    }

    return hasPlayer && gameplayStartGlobal <= 0;
  }

  auto currentGameplayFrame() -> u64 {
    const char* source = nullptr;
    u32 player = selectedPlayer(source);
    s32 currentStage = readS32(currentStageToLoadAddress);
    if(!targetStageMatches(currentStage) || !validPlayerPointer(player)) return 0;

    u32 prop = readU32(player + PlayerProp);
    if(prop == 0 || !validRdram(prop, PropPos + 12)) return 0;

    s32 global = readS32(globalTimerAddress);
    if(global < gameplayStartGlobal) return 0;

    if(!gameplayTimelineStarted) {
      gameplayTimelineStarted = true;
      gameplayOriginGlobal = global;
      gameplayOriginInputFrame = inputFrame;
    }

    u32 speedFrames = gameplaySpeedFrames;
    if(speedFrames == 0) {
      f32 delta = readF32(globalTimerDeltaAddress);
      s32 rounded = (s32)std::lround(delta);
      speedFrames = rounded >= 1 && rounded <= 10 ? (u32)rounded : 1;
    }
    if(speedFrames == 0) speedFrames = 1;
    if(global <= gameplayOriginGlobal) return 1;
    return (u64)((global - gameplayOriginGlobal) / (s32)speedFrames) + 1;
  }

  auto traceFrame() -> void {
    configure();
    if(complete) return;

    videoFrame++;

    const char* playerSource = nullptr;
    u32 player = selectedPlayer(playerSource);
    s32 currentStage = readS32(currentStageToLoadAddress);
    bool hasPlayer = targetStageMatches(currentStage) && validPlayerPointer(player);
    u64 gameplayFrame = currentGameplayFrame();
    u32 players[4] = {
      readU32(playerPointersAddress + 0),
      readU32(playerPointersAddress + 4),
      readU32(playerPointersAddress + 8),
      readU32(playerPointersAddress + 12),
    };

    f32 posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    f32 colX = 0.0f, colY = 0.0f, colZ = 0.0f;
    f32 camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    f32 camTargetX = 0.0f, camTargetY = 0.0f, camTargetZ = 0.0f;
    f32 camUpX = 0.0f, camUpY = 0.0f, camUpZ = 0.0f;
    f32 camFloorX = 0.0f, camFloorY = 0.0f, camFloorZ = 0.0f;
    f32 camDx = 0.0f, camDy = 0.0f, camDz = 0.0f;
    f32 facingX = 0.0f, facingY = 0.0f, facingZ = 0.0f;
    f32 roomBasisX = 0.0f, roomBasisY = 0.0f, roomBasisZ = 0.0f;
    f32 vvTheta = 0.0f;
    f32 field70 = 0.0f, stanHeight = 0.0f;
    f32 colourFadeMax = 0.0f;
    f32 speedForwards = 0.0f, speedSideways = 0.0f;
    f32 speedGo = 0.0f, speedStrafe = 0.0f, speedBoost = 0.0f;
    f32 speedTheta = 0.0f, speedVerta = 0.0f;
    f32 headX = 0.0f, headY = 0.0f, headZ = 0.0f;
    f32 prevX = 0.0f, prevY = 0.0f, prevZ = 0.0f;
    s32 speedMaxTime60 = 0;
    s32 watchPause = 0;
    s32 watchState = 0;
    s32 outsideWatchMenu = 0;
    s32 watchOpenReq = 0;
    s32 pausingFlag = 0;
    u32 prop = 0;
    u32 currentTile = 0;
    u32 portalTile = 0;
    bool frozenIntroCamera = false;
    s32 cameraMode = readS32(cameraModeAddress);
    s32 cameraAfterCinema = readS32(cameraAfterCinemaAddress);
    f32 cameraTransitionTimer = readF32(cameraTransitionTimerAddress);
    s32 introCameraIndex = readS32(introCameraIndexAddress);
    u32 introSwirl = readU32(introSwirlAddress);
    s32 introAnimationIndex = readS32(introAnimationIndexAddress);
    s32 introSwirlPresent = validRdram(introSwirl, 0x20) ? 1 : 0;
    s32 introSwirlCount = 0;
    u64 introSwirlHash = 0;
    s32 introSwirlCurrentIndex = -1;
    s32 introSwirlCurrentFlags = 0;
    f32 introSwirlCurrentX = 0.0f;
    f32 introSwirlCurrentY = 0.0f;
    f32 introSwirlCurrentZ = 0.0f;
    f32 introSwirlCurrentCurve = 0.0f;
    f32 introSwirlCurrentDuration = 0.0f;
    s32 introSwirlCurrentPad = -1;
    u32 selectedIntroCamera = readU32(selectedIntroCameraAddress);
    bool selectedIntroCameraPresent = validRdram(selectedIntroCamera, 0x1c);
    f32 selectedIntroCameraX = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x04) : 0.0f;
    f32 selectedIntroCameraY = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x08) : 0.0f;
    f32 selectedIntroCameraZ = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x0c) : 0.0f;
    f32 selectedIntroCameraYaw = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x10) : 0.0f;
    f32 selectedIntroCameraPitch = selectedIntroCameraPresent ? readF32(selectedIntroCamera + 0x14) : 0.0f;
    s32 selectedIntroCameraPad = selectedIntroCameraPresent ? readS32(selectedIntroCamera + 0x18) : -1;
    bool introBondPresent = false;
    s32 introBondChrnum = -1;
    s32 introBondAction = -1;
    s32 introBondSleep = -1;
    s32 introBondField20 = 0;
    s32 introBondOnscreen = 0;
    s32 introBondModelMtx = 0;
    s32 introBondAnimValid = 0;
    f32 introBondAnimFrame = 0.0f;
    f32 introBondAnimEnd = 0.0f;
    f32 introBondAnimSpeed = 0.0f;
    f32 introBondAnimAbsSpeed = 0.0f;
    s32 introBondAnimLooping = 0;
    s32 introBondAnimGunhand = -1;
    u32 introBondAnimPtr = 0;
    s32 introBondAnimFrames = -1;
    s32 introBondAnimEntryOffset = -1;
    s32 introBondAnimBitsOffset = -1;
    u64 introBondAnimHash = 0;
    s32 currentMenuValue = currentMenu();
    bool menuClosed = stockMenuInputClosed(hasPlayer);

    if(introSwirlPresent) {
      introSwirlHash = 1469598103934665603ull;
      for(s32 index = 0; index < 64; index++) {
        u32 entry = introSwirl + (u32)index * 0x20u;
        if(!validRdram(entry, 0x20)) break;
        if(readS32(entry + 0x00) != 3) break;

        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x00));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x04));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x08));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x0c));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x10));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x14));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x18));
        introSwirlHash = hashU32(introSwirlHash, readU32(entry + 0x1c));
        introSwirlCount++;
      }

      if(introSwirlCount > 0) {
        introSwirlCurrentIndex = introCameraIndex;
        if(introSwirlCurrentIndex < 0 || introSwirlCurrentIndex >= introSwirlCount) {
          introSwirlCurrentIndex = 0;
        }

        u32 entry = introSwirl + (u32)introSwirlCurrentIndex * 0x20u;
        introSwirlCurrentFlags = readS32(entry + 0x04);
        introSwirlCurrentX = readF32(entry + 0x08);
        introSwirlCurrentY = readF32(entry + 0x0c);
        introSwirlCurrentZ = readF32(entry + 0x10);
        introSwirlCurrentCurve = readF32(entry + 0x14);
        introSwirlCurrentDuration = readF32(entry + 0x18);
        introSwirlCurrentPad = readS32(entry + 0x1c);
      }
    }

    if(hasPlayer) {
      prop = readU32(player + PlayerProp);
      if(prop != 0 && validRdram(prop, PropPos + 12)) {
        posX = readF32(prop + PropPos + 0);
        posY = readF32(prop + PropPos + 4);
        posZ = readF32(prop + PropPos + 8);
      }

      if(prop != 0 && validRdram(prop, PropChr + 4)) {
        u32 introChr = readU32(prop + PropChr);
        if(validRdram(introChr, ChrField20 + 4)) {
          u32 introModel = readU32(introChr + ChrModel);
          u32 introProp = readU32(introChr + ChrProp);

          introBondPresent = true;
          introBondChrnum = readS16(introChr + ChrChrnum);
          introBondAction = readS8(introChr + ChrActiontype);
          introBondSleep = readS8(introChr + ChrSleep);
          introBondField20 = readU32(introChr + ChrField20) != 0 ? 1 : 0;

          if(validRdram(introProp, PropFlags + 1)) {
            introBondOnscreen = (readU8(introProp + PropFlags) & 0x02) != 0 ? 1 : 0;
          }

          if(validRdram(introModel, ModelSpeed + 4)) {
            u32 introAnim = readU32(introModel + ModelAnim);
            introBondModelMtx = readU32(introModel + ModelRenderPos) != 0 ? 1 : 0;

            if(validRdram(introAnim, 0x14)) {
              f32 endFrame = readF32(introModel + ModelEndFrame);
              u16 animFrameCount = readU16(introAnim + ModelAnimationFrameCount);

              introBondAnimValid = 1;
              introBondAnimPtr = introAnim;
              introBondAnimFrames = animFrameCount;
              introBondAnimEntryOffset = (s32)logicalAnimationOffset(readU32(introAnim + 0x08));
              introBondAnimBitsOffset = (s32)logicalAnimationOffset(readU32(introAnim + 0x10));
              introBondAnimHash = 1469598103934665603ull;
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x04));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU8(introAnim + 0x06));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU8(introAnim + 0x07));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)introBondAnimEntryOffset);
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x0c));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)readU16(introAnim + 0x0e));
              introBondAnimHash = hashU32(introBondAnimHash, (u32)introBondAnimBitsOffset);
              introBondAnimFrame = readF32(introModel + ModelFrame);
              introBondAnimEnd = endFrame >= 0.0f ? endFrame : (f32)animFrameCount - 1.0f;
              introBondAnimSpeed = readF32(introModel + ModelSpeed);
              introBondAnimAbsSpeed = introBondAnimSpeed < 0.0f ? -introBondAnimSpeed : introBondAnimSpeed;
              introBondAnimLooping = readS8(introModel + ModelAnimLooping);
              introBondAnimGunhand = readS8(introModel + ModelGunhand);
            }
          }
        }
      }

      frozenIntroCamera = readS32(player + PlayerViewMode) == 1;
      vvTheta = readF32(player + PlayerVvTheta);
      field70 = readF32(player + PlayerField70);
      stanHeight = readF32(player + PlayerStanHeight);
      colourFadeMax = readF32(player + PlayerColourFadeTimeMax60);
      roomBasisX = readF32(player + PlayerCurrentModelPos + 0);
      roomBasisY = readF32(player + PlayerCurrentModelPos + 4);
      roomBasisZ = readF32(player + PlayerCurrentModelPos + 8);
      currentTile = readU32(player + PlayerField488 + CollisionCurrentTile);
      portalTile = readU32(player + PlayerField488 + CollisionPortalTile);
      colX = readF32(player + PlayerCollisionPos + 0);
      colY = readF32(player + PlayerCollisionPos + 4);
      colZ = readF32(player + PlayerCollisionPos + 8);
      if(frozenIntroCamera) {
        camX = readF32(player + PlayerIntroPos + 0);
        camY = readF32(player + PlayerIntroPos + 4);
        camZ = readF32(player + PlayerIntroPos + 8);
        camTargetX = readF32(player + PlayerIntroTarget + 0);
        camTargetY = readF32(player + PlayerIntroTarget + 4);
        camTargetZ = readF32(player + PlayerIntroTarget + 8);
        camUpX = readF32(player + PlayerIntroUp + 0);
        camUpY = readF32(player + PlayerIntroUp + 4);
        camUpZ = readF32(player + PlayerIntroUp + 8);
        camFloorX = readF32(player + PlayerIntroFloor + 0);
        camFloorY = readF32(player + PlayerIntroFloor + 4);
        camFloorZ = readF32(player + PlayerIntroFloor + 8);
        f32 lookX = camTargetX - camX;
        f32 lookY = camTargetY - camY;
        f32 lookZ = camTargetZ - camZ;
        f32 lookLen = std::sqrt((lookX * lookX) + (lookY * lookY) + (lookZ * lookZ));
        if(lookLen > 0.0001f) {
          facingX = lookX / lookLen;
          facingY = lookY / lookLen;
          facingZ = lookZ / lookLen;
        }
      } else {
        camX = readF32(player + PlayerField488 + CollisionCameraPos + 0);
        camY = readF32(player + PlayerField488 + CollisionCameraPos + 4);
        camZ = readF32(player + PlayerField488 + CollisionCameraPos + 8);
        f32 viewX = readF32(player + PlayerField488 + CollisionAppliedView + 0);
        f32 viewY = readF32(player + PlayerField488 + CollisionAppliedView + 4);
        f32 viewZ = readF32(player + PlayerField488 + CollisionAppliedView + 8);
        camTargetX = camX + viewX;
        camTargetY = camY + viewY;
        camTargetZ = camZ + viewZ;
        camUpX = readF32(player + PlayerField488 + CollisionAppliedUp + 0);
        camUpY = readF32(player + PlayerField488 + CollisionAppliedUp + 4);
        camUpZ = readF32(player + PlayerField488 + CollisionAppliedUp + 8);
        camFloorX = readF32(player + PlayerField488 + CollisionFloor + 0);
        camFloorY = readF32(player + PlayerField488 + CollisionFloor + 4);
        camFloorZ = readF32(player + PlayerField488 + CollisionFloor + 8);
        facingX = readF32(player + PlayerField488 + CollisionTheta + 0);
        facingY = readF32(player + PlayerField488 + CollisionTheta + 4);
        facingZ = readF32(player + PlayerField488 + CollisionTheta + 8);
      }
      camDx = camX - colX;
      camDy = camY - colY;
      camDz = camZ - colZ;

      speedForwards = readF32(player + PlayerSpeedForwards);
      speedSideways = readF32(player + PlayerSpeedSideways);
      speedGo = readF32(player + PlayerSpeedGo);
      speedStrafe = readF32(player + PlayerSpeedStrafe);
      speedBoost = readF32(player + PlayerSpeedBoost);
      speedTheta = readF32(player + PlayerSpeedTheta);
      speedVerta = readF32(player + PlayerSpeedVerta);
      speedMaxTime60 = readS32(player + PlayerSpeedMaxTime60);
      headX = readF32(player + PlayerHeadPos + 0);
      headY = readF32(player + PlayerHeadPos + 4);
      headZ = readF32(player + PlayerHeadPos + 8);
      prevX = readF32(player + PlayerBondPrevPos + 0);
      prevY = readF32(player + PlayerBondPrevPos + 4);
      prevZ = readF32(player + PlayerBondPrevPos + 8);
      watchPause = readS32(player + PlayerWatchPauseTime);
      watchState = readS32(player + PlayerWatchAnimationState);
      outsideWatchMenu = readS32(player + PlayerOutsideWatchMenu);
      watchOpenReq = readS32(player + PlayerOpenCloseSoloWatchMenu);
      pausingFlag = readS32(player + PlayerPausingFlag);
    }

    fprintf(trace,
      "{\"f\":%llu,\"p\":%d,\"source\":\"ares_mgb64_oracle\","
      "\"pos\":[%.2f,%.2f,%.2f],\"col\":[%.2f,%.2f,%.2f],"
      "\"cam_pos\":[%.2f,%.2f,%.2f],"
      "\"cam_target\":[%.2f,%.2f,%.2f],"
      "\"cam_up\":[%.2f,%.2f,%.2f],"
      "\"cam_floor\":[%.2f,%.2f,%.2f],"
      "\"cam_delta\":[%.2f,%.2f,%.2f],"
      "\"room_basis\":[%.2f,%.2f,%.2f],"
      "\"theta\":%.4f,\"floor\":%.2f,\"stan_h\":%.2f,"
      "\"facing\":[%.4f,%.4f,%.4f],"
      "\"cam\":%d,\"cam_after\":%d,\"icam\":%d,\"p_unk\":%d,"
      "\"move\":{\"speed\":[%.5f,%.5f],\"raw\":[%.5f,%.5f],\"boost\":%.5f,"
      "\"turn\":%.5f,\"pitch\":%.5f,\"max_t\":%d,"
      "\"head\":[%.3f,%.3f,%.3f],\"prev\":[%.2f,%.2f,%.2f],"
      "\"clock\":%d,\"dt\":%.2f,\"global\":%d},"
      "\"watch\":{\"state\":%d,\"pause\":%d,\"outside\":%d,\"open_req\":%d,\"pausing\":%d},"
      "\"front\":{\"active_stage\":%d,\"menu\":%d},"
      "\"intro\":{\"frozen\":%d,\"timer\":%.2f,\"colour_fade_max\":%.2f,"
      "\"setup\":{\"anim_index\":%d,\"swirl\":{\"present\":%d,\"count\":%d,\"hash\":\"0x%016llX\","
      "\"current\":{\"index\":%d,\"flags\":%d,\"pos\":[%.4f,%.4f,%.4f],"
      "\"curve\":%.4f,\"duration\":%.4f,\"pad\":%d}}},"
      "\"swirl\":\"0x%08x\",\"selected_camera_ptr\":\"0x%08x\","
      "\"selected_camera\":{\"present\":%d,"
      "\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.6f,\"pitch\":%.6f,\"pad\":%d},"
      "\"bond_present\":%d,\"bond_chrnum\":%d,\"bond_action\":%d,\"bond_sleep\":%d,"
      "\"bond_field20\":%d,\"bond_model_mtx\":%d,\"bond_onscreen\":%d,"
      "\"bond_anim\":{\"valid\":%d,\"ptr\":\"0x%08x\",\"frames\":%d,\"hash\":\"0x%016llX\","
      "\"entry_offset\":%d,\"bits_offset\":%d,\"frame\":%.2f,\"end\":%.2f,\"speed\":%.4f,"
      "\"abs_speed\":%.4f,\"looping\":%d,\"gunhand\":%d},"
      "\"tile\":\"0x%08x\",\"portal_tile\":\"0x%08x\"},"
      "\"oracle\":{\"provider\":\"ares\",\"layout\":\"%s\",\"stage\":%d,\"target_stage\":%d,\"player_source\":\"%s\","
      "\"player\":\"0x%08x\",\"prop\":\"0x%08x\","
      "\"players\":[\"0x%08x\",\"0x%08x\",\"0x%08x\",\"0x%08x\"],"
      "\"input_frame\":%llu,\"gameplay_frame\":%llu,"
      "\"gameplay_origin_global\":%d,\"gameplay_origin_input\":%llu,"
      "\"gate\":{\"menu_closed\":%d,\"gameplay_start_global\":%d,\"close_menu_on_player\":%d},"
      "\"input\":{\"frame\":%llu,\"gameplay_frame\":%llu,\"buttons\":\"0x%04x\","
      "\"stick\":[%d,%d],\"menu_events\":%u,\"gameplay_events\":%u,"
      "\"suppressed_menu_events\":%u,\"suppressed_gameplay_events\":%u,"
      "\"menu_closed\":%d}}}\n",
      (unsigned long long)videoFrame, hasPlayer ? 1 : 0,
      posX, posY, posZ, colX, colY, colZ,
      camX, camY, camZ,
      camTargetX, camTargetY, camTargetZ,
      camUpX, camUpY, camUpZ,
      camFloorX, camFloorY, camFloorZ,
      camDx, camDy, camDz,
      roomBasisX, roomBasisY, roomBasisZ,
      vvTheta, field70, stanHeight,
      facingX, facingY, facingZ,
      cameraMode, cameraAfterCinema, introCameraIndex, readS32(player + PlayerViewMode),
      speedForwards, speedSideways, speedGo, speedStrafe, speedBoost,
      speedTheta, speedVerta, speedMaxTime60,
      headX, headY, headZ, prevX, prevY, prevZ,
      readS32(clockTimerAddress), readF32(globalTimerDeltaAddress), readS32(globalTimerAddress),
      watchState, watchPause, outsideWatchMenu, watchOpenReq, pausingFlag,
      currentStage, currentMenuValue,
      frozenIntroCamera ? 1 : 0, cameraTransitionTimer, colourFadeMax,
      introAnimationIndex,
      introSwirlPresent, introSwirlCount, (unsigned long long)introSwirlHash,
      introSwirlCurrentIndex, introSwirlCurrentFlags,
      introSwirlCurrentX, introSwirlCurrentY, introSwirlCurrentZ,
      introSwirlCurrentCurve, introSwirlCurrentDuration, introSwirlCurrentPad,
      introSwirl, selectedIntroCamera,
      selectedIntroCameraPresent ? 1 : 0,
      selectedIntroCameraX, selectedIntroCameraY, selectedIntroCameraZ,
      selectedIntroCameraYaw, selectedIntroCameraPitch, selectedIntroCameraPad,
      introBondPresent ? 1 : 0, introBondChrnum, introBondAction, introBondSleep,
      introBondField20, introBondModelMtx, introBondOnscreen,
      introBondAnimValid, introBondAnimPtr, introBondAnimFrames,
      (unsigned long long)introBondAnimHash,
      introBondAnimEntryOffset, introBondAnimBitsOffset,
      introBondAnimFrame, introBondAnimEnd,
      introBondAnimSpeed, introBondAnimAbsSpeed, introBondAnimLooping,
      introBondAnimGunhand,
      currentTile, portalTile,
      symbolLayout, currentStage, targetStage, playerSource ? playerSource : "none",
      player, prop, players[0], players[1], players[2], players[3],
      (unsigned long long)inputFrame, (unsigned long long)gameplayFrame,
      gameplayTimelineStarted ? gameplayOriginGlobal : -1,
      (unsigned long long)(gameplayTimelineStarted ? gameplayOriginInputFrame : 0),
      menuClosed ? 1 : 0, gameplayStartGlobal, closeMenuOnPlayer ? 1 : 0,
      (unsigned long long)lastInputFrame,
      (unsigned long long)lastInputGameplayFrame,
      lastInputButtons,
      lastInputStickX, lastInputStickY,
      lastInputMenuEvents, lastInputGameplayEvents,
      lastInputSuppressedMenuEvents, lastInputSuppressedGameplayEvents,
      lastInputMenuClosed ? 1 : 0);

    if(frameLimit && videoFrame >= frameLimit) {
      fclose(trace);
      trace = nullptr;
      complete = true;
      fprintf(stderr, "mgb64 oracle: captured %llu frame(s)\n", (unsigned long long)videoFrame);
    }
  }

  ~OracleState() {
    if(trace) fclose(trace);
  }
};

auto oracleState() -> OracleState& {
  static OracleState state;
  return state;
}

}

auto mgb64OracleControllerRead(n32 data) -> n32 {
  return oracleState().scriptedController(data);
}

auto mgb64OracleFrameHook() -> void {
  oracleState().traceFrame();
}

}
'''

n64_hpp = src / "ares/n64/n64.hpp"
text = n64_hpp.read_text(encoding="utf-8")
if "mgb64OracleFrameHook" not in text:
    text = text.replace(
        "  auto option(string name, string value) -> bool;\n",
        "  auto option(string name, string value) -> bool;\n"
        "  auto mgb64OracleControllerRead(n32 data) -> n32;\n"
        "  auto mgb64OracleFrameHook() -> void;\n",
        1,
    )
    n64_hpp.write_text(text, encoding="utf-8")

gamepad_cpp = src / "ares/n64/controller/gamepad/gamepad.cpp"
text = gamepad_cpp.read_text(encoding="utf-8")
if "mgb64OracleControllerRead" not in text:
    text = text.replace("\n  return data;\n}\n\nauto Gamepad::getInodeChecksum", "\n  return mgb64OracleControllerRead(data);\n}\n\nauto Gamepad::getInodeChecksum", 1)
    gamepad_cpp.write_text(text, encoding="utf-8")

vi_cpp = src / "ares/n64/vi/vi.cpp"
text = vi_cpp.read_text(encoding="utf-8")
if "mgb64OracleFrameHook" not in text:
    text = text.replace(
        "        refreshed = true;\n        screen->frame();\n",
        "        refreshed = true;\n        mgb64OracleFrameHook();\n        screen->frame();\n",
        1,
    )
    vi_cpp.write_text(text, encoding="utf-8")
text = vi_cpp.read_text(encoding="utf-8")
oracle_block_start = text.find("#include <n64/n64.hpp>\n\n#include <cmath>\n#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n\nnamespace ares::Nintendo64 {\n\nnamespace {\n\nstruct OracleInputEvent")
if oracle_block_start >= 0:
    vi_cpp.write_text(text[:oracle_block_start].rstrip() + "\n\n" + oracle_cpp.rstrip() + "\n", encoding="utf-8")
elif "struct OracleInputEvent" not in text:
    vi_cpp.write_text(text.rstrip() + "\n\n" + oracle_cpp.rstrip() + "\n", encoding="utf-8")

compilerconfig = src / "cmake/macos/compilerconfig.cmake"
if compilerconfig.exists():
    text = compilerconfig.read_text(encoding="utf-8")
    if "--show-sdk-version" not in text:
        needle = """  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-platform-version
    OUTPUT_VARIABLE ares_macos_current_sdk
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
"""
        replacement = needle + """  if(NOT result EQUAL 0)
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-version
      OUTPUT_VARIABLE ares_macos_current_sdk
      RESULT_VARIABLE result
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
"""
        text = text.replace(needle, replacement, 1)
        compilerconfig.write_text(text, encoding="utf-8")

macos_ui = src / "desktop-ui/cmake/os-macos.cmake"
if macos_ui.exists():
    text = macos_ui.read_text(encoding="utf-8")
    text = text.replace(
        "if(ACTOOL_PROGRAM)\n",
        'if(ACTOOL_PROGRAM AND NOT "$ENV{ARES_CLT_BUILD}" STREQUAL "1")\n',
        1,
    )
    macos_ui.write_text(text, encoding="utf-8")
PY

if [[ "$NO_BUILD" -eq 1 ]]; then
    echo "Prepared instrumented ares checkout: $SRC_DIR"
    exit 0
fi

ARES_CLT_BUILD=1 cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target desktop-ui --parallel "$JOBS"

if [[ -x "$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares"
elif [[ -x "$BUILD_DIR/desktop-ui/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares"
else
    echo "FAIL: built desktop-ui target, but could not find the ares executable" >&2
    exit 1
fi

echo ""
echo "PASS: instrumented ares binary:"
echo "  $ARES_BIN"
echo ""
echo "Run a ROM-vs-native movement capture with:"
echo "  tools/movement_oracle_capture.sh --ares-bin \"$ARES_BIN\" --rom baserom.u.z64"
