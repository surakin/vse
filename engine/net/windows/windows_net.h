#pragma once

// The Windows socket backend's shared half (Phase 20 Day 278): starting Winsock, the last error, and endpoint <->
// sockaddr conversion. Internal to engine/net; included only by the files under engine/net/windows/.

#include "net/endpoint.h"
#include "net/status.h"
#include "net/windows/winsock.h"
#include "net/windows/wsa_error.h"

namespace engine_placeholder::net::windows
{

// Starts Winsock 2.2 for the process the first time it is called, from any thread; later calls return the first call's
// outcome. Every entry point of the backend that makes a socket or resolves a name calls it first. There is no
// matching WSACleanup: the process's exit releases Winsock, and a cleanup while a resolver or reactor thread may still
// be inside a Winsock call would pull the library from under it.
Status EnsureWinsock();

// The failure status of the calling thread's last Winsock error.
inline Status LastWsaStatus()
{
    return StatusFromWsa(::WSAGetLastError());
}

// The native handle (an `i64`, as [[net::Socket::NativeHandle]] holds it) of a SOCKET, and back. INVALID_SOCKET is
// ~0, which is -1 as an `i64`: "not open".
inline i64 HandleOf(SOCKET socket)
{
    return static_cast<i64>(socket);
}
inline SOCKET SocketOf(i64 handle)
{
    return static_cast<SOCKET>(handle);
}

// Fills `out` with `endpoint` for a socket of `family` (an IPv4 endpoint on an IPv6 socket is mapped, an IPv4-mapped
// one on an IPv4 socket is unmapped) and returns its length, or 0 with `*status` set when the endpoint cannot be used
// with the socket (Invalid, or AddressNotAvailable for an unknown scope name). The Windows twin of
// posix::ToSockaddr.
int ToSockaddr(const Endpoint& endpoint, AddressFamily family, sockaddr_storage* out, Status* status);
// Reads an endpoint back; an IPv4-mapped IPv6 address becomes IPv4. Returns false for a family other than IPv4 and
// IPv6.
bool FromSockaddr(const sockaddr_storage& storage, Endpoint* out);

}  // namespace engine_placeholder::net::windows
