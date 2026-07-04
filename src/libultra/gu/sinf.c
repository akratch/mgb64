/*
 * Clean-room single-precision sine approximation for the matching target.
 */

#include "guint.h"

#define GU_PI        3.14159265358979323846
#define GU_HALF_PI   (GU_PI * 0.5)
#define GU_TWO_PI    (GU_PI * 2.0)

static double guReduceRadians(double angle)
{
    int turns;

    if (angle != angle) {
        return angle;
    }

    if (angle > 268435456.0 || angle < -268435456.0) {
        return 0.0;
    }

    turns = (int)(angle / GU_TWO_PI);
    angle -= (double)turns * GU_TWO_PI;

    while (angle > GU_PI) {
        angle -= GU_TWO_PI;
    }
    while (angle < -GU_PI) {
        angle += GU_TWO_PI;
    }

    return angle;
}

float sinf(float angle)
{
    double x;
    double x2;
    double polynomial;

    x = guReduceRadians((double)angle);
    if (x != x) {
        return angle;
    }

    if (x > GU_HALF_PI) {
        x = GU_PI - x;
    } else if (x < -GU_HALF_PI) {
        x = -GU_PI - x;
    }

    x2 = x * x;
    polynomial = 1.0
        + x2 * (-1.0 / 6.0
        + x2 * (1.0 / 120.0
        + x2 * (-1.0 / 5040.0
        + x2 * (1.0 / 362880.0
        + x2 * (-1.0 / 39916800.0
        + x2 * (1.0 / 6227020800.0))))));

    return (float)(x * polynomial);
}
