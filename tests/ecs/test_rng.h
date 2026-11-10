#pragma once

// A small deterministic random generator for the randomized ECS tests (SplitMix64): the same seed gives the same sequence on every
// platform, so a failing seed can be replayed. Not for use outside tests.

#include "types.h"

namespace ecs_test {

class Rng {
public:
    explicit Rng(engine_placeholder::u64 seed) : state_(seed) {}
    engine_placeholder::u64 Next() {
        state_ += 0x9E3779B97F4A7C15ull;
        engine_placeholder::u64 z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    /// A value in [0, bound), bound > 0.
    engine_placeholder::u32 Below(engine_placeholder::u32 bound) { return static_cast<engine_placeholder::u32>(Next() % bound); }

private:
    engine_placeholder::u64 state_;
};

} // namespace ecs_test
