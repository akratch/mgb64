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

# High-detail variants for the G_MODERNMESH path (float verts, big textures)
HD=(--detail high --tex-size 512)
python3 "$GEN" --name spruce_hd_a --seed 7  --whorls 22 --cards 7 "${HD[@]}" "${COMMON[@]}"
python3 "$GEN" --name spruce_hd_b --seed 23 --whorls 26 --cards 8 "${HD[@]}" "${COMMON[@]}"
python3 "$GEN" --name spruce_hd_c --seed 41 --whorls 20 --cards 7 --snow 0.8 "${HD[@]}" "${COMMON[@]}"

# Photoscan hero: Poly Haven fir_sapling_medium (CC0), decimated 685k->52k.
# ~74 MB one-time download into the gitignored cache.
FIR="$CACHE/fir_sapling_medium"
if [ ! -s "$FIR/fir_sapling_medium_1k.gltf" ]; then
  mkdir -p "$FIR/textures"
  python3 - "$FIR" <<'PY'
import json, sys, urllib.request
dst = sys.argv[1]
api = json.load(urllib.request.urlopen(
    "https://api.polyhaven.com/files/fir_sapling_medium"))
inc = api["gltf"]["1k"]["gltf"]
urllib.request.urlretrieve(inc["url"], f"{dst}/fir_sapling_medium_1k.gltf")
for rel, meta in inc["include"].items():
    urllib.request.urlretrieve(meta["url"], f"{dst}/{rel}")
PY
fi
python3 "$REPO/tools/decor/decimate_gltf.py" "$FIR/fir_sapling_medium_1k.gltf"   --name fir_hd --grid 170 --ambient 0.85 --license CC0   --url "https://polyhaven.com/a/fir_sapling_medium"   --cutout-material twig --out "$HERE/models"

echo "decor models ready: $HERE/models"
