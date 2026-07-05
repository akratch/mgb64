#!/usr/bin/env bash
#
# release.sh -- maintainer release helper.
#
# Builds + validates the macOS app LOCALLY (the platform this repo is developed
# on), assembles the release assets into dist/, and OPTIONALLY cuts/updates the
# GitHub Release. Windows + Linux binaries come from the release CI
# (.github/workflows/release.yml) — download those artifacts into dist/ first if
# you want them in the same release.
#
# Nothing here is destructive by default: without --publish it only builds local
# assets and prints the next command. See docs/RELEASING.md.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

version="dev"
publish=0
rolling=0
repo=""
skip_macos=0
universal=0

usage() {
  cat <<'USAGE'
Usage: scripts/release.sh [options]
  --version VER     Version label (e.g. v0.3.0). Default: dev
  --repo OWNER/NAME GitHub repo to publish to (required with --publish)
  --publish         Create/update the GitHub Release with the assets in dist/
  --rolling-latest  Publish to a rolling 'latest' prerelease instead of a tag
  --skip-macos      Don't build macOS (just publish whatever is in dist/)
  --universal       Build a universal arm64+x86_64 macOS binary. Default: host
                    arch only. A universal build needs a universal SDL2; a plain
                    Homebrew SDL2 is single-arch and will fail the x86_64 link,
                    so the shipped prebuilt is Apple-Silicon-only (see README).
USAGE
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --repo) repo="$2"; shift 2 ;;
    --publish) publish=1; shift ;;
    --rolling-latest) rolling=1; shift ;;
    --skip-macos) skip_macos=1; shift ;;
    --universal) universal=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

dist="dist"; mkdir -p "$dist"

# 1. macOS app + .zip (built + validated on this machine).
if [[ "$skip_macos" -eq 0 ]]; then
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: macOS build must run on macOS (or pass --skip-macos)." >&2; exit 1
  fi
  if [[ "$universal" -eq 1 ]]; then
    echo "[release] building MGB64.app (universal arm64+x86_64)..."
    ./macos/Scripts/build_gl_app.sh --universal --output build-macos-app/MGB64.app
  else
    echo "[release] building MGB64.app (host arch: $(uname -m))..."
    ./macos/Scripts/build_gl_app.sh --output build-macos-app/MGB64.app
  fi
  ./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app
  ( cd build-macos-app && ditto -c -k --sequesterRsrc --keepParent MGB64.app \
      "$OLDPWD/$dist/mgb64-macos-$version.zip" )
  echo "[release] macOS asset: dist/mgb64-macos-$version.zip"
fi

echo "[release] assets staged in dist/:"
ls -1 "$dist"/mgb64-*-"$version".* 2>/dev/null || echo "  (none for version $version)"

# 2. Publish (explicit only).
if [[ "$publish" -eq 1 ]]; then
  [[ -n "$repo" ]] || { echo "ERROR: --publish requires --repo OWNER/NAME." >&2; exit 1; }
  command -v gh >/dev/null || { echo "ERROR: gh CLI required for --publish." >&2; exit 1; }
  mapfile -t assets < <(ls -1 "$dist"/mgb64-*-"$version".* 2>/dev/null)
  [[ ${#assets[@]} -gt 0 ]] || { echo "ERROR: no dist/ assets for version $version." >&2; exit 1; }

  tag="$version"; extra=()
  if [[ "$rolling" -eq 1 ]]; then tag="latest"; extra=(--prerelease); fi
  notes_file="RELEASE_NOTES.md"; [[ -f "$notes_file" ]] || notes_file="/dev/null"

  echo "[release] publishing ${#assets[@]} asset(s) to $repo @ $tag ..."
  if gh release view "$tag" --repo "$repo" >/dev/null 2>&1; then
    gh release upload "$tag" "${assets[@]}" --repo "$repo" --clobber
  else
    gh release create "$tag" "${assets[@]}" --repo "$repo" \
      --title "MGB64 $version" --notes-file "$notes_file" "${extra[@]}"
  fi
  echo "[release] done: https://github.com/$repo/releases/tag/$tag"
else
  echo "[release] not published. To publish:"
  echo "  scripts/release.sh --version $version --repo <owner/name> --publish"
fi
