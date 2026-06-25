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


def _pil():
    try:
        from PIL import Image
        return Image
    except ImportError:
        sys.exit("Pillow required: pip install -r tools/texpack/requirements.txt")


def load_rgba(src):
    """Decode an engine dump (.ppm/.png) to a PIL RGBA image.

    The engine dumps RGB to <tok>.rgba.ppm and the ALPHA channel SEPARATELY to
    <tok>.alpha.pgm (PPM has no alpha). We must re-attach it, or alpha-cutout
    textures lose their transparency through the pipeline: round wheels become
    solid squares, grates fill in, and glass goes opaque."""
    Image = _pil()
    im = Image.open(src).convert("RGBA")
    if src.endswith(".rgba.ppm"):
        alpha_path = src[:-len(".rgba.ppm")] + ".alpha.pgm"
        if os.path.exists(alpha_path):
            a = Image.open(alpha_path).convert("L")
            if a.size == im.size:
                im.putalpha(a)
    return im


def is_tileable(im, tol=20.0):
    """Heuristic: a texture wraps seamlessly if opposite edges roughly match
    (the right edge continues into the left, top into bottom). Such textures
    tile across large surfaces, so they must be upscaled seam-safe."""
    w, h = im.size
    if w < 8 or h < 8:
        return False
    px = im.load()

    def edge_cols(x0, x1):
        s = 0.0
        for y in range(h):
            a = px[x0, y]; b = px[x1, y]
            s += abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])
        return s / (h * 3.0)

    def edge_rows(y0, y1):
        s = 0.0
        for x in range(w):
            a = px[x, y0]; b = px[x, y1]
            s += abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])
        return s / (w * 3.0)

    return edge_cols(0, w - 1) < tol and edge_rows(0, h - 1) < tol


def tile_3x3(im):
    """Replicate a texture into a 3x3 grid so the upscaler sees the wrap context."""
    Image = _pil()
    w, h = im.size
    out = Image.new("RGBA", (w * 3, h * 3))
    for ty in range(3):
        for tx in range(3):
            out.paste(im, (tx * w, ty * h))
    return out


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
    ap.add_argument("--no-seamless", action="store_true",
                    help="disable seam-safe (tile-and-crop) upscaling for tileable textures")
    args = ap.parse_args()

    if not (os.path.isfile(args.realesrgan) and os.access(args.realesrgan, os.X_OK)):
        sys.exit(f"Real-ESRGAN not found at {args.realesrgan}\n  run tools/texpack/fetch_realesrgan.sh first")

    srcs = sorted(glob.glob(os.path.join(args.dump, "*.rgba.ppm")) +
                  glob.glob(os.path.join(args.dump, "*.png")))
    if not srcs:
        sys.exit(f"no *.rgba.ppm / *.png dumps in {args.dump}")

    Image = _pil()
    manifest = load_manifest(args.dump) if args.route else {}
    stage = os.path.join(args.out, ".stage"); os.makedirs(stage, exist_ok=True)
    outdir = os.path.join(args.out, "textures"); os.makedirs(outdir, exist_ok=True)
    TINY = 16   # source textures this small or smaller go through Lanczos, not the AI
                # upscaler — Real-ESRGAN hallucinates faces/junk on tiny ambiguous tiles.

    by_model = {}          # model -> [tokens] for the ESRGAN batch
    tiled = {}             # token -> (orig_w, orig_h) for seam-safe crop after upscale
    n = lanczos_n = 0
    for s in srcs:
        tok = parse_token(os.path.basename(s))
        if not tok:
            continue
        im = load_rgba(s)
        w, h = im.size
        if max(w, h) <= TINY:
            # anti-hallucination: deterministic Lanczos for tiny textures
            im.resize((w * args.scale, h * args.scale), Image.LANCZOS).save(
                os.path.join(outdir, tok + ".png"))
            lanczos_n += 1; n += 1
            continue
        seamless = (not args.no_seamless) and is_tileable(im)
        staged = tile_3x3(im) if seamless else im
        if seamless:
            tiled[tok] = (w, h)
        staged.save(os.path.join(stage, tok + ".png"))
        model = CLASS_MODEL.get(manifest.get(tok, ""), args.model)
        by_model.setdefault(model, []).append(tok)
        n += 1

    print(f"staged {n} textures ({lanczos_n} tiny->Lanczos, {len(tiled)} tileable->seam-safe); "
          f"AI-upscaling x{args.scale} with: {', '.join(by_model) or '(none)'}")
    for model, toks in by_model.items():
        mdir = os.path.join(stage, "_in_" + model); os.makedirs(mdir, exist_ok=True)
        odir = os.path.join(stage, "_out_" + model); os.makedirs(odir, exist_ok=True)
        for t in toks:
            shutil.copyfile(os.path.join(stage, t + ".png"), os.path.join(mdir, t + ".png"))
        subprocess.run([args.realesrgan, "-i", mdir, "-o", odir,
                        "-s", str(args.scale), "-n", model, "-m", args.models, "-f", "png"],
                       check=True)
        for t in toks:
            up = os.path.join(odir, t + ".png")
            if t in tiled:
                # seam-safe: the staged image was a 3x3 tiling; crop the center tile so
                # the upscaled result wraps seamlessly across mapped quads (no seams).
                ow, oh = tiled[t]; s_ = args.scale
                cu = Image.open(up)
                cu.crop((ow * s_, oh * s_, 2 * ow * s_, 2 * oh * s_)).save(
                    os.path.join(outdir, t + ".png"))
            else:
                shutil.move(up, os.path.join(outdir, t + ".png"))

    shutil.rmtree(stage, ignore_errors=True)
    print(f"pack ready: {outdir}  ({n} textures)")
    print("  (HD packs are ROM-derived -- keep them local, never commit.)")


if __name__ == "__main__":
    main()
