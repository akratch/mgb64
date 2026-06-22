/*
 * ADS-2.2 — Port-side per-weapon ADS profile table (NATIVE_PORT only).
 *
 * Stores a *factor* (multiple of the runtime base FOV) for iron-sight weapons
 * and resolves the absolute ADS FOV at query time against bondviewGetBaseFovY()
 * (ADS-0.2). WeaponStats is read-only here and never mutated. True scopes
 * (sniper / spy camera) yield to the existing analog zoom path.
 *
 * Hand-selection policy lives at the call sites (dominant hand for FOV / sens /
 * movement; firing hand for spread) — this module is hand-agnostic.
 */

#ifdef NATIVE_PORT

#include <stdlib.h>
#include <ultra64.h>
#include <bondconstants.h>
#include <bondtypes.h>
#include "ads_profiles.h"
#include "gun.h"
#include "bondview.h"

/* Computed-default constants (doc ADS-2.2). */
#define ADS_DEFAULT_IN_TIME     0.12f
#define ADS_DEFAULT_OUT_TIME    0.10f
#define ADS_DEFAULT_MOVE_MULT   0.70f
#define ADS_DEFAULT_STRAFE_MULT 0.60f
#define ADS_ZERO_ZOOM_FACTOR    0.85f  /* Zoom==0 iron weapons get a mild computed zoom */

extern s32 g_pcAdsFaithfulZoom;

/* GE007_ADS_* env knobs, read once at first use (mirrors the GE007_FP_* pattern
 * in gun.c). When set they override the in-hand weapon's resolved values. */
static int   s_ads_env_init      = 0;
static int   s_ads_env_fov_set   = 0;
static f32   s_ads_env_fov       = 0.0f;   /* GE007_ADS_FOV: absolute ADS FOV-Y override (deg) */
static int   s_ads_env_move_set  = 0;
static f32   s_ads_env_move      = 0.0f;   /* GE007_ADS_MOVE   */
static int   s_ads_env_strafe_set= 0;
static f32   s_ads_env_strafe    = 0.0f;   /* GE007_ADS_STRAFE */
static int   s_ads_env_posex_set = 0;
static f32   s_ads_env_posex     = 0.0f;   /* GE007_ADS_POSE_X */
static int   s_ads_env_posey_set = 0;
static f32   s_ads_env_posey     = 0.0f;   /* GE007_ADS_POSE_Y */
static int   s_ads_env_posez_set = 0;
static f32   s_ads_env_posez     = 0.0f;   /* GE007_ADS_POSE_Z */

static void adsReadEnvFloat(const char *name, int *set_flag, f32 *out)
{
    const char *v = getenv(name);
    if (v != NULL && v[0] != '\0') {
        *out = (f32)strtod(v, NULL);
        *set_flag = 1;
    }
}

static void adsInitEnv(void)
{
    if (s_ads_env_init) {
        return;
    }
    s_ads_env_init = 1;
    adsReadEnvFloat("GE007_ADS_FOV",    &s_ads_env_fov_set,    &s_ads_env_fov);
    adsReadEnvFloat("GE007_ADS_MOVE",   &s_ads_env_move_set,   &s_ads_env_move);
    adsReadEnvFloat("GE007_ADS_STRAFE", &s_ads_env_strafe_set, &s_ads_env_strafe);
    adsReadEnvFloat("GE007_ADS_POSE_X", &s_ads_env_posex_set,  &s_ads_env_posex);
    adsReadEnvFloat("GE007_ADS_POSE_Y", &s_ads_env_posey_set,  &s_ads_env_posey);
    adsReadEnvFloat("GE007_ADS_POSE_Z", &s_ads_env_posez_set,  &s_ads_env_posez);
}

/* True for the two real optics that own the analog scope path. */
static u8 adsItemIsScope(ITEM_IDS item)
{
    return (item == ITEM_SNIPERRIFLE || item == ITEM_CAMERA) ? 1 : 0;
}

/* Authored rows (doc ADS-2.2). Factors resolved at base 60. */
typedef struct {
    ITEM_IDS          item;
    struct AdsProfile prof;
} AdsAuthoredRow;

static const AdsAuthoredRow s_ads_authored[] = {
    /* PP7 (WPPK):       factor in_t  out_t sens spread move  strafe  pose...           scope */
    { ITEM_WPPK,        { 0.85f, ADS_DEFAULT_IN_TIME, ADS_DEFAULT_OUT_TIME, 1.0f, 0.6f, 0.90f, 0.80f, 0,0,0, 0,0,0, 0 } },
    /* RC-P90 (FNP90) */
    { ITEM_FNP90,       { 0.85f, ADS_DEFAULT_IN_TIME, ADS_DEFAULT_OUT_TIME, 1.0f, 0.7f, 0.88f, 0.78f, 0,0,0, 0,0,0, 0 } },
    /* KF7 (AK47) — 0.67 (=>40, mild-clamped from 30) */
    { ITEM_AK47,        { 0.67f, ADS_DEFAULT_IN_TIME, ADS_DEFAULT_OUT_TIME, 1.0f, 0.5f, 0.85f, 0.75f, 0,0,0, 0,0,0, 0 } },
    /* AR33 (M16) — 0.67 (=>40, mild-clamped from 20=3x) */
    { ITEM_M16,         { 0.67f, ADS_DEFAULT_IN_TIME, ADS_DEFAULT_OUT_TIME, 1.0f, 0.5f, 0.55f, 0.45f, 0,0,0, 0,0,0, 0 } },
    /* Sniper — true scope, yields to analog zoom */
    { ITEM_SNIPERRIFLE, { -1.0f, 0.30f, 0.24f, 1.0f, 1.0f, 0.40f, 0.30f, 0,0,0, 0,0,0, 1 } },
    /* Spy camera — true scope, yields */
    { ITEM_CAMERA,      { -1.0f, 0.30f, 0.24f, 1.0f, 1.0f, 0.70f, 0.60f, 0,0,0, 0,0,0, 1 } },
};

#define ADS_AUTHORED_COUNT ((int)(sizeof(s_ads_authored) / sizeof(s_ads_authored[0])))

/* Build the computed default profile for `item` into `out`. */
static void adsComputeDefault(ITEM_IDS item, struct AdsProfile *out)
{
    f32 base = bondviewGetBaseFovY();

    out->ads_in_time     = ADS_DEFAULT_IN_TIME;
    out->ads_out_time    = ADS_DEFAULT_OUT_TIME;
    out->sens_mult       = 1.0f;
    out->spread_mult     = 1.0f;
    out->ads_move_mult   = ADS_DEFAULT_MOVE_MULT;
    out->ads_strafe_mult = ADS_DEFAULT_STRAFE_MULT;
    out->pose_yaw_rad    = 0.0f;
    out->pose_pitch_rad  = 0.0f;
    out->pose_roll_rad   = 0.0f;
    out->pose_off_x      = 0.0f;
    out->pose_off_y      = 0.0f;
    out->pose_off_z      = 0.0f;

    if (adsItemIsScope(item)) {
        out->is_scope       = 1;
        out->ads_fov_factor = -1.0f; /* yield to analog scope */
        return;
    }

    out->is_scope = 0;

    {
        WeaponStats *ws = get_ptr_item_statistics(item);
        f32 zoom = (ws != NULL) ? ws->Zoom : 0.0f;

        if (zoom > 0.0f && base > 0.0f) {
            /* factor = Zoom/base; the mild-iron clamp is applied in
             * adsResolveFovY at query time (so AdsFaithfulZoom can disable it). */
            out->ads_fov_factor = zoom / base;
        } else {
            out->ads_fov_factor = ADS_ZERO_ZOOM_FACTOR;
        }
    }
}

const struct AdsProfile *adsGetProfile(ITEM_IDS item)
{
    static struct AdsProfile resolved;
    int i;

    adsInitEnv();

    /* Authored row? Copy into the function-static so env knobs can override. */
    for (i = 0; i < ADS_AUTHORED_COUNT; i++) {
        if (s_ads_authored[i].item == item) {
            resolved = s_ads_authored[i].prof;
            goto apply_env;
        }
    }

    /* No authored row: compute the default. */
    adsComputeDefault(item, &resolved);

apply_env:
    /* GE007_ADS_* live overrides for the in-hand weapon (unset => table value).
     * GE007_ADS_FOV is an absolute FOV-Y; convert to a factor against the base.
     * Scope profiles are left alone (they yield to the analog path). */
    if (!resolved.is_scope) {
        if (s_ads_env_fov_set) {
            f32 base = bondviewGetBaseFovY();
            if (base > 0.0f && s_ads_env_fov > 0.0f) {
                resolved.ads_fov_factor = s_ads_env_fov / base;
            }
        }
        if (s_ads_env_posex_set) { resolved.pose_off_x = s_ads_env_posex; }
        if (s_ads_env_posey_set) { resolved.pose_off_y = s_ads_env_posey; }
        if (s_ads_env_posez_set) { resolved.pose_off_z = s_ads_env_posez; }
    }
    if (s_ads_env_move_set)   { resolved.ads_move_mult   = s_ads_env_move; }
    if (s_ads_env_strafe_set) { resolved.ads_strafe_mult = s_ads_env_strafe; }

    return &resolved;
}

f32 adsResolveFovY(const struct AdsProfile *p, f32 baseFov)
{
    f32 fov;

    if (p == NULL || p->is_scope || p->ads_fov_factor < 0.0f) {
        return -1.0f; /* caller yields to get_item_in_hand_zoom() */
    }

    fov = p->ads_fov_factor * baseFov;

    /* Mild-iron clamp: keep iron-sight ADS FOV >= baseFov/1.5 (~40 at base 60)
     * so scope-grade WeaponStats.Zoom doesn't produce sniper-grade iron zoom.
     * Input.AdsFaithfulZoom disables the clamp for GE-authentic aim. */
    if (!g_pcAdsFaithfulZoom) {
        f32 floor = baseFov / 1.5f;
        if (fov < floor) {
            fov = floor;
        }
    }

    return fov;
}

#endif /* NATIVE_PORT */
