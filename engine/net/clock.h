#pragma once

//! The time source of the networking layer: [[net::Clock]], the real [[net::MonotonicClock]] and the test double
//! [[net::ManualClock]].

#include "os/os.h"
#include "sync/atomic.h"
#include "types.h"

namespace vse::net
{

/// A source of monotonic time in nanoseconds. Timeouts and sleeps of the reactor, and the timers of the transports,
/// read it, so a test can replace the real clock with a [[net::ManualClock]] and decide when time passes.
///
/// - Thread-safety: thread-safe; implementations must be.
class Clock
{
public:
    virtual ~Clock() = default;

    /// Returns the current time in nanoseconds; it never goes back. The origin is arbitrary.
    /// - Returns: the current time.
    virtual u64 NowNanos() const = 0;
};

/// The real clock: [[os::MonotonicTimeNanos]].
///
/// - Thread-safety: thread-safe.
class MonotonicClock final : public Clock
{
public:
    /// Returns the operating system's monotonic time.
    /// - Returns: the current time in nanoseconds.
    u64 NowNanos() const override { return os::MonotonicTimeNanos(); }
};

/// A clock that moves only when a test says so.
///
/// After [[net::ManualClock::Advance]], call [[net::Reactor::Poke]] on the reactor that uses it, so the reactor looks
/// at its timers again.
///
/// - Thread-safety: thread-safe.
class ManualClock final : public Clock
{
public:
    /// Creates a clock at `start` nanoseconds.
    /// - Param start: the first time the clock reports.
    explicit ManualClock(u64 start = 1'000'000) : now_(start) {}

    /// Returns the current manual time.
    /// - Returns: the time in nanoseconds.
    u64 NowNanos() const override { return now_.Load(MemoryOrder::Acquire); }

    /// Moves the clock forward.
    /// - Param nanos: how far to move it.
    void Advance(u64 nanos) { now_.FetchAdd(nanos, MemoryOrder::AcqRel); }

private:
    Atomic<u64> now_;
};

}  // namespace vse::net
