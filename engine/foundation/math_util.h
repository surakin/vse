#pragma once

#include "log.h"
#include "types.h"

// Hand-rolled scalar math helpers (PLAN.md assumption #2: foundation math
// is hand-rolled, not reached for from <cmath>).
namespace engine_placeholder {

// Newton-Raphson (Babylonian method): x_{n+1} = 0.5*(x_n + value/x_n),
// which converges quadratically to sqrt(value) from any positive
// starting guess — using `value` itself as the initial guess is
// deliberately simple, not tuned for iteration count.
inline f32 Sqrt(f32 value) {
    ENGINE_ASSERT(value >= 0.0f, "Sqrt of a negative number");
    if (value == 0.0f) {
        return 0.0f;
    }
    f32 guess = value;
    for (int i = 0; i < 20; ++i) {
        f32 next = 0.5f * (guess + value / guess);
        if (next == guess) {
            break;
        }
        guess = next;
    }
    return guess;
}

} // namespace engine_placeholder
