#pragma once

#include "math/vector.h"
#include "types.h"

namespace spatial_test
{

// A small deterministic generator (SplitMix64) so every random test is reproducible from its seed, as
// tests/ecs/test_rng.h.
class Rng
{
public:
    explicit Rng(vse::u64 seed) : state_(seed) {}
    vse::u64 Next()
    {
        state_ += 0x9E3779B97F4A7C15ull;
        vse::u64 z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // A value in [0, bound), bound > 0.
    vse::u32 Below(vse::u32 bound) { return static_cast<vse::u32>(Next() % bound); }
    // A value in [lo, hi).
    vse::f32 Float(vse::f32 lo, vse::f32 hi)
    {
        const vse::f32 unit = static_cast<vse::f32>(Next() >> 40) / static_cast<vse::f32>(1ull << 24);
        return lo + (hi - lo) * unit;
    }
    // A point in the box [lo, hi) on every axis.
    vse::Vec3 Point(vse::f32 lo, vse::f32 hi) { return vse::Vec3(Float(lo, hi), Float(lo, hi), Float(lo, hi)); }

private:
    vse::u64 state_;
};

}  // namespace spatial_test
