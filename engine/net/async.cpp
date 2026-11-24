#include "net/async.h"
#include "net/time_budget.h"

namespace vse::net
{

namespace detail  // named, not anonymous: GCC refuses a coroutine frame or a member whose type has internal linkage
{

// The loop all the operations share: try; if the socket would block, wait (for the time that is left of the whole
// operation, and unless cancelled) in the direction the operation needs; try again. `op` is called for each try and
// returns the status of the try and the bytes moved so far; it reports WouldBlock to ask for a wait.
template <class Op>
jobs::Task<IoResult> RetryWhileBlocked(Reactor& reactor, Socket& socket, bool wait_for_read, WaitOptions options, Op op)
{
    const TimeBudget budget(reactor, options);
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

// The try of each operation, a named type (a lambda inside the operation has no linkage, and a type in an anonymous
// namespace has internal linkage: GCC refuses both as a field of the coroutine frame of `RetryWhileBlocked`).
struct ReceiveTry
{
    Socket* socket;
    MutableByteSpan buffer;
    IoResult operator()()
    {
        usize received = 0;
        const Status s = socket->Receive(buffer, &received);
        return IoResult{s, received};
    }
};

struct SendTry
{
    Socket* socket;
    ByteSpan bytes;
    usize total = 0;
    IoResult operator()()
    {
        // Keep sending while the socket takes data; ask for a wait when it is full.
        for (;;)
        {
            usize sent = 0;
            const Status s = socket->Send(bytes.Subspan(total, bytes.Size() - total), &sent);
            total += sent;
            if (s.Failed() || total >= bytes.Size())
            {
                return IoResult{s, total};
            }
        }
    }
};

struct ReceiveFromTry
{
    Socket* socket;
    MutableByteSpan buffer;
    Endpoint* from;
    IoResult operator()()
    {
        usize received = 0;
        const Status s = socket->ReceiveFrom(buffer, &received, from);
        return IoResult{s, received};
    }
};

struct SendToTry
{
    Socket* socket;
    ByteSpan bytes;
    const Endpoint* to;
    IoResult operator()()
    {
        usize sent = 0;
        const Status s = socket->SendTo(bytes, *to, &sent);
        return IoResult{s, sent};
    }
};

struct AcceptTry
{
    Socket* listener;
    Socket* connection;
    Endpoint* peer;
    IoResult operator()() { return IoResult{listener->Accept(connection, peer), 0}; }
};

struct ConnectTry
{
    Socket* socket;
    const Endpoint* remote;
    bool started = false;
    IoResult operator()()
    {
        // The first try starts the connect; the later ones, after the socket became writable, ask how it ended.
        const Status s = started ? socket->FinishConnect() : socket->Connect(*remote);
        started = true;
        return IoResult{s, 0};
    }
};

}  // namespace detail

using namespace detail;

jobs::Task<IoResult> Receive(Reactor& reactor, Socket& socket, MutableByteSpan buffer, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, true, options, ReceiveTry{&socket, buffer});
}

jobs::Task<IoResult> Send(Reactor& reactor, Socket& socket, ByteSpan bytes, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options, SendTry{&socket, bytes});
}

jobs::Task<IoResult> ReceiveFrom(Reactor& reactor, Socket& socket, MutableByteSpan buffer, Endpoint* from,
                                 WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, true, options, ReceiveFromTry{&socket, buffer, from});
}

jobs::Task<IoResult> SendTo(Reactor& reactor, Socket& socket, ByteSpan bytes, const Endpoint& to, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options, SendToTry{&socket, bytes, &to});
}

jobs::Task<IoResult> Accept(Reactor& reactor, Socket& listener, Socket* connection, Endpoint* peer, WaitOptions options)
{
    return RetryWhileBlocked(reactor, listener, true, options, AcceptTry{&listener, connection, peer});
}

jobs::Task<IoResult> Connect(Reactor& reactor, Socket& socket, const Endpoint& remote, WaitOptions options)
{
    return RetryWhileBlocked(reactor, socket, false, options, ConnectTry{&socket, &remote});
}

}  // namespace vse::net
