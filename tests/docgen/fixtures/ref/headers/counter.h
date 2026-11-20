#pragma once

//! The join primitive of the demo module.
//!
//! This overview is shown at the top of the module page.

namespace vse {
namespace demo {

/// The kinds of wake-up a counter can perform.
enum class WakeMode : unsigned char {
    Inline,    ///< Resume the waiter on the thread that finished the work.
    Requeue = 3, ///< Put the waiter back on the queue instead.
};

/// Tracks outstanding work and lets one coroutine wait for it to finish.
///
/// Increment before submitting the work, decrement when it completes.
///
/// - The count never goes below zero.
/// - At most one waiter at a time.
///
/// - Thread-safety: thread-safe. Any thread may call [[demo::Counter::Increment]]
///   and [[demo::Counter::Decrement]].
/// - Ownership: must outlive every pending decrement.
/// - Example: counting two jobs.
/// ```cpp
/// Counter c;
/// c.Increment(2);
/// ```
/// - See: [[Shape]], [[demo::Counter::Counter]]
class Counter {
public:
    /// Creates a counter at zero.
    Counter();

    /// Adds `n` outstanding units of work.
    /// - Param n: how many units to add.
    void Increment(unsigned long n = 1);

    /// Marks one unit done.
    /// - Aborts: the count is already zero.
    void Decrement();

    /// Returns the current count.
    /// - Returns: the number of outstanding units.
    unsigned long Count() const;

    /// Returns the count after adding `n`, without changing it.
    /// - Param n: units to add.
    /// - Returns: the hypothetical count.
    unsigned long Peek(unsigned long n) const;

    /// A nested helper type.
    struct Stats {
        unsigned long resumes; ///< How often waiters were resumed.
    };

    // Private members are excluded from the reference.
private:
    unsigned long count_ = 0;
};

/// Creates a counter with a starting value.
/// - Param start: the initial count.
/// - Returns: a new counter.
Counter MakeCounter(unsigned long start);

/// A documented macro.
/// - Param x: the value.
#define DEMO_SQUARE(x) ((x) * (x))

/// An alias for the count type.
using Count = unsigned long;

} // namespace demo
} // namespace vse
