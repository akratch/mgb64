/*
 * Clean-room single-precision cosine approximation for the matching target.
 */

#include "guint.h"

#define GU_HALF_PI 1.57079632679489661923f

float cosf(float angle)
{
    return sinf(angle + GU_HALF_PI);
}
