#pragma once

// The POSIX half of the socket backend (Linux and macOS share it): error mapping, endpoint <-> sockaddr conversion and the hooks each OS supplies
// (engine/net/linux/linux_hooks.cpp). Internal to engine/net; not part of the public API.

#include <sys/socket.h>

#include "net/endpoint.h"
#include "net/status.h"

namespace engine_placeholder::net::posix {

// The portable code of an errno value.
ErrorCode ErrorFromErrno(int error);
// A failure status for an errno value (the code plus the number).
inline Status StatusFromErrno(int error) { return Status::Make(ErrorFromErrno(error), error); }

// Fills `out` with `endpoint` for a socket of `family` (an IPv4 endpoint on an IPv6 socket is mapped, an IPv4-mapped one on an IPv4 socket is unmapped)
// and returns its length, or 0 with `*status` set when the endpoint cannot be used with the socket (Invalid, or AddressNotAvailable for an unknown scope name).
socklen_t ToSockaddr(const Endpoint& endpoint, AddressFamily family, sockaddr_storage* out, Status* status);
// Reads an endpoint back; an IPv4-mapped IPv6 address becomes IPv4. Returns false for a family other than IPv4 and IPv6.
bool FromSockaddr(const sockaddr_storage& storage, Endpoint* out);

// The hooks each operating system supplies.
// A new non-blocking, close-on-exec socket; -1 with errno set on failure.
int OpenSocketFd(int domain, int type);
// Accepts a connection as a non-blocking, close-on-exec descriptor; -1 with errno set on failure.
int AcceptFd(int listener, sockaddr* address, socklen_t* length);
// The flags that stop a write to a vanished peer from raising SIGPIPE (MSG_NOSIGNAL; 0 where the socket option does it instead).
int SendFlags();
// The flag that makes a datagram receive report the datagram's real size when it was cut (MSG_TRUNC), or 0 where there is none.
int TruncationFlag();

} // namespace engine_placeholder::net::posix
