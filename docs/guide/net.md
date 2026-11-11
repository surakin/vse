# Networking: addresses, endpoints and errors

Status: Phase 20. Built: addresses, endpoints and the error type (Day 267). Next: the Linux socket backend (Day 268), the reactor (Day 269), the transports (Days 273-277).

**Audience:** engine programmers who talk to another machine or process. Design, spikes and the benchmark pass conditions:
`docs/superpowers/specs/2026-11-11-networking-design.md`.

**Trust:** until the encryption outcome of Day 280 is built, the engine's transports are for trusted networks only. Nothing here hides or authenticates data.

The module `engine/net` (namespace `net`) is the only place that will call the operating system's socket API (a gate from Day 268 enforces it). This page covers the
part with no system calls at all: values that name a peer, and the way failures are reported. Because they make no syscalls they behave the same on every
platform and are tested exhaustively on any machine, Windows scope-id rules included.

## The vocabulary

| Type | What it is |
|---|---|
| `Address` | An IPv4 or IPv6 address: four or sixteen bytes in network order and, for IPv6, an optional scope (a numeric interface index or an interface name). |
| `Endpoint` | An `Address` and a `u16` port. Port 0 means "any port" when binding. |
| `EndpointSet` | At most 16 endpoints, in order: the shape of a resolver's answer (Day 270). |
| `AddressText`, `EndpointText` | Fixed buffers holding canonical text. Formatting allocates nothing. |
| `Status`, `ErrorCode` | The result of every fallible call: a portable code and the operating system's error number, kept for logs. |

## Parsing is strict

`Address::Parse` accepts exactly one address and refuses everything else with `ErrorCode::Invalid`, leaving the output untouched:

- IPv4 is four decimal parts of one to three digits, each at most 255, **no leading zero** (`010.0.0.1` is refused, not read as octal), nothing before or after.
- IPv6 follows the RFC 4291 text forms: groups of one to four hex digits in either case, one `::` for at least one group of zeros, an embedded dotted IPv4 tail
  (`::ffff:1.2.3.4`). A pair of brackets around the whole address is accepted.
- A scope follows `%`: digits up to nine are a numeric interface index (`fe80::1%3`), anything else of up to 15 letters, digits, `_`, `-` and `.` is an interface
  name (`fe80::1%eth0`) that a socket backend turns into an index. IPv4 has no scope.

`Endpoint::Parse` takes `a.b.c.d:port` and `[v6]:port`. A bare IPv6 address with a port is ambiguous and refused. The port is decimal, 0 to 65535, with no sign,
space or leading zero. A host name is not an address; names go through the resolver.

Formatting is the canonical form of RFC 5952: lower case, no leading zeros, the longest run of two or more zero groups compressed (the first on a tie, never a
single group), an IPv4-mapped address as `::ffff:a.b.c.d`. Parsing the formatted text always gives back an equal value.

```cpp compile run standalone
#include <cstdio>

#include "net/endpoint.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

int main() {
    Address a;
    Status s = Address::Parse("2001:DB8:0:0:1:0:0:1", &a);
    Endpoint e;
    Status t = Endpoint::Parse("[fe80::1%eth0]:5000", &e);
    if (s.Failed() || t.Failed()) {
        return 1;
    }
    std::printf("%s %s %u\n", a.Format().CStr(), e.Format().CStr(), static_cast<unsigned>(e.Port()));
    return 0;
}
```

```output
2001:db8::1:0:0:1 [fe80::1%eth0]:5000 5000
```

## Classification

`IsAny`, `IsLoopback`, `IsLinkLocal`, `IsPrivate`, `IsMulticast` ask which range an address is in (the ranges are in the header's comments). An IPv4-mapped IPv6
address answers as the IPv4 address it wraps, and `Unmapped()` / `Mapped()` convert between the two forms. A server that accepts connections from the Internet
decides with these what counts as local; `Address::Loopback(family)` and `Address::Any(family)` build the two addresses a listener most often wants.

## Ordering and hashing

`Address` and `Endpoint` have a total order (family, bytes, scope, then port) and a `Hash` specialization, so both work as keys of `HashMap` and in sorted
containers. Equality includes the scope: `fe80::1%eth0` and `fe80::1%3` are different values until a backend resolves the name.

## Errors are values

No network call throws or aborts on a network failure. A call returns a `Status` (or a count together with one) whose `ErrorCode` is portable:

| Code | Meaning |
|---|---|
| `WouldBlock` | A non-blocking operation cannot finish now; wait for readiness and retry. |
| `Refused`, `Reset`, `TimedOut`, `Unreachable` | The peer or the path said no, vanished, or did not answer. |
| `AddressInUse`, `AddressNotAvailable` | A local endpoint is bound already, or does not belong to this machine. |
| `NotConnected`, `AlreadyConnected` | The socket is in the wrong state for the call. |
| `MessageTooLarge`, `LimitExceeded` | A size or count above what the socket, the transport or a setting allows. |
| `Closed`, `Cancelled` | The socket or connection was closed; the operation was cancelled before it finished. |
| `Invalid`, `Unsupported`, `Other` | The caller passed something refused; the platform lacks the feature; anything else. |

Branch on the code, never on `OsError()`; the number (`errno` or the Winsock code) is there for logs. The table that maps each operating system's numbers to these
codes lives in the backends (Days 268 and 278) and is tested against a table of the numbers.

`ENGINE_TRY(expr)` returns a failed `Status` from the enclosing function in one line, as it does for `database::Status`.
