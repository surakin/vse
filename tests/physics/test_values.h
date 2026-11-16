// Values shared by the physics tests.
#pragma once

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

}  // namespace engine_placeholder::physics::testing
