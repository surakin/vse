#pragma once

#include <limits>

#include "types.h"

//! Numeric limits (Phase 16): the engine's spelling of `<limits>`.
namespace engine_placeholder
{

/// The limits of the arithmetic type `T` (`std::numeric_limits<T>`).
///
/// Every function is `constexpr` and returns the standard's value for `T`.
template <class T> struct Limits
{
    /// True when the limits of `T` are specialized (it is an arithmetic type).
    static constexpr bool kSpecialized = std::numeric_limits<T>::is_specialized;
    /// True when `T` is a signed type.
    static constexpr bool kIsSigned = std::numeric_limits<T>::is_signed;
    /// True when `T` is an integer type.
    static constexpr bool kIsInteger = std::numeric_limits<T>::is_integer;
    /// True when `T` is an IEC 559 (IEEE-754) type.
    static constexpr bool kIsIec559 = std::numeric_limits<T>::is_iec559;
    /// The number of radix digits that can be represented without change.
    static constexpr int kDigits = std::numeric_limits<T>::digits;
    /// The number of decimal digits that can be represented without change.
    static constexpr int kDigits10 = std::numeric_limits<T>::digits10;

    /// The smallest finite value (for floating point: the smallest positive normal value).
    static constexpr T Min() noexcept { return std::numeric_limits<T>::min(); }
    /// The largest finite value.
    static constexpr T Max() noexcept { return std::numeric_limits<T>::max(); }
    /// The most negative finite value.
    static constexpr T Lowest() noexcept { return std::numeric_limits<T>::lowest(); }
    /// The difference between 1 and the next representable value (floating point).
    static constexpr T Epsilon() noexcept { return std::numeric_limits<T>::epsilon(); }
    /// Positive infinity (floating point).
    static constexpr T Infinity() noexcept { return std::numeric_limits<T>::infinity(); }
    /// A quiet not-a-number (floating point).
    static constexpr T QuietNaN() noexcept { return std::numeric_limits<T>::quiet_NaN(); }
    /// The smallest positive subnormal value (floating point).
    static constexpr T DenormMin() noexcept { return std::numeric_limits<T>::denorm_min(); }
};

}  // namespace engine_placeholder
