// Values shared by the physics tests.
#pragma once

#include "lang/scalar.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "types.h"

namespace engine_placeholder::physics::testing
{

/// A quiet NaN, for the tests that feed bad numbers.
inline const f32 kNaN = __builtin_nanf("");

/// A small deterministic generator (xorshift) for the randomized tests.
struct Rng
{
    u64 state;
    explicit Rng(u64 seed) : state(seed * 2862933555777941757ull + 3037000493ull) {}
    u32 Next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<u32>(state >> 16);
    }
    f32 Unit() { return static_cast<f32>(Next() % 100000u) / 100000.0f; }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

// The inputs of a hash that is compared across machines are built with + - * / Sqrt and Floor only, never with libm's
// Sin or Cos: those differ by an ulp between glibc, the UCRT and Apple's libm, and the hash then measures the libm, not
// the physics (Phase 25 Day 399, the Mac).

/// A rotation about `unit_axis` whose half-angle has cosine `half_cos` (in [-1, 1]; -1 to 1 covers every turn), made
/// without trigonometry.
inline Quaternion TurnFromHalfCos(const Vec3& unit_axis, f32 half_cos)
{
    const f32 s = Sqrt(FMax(0.0f, 1.0f - half_cos * half_cos));
    return Quaternion(unit_axis.x * s, unit_axis.y * s, unit_axis.z * s, half_cos);
}

/// A smooth wave of period 2 pi between -1 and 1 shaped like a sine (two parabolas), made without trigonometry.
inline f32 Wave(f32 t)
{
    f32 u = t * 0.15915494f;  // turns
    u -= Floor(u);
    const f32 v = 2.0f * u - 1.0f;  // [-1, 1)
    return -4.0f * v * (1.0f - Abs(v));
}

}  // namespace engine_placeholder::physics::testing
