#include "net/async.h"

namespace engine_placeholder::net
{

namespace
{

// The time left for the whole operation: tracks one deadline across the waits of a loop.
class Budget
{
public:
    Budget(const Reactor& reactor, const WaitOptions& options) : clock_(&reactor.GetClock()), token_(options.token)
    {
        if (options.timeout_nanos >= 0)
        {
            limited_ = true;
            deadline_ = clock_->NowNanos() + static_cast<u64>(options.timeout_nanos);
        }
    }

    // The options for the next wait: the same token and what is left of the timeout. `expired` is set when nothing is
    // left.
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

// The loop all the operations share: try; if the socket would block, wait (for the time that is left of the whole
// operation, and unless cancelled) in the direction the operation needs; try again. `op` is called for each try and
// returns the status of the try and the bytes moved so far; it reports WouldBlock to ask for a wait.
template <class Op>
jobs::Task<IoResult> RetryWhileBlocked(Reactor& reactor, Socket& socket, bool wait_for_read, WaitOptions options, Op op)
{
    const Budget budget(reactor, options);
    for (;;)
    {
        const IoResult attempt = op();
        if (!attempt.status.Is(ErrorCode::WouldBlock))
        {
            co_return attempt;
        }
        bool expired = false;
        const WaitOptions next = budget.Next(&expired);
        if (expired)
        {
            co_return IoResult{Status::Of(ErrorCode::TimedOut), attempt.count};
        }
        const Status waited = wait_for_read ? co_await reactor.WaitReadable(socket, next)
                                            : co_await reactor.WaitWritable(socket, next);
        if (waited.Failed())
        {
            co_return IoResult{waited, attempt.count};
        }
    }
}

}  // namespace

jobs::Task<IoResult> Receive(Reactor& reactor, Socket& socket, MutableByteSpan buffer, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, true, options,
                             [&socket, buffer]
                             {
                                 usize received = 0;
                                 const Status s = socket.Receive(buffer, &received);
                                 return IoResult{s, received};
                             });
}

jobs::Task<IoResult> Send(Reactor& reactor, Socket& socket, ByteSpan bytes, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options,
                             [&socket, bytes, total = usize{0}]() mutable
                             {
                                 // Keep sending while the socket takes data; ask for a wait when it is full.
                                 for (;;)
                                 {
                                     usize sent = 0;
                                     const Status s = socket.Send(bytes.Subspan(total, bytes.Size() - total), &sent);
                                     total += sent;
                                     if (s.Failed() || total >= bytes.Size())
                                     {
                                         return IoResult{s, total};
                                     }
                                 }
                             });
}

jobs::Task<IoResult> ReceiveFrom(Reactor& reactor, Socket& socket, MutableByteSpan buffer, Endpoint* from,
                                 WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, true, options,
                             [&socket, buffer, from]
                             {
                                 usize received = 0;
                                 const Status s = socket.ReceiveFrom(buffer, &received, from);
                                 return IoResult{s, received};
                             });
}

jobs::Task<IoResult> SendTo(Reactor& reactor, Socket& socket, ByteSpan bytes, const Endpoint& to, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options,
                             [&socket, bytes, &to]
                             {
                                 usize sent = 0;
                                 const Status s = socket.SendTo(bytes, to, &sent);
                                 return IoResult{s, sent};
                             });
}

jobs::Task<IoResult> Accept(Reactor& reactor, Socket& listener, Socket* connection, Endpoint* peer, WaitOptions options)
{
    return RetryWhileBlocked(reactor, listener, true, options,
                             [&listener, connection, peer] { return IoResult{listener.Accept(connection, peer), 0}; });
}

jobs::Task<IoResult> Connect(Reactor& reactor, Socket& socket, const Endpoint& remote, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options,
                             [&socket, &remote, started = false]() mutable
                             {
                                 // The first try starts the connect; the later ones, after the socket became writable,
                                 // ask how it ended.
                                 const Status s = started ? socket.FinishConnect() : socket.Connect(remote);
                                 started = true;
                                 return IoResult{s, 0};
                             });
}

}  // namespace engine_placeholder::net
