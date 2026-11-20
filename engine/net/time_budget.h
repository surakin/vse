#pragma once

//! [[net::TimeBudget]]: the time left of one operation that waits several times.

#include "net/cancel.h"
#include "net/clock.h"
#include "net/reactor.h"
#include "types.h"

namespace vse::net
{

/// Tracks one deadline across the waits of a loop, so that the timeout of an operation covers the whole operation and
/// not each wait.
///
/// - Thread-safety: thread-compatible; one coroutine uses it.
class TimeBudget
{
public:
    /// Starts the budget now.
    /// - Param reactor: gives the clock.
    /// - Param options: the timeout (negative for none) and the cancel token to pass on.
    TimeBudget(const Reactor& reactor, const WaitOptions& options) : clock_(&reactor.GetClock()), token_(options.token)
    {
        if (options.timeout_nanos >= 0)
        {
            limited_ = true;
            const u64 now = clock_->NowNanos();
            const u64 span = static_cast<u64>(options.timeout_nanos);
            deadline_ = now > (~u64{0} >> 1) - span ? (~u64{0} >> 1) : now + span;
        }
    }

    /// The options for the next wait: the same token and what is left of the timeout.
    /// - Param expired: set to true when nothing is left (the returned timeout is then zero).
    /// - Ownership: `*expired` is written during the call.
    /// - Returns: the options.
    WaitOptions Next(bool* expired) const
    {
        WaitOptions next;
        next.token = token_;
        *expired = false;
        if (limited_)
        {
            const u64 now = clock_->NowNanos();
            if (now >= deadline_)
            {
                *expired = true;
                next.timeout_nanos = 0;
            }
            else
            {
                next.timeout_nanos = static_cast<i64>(deadline_ - now);
            }
        }
        return next;
    }

private:
    const Clock* clock_;
    CancelToken* token_;
    bool limited_ = false;
    u64 deadline_ = 0;
};

}  // namespace vse::net
