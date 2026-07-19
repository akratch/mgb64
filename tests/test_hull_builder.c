/*
 * test_hull_builder.c — ROM-free corpus regression for the collision-hull
 * builder at 0x7F03ECC0 (FID-0099).
 *
 * Drives the EXACT production geometry (src/game/chrprop_hull_impl.inc, the same
 * single source compiled into ge007) over a corpus of box + transform inputs and
 * asserts the retail-faithful properties:
 *   (a) a projected box with real 2D area yields >= 4 distinct hull corners;
 *   (b) all 8 projected box corners lie inside-or-on the emitted hull;
 *   (c) exact expected hulls for hand-derived axis-aligned / rotated cases.
 *
 * The corpus includes the exact degenerate desk boxes captured from Dam by the
 * GE007_TRACE_HULL_DEGEN census, axis-aligned ties, rotated boxes, and a fixed-
 * seed randomized batch.
 *
 * RED / GREEN: the fix routes the object/door collision path through the builder
 * UNCLAMPED (retail count). The old code clamped to 4, dropping real corners.
 *   - default run  -> unclamped (the fix)      -> all properties hold  -> PASS
 *   - HULL_TEST_CLAMP=1 -> clamped (pre-fix)    -> desk/thin boxes fail -> FAIL
 * The registered ctest runs the default (GREEN); the clamped run reproduces the
 * pre-fix RED.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Minimal decomp scalar typedefs used by the shared .inc (normally from
 * ultra64.h in the game build). */
typedef float f32;
typedef double f64;
typedef int s32;
typedef unsigned char u8;

#include "hull_vertex_clamp.h"       /* hullVertexCount() */
#include "chrprop_hull_impl.inc"     /* chrpropHullBuildFootprint() — production code */

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Projected + translated position of bbox corner (x,y,z), matching the .inc. */
static void project_corner(const f32 m[4][4], double x, double y, double z,
                           double *ox, double *oz) {
    *ox = (double)m[0][0] * x + (double)m[1][0] * y + (double)m[2][0] * z + (double)m[3][0];
    *oz = (double)m[0][2] * x + (double)m[1][2] * y + (double)m[2][2] * z + (double)m[3][2];
}

/* Are the 8 projected corners collinear (no 2D area)? */
static int footprint_has_area(const f32 m[4][4], double xmin, double xmax,
                              double ymin, double ymax, double zmin, double zmax) {
    double xs[2] = {xmin, xmax}, ys[2] = {ymin, ymax}, zs[2] = {zmin, zmax};
    double c[8][2];
    int n = 0, i, ix, iy, iz;
    double dirx = 0, dirz = 0;
    int have_dir = 0;
    for (ix = 0; ix < 2; ix++)
        for (iy = 0; iy < 2; iy++)
            for (iz = 0; iz < 2; iz++) {
                project_corner(m, xs[ix], ys[iy], zs[iz], &c[n][0], &c[n][1]);
                n++;
            }
    for (i = 1; i < 8; i++) {
        double dx = c[i][0] - c[0][0];
        double dz = c[i][1] - c[0][1];
        if (fabs(dx) < 1e-6 && fabs(dz) < 1e-6) continue;
        if (!have_dir) { dirx = dx; dirz = dz; have_dir = 1; continue; }
        if (fabs(dx * dirz - dz * dirx) > 1e-3) return 1; /* not collinear */
    }
    return 0;
}

/* Inside-or-on test for a convex polygon of n (x,z) pairs, tolerant of
 * zero-length edges (duplicated corners) and on-edge points. */
static int point_in_hull(double px, double pz, const f32 *hull, int n) {
    int i, sign = 0;
    for (i = 0; i < n; i++) {
        double ax = hull[i * 2], az = hull[i * 2 + 1];
        double bx = hull[((i + 1) % n) * 2], bz = hull[((i + 1) % n) * 2 + 1];
        double ex = bx - ax, ez = bz - az;
        double elen = sqrt(ex * ex + ez * ez);
        double perp; /* signed perpendicular distance of P from the edge line */
        if (elen < 1e-6) continue;                        /* zero-length edge */
        perp = (ex * (pz - az) - ez * (px - ax)) / elen;
        if (fabs(perp) < 1.0) continue;                   /* on the edge (>= f32 noise) */
        if (sign == 0) sign = (perp > 0) ? 1 : -1;
        else if (((perp > 0) ? 1 : -1) != sign) return 0;
    }
    return 1;
}

static int distinct_corners(const f32 *hull, int n) {
    int i, j, d = 0;
    for (i = 0; i < n; i++) {
        int dup = 0;
        for (j = 0; j < i; j++)
            if (fabs(hull[i * 2] - hull[j * 2]) < 1e-2 &&
                fabs(hull[i * 2 + 1] - hull[j * 2 + 1]) < 1e-2) { dup = 1; break; }
        if (!dup) d++;
    }
    return d;
}

/* Run properties (a) + (b) for one box under a given clamp mode. Returns #fails. */
static int check_box(const char *name, const f32 m[4][4],
                     f32 xmin, f32 xmax, f32 ymin, f32 ymax, f32 zmin, f32 zmax,
                     int retail_unclamped) {
    f32 hull[16];
    int before = g_failures;
    int count = chrpropHullBuildFootprint(xmin, xmax, ymin, ymax, zmin, zmax,
                                          m, hull, retail_unclamped);
    double xs[2] = {xmin, xmax}, ys[2] = {ymin, ymax}, zs[2] = {zmin, zmax};
    int ix, iy, iz;
    char msg[160];

    if (footprint_has_area(m, xmin, xmax, ymin, ymax, zmin, zmax)) {
        int d = distinct_corners(hull, count);
        snprintf(msg, sizeof msg, "%s: >=4 distinct corners (got %d, count=%d)", name, d, count);
        CHECK(d >= 4, msg);
    }
    /* (b) every projected box corner inside-or-on the hull */
    for (ix = 0; ix < 2; ix++)
        for (iy = 0; iy < 2; iy++)
            for (iz = 0; iz < 2; iz++) {
                double ox, oz;
                project_corner(m, xs[ix], ys[iy], zs[iz], &ox, &oz);
                snprintf(msg, sizeof msg, "%s: corner(%g,%g,%g) inside hull",
                         name, xs[ix], ys[iy], zs[iz]);
                CHECK(point_in_hull(ox, oz, hull, count), msg);
            }
    return g_failures - before;
}

/* Deterministic LCG for the randomized batch. */
static unsigned long s_rng = 0x1234567u;
static double frand(double lo, double hi) {
    s_rng = s_rng * 6364136223846793005ull + 1442695040888963407ull;
    double u = (double)((s_rng >> 11) & 0xfffffffful) / (double)0xfffffffful;
    return lo + u * (hi - lo);
}

int main(void) {
    /* Clamp mode is the pre-fix collision behavior; unclamped is the fix. */
    int clamp = (getenv("HULL_TEST_CLAMP") != NULL);
    int unclamped = clamp ? 0 : 1;
    if (clamp) fprintf(stderr, "[hull_builder] HULL_TEST_CLAMP set: exercising PRE-FIX clamped behavior (expect FAIL)\n");

    /* ---- (c) hand-derived exact case: axis-aligned box, identity rotation ---- */
    {
        f32 m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{100,0,200,1}};
        f32 hull[16];
        /* box x[-10,10] z[-5,5], y thin. Footprint corners (post-translate):
         * (90,195)(90,205)(110,195)(110,205). */
        int count = chrpropHullBuildFootprint(-10, 10, -1, 1, -5, 5, m, hull, unclamped);
        int d = distinct_corners(hull, count);
        double want[4][2] = {{90,195},{90,205},{110,195},{110,205}};
        int wi, hi, found;
        CHECK(d == 4, "axis-aligned: exactly 4 distinct corners");
        for (wi = 0; wi < 4; wi++) {
            found = 0;
            for (hi = 0; hi < count; hi++)
                if (fabs(hull[hi * 2] - want[wi][0]) < 1e-2 &&
                    fabs(hull[hi * 2 + 1] - want[wi][1]) < 1e-2) found = 1;
            CHECK(found, "axis-aligned: expected corner present");
        }
        check_box("axis-aligned", m, -10, 10, -1, 1, -5, 5, unclamped);
    }

    /* ---- (c) hand-derived: 45-degree yaw rotation of a rectangle ---- */
    {
        double a = 45.0 * M_PI / 180.0;
        float c = (float)cos(a), s = (float)sin(a);
        /* yaw about Y: X' = c*x + s*z ; Z' = -s*x + c*z */
        f32 m[4][4] = {{c,0,-s,0},{0,1,0,0},{s,0,c,0},{0,0,0,1}};
        /* box x[-100,100] z[-40,40]; corners rotate to a tilted rectangle. */
        check_box("yaw45", m, -100, 100, -2, 2, -40, 40, unclamped);
        f32 hull[16];
        int count = chrpropHullBuildFootprint(-100, 100, -2, 2, -40, 40, m, hull, unclamped);
        CHECK(distinct_corners(hull, count) >= 4, "yaw45: >=4 distinct corners");
    }

    /* ---- exact degenerate desk boxes captured from Dam (GE007_TRACE_HULL_DEGEN) ---- */
    /* #25/109: obj collision box, near-axis-aligned; pre-fix -> 3 distinct (bug). */
    {
        f32 m[4][4] = {{-0.341197f,0,-0.254780f,0},
                       {-0.062093f,1,-0.050196f,0},
                       {0.260294f,0,-0.347516f,0},
                       {14391.7383f,0,1861.9156f,1}};
        check_box("dam_desk_25", m, -315, 315, -284, 284, 0, 0, unclamped);
    }
    /* #30/122: real 3D box (z != 0), 4 distinct extremes but pre-fix duplicated. */
    {
        f32 m[4][4] = {{1.299397f,0,-1.754997f,0},
                       {0,1,0,0},
                       {-0.688195f,0,-0.509538f,0},
                       {14624.2139f,0,2345.9692f,1}};
        check_box("dam_desk_30", m, -98, 98, -140, 140, -9, 9, unclamped);
    }
    /* #31/123: larger box. */
    {
        f32 m[4][4] = {{-0.128077f,0,0.171451f,0},
                       {0,1,0,0},
                       {-0.571495f,0,-0.426916f,0},
                       {15632.2588f,0,2503.2532f,1}};
        check_box("dam_desk_31", m, -758, 758, -700, 700, -15, 15, unclamped);
    }

    /* ---- axis-aligned tie: projected rectangle exactly axis-aligned ---- */
    {
        f32 m[4][4] = {{0.5f,0,0,0},{0,1,0,0},{0,0,0.5f,0},{1000,0,-2000,1}};
        check_box("axis_tie", m, -200, 200, -3, 3, -120, 120, unclamped);
    }

    /* ---- fixed-seed randomized batch ----
     * Two families that both project to a QUAD footprint (the only shapes real
     * collision boxes produce, per the census): a general 3D box under a yaw-only
     * transform (Y does not project -> quad), and a z-flat box under an arbitrary
     * rotation (only X/Y contribute -> quad). A simultaneously pitched full-3D box
     * projects to a HEXAGON, which the retail 4-extreme+insert algorithm does not
     * fully hull and which does not occur in the game data, so it is out of scope. */
    {
        int t;
        for (t = 0; t < 250; t++) {
            /* family A: yaw-only, full 3D extents */
            double yaw = frand(-M_PI, M_PI);
            float cy = (float)cos(yaw), sy = (float)sin(yaw);
            f32 m[4][4];
            f32 ex = (f32)frand(20, 800), ey = (f32)frand(1, 300), ez = (f32)frand(20, 800);
            char nm[32];
            m[0][0] = cy;   m[0][1] = 0; m[0][2] = -sy;  m[0][3] = 0;
            m[1][0] = 0;    m[1][1] = 1; m[1][2] = 0;    m[1][3] = 0;
            m[2][0] = sy;   m[2][1] = 0; m[2][2] = cy;   m[2][3] = 0;
            m[3][0] = (f32)frand(-20000, 20000); m[3][1] = 0;
            m[3][2] = (f32)frand(-20000, 20000); m[3][3] = 1;
            snprintf(nm, sizeof nm, "randYaw%d", t);
            check_box(nm, m, -ex, ex, -ey, ey, -ez, ez, unclamped);
        }
        for (t = 0; t < 250; t++) {
            /* family B: z-flat box, arbitrary rotation of the X/Y plane (like the
             * Dam desk collision boxes: zmin==zmax==0, small pitch on rows 0/1). */
            double a = frand(-M_PI, M_PI), b = frand(-0.4, 0.4);
            f32 m[4][4];
            f32 ex = (f32)frand(20, 800), ey = (f32)frand(20, 800);
            char nm[32];
            m[0][0] = (f32)(cos(a));        m[0][1] = 0; m[0][2] = (f32)(-sin(a));       m[0][3] = 0;
            m[1][0] = (f32)(sin(a) * sin(b)); m[1][1] = 1; m[1][2] = (f32)(cos(a) * sin(b)); m[1][3] = 0;
            m[2][0] = (f32)(sin(a) * cos(b)); m[2][1] = 0; m[2][2] = (f32)(cos(a) * cos(b)); m[2][3] = 0;
            m[3][0] = (f32)frand(-20000, 20000); m[3][1] = 0;
            m[3][2] = (f32)frand(-20000, 20000); m[3][3] = 1;
            snprintf(nm, sizeof nm, "randFlat%d", t);
            check_box(nm, m, -ex, ex, -ey, ey, 0, 0, unclamped);
        }
    }

    if (g_failures == 0) {
        printf("PASS: hull_builder corpus (%s)\n", clamp ? "clamped/pre-fix" : "unclamped/fixed");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed (%s)\n", g_failures, clamp ? "clamped/pre-fix" : "unclamped/fixed");
    return 1;
}
