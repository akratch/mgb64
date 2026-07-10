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
