#ifndef NG_SHIM_MATH_H
#define NG_SHIM_MATH_H

double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double sqrt(double);
double log(double);
double log10(double);
double exp(double);
double pow(double, double);
double floor(double);
double ceil(double);
double fmod(double, double);
double fabs(double);
double ldexp(double, int);
double modf(double, double*);
double frexp(double, int*);
double fmin(double, double);
double fmax(double, double);
float sqrtf(float);
float sinf(float);
float cosf(float);

#endif