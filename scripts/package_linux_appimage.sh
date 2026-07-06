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

dist="dist"; mkdir -p "$dist"
work="$(mktemp -d)"
appdir="$work/MGB64.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/lib"

cp "$binary" "$appdir/usr/bin/ge007"

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
elif command -v wget >/dev/null 2>&1; then
  wget -q -O "$tool" \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
    && chmod +x "$tool" || tool=""
else
  tool=""
fi
if [[ -n "$tool" ]]; then
  ARCH=x86_64 "$tool" --appimage-extract-and-run "$appdir" "$dist/mgb64-linux-$version.AppImage" \
    && echo "wrote $dist/mgb64-linux-$version.AppImage" \
    || echo "WARN: AppImage build failed; the .tar.gz is still available." >&2
else
  echo "WARN: appimagetool unavailable; produced .tar.gz only." >&2
fi

rm -rf "$work"
