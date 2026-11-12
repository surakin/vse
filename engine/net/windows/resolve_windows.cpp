// net::SystemResolve on Windows (Phase 20 Day 278): GetAddrInfoW, the only name-resolution call of the engine there.
// The name goes in as UTF-16, so an internationalized name reaches the system as the user wrote it (the ANSI
// getaddrinfo would pass it through the process's code page). A numeric host never gets here (net/resolve.cpp parses
// it first).

#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "net/resolver.h"
#include "net/windows/windows_net.h"
#include "os/windows/utf16.h"

namespace engine_placeholder::net
{

Status SystemResolve(StringView host, u16 port, AddressFamily family, EndpointSet* out)
{
    if (host.Empty() || host.Length() > kMaxHostNameBytes || host.IndexOf('\0') != host.Length())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    const Status started = windows::EnsureWinsock();
    if (started.Failed())
    {
        return started;
    }
    // A UTF-8 name of at most 253 bytes needs at most 253 UTF-16 code units.
    Array<wchar_t, kMaxHostNameBytes + 1> name{};
    usize replaced = 0;
    const usize needed = os::windows::Utf8ToUtf16(host, name.Data(), name.Size(), &replaced);
    if (needed >= name.Size() || replaced != 0)
    {
        return Status::Of(ErrorCode::Invalid);  // not UTF-8: no name the system could look up
    }
    Array<wchar_t, 8> service{};
    {
        u32 value = port;
        usize digits = 0;
        Array<wchar_t, 8> reversed{};
        do
        {
            reversed[digits++] = static_cast<wchar_t>(L'0' + value % 10);
            value /= 10;
        } while (value != 0);
        for (usize i = 0; i < digits; ++i)
        {
            service[i] = reversed[digits - 1 - i];
        }
        service[digits] = L'\0';
    }
    ADDRINFOW hints{};
    hints.ai_family = family == AddressFamily::V4 ? AF_INET : (family == AddressFamily::V6 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = SOCK_STREAM;  // one entry per address, not one per socket type
    ADDRINFOW* list = nullptr;
    const int error = ::GetAddrInfoW(name.Data(), service.Data(), &hints, &list);
    if (error != 0)
    {
        return Status::Make(windows::ErrorFromResolveError(error), error);
    }
    // A hostile or odd list is bounded by the set: the rest is dropped, and an address is kept once.
    for (const ADDRINFOW* entry = list; entry != nullptr; entry = entry->ai_next)
    {
        if (entry->ai_addr == nullptr || entry->ai_addrlen > sizeof(sockaddr_storage))
        {
            continue;
        }
        sockaddr_storage storage{};
        MemCopy(&storage, entry->ai_addr, entry->ai_addrlen);
        Endpoint endpoint;
        if (!windows::FromSockaddr(storage, &endpoint))
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
    ::FreeAddrInfoW(list);
    return out->Empty() ? Status::Of(ErrorCode::NotFound) : Status::Success();
}

}  // namespace engine_placeholder::net
