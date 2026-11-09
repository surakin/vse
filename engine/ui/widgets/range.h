#pragma once

#include "lang/scalar.h"

#include "types.h"

//! The value range the value widgets share (Phase 15, Day 196): clamp, snap to a step exactly, and the clamp-or-reject choice.

namespace engine_placeholder::ui {

/// What a range does with a value outside it.
enum class RangePolicy : u8 {
    Clamp,  ///< The value is moved to the nearest end.
    Reject, ///< The value is refused and the old one stays.
};

/// A range of `f64` values with an optional step.
///
/// A value snaps to the grid `min + k * step` for a whole number `k`, computed as one multiplication from the minimum, so a thousand steps drift by nothing; the grid stops at the
/// last point not above `max`. In integer mode the step is at least 1. Without a step (0) the value is only clamped. A NaN becomes the minimum.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct ValueRange {
    f64 min = 0;                          ///< The smallest value.
    f64 max = 1;                          ///< The largest value; a maximum below the minimum is taken as the minimum.
    f64 step = 0;                         ///< The grid spacing, 0 for none.
    bool integer = false;                 ///< Whole numbers only (the step is at least 1).
    RangePolicy policy = RangePolicy::Clamp; ///< What `Apply` does with a value outside `min`..`max`.

    /// Returns the effective step: `step`, or 1 in integer mode when that is larger.
    ///
    /// Fails: never.
    f64 EffectiveStep() const { return (integer && step < 1) ? 1.0 : step; }

    /// Returns `value` clamped to the range and snapped to the grid.
    ///
    /// Fails: never.
    f64 Snap(f64 value) const {
        if (!(value == value)) return min;
        const f64 lo = min;
        const f64 hi = max < min ? min : max;
        const f64 st = EffectiveStep();
        if (!(st > 0)) return value < lo ? lo : (value > hi ? hi : value);
        if (value <= lo) return lo;
        const f64 last = Floor((hi - lo) / st + 1e-9);
        f64 k = Floor((value - lo) / st + 0.5);
        if (k > last) k = last;
        if (k < 0) k = 0;
        return lo + k * st;
    }

    /// Applies the policy to `value` against the current value `current`: `Clamp` gives `Snap(value)`, `Reject` gives `current` when `value` is outside the range and `Snap(value)` otherwise.
    ///
    /// Fails: never.
    f64 Apply(f64 value, f64 current) const {
        if (policy == RangePolicy::Reject && (!(value == value) || value < min || value > (max < min ? min : max))) return current;
        return Snap(value);
    }
};

} // namespace engine_placeholder::ui
