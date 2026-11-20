#include "net/resolve.h"

#include "cstr/mem_ops.h"
#include "net/time_budget.h"

namespace vse::net
{

jobs::Task<ResolveResult> Resolve(Reactor& reactor, Resolver& resolver, StringView host, u16 port, AddressFamily family,
                                  WaitOptions options)
{
    ResolveResult result;
    if (host.Empty() || host.Length() > kMaxHostNameBytes || host.IndexOf('\0') != host.Length())
    {
        result.status = Status::Of(
            ErrorCode::Invalid);  // the name that is checked must be the name that is looked up: no embedded zero
        co_return result;
    }
    // A numeric host needs no name server (and no thread).
    Address numeric;
    if (Address::Parse(host, &numeric).Ok())
    {
        const bool wanted = family == AddressFamily::None || numeric.Family() == family;
        result.status = wanted ? Status::Success() : Status::Of(ErrorCode::NotFound);
        if (wanted)
        {
            const bool added = result.endpoints.Add(Endpoint(numeric, port));
            result.status = added ? Status::Success() : Status::Of(ErrorCode::LimitExceeded);
        }
        co_return result;
    }
    // The request is shared with the resolver thread: if this coroutine gives up, the thread still has it to finish.
    SharedPtr<ResolveRequest> request = MakeShared<ResolveRequest>();
    MemCopy(request->host, host.Data(), host.Length());
    request->host[host.Length()] = '\0';
    request->port = port;
    request->family = family;
    const Status queued = resolver.Submit(request);
    if (queued.Failed())
    {
        result.status = queued;
        co_return result;
    }
    const Status waited = co_await reactor.WaitSignal(request->done, options);
    if (waited.Failed())
    {
        request->abandoned.Store(true, MemoryOrder::Release);  // the thread skips it if it has not started the lookup
        result.status = waited;  // timed out or cancelled: the answer, when it comes, is dropped
        co_return result;
    }
    result.status = request->status;
    if (request->status.Ok())
    {
        result.endpoints = request->endpoints;
    }
    co_return result;
}

jobs::Task<IoResult> ConnectAny(Reactor& reactor, const EndpointSet& endpoints, Socket* socket, ConnectOptions options)
{
    if (endpoints.Empty() || socket->IsOpen())
    {
        co_return IoResult{Status::Of(ErrorCode::Invalid), 0};
    }
    Status last = Status::Of(ErrorCode::Unreachable);
    for (usize i = 0; i < endpoints.Count(); ++i)
    {
        if (options.token != nullptr && options.token->IsCancelled())
        {
            co_return IoResult{Status::Of(ErrorCode::Cancelled), 0};
        }
        const Endpoint& endpoint = endpoints[i];
        Socket attempt;
        const Status opened = Socket::OpenTcp(endpoint.GetAddress().Family(), &attempt);
        if (opened.Failed())
        {
            last = opened;
            continue;
        }
        WaitOptions wait;
        wait.token = options.token;
        wait.timeout_nanos = options.attempt_timeout_nanos;
        const IoResult connected = co_await Connect(reactor, attempt, endpoint, wait);
        if (connected.status.Ok())
        {
            *socket = static_cast<Socket&&>(attempt);
            co_return IoResult{Status::Success(), 0};
        }
        last = connected.status;
        [[maybe_unused]] const Status closed = reactor.Close(attempt);  // a failed attempt leaves nothing behind
        if (last.Is(ErrorCode::Cancelled))
        {
            break;
        }
    }
    co_return IoResult{last, 0};
}

jobs::Task<IoResult> ConnectTo(Reactor& reactor, Resolver& resolver, StringView host, u16 port, Socket* socket,
                               ConnectOptions options)
{
    WaitOptions resolve_options;
    resolve_options.token = options.token;
    resolve_options.timeout_nanos = options.resolve_timeout_nanos;
    const ResolveResult resolved = co_await Resolve(reactor, resolver, host, port, AddressFamily::None,
                                                    resolve_options);
    if (resolved.status.Failed())
    {
        co_return IoResult{resolved.status, 0};
    }
    co_return co_await ConnectAny(reactor, resolved.endpoints, socket, options);
}

}  // namespace vse::net
