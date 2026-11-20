#include "net/listener.h"

#include "net/time_budget.h"

namespace vse::net
{

Status Listener::Open(const Endpoint& at, const ListenerOptions& options)
{
    if (socket_.IsOpen() || !at.GetAddress().IsValid())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    Socket socket;
    Status s = Socket::OpenTcp(at.GetAddress().Family(), &socket);
    if (s.Failed())
    {
        return s;
    }
    if (options.reuse_address)
    {
        s = socket.SetReuseAddress(true);
        if (s.Failed())
        {
            return s;
        }
    }
    bool dual = false;
    if (at.GetAddress().IsV6() && at.GetAddress().IsAny())
    {
        // Must be set before the bind. Where the system refuses, the listener is IPv6 only and says so.
        dual = options.dual_stack && socket.SetDualStack(true).Ok();
        if (!options.dual_stack)
        {
            [[maybe_unused]] const Status v6_only = socket.SetDualStack(false);
        }
    }
    s = socket.Bind(at);
    if (s.Failed())
    {
        return s;
    }
    s = socket.Listen(options.backlog);
    if (s.Failed())
    {
        return s;
    }
    Endpoint bound;
    s = socket.LocalEndpoint(&bound);
    if (s.Failed())
    {
        return s;
    }
    socket_ = static_cast<Socket&&>(socket);
    local_ = bound;
    dual_stack_ = dual;
    max_unhandled_ = options.max_unhandled;
    unhandled_.Store(0, MemoryOrder::Release);
    return Status::Success();
}

// Takes one place under the cap (or counts one more when there is no cap); false when the cap is reached. Exact across
// threads.
bool Listener::Reserve()
{
    usize current = unhandled_.Load(MemoryOrder::Acquire);
    do
    {
        if (max_unhandled_ != 0 && current >= max_unhandled_)
        {
            return false;
        }
    } while (!unhandled_.CompareExchangeWeak(current, current + 1, MemoryOrder::AcqRel, MemoryOrder::Acquire));
    return true;
}

namespace
{

// How long to pause: the wanted time, or what is left of the budget if that is shorter (Sleep itself takes no timeout).
u64 PauseNanos(u64 wanted, const WaitOptions& next)
{
    return next.timeout_nanos >= 0 && static_cast<u64>(next.timeout_nanos) < wanted
               ? static_cast<u64>(next.timeout_nanos)
               : wanted;
}

}  // namespace

jobs::Task<IoResult> Listener::Accept(Reactor& reactor, Socket* connection, Endpoint* peer, WaitOptions options)
{
    const TimeBudget budget(reactor, options);
    u64 backoff = 5'000'000;  // nanoseconds
    for (;;)
    {
        bool expired = false;
        WaitOptions next = budget.Next(&expired);
        if (expired)
        {
            co_return IoResult{Status::Of(ErrorCode::TimedOut), 0};
        }
        if (!Reserve())
        {
            // At the cap: leave the connections in the kernel's queue and look again shortly.
            const Status slept = co_await reactor.Sleep(PauseNanos(1'000'000, next), next);
            if (slept.Failed())
            {
                co_return IoResult{slept, 0};
            }
            continue;
        }
        const IoResult accepted = co_await net::Accept(reactor, socket_, connection, peer, next);
        if (accepted.status.Ok())
        {
            co_return accepted;  // the reservation stays: this connection is unhandled until Handled()
        }
        Handled();  // nothing was accepted: give the place back
        if (!accepted.status.Is(ErrorCode::LimitExceeded))
        {
            co_return accepted;
        }
        // Out of descriptors (or memory): sleeping lets handled connections close, instead of spinning on the failure.
        next = budget.Next(&expired);
        if (expired)
        {
            co_return IoResult{Status::Of(ErrorCode::TimedOut), 0};
        }
        const Status slept = co_await reactor.Sleep(PauseNanos(backoff, next), next);
        if (slept.Failed())
        {
            co_return IoResult{slept, 0};
        }
        backoff = backoff * 2 > 200'000'000 ? 200'000'000 : backoff * 2;
    }
}

}  // namespace vse::net
