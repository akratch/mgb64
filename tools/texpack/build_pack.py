#!/usr/bin/env python3
"""
build_pack.py -- turn a MGB64 texture dump into an HD texture pack via Real-ESRGAN.

Pipeline (all local, all from YOUR ROM dump -- nothing here is redistributable):

  1. Dump textures from the game:
       GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td \
         ./build/ge007 --level 33 ...            (static/world/HUD art, token-keyed)
     (or GE007_DUMP_LOADED_TEXTURES='*' GE007_DUMP_LOADED_TEXTURE_DIR=/tmp/td)

  2. Fetch the upscaler once:    tools/texpack/fetch_realesrgan.sh

  3. Build the pack:
       python3 tools/texpack/build_pack.py --dump /tmp/td --out /tmp/mypack

  4. (Once the Phase 2 in-game loader lands) point the game at it:
       GE007_TEXTURE_PACK=/tmp/mypack ./build/ge007 ...

Output layout matches the planned loader key (docs/PHASE2_PLAN.md):
  <out>/textures/<token>.png

Requires Pillow (pip install -r tools/texpack/requirements.txt) only to decode the
engine's .ppm dumps. Once the engine dumps PNG directly (Phase 2 STEP 4), Pillow is
optional. The heavy lifting is the bundled Real-ESRGAN ncnn-vulkan (GPU).
"""
import argparse, os, re, shutil, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, ".bin", "realesrgan", "realesrgan-ncnn-vulkan")
DEFAULT_MODELS = os.path.join(HERE, ".bin", "realesrgan", "models")

# draw-class -> model. ESRGAN photo model for surfaces/props/characters; the
# "anime" model is often cleaner on flat/cel UI art. Routing is best-effort; if no
# manifest is present everything uses --model.
CLASS_MODEL = {
    "hud": "realesrgan-x4plus-anime",
    "room": "realesrgan-x4plus",
    "weapon": "realesrgan-x4plus",
    "chrprop": "realesrgan-x4plus",
    "effect": "realesrgan-x4plus",
}

TOKEN_RE = re.compile(r"(?:settex_|tok)(\d+)|loaded_tex_\d+_f\d+_([0-9a-fA-F]+)")


def parse_token(fname):
    """Extract the pack key (settex token, or content/cache hash) from a dump name."""
    m = TOKEN_RE.search(fname)
    if not m:
        return None
    return ("tok" + m.group(1)) if m.group(1) else ("h" + m.group(2))


def to_png(src, dst):
    """Decode an engine dump (.ppm/.png) to a clean RGBA PNG."""
    if src.lower().endswith(".png"):
        shutil.copyfile(src, dst); return True
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow required to decode .ppm dumps: pip install -r tools/texpack/requirements.txt")
    Image.open(src).convert("RGBA").save(dst)
    return True


def load_manifest(dump_dir):
    """token -> draw_class, if a *.texmanifest.csv is present (Phase 2 STEP 1)."""
    cls = {}
    for mf in glob.glob(os.path.join(dump_dir, "*.texmanifest.csv")):
        for line in open(mf):
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 7:
                cls[parts[0]] = parts[6]
    return cls


def main():
    ap = argparse.ArgumentParser(description="Build an HD texture pack from a MGB64 dump via Real-ESRGAN.")
    ap.add_argument("--dump", required=True, help="texture dump dir (GE007_DUMP_*_DIR output)")
    ap.add_argument("--out", required=True, help="output pack dir (creates <out>/textures/)")
    ap.add_argument("--scale", type=int, default=4, choices=[2, 3, 4])
    ap.add_argument("--model", default="realesrgan-x4plus", help="default model when no manifest routing")
    ap.add_argument("--realesrgan", default=DEFAULT_BIN)
    ap.add_argument("--models", default=DEFAULT_MODELS)
    ap.add_argument("--route", action="store_true", help="route model by draw_class via *.texmanifest.csv")
    args = ap.parse_args()

    if not (os.path.isfile(args.realesrgan) and os.access(args.realesrgan, os.X_OK)):
        sys.exit(f"Real-ESRGAN not found at {args.realesrgan}\n  run tools/texpack/fetch_realesrgan.sh first")

    srcs = sorted(glob.glob(os.path.join(args.dump, "*.rgba.ppm")) +
                  glob.glob(os.path.join(args.dump, "*.png")))
    if not srcs:
        sys.exit(f"no *.rgba.ppm / *.png dumps in {args.dump}")

    manifest = load_manifest(args.dump) if args.route else {}
    stage = os.path.join(args.out, ".stage"); os.makedirs(stage, exist_ok=True)
    outdir = os.path.join(args.out, "textures"); os.makedirs(outdir, exist_ok=True)

    # group inputs by the model they will use (so we can batch each model in dir-mode)
    by_model = {}
    n = 0
    for s in srcs:
        tok = parse_token(os.path.basename(s))
        if not tok:
            continue
        png = os.path.join(stage, tok + ".png")
        to_png(s, png)
        model = CLASS_MODEL.get(manifest.get(tok, ""), args.model)
        by_model.setdefault(model, []).append(tok)
        n += 1

    print(f"staged {n} textures; upscaling x{args.scale} with: {', '.join(by_model)}")
    for model, toks in by_model.items():
        mdir = os.path.join(stage, "_in_" + model); os.makedirs(mdir, exist_ok=True)
        odir = os.path.join(stage, "_out_" + model); os.makedirs(odir, exist_ok=True)
        for t in toks:
            shutil.copyfile(os.path.join(stage, t + ".png"), os.path.join(mdir, t + ".png"))
        subprocess.run([args.realesrgan, "-i", mdir, "-o", odir,
                        "-s", str(args.scale), "-n", model, "-m", args.models, "-f", "png"],
                       check=True)
        for t in toks:
            shutil.move(os.path.join(odir, t + ".png"), os.path.join(outdir, t + ".png"))

    shutil.rmtree(stage, ignore_errors=True)
    print(f"pack ready: {outdir}  ({n} textures)")
    print("  (HD packs are ROM-derived -- keep them local, never commit.)")


if __name__ == "__main__":
    main()
