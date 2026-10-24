#ifndef _DOLPHIN_CMATH_H_
#define _DOLPHIN_CMATH_H_

#ifdef TARGET_PC
#include <math.h>
#else

float powf(float x, float y);
float tanf(float);

extern float sinf(float);
extern float cosf(float);
extern float acosf(float);
extern float atan2f(float, float);

#endif

#endif // _DOLPHIN_CMATH_H_
