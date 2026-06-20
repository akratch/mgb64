#ifndef _MATH_EXT_H_
#define _MATH_EXT_H_

#ifdef NATIVE_PORT
float sqrtf(float);
double sqrt(double);
float sinf(float);
double sin(double);
float cosf(float);
double cos(double);
float fabsf(float);
double fabs(double);
float floorf(float);
double floor(double);
float ceilf(float);
double ceil(double);
float atan2f(float, float);
float acosf(float);
float asinf(float);
float tanf(float);
float powf(float, float);
double pow(double, double);
float fmodf(float, float);
float logf(float);
double log(double);
#else
#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
#define FLT_EPSILON 1.19209290E-07F

float sqrtf(float);
double sqrt(double);
float sinf(float);
double sin(double);
float cosf(float);
double cos(double);
#endif

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif
#define M_MINUS_PI_F -3.1415927f

#ifndef M_TAU
#define M_TAU 6.28318530717958647692
#endif
#ifndef M_TAU_F
#define M_TAU_F 6.2831855f
#endif

#define M_HALF_PI (M_PI_F / 2)
#define M_THREE_HALF_PI (3 * M_HALF_PI)

#define M_U16_MAX_VALUE_F 65536.0f
#define M_U32_MAX_VALUE_F 4294967296.0f

#define SECS_TO_TIMER60(SECS) ((SECS) * GAME_TICKRATE)
#define MINS_TO_TIMER60(MINS) (SECS_TO_TIMER60((MINS) * GAME_TICKRATE))
#define DEG2BYTE(DEG) (char)(256.0f / 360.0f * (DEG))
#define RAD2BYTE(RAD) (char)(256.0f / M_TAU_F * (RAD))
#define DegToRad(DEG) (float)((DEG) * M_TAU_F / 360.0f)
#define DegToRad1Fact(DEG) (float)((DEG) * (float)(M_TAU / 360.0))
#define mDegToHalfRad(x) (((x) * M_PI_F) / 360.0f)
#define RadToDeg(RAD) (float)((RAD) * (360.0f / M_TAU_F))
#define ByteToRadian(Byte) (((Byte) * M_TAU_F) * (1.0f / 256.0f))

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif
#ifndef SGN
#define SGN(x) ((x) < 0 ? -1 : (x) > 0 ? 1 : 0)
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef SQR
#define SQR(x) ((x) * (x))
#endif

#define IDO_POINT_ONE 0.10000001f

#endif
