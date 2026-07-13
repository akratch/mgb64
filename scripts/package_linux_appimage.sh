#!/usr/bin/env bash
#
# package_linux_appimage.sh -- package the built Linux `ge007` app into a
# portable AppImage (bundles SDL2 + GL loader) and a plain .tar.gz fallback.
#
# Runs in the release CI (ubuntu-22.04) or locally on Linux. Produces:
#   dist/mgb64-linux-<version>.AppImage
#   dist/mgb64-linux-<version>.tar.gz
#
# The app ships NO game data (bring-your-own-ROM); nothing here embeds ROM bytes.
set -euo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

binary="build/ge007"
version="dev"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) binary="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [--binary PATH] [--version VER]"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done
[[ -x "$binary" ]] || { echo "ERROR: binary not found/executable: $binary" >&2; exit 1; }

# appimagetool is a build-time EXECUTABLE dependency, so it is pinned to an
# immutable release and verified by SHA-256 (fail closed on mismatch) rather than
# fetched from the moving `continuous` alias and run unverified [AUDIT-0037].
APPIMAGETOOL_VERSION="1.9.1"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-x86_64.AppImage"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}';
  else echo ""; fi
}

dist="dist"; mkdir -p "$dist"
work="$(mktemp -d)"
appdir="$work/MGB64.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/lib"

cp "$binary" "$appdir/usr/bin/ge007"

# Community controller-mapping DB (MC.2), next to the binary where
# SDL_GetBasePath() resolves it at controller init.
cp lib/sdl_gamecontrollerdb/gamecontrollerdb.txt "$appdir/usr/bin/" 2>/dev/null || true

# Bundle the linked SDL2 (GL/glibc come from the host; AppImage targets glibc>=host).
sdl="$(ldd "$binary" | awk '/libSDL2/{print $3; exit}')"
if [[ -n "${sdl:-}" && -f "$sdl" ]]; then
  cp -L "$sdl" "$appdir/usr/lib/"
  echo "bundled SDL2: $(basename "$sdl")"
else
  echo "WARN: could not resolve SDL2 to bundle; the AppImage will need a system SDL2." >&2
fi

# AppRun launcher.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/ge007" "$@"
EOF
chmod +x "$appdir/AppRun"

# Desktop entry + a minimal icon (AppImage requires both).
cat > "$appdir/mgb64.desktop" <<'EOF'
[Desktop Entry]
Name=MGB64
Exec=ge007
Icon=mgb64
Type=Application
Categories=Game;
Comment=The Man with the Golden Build
EOF
icon_source="branding/appicon-source.png"
if [[ -f "$icon_source" ]]; then
  cp "$icon_source" "$appdir/mgb64.png"
else
  # 1x1 charcoal PNG placeholder (base64) — used if branding art is missing.
  base64 -d > "$appdir/mgb64.png" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==
EOF
fi

cp LICENSE README.md "$appdir/" 2>/dev/null || true

# .tar.gz (always produced — the reliable fallback).
tar -C "$work" -czf "$dist/mgb64-linux-$version.tar.gz" MGB64.AppDir
echo "wrote $dist/mgb64-linux-$version.tar.gz"

# AppImage (best-effort: needs FUSE at build via appimagetool).
tool="$work/appimagetool"
if command -v appimagetool >/dev/null 2>&1; then
  tool="$(command -v appimagetool)"
else
  # Fetch the pinned appimagetool. A download failure is best-effort (the .tar.gz
  # still ships), but a DIGEST MISMATCH fails closed -- we never run an unverified
  # or tampered tool [AUDIT-0037].
  fetched=""
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "$tool" "$APPIMAGETOOL_URL" && fetched=1 || fetched=""
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$tool" "$APPIMAGETOOL_URL" && fetched=1 || fetched=""
  fi
  if [[ -n "$fetched" && -f "$tool" ]]; then
    got="$(sha256_of "$tool")"
    if [[ -z "$got" ]]; then
      echo "ERROR: no sha256 tool available to verify appimagetool; refusing to run it." >&2
      exit 1
    elif [[ "$got" != "$APPIMAGETOOL_SHA256" ]]; then
      echo "ERROR: appimagetool ${APPIMAGETOOL_VERSION} digest mismatch:" >&2
      echo "       got      ${got}" >&2
      echo "       expected ${APPIMAGETOOL_SHA256}" >&2
      rm -f "$tool"
      exit 1
    fi
    chmod +x "$tool"
    echo "appimagetool ${APPIMAGETOOL_VERSION} verified (sha256 ${APPIMAGETOOL_SHA256:0:12}...)"
  else
    echo "WARN: appimagetool download unavailable; producing .tar.gz only." >&2
    tool=""
  fi
fi
if [[ -n "$tool" ]]; then
  ARCH=x86_64 "$tool" --appimage-extract-and-run "$appdir" "$dist/mgb64-linux-$version.AppImage" \
    && echo "wrote $dist/mgb64-linux-$version.AppImage" \
    || echo "WARN: AppImage build failed; the .tar.gz is still available." >&2
else
  echo "WARN: appimagetool unavailable; produced .tar.gz only." >&2
fi

rm -rf "$work"
