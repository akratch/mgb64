# Renderer Approximation Registry

> Charter §4.6 document #1. These are the renderer approximation classes we
> **accept as non-goals for pixel-exactness**. A native/ares pixel difference
> that falls inside one of these classes' bounds is *explained* — it is not a
> port defect, and the S2 pixel sweep must not surface it as a candidate.
> Everything outside these bounds is *unexplained* and becomes a candidate for
> the ledger.
>
> Authority (charter rule 2): the renderer has no retail ASM. Ground truth is
> the ares RDP/pixel capture plus documented N64 RDP/VI semantics. Each class
> below cites the documented hardware behaviour it stands in for, states the
> bound (the maximum pixel delta / the region shape it may occupy), and encodes
> that bound as a machine-readable predicate consumed by
> `tools/fidelity/pixel_diff.py`.
>
> **This file is the single source of truth for the classifier.** The fenced
> `json` block below is parsed verbatim by `pixel_diff.py` — edit the prose and
> the block together. Bounds are deliberately conservative (tight): the cost of
> a too-loose bound is a masked real defect, so we would rather surface a
> borderline cluster as a candidate and triage it than hide it here.

The pipeline: a super-threshold pixel is one whose per-channel absolute delta
(max over R/G/B) exceeds `global.delta_threshold`. Super-threshold pixels are
grouped into connected components (4-connectivity). Each cluster is offered to
every class predicate below; if **any** class accepts it, the cluster is
`explained` and tagged with that class. A cluster no class accepts is
`unexplained` and is what the sweep reports.

## Classes

A note on the threshold vs. the bounds: the classifier first drops every pixel
whose delta is ≤ `global.delta_threshold` (the noise floor). So every pixel
*inside* a cluster already exceeds the floor, and a cluster's mean delta is
always strictly above it. The floor is therefore set **below** the smallest
class bound (`vi_filter`) so that low-amplitude approximation residuals still
form clusters and get *explained* rather than silently swallowed by the floor —
the floor removes only sub-perceptual sensor/compression noise; the class
predicates do the actual explaining.

### 1. `vi_filter` — VI output filter (anti-alias / de-dither / gamma)
The N64 Video Interface applies a horizontal AA + de-dither + optional gamma
pass between the framebuffer and the analog output. ares may emit the raw
framebuffer (pre-VI) while native renders a filtered-equivalent image, or vice
versa. The residual is a **low-amplitude, spatially-diffuse** difference that
can cover the whole frame. Bound: mean per-cluster delta ≤ 16, any area, any
location. A cluster whose average delta is that small is filter noise
regardless of how large it is.

### 2. `three_point_filter` — 3-point texture-filter derivative approximation
The RDP samples textures with a 3-point barycentric filter; our GL/Metal path
approximates the screen-space derivative used to pick the filter footprint.
This produces **moderate-amplitude differences in texture interiors** (gradient
banding on smoothly-shaded textured surfaces). Bound: mean delta ≤ 32 over a
region of bounded area, and **not** a thin edge band (minor extent ≥ 3 px, so
silhouette edges fall to `coverage_aa` instead).

### 3. `rdp_dither` — RDP dither pattern
The RDP dithers color (and alpha) with a magic-square / bayer pattern before
5-bit truncation. A different dither phase/pattern yields **many tiny,
high-frequency, spatially-scattered clusters** — each just a few pixels. Bound:
per-cluster area ≤ 64 px and mean delta ≤ 64. (High-frequency speckle, not a
solid block.)

### 4. `coverage_aa` — coverage-based edge anti-aliasing
The RDP's coverage AA blends silhouette/edge pixels against what is behind
them; our coverage model differs at sub-pixel edges, so **thin 1–2px-wide
bands along edges** differ, sometimes strongly (a full-contrast edge that moved
one pixel). Bound: cluster minor extent (min of bbox width/height) ≤ 2 px —
i.e. a thin band — at any amplitude.

## Initial thresholds

`global.delta_threshold = 8` — a per-channel delta at/below this is the noise
floor and is discarded before clustering. `min_cluster_area = 4` drops isolated
speckle below the smallest real cluster. Tighten `delta_threshold` (lower it)
to widen sensitivity once a route's stock baseline is proven clean.

```json
{
  "schema": "mgb64.fidelity.approximations.v1",
  "global": {
    "delta_threshold": 8,
    "connectivity": 4,
    "min_cluster_area": 4
  },
  "classes": [
    {
      "id": "vi_filter",
      "predicate": { "max_mean_delta": 16.0 }
    },
    {
      "id": "three_point_filter",
      "predicate": { "max_mean_delta": 32.0, "max_area": 4096, "min_minor_extent": 3 }
    },
    {
      "id": "rdp_dither",
      "predicate": { "max_mean_delta": 64.0, "max_area": 64 }
    },
    {
      "id": "coverage_aa",
      "predicate": { "max_minor_extent": 2 }
    }
  ]
}
```

Predicate keys (all optional; a cluster satisfies a class only if it satisfies
**every** key the class specifies):

| key | meaning |
| --- | --- |
| `max_mean_delta` | cluster mean per-channel delta must be ≤ this |
| `max_area` | cluster pixel area must be ≤ this |
| `min_area` | cluster pixel area must be ≥ this |
| `max_minor_extent` | min(bbox_w, bbox_h) must be ≤ this (thin band) |
| `min_minor_extent` | min(bbox_w, bbox_h) must be ≥ this (rules out thin edges) |

Every skip / near-miss the classifier makes is recorded in the verdict JSON so a
too-tight or too-loose bound is auditable (charter rule 9).

## Non-classifier renderer approximations

These are accepted, bounded renderer approximations that are **not** expressed as
pixel-cluster classes and are therefore **not** part of the `json` block above —
`pixel_diff.py` never consumes them. Each is a deliberate port divergence from a
documented retail behavior where reproducing retail exactly is either unsafe on a
host (out-of-bounds host memory read) or has been relocated to a different, more
appropriate pipeline stage. Each cites the retail behavior, the port's
approximation, and the bound within which the difference is confined.

### T1. Text control/DEL bytes are not drawn (FID-0108 textRender, FID-0109 textRenderGlow)

Retail (US) `textRender` and `textRenderGlow` gate only `ch < 0x80` before the
ASCII fontchar construct (slti 0x80 at `src/game/textrelated.c:2176` and the
`textRenderGlow` body at `src/game/textrelated.c:3901`), then index the font
table at `second_font_table + ch*24 - 0x318`. Because `0x318 == 33*24` and
`struct fontchar` is 24 bytes, that offset is exactly `(ch-33)` entries into the
table. For a control byte in `0x01..0x1f` (excluding the specially-handled
newline `0x0A`) the offset is **negative** — retail reads *before* the font
table — and for DEL `0x7f` it is entry 94, one past the printable `0x21..0x7e`
range (entries 0..93). On the N64 those reads return the deterministic bytes
adjacent to the table in ROM/RAM and draw a spurious glyph. On the port the same
indexing is `(&((struct fontchar *)second_font_table)[ch - 33])`
(`src/game/textrelated.c:2018` and `:3775`), so replicating retail would perform
a genuine **out-of-bounds host array read** (negative index, or one element past
the printable subrange) — undefined behavior, possibly a crash, and in no case
the same bytes as the N64. The port therefore narrows the render to `0x21..0x7e`
and skips other `ch < 0x80` bytes (advance pointer, reset `prev_char`, no glyph:
`src/game/textrelated.c:2032` and `:3790`). **Bound:** behavior is byte-identical
to retail for every printable ASCII glyph `0x21..0x7e`, for space `0x20`, for
newline `0x0A`, and for the full Japanese path `ch >= 0x80`; it differs *only*
for control/DEL bytes that no authored game string is known to contain (game
text is printable ASCII plus JIS). The divergence is unreachable by real content
and, where it would trigger, replicating retail is memory-unsafe on a host.

### M1. Level-visibility scale relocated from the RSP fixed-point pack to the float gfx domain (FID-0110)

Retail scales every model/room view matrix by the per-level visibility factor
**inside** the s16.16 fixed-point pack: `matrix_4x4_f32_to_s32` multiplies matrix
elements by `D_80032310` (`src/game/matrixmath.c:598`), and `D_80032310[0]` is set
to `65536 * visibility` by `matrix_4x4_7F058C4C` (`src/game/matrixmath.c:478`),
driven from the per-level `mCurrentLevelVisibilityScale` at `src/game/bg.c:5143`.
This was a precision optimization for the RSP's 16.16 hardware. The port's native
converters instead hardcode the plain `65536` scale (`FTOFIX32` native branch
`src/game/matrixmath.c:506`; unpack divides `src/game/matrixmath.c:721-722`) — but
it does **not** drop the visibility scale. The scale is *relocated* into the PC
float domain and re-applied at draw time: `gfx_apply_level_visibility_scale_to_matrix`
multiplies the spatial columns of each registered modelview matrix
(`src/platform/fast3d/gfx_pc.c:427`, applied at `:15971-15976`, excluding
projection, room, and field_10e0 matrices), world-space `render_pos` regions are
registered for that pass by `bondviewRegisterLevelVisibilityScaledRenderPos`
(`src/game/bondview.c:27301`), dynamic-model matrices are scaled directly by
`matrix_scalar_multiply_3` (`src/game/bondview.c:27294`), and the room/character
paths read `bgGetLevelVisibilityScale` in float (`src/game/unk_0BC530.c:416`,
`src/game/unk_0A1DA0.c:1227`, `src/game/chrprop.c:2690`). The reciprocal z-range
compensation is preserved (`src/game/bg.c:7409`, `:7506`, `:7927`). Retail also
force-resets `D_80032310[0]` to plain `65536` around HUD/weapon conversions via
the `matrix_4x4_7F058C64` / `_7F058C88` save-restore wrap, so those conversions
match the port's constant exactly on both sides. **Bound:** net rendering
(z-buffer ordering / visibility compression on levels such as Dam and Surface)
is preserved; the divergence is the pipeline stage and numeric domain at which
the scale is applied (a float multiply of columns 0..2, versus retail's
per-element fixed-point pre-scale with 16.16 truncation). The FID-0110 filer
observed only the `matrixmath.c` converters and did not see the compensating
gfx-layer application.
