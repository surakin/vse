// net::SystemResolve on POSIX (Linux and macOS): getaddrinfo, the only name-resolution call of the engine.

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "cstr/mem_ops.h"
#include "net/posix/posix_net.h"
#include "net/resolver.h"

namespace engine_placeholder::net
{

namespace
{

ErrorCode FromGaiError(int error)
{
    switch (error)
    {
    case EAI_NONAME:
#ifdef EAI_NODATA
    case EAI_NODATA:
#endif
    case EAI_SERVICE: return ErrorCode::NotFound;
    case EAI_AGAIN: return ErrorCode::TimedOut;  // a temporary failure of the name server
    case EAI_FAIL: return ErrorCode::Unreachable;
    case EAI_MEMORY: return ErrorCode::LimitExceeded;
    case EAI_FAMILY:
    case EAI_SOCKTYPE: return ErrorCode::Unsupported;
    default: return ErrorCode::Other;
    }
}

}  // namespace

Status SystemResolve(StringView host, u16 port, AddressFamily family, EndpointSet* out)
{
    if (host.Empty() || host.Length() > kMaxHostNameBytes || host.IndexOf('\0') != host.Length())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    char name[kMaxHostNameBytes + 1];
    MemCopy(name, host.Data(), host.Length());
    name[host.Length()] = '\0';
    char service[8];
    {
        u32 value = port;
        int digits = 0;
        char reversed[8];
        do
        {
            reversed[digits++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        for (int i = 0; i < digits; ++i)
        {
            service[i] = reversed[digits - 1 - i];
        }
        service[digits] = '\0';
    }
    addrinfo hints{};
    hints.ai_family = family == AddressFamily::V4 ? AF_INET : (family == AddressFamily::V6 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = SOCK_STREAM;  // one entry per address, not one per socket type
    addrinfo* list = nullptr;
    const int error = ::getaddrinfo(name, service, &hints, &list);
    if (error != 0)
    {
        return Status::Make(FromGaiError(error), error);
    }
    // A hostile or odd list is bounded by the set: the rest is dropped, and an address is kept once.
    for (const addrinfo* entry = list; entry != nullptr; entry = entry->ai_next)
    {
        if (entry->ai_addr == nullptr || entry->ai_addrlen > sizeof(sockaddr_storage))
        {
            continue;
        }
        sockaddr_storage storage{};
        MemCopy(&storage, entry->ai_addr, entry->ai_addrlen);
        Endpoint endpoint;
        if (!posix::FromSockaddr(storage, &endpoint))
        {
            continue;
        }
        bool seen = false;
        for (usize i = 0; i < out->Count(); ++i)
        {
            seen = seen || (*out)[i] == endpoint;
        }
        if (!seen && !out->Add(endpoint))
        {
            break;
        }
    }
    ::freeaddrinfo(list);
    return out->Empty() ? Status::Of(ErrorCode::NotFound) : Status::Success();
}

}  // namespace engine_placeholder::net
