// Oracle test of lang/scalar.h: each function is compared with its <cmath> original over sampled inputs. Includes
// <cmath> on purpose.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "lang/limits.h"
#include "lang/scalar.h"

using namespace engine_placeholder;

namespace
{

template <class T> bool Same(T a, T b)
{
    if (std::isnan(a))
    {
        return std::isnan(b);
    }
    return a == b && std::signbit(a) == std::signbit(b);
}

template <class T> void Check()
{
    const T values[] = {T(0),
                        T(-0.0),
                        T(0.5),
                        T(-0.5),
                        T(1),
                        T(-1),
                        T(1.5),
                        T(2.5),
                        T(-2.5),
                        T(3.14159),
                        T(100.75),
                        T(-100.75),
                        T(1e-30),
                        Limits<T>::Max(),
                        Limits<T>::Infinity(),
                        -Limits<T>::Infinity(),
                        Limits<T>::QuietNaN(),
                        Limits<T>::DenormMin()};
    for (T v : values)
    {
        CHECK(Same(Sqrt(v), std::sqrt(v)));
        CHECK(Same(Sin(v), std::sin(v)));
        CHECK(Same(Cos(v), std::cos(v)));
        CHECK(Same(Tan(v), std::tan(v)));
        CHECK(Same(Acos(v), std::acos(v)));
        CHECK(Same(Asin(v), std::asin(v)));
        CHECK(Same(Exp(v), std::exp(v)));
        CHECK(Same(Ln(v), std::log(v)));
        CHECK(Same(Floor(v), std::floor(v)));
        CHECK(Same(Ceil(v), std::ceil(v)));
        CHECK(Same(Round(v), std::round(v)));
        CHECK(Same(Abs(v), std::fabs(v)));
        CHECK(IsFinite(v) == std::isfinite(v));
        CHECK(IsNaN(v) == std::isnan(v));
        CHECK(IsInf(v) == std::isinf(v));
        CHECK(SignBit(v) == std::signbit(v));
        CHECK(Same(LdExp(v, 3), std::ldexp(v, 3)));
        if (std::isfinite(v) && std::fabs(v) < T(1e9))
        {
            CHECK(LRound(v) == std::lround(v));
        }
        for (T w : values)
        {
            CHECK(Same(Pow(v, w), std::pow(v, w)));
            CHECK(Same(Atan2(v, w), std::atan2(v, w)));
            CHECK(Same(Fmod(v, w), std::fmod(v, w)));
            CHECK(Same(FMin(v, w), std::fmin(v, w)));
            CHECK(Same(FMax(v, w), std::fmax(v, w)));
        }
    }
}

}  // namespace

TEST_CASE("Scalar math matches <cmath> for f32", "[cstr][math]")
{
    Check<f32>();
}
TEST_CASE("Scalar math matches <cmath> for f64", "[cstr][math]")
{
    Check<f64>();
}

TEST_CASE("Integer overloads", "[cstr][math]")
{
    CHECK(Abs(-5) == 5);
    CHECK(Abs(7) == 7);
    CHECK(Abs(static_cast<i64>(-9000000000ll)) == 9000000000ll);
    CHECK(Abs(static_cast<i8>(-3)) == 3);
    CHECK(Sqrt(16) == 4.0);
    CHECK(Sqrt(static_cast<u32>(81)) == 9.0);
}
