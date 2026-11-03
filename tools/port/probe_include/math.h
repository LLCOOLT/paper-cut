#pragma once
double sqrt(double); float sqrtf(float); double fabs(double); float fabsf(float);
double sin(double); double cos(double); double tan(double); double atan2(double, double);
float sinf(float); float cosf(float); float tanf(float); float atan2f(float, float);
double floor(double); double ceil(double); float floorf(float); float ceilf(float);
double pow(double, double); float powf(float, float); double fmod(double, double);
double acos(double); double asin(double); double atan(double); double exp(double); double log(double);
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
