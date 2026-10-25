#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
typedef uint8_t byte;
uint32_t millis();
inline double radians(double degrees) { return degrees * 3.14159265358979323846 / 180; }
inline double degrees(double radians) { return radians * 180 / 3.14159265358979323846; }
#define sq(x) ((x) * (x))
#define TWO_PI (2 * 3.14159265358979323846)
