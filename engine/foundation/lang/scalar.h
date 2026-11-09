#pragma once

#include <cmath>

#include "lang/traits.h"
#include "types.h"

//! Scalar math (Phase 16): the engine's spelling of `<cmath>`. Every function forwards inline to the standard one of the same meaning
//! and has `f32` and `f64` forms (an integer argument converts to `f64`, as for the standard functions). Vector, matrix and quaternion
//! types are in `math/`; these are the functions on one number. The `f64` overloads are new: `Sqrt`, `Sin`, `Cos` and `Acos` used to take
//! only `f32`, so a `double` argument now keeps its precision instead of narrowing.
namespace engine_placeholder {

/// The square root of `value`; NaN for a negative input (`sqrt`).
inline f32 Sqrt(f32 value) noexcept { return std::sqrt(value); }
/// The square root of `value` (`sqrt`).
inline f64 Sqrt(f64 value) noexcept { return std::sqrt(value); }
/// The square root of an integer, as `f64`.
template <class T, EnableIf<IsIntegral<T>, int> = 0>
inline f64 Sqrt(T value) noexcept { return std::sqrt(static_cast<f64>(value)); }

/// The sine of an angle in radians (`sin`).
inline f32 Sin(f32 radians) noexcept { return std::sin(radians); }
/// The sine of an angle in radians (`sin`).
inline f64 Sin(f64 radians) noexcept { return std::sin(radians); }
/// The cosine of an angle in radians (`cos`).
inline f32 Cos(f32 radians) noexcept { return std::cos(radians); }
/// The cosine of an angle in radians (`cos`).
inline f64 Cos(f64 radians) noexcept { return std::cos(radians); }
/// The tangent of an angle in radians (`tan`).
inline f32 Tan(f32 radians) noexcept { return std::tan(radians); }
/// The tangent of an angle in radians (`tan`).
inline f64 Tan(f64 radians) noexcept { return std::tan(radians); }
/// The arc cosine in radians, in [0, pi]; NaN outside [-1, 1] (`acos`).
inline f32 Acos(f32 value) noexcept { return std::acos(value); }
/// The arc cosine in radians (`acos`).
inline f64 Acos(f64 value) noexcept { return std::acos(value); }
/// The arc sine in radians, in [-pi/2, pi/2] (`asin`).
inline f32 Asin(f32 value) noexcept { return std::asin(value); }
/// The arc sine in radians (`asin`).
inline f64 Asin(f64 value) noexcept { return std::asin(value); }
/// The angle in radians of the point (`x`, `y`), in [-pi, pi] (`atan2`).
inline f32 Atan2(f32 y, f32 x) noexcept { return std::atan2(y, x); }
/// The angle in radians of the point (`x`, `y`) (`atan2`).
inline f64 Atan2(f64 y, f64 x) noexcept { return std::atan2(y, x); }

/// `base` raised to `exponent` (`pow`).
inline f32 Pow(f32 base, f32 exponent) noexcept { return std::pow(base, exponent); }
/// `base` raised to `exponent` (`pow`).
inline f64 Pow(f64 base, f64 exponent) noexcept { return std::pow(base, exponent); }
/// The exponential function e^`value` (`exp`).
inline f32 Exp(f32 value) noexcept { return std::exp(value); }
/// The exponential function e^`value` (`exp`).
inline f64 Exp(f64 value) noexcept { return std::exp(value); }
/// The natural logarithm (`log`); named `Ln` so it never meets the logging macro family's `Log`.
inline f32 Ln(f32 value) noexcept { return std::log(value); }
/// The natural logarithm (`log`).
inline f64 Ln(f64 value) noexcept { return std::log(value); }

/// The largest integral value not greater than `value` (`floor`).
inline f32 Floor(f32 value) noexcept { return std::floor(value); }
/// The largest integral value not greater than `value` (`floor`).
inline f64 Floor(f64 value) noexcept { return std::floor(value); }
/// The smallest integral value not less than `value` (`ceil`).
inline f32 Ceil(f32 value) noexcept { return std::ceil(value); }
/// The smallest integral value not less than `value` (`ceil`).
inline f64 Ceil(f64 value) noexcept { return std::ceil(value); }
/// The nearest integral value, halves away from zero (`round`).
inline f32 Round(f32 value) noexcept { return std::round(value); }
/// The nearest integral value, halves away from zero (`round`).
inline f64 Round(f64 value) noexcept { return std::round(value); }
/// The nearest integer as `long`, halves away from zero (`lround`).
inline long LRound(f32 value) noexcept { return std::lround(value); }
/// The nearest integer as `long`, halves away from zero (`lround`).
inline long LRound(f64 value) noexcept { return std::lround(value); }
/// The remainder of `x / y` with the sign of `x` (`fmod`).
inline f32 Fmod(f32 x, f32 y) noexcept { return std::fmod(x, y); }
/// The remainder of `x / y` with the sign of `x` (`fmod`).
inline f64 Fmod(f64 x, f64 y) noexcept { return std::fmod(x, y); }
/// `value` times 2 to the power `exponent` (`ldexp`).
inline f32 LdExp(f32 value, int exponent) noexcept { return std::ldexp(value, exponent); }
/// `value` times 2 to the power `exponent` (`ldexp`).
inline f64 LdExp(f64 value, int exponent) noexcept { return std::ldexp(value, exponent); }

/// The absolute value (`fabs`).
inline f32 Abs(f32 value) noexcept { return std::fabs(value); }
/// The absolute value (`fabs`).
inline f64 Abs(f64 value) noexcept { return std::fabs(value); }
/// The absolute value of a signed integer (`abs`); the most negative value has no result (undefined, as for `abs`).
template <class T, EnableIf<IsIntegral<T> && IsSigned<T>, int> = 0>
inline T Abs(T value) noexcept { return value < 0 ? static_cast<T>(-value) : value; }

/// The smaller of two floats; if one is NaN the other is returned (`fmin`).
inline f32 FMin(f32 a, f32 b) noexcept { return std::fmin(a, b); }
/// The smaller of two doubles; if one is NaN the other is returned (`fmin`).
inline f64 FMin(f64 a, f64 b) noexcept { return std::fmin(a, b); }
/// The larger of two floats; if one is NaN the other is returned (`fmax`).
inline f32 FMax(f32 a, f32 b) noexcept { return std::fmax(a, b); }
/// The larger of two doubles; if one is NaN the other is returned (`fmax`).
inline f64 FMax(f64 a, f64 b) noexcept { return std::fmax(a, b); }

/// True when `value` is neither infinite nor NaN (`isfinite`).
inline bool IsFinite(f32 value) noexcept { return std::isfinite(value); }
/// True when `value` is neither infinite nor NaN (`isfinite`).
inline bool IsFinite(f64 value) noexcept { return std::isfinite(value); }
/// True when `value` is NaN (`isnan`).
inline bool IsNaN(f32 value) noexcept { return std::isnan(value); }
/// True when `value` is NaN (`isnan`).
inline bool IsNaN(f64 value) noexcept { return std::isnan(value); }
/// True when `value` is negative infinity or positive infinity (`isinf`).
inline bool IsInf(f32 value) noexcept { return std::isinf(value); }
/// True when `value` is negative infinity or positive infinity (`isinf`).
inline bool IsInf(f64 value) noexcept { return std::isinf(value); }
/// True when the sign bit of `value` is set, including -0 and negative NaN (`signbit`).
inline bool SignBit(f32 value) noexcept { return std::signbit(value); }
/// True when the sign bit of `value` is set (`signbit`).
inline bool SignBit(f64 value) noexcept { return std::signbit(value); }

} // namespace engine_placeholder
