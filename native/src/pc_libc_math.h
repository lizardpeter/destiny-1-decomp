#pragma once
#include <cmath>
#include <cstdint>
#include <ctime>

namespace d1::pc_libc {
// Explicit retail scalar widths: Windows long is 32-bit, retail long is 64-bit.
// Native libm is not promised bit-identical to the original console libm.
inline float exp_f(float x) { return std::exp(x); }
inline float pow_f(float x, float y) { return std::pow(x, y); }
inline float asin_f(float x) { return std::asin(x); }
inline float acos_f(float x) { return std::acos(x); }
inline float atan2_f(float y, float x) { return std::atan2(y, x); }
inline int64_t lrint_f(float x) { return std::llrint(x); }
inline float fmod_f(float x, float y) { return std::fmod(x, y); }
inline float tan_f(float x) { return std::tan(x); }
inline float exp2_f(float x) { return std::exp2(x); }
inline float modf_f(float x, float* whole) { return std::modf(x, whole); }
inline float frexp_f(float x, int* exponent) { return std::frexp(x, exponent); }
inline float ldexp_f(float x, int exponent) { return std::ldexp(x, exponent); }
inline float atan_f(float x) { return std::atan(x); }
inline double fmod_d(double x, double y) { return std::fmod(x, y); }
inline double pow_d(double x, double y) { return std::pow(x, y); }
inline double exp2_d(double x) { return std::exp2(x); }
inline double tan_d(double x) { return std::tan(x); }
inline double diff_time(int64_t later, int64_t earlier) {
    static_assert(sizeof(std::time_t) == 8);
    return std::difftime(static_cast<std::time_t>(later), static_cast<std::time_t>(earlier));
}
} // namespace d1::pc_libc
