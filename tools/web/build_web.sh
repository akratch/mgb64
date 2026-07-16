#!/usr/bin/env bash
# One-command web build. Usage: tools/web/build_web.sh [--debug]
# Produces dist/web/ (ge007_web.js, ge007_web.wasm, index.html, shell js/css).
set -euo pipefail
EMSDK_VERSION="4.0.10"   # floor: first release bundling the emdawnwebgpu port
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "emsdk not found at $EMSDK_DIR — install:" >&2
    echo "  git clone https://github.com/emscripten-core/emsdk.git $EMSDK_DIR" >&2
    echo "  $EMSDK_DIR/emsdk install $EMSDK_VERSION && $EMSDK_DIR/emsdk activate $EMSDK_VERSION" >&2
    exit 3
fi
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null
BUILD_TYPE="Release"; [ "${1:-}" = "--debug" ] && BUILD_TYPE="Debug"
emcmake cmake -S "$ROOT" -B "$ROOT/build-web" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT/build-web" -j8 --target ge007_web
mkdir -p "$ROOT/dist/web"
cp "$ROOT/build-web/ge007_web.js" "$ROOT/build-web/ge007_web.wasm" "$ROOT/dist/web/"
cp "$ROOT"/web/index.html "$ROOT"/web/mgb64-shell.js "$ROOT"/web/style.css "$ROOT"/web/favicon.svg "$ROOT/dist/web/"
echo "dist/web ready:"; ls -la "$ROOT/dist/web"
