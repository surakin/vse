#pragma once

// The Winsock error table of the Windows socket backend (Phase 20 Day 278). No Windows header: the codes are plain
// numbers (WSAEWOULDBLOCK is 10035, ...), so the table's test needs nothing of the SDK. Internal to engine/net; not
// part of the public API.

#include "net/status.h"

namespace vse::net::windows
{

// The portable code of a Winsock error (`WSAGetLastError`, or the `int` that `GetAddrInfoW` returns for a name lookup:
// see `ErrorFromResolveError`).
ErrorCode ErrorFromWsa(int error);
// A failure status for a Winsock error (the code plus the number).
inline Status StatusFromWsa(int error)
{
    return Status::Make(ErrorFromWsa(error), error);
}
// The portable code of a `GetAddrInfoW` result: a name that does not exist is NotFound, a name server that did not
// answer is TimedOut (the same meanings as POSIX's EAI_ codes).
ErrorCode ErrorFromResolveError(int error);

}  // namespace vse::net::windows
