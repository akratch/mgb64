#!/usr/bin/env bash
# Rebuild the Surface showcase decor models (deterministic: same args =>
# byte-identical .glb). Models are generated, not tracked -- the repo ships
# generators + this recipe + the manifest, keeping the tree binary-free.
#
# Bark source: Poly Haven "pine_tree_01" bark diffuse (CC0,
# https://polyhaven.com/a/pine_tree_01). Fetched on demand into a gitignored
# cache; everything else is first-party gen_tree3d.py art (Tier A1).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
CACHE="$HERE/.cache"
BARK="$CACHE/pine_tree_01_bark_diff_1k.jpg"

mkdir -p "$CACHE" "$HERE/models"
if [ ! -s "$BARK" ]; then
  curl -sL --fail \
    "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_tree_01/pine_tree_01_bark_diff_1k.jpg" \
    -o "$BARK"
fi

GEN="$REPO/tools/decor/gen_tree3d.py"
COMMON=(--bark "$BARK" --bark-license CC0
        --bark-url "https://polyhaven.com/a/pine_tree_01"
        --out "$HERE/models")

python3 "$GEN" --name spruce_a --seed 7  --whorls 10 --cards 6 "${COMMON[@]}"
python3 "$GEN" --name spruce_b --seed 23 --whorls 12 --cards 7 "${COMMON[@]}"
python3 "$GEN" --name spruce_c --seed 41 --whorls 9  --cards 6 --snow 0.75 "${COMMON[@]}"
echo "decor models ready: $HERE/models"
