#ifndef _MATH_H
#define _MATH_H 1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define FP_INFINITE  1
#define FP_NAN       2
#define FP_NORMAL    3
#define FP_SUBNORMAL 4
#define FP_ZERO      5

#define isfinite(x) __builtin_isfinite(x)

double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double ceil(double x);
double cos(double x);
double cosh(double x);
double exp(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double log(double x);
double log10(double x);
double modf(double x, double *iptr);
double pow(double x, double y);
double sin(double x);
double sinh(double x);
double sqrt(double x);
double tan(double x);
double tanh(double x);

float roundf(float x);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _MATH_H */
