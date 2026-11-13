# Socket layer: what a platform backend implements

`net::Socket` (`engine/net/socket.h`) is shaped so Windows, macOS, Android, iOS and the consoles can each implement it without changing `Address`,
`Endpoint`, `Status` or any caller. This list is kept in step with the header by the test `net_backend_lists`: every function whose documentation
carries a `Backend:` label has a `###` section here, and every section names such a function. Design and reasons:
`docs/superpowers/specs/2026-11-11-networking-design.md`.

Linux and macOS share the POSIX backend in `engine/net/posix/` (`socket_posix.cpp`, `posix_error.cpp`, `resolve_posix.cpp`), each adding its own hooks and
poller (`net/linux/`: `linux_hooks.cpp`, `poller_epoll.cpp`; `net/macos/`: `macos_hooks.cpp`, `poller_kqueue.cpp`, Phase 26 Day 326). Windows has its own in `engine/net/windows/` (Phase 20 Day 278: `socket_windows.cpp`, `wsa_error.cpp`,
`poller_select.cpp`, `resolve_windows.cpp`; the Winsock headers only through `windows/winsock.h`). A new platform adds `engine/net/<platform>/` and selects it in
`engine/net/CMakeLists.txt`. Only these
directories may call the operating system's socket API; the gate `net_access_check` fails on a socket call anywhere else.

## Rules every backend follows

- **Non-blocking, not inherited, from creation.** Every socket, including an accepted one, is non-blocking and not inherited by child processes before
  the call returns (Linux: `SOCK_NONBLOCK | SOCK_CLOEXEC`, `accept4`; where a flag cannot be set at creation, set it before returning and never leave a window in
  which another thread could `fork` and exec with the descriptor).
- **Errors are values.** Map the platform's code to `net::ErrorCode` with one table (`ErrorFromErrno` on POSIX, one for Winsock) and keep the number in
  `Status::OsError()`. The table has a test that runs from a list of numbers. Never throw, never abort on a bad socket.
- **Would-block is a result.** A call that cannot finish returns `WouldBlock` and has done nothing; a connect under way is `WouldBlock` too.
  An interrupted call is retried inside the backend and never reaches the caller.
- **No signals.** A write to a vanished peer reports `Closed` or `Reset`; `SIGPIPE` is never raised (`MSG_NOSIGNAL`, or `SO_NOSIGPIPE` set at creation).
- **The end of a stream is `Closed`**, with a count of zero. A zero-length datagram is a result (`Ok`, zero).
- **Families.** An IPv6 socket maps IPv4 endpoints; an IPv4 socket unmaps an IPv4-mapped endpoint and refuses other IPv6 ones; an IPv4-mapped peer
  is reported as IPv4. A scope name (`fe80::1%eth0`) is turned into an interface index here (`if_nametoindex`; `ConvertInterfaceNameToLuid` on Windows).
- **Misuse is `Invalid`.** A closed, moved-from or never-opened socket answers `Invalid` to every call.
- **No handle leaks.** `Close` releases the handle even when the system reports an error, so it is never closed twice.
- **Conformance.** Run `tests/net/socket_test.cpp` (loopback, label `network`) and the error-table test against the new backend. A machine that cannot
  bind loopback skips with its reason.

## Platform notes

- **Windows (Phase 20 Day 278, `engine/net/windows/`):** Winsock 2.2, started once per process by the first socket, poller or resolution
  (`windows::EnsureWinsock`, a `CallOnce`); no `WSACleanup`: the process's exit releases it, and a cleanup while the resolver or reactor thread may be inside
  Winsock would pull the library from under them. `WSASocketW` with `WSA_FLAG_NO_HANDLE_INHERIT` (not inherited from creation), then `ioctlsocket(FIONBIO)`;
  an accepted socket takes the listener's "not inherited" (seen on Day 278, so there is no window in which a child could get it) and gets both flags set
  again anyway (`SetHandleInformation`, `FIONBIO`). `WSAGetLastError` is mapped through one table (`wsa_error.cpp`, tested from a list of
  numbers); `closesocket`; `SOCKET` is an unsigned handle, held in `NativeHandle()` as an `i64` (`INVALID_SOCKET` is -1 there). No signal for a vanished
  peer, so no flag. `MSG_TRUNC` does not exist: a cut datagram fails with `WSAEMSGSIZE` (MessageTooLarge, the rest dropped). A UDP socket has
  `SIO_UDP_CONNRESET` off, or an ICMP "port unreachable" for an earlier datagram would fail the next receive with `WSAECONNRESET`. Windows' defaults are
  changed to the interface's at creation: an IPv6 socket is dual stack (`IPV6_V6ONLY` off; Windows' default is on), and every socket has an exclusive port
  (`SO_EXCLUSIVEADDRUSE`; `SO_REUSEADDR` is never set, see `SetReuseAddress`); a bind refused with `WSAEACCES` (a port held exclusively) is AddressInUse.
  A connect to a **loopback** address turns off SYN retransmission (`SIO_TCP_INITIAL_RTO`), because Windows answers a reset to its SYN by sending the SYN
  again and a refused loopback connect would take two seconds; other destinations keep that behavior. A full accept queue refuses at once on Windows
  either way (it answers a reset where Linux drops the SYN and the client tries again): of 300 loopback connects to a listener with a backlog of 1 that
  accepted 20 every 100 ms, 299 were refused with the setting and 291 without it (measured on Day 278). A non-blocking `send` takes the whole buffer when
  the send buffer has any room (the kernel keeps the caller's bytes), so a large write is rarely partial. Scope names go through `if_nametoindex`
  (`iphlpapi`). Libraries: `ws2_32` and `iphlpapi`, both part of Windows.
- **macOS, as built (Phase 26 Day 326, `engine/net/macos/`):** the POSIX backend with macOS hooks. No `accept4` and no `SOCK_NONBLOCK`/`SOCK_CLOEXEC`: a
  socket is created (or accepted) and gets `FD_CLOEXEC`, `O_NONBLOCK` and `SO_NOSIGPIPE` before the call returns, closed if one is refused. **The one window:**
  between `socket` (or `accept`) and `FD_CLOEXEC`, another thread that forks and execs passes the descriptor to its child; macOS cannot close it for a plain
  socket. The engine starts children only for death tests; an application that forks while sockets open should spawn with `POSIX_SPAWN_CLOEXEC_DEFAULT`.
  `SO_NOSIGPIPE` in place of `MSG_NOSIGNAL` (the send flags are 0). Datagrams are received with `recvmsg` and its `MSG_TRUNC` result flag on both
  POSIX systems (`recvfrom`'s `MSG_TRUNC` input flag, which returns the real size, is Linux's only). A send on a connection the peer is tearing down
  fails with `EPROTOTYPE`, read as Closed (the `SendErrorCode` hook; the errno is kept). `ENOTSUP` (45) and `EOPNOTSUPP` (102) are both Unsupported. Keep-alive's idle time is
  `TCP_KEEPALIVE`. A refused connect can land between `FinishConnect`'s `SO_ERROR` and `getpeername` under load (which then fails with `EINVAL`): the
  pending error is read again, and with none an `EINVAL` is Reset. A connection the peer reset before it was accepted refuses the socket options
  (`EINVAL`): it is reported as aborted, which `Accept` skips. **An `accept` that fails for lack of a descriptor drops the waiting connection** (Linux leaves it queued): the listener's
  back-off still recovers, with the next connection. Name resolution is the shared `getaddrinfo` code, with no difference seen. Loopback moves sent bytes
  to the peer after `send` returns, so a socket found full can become writable a moment later.
- **Android and iOS (Phase 27):** the same POSIX backend; the application lifecycle closes or suspends sockets when it moves to the background, which the
  transports above must survive (Phase 27 lists it); no raw sockets.
- **Consoles (Phase 28):** the vendor's BSD-style socket API behind the same interface; certification rules on listening sockets and on the ports in use.

## Operations

### `OpenTcp`

A non-blocking, non-inherited stream socket of the family; remember the family in the `Socket`.

### `OpenUdp`

The same for a datagram socket.

### `Bind`

Convert the `Endpoint` for the socket's family (map or unmap IPv4, resolve a scope name) and bind; port 0 asks the system for a free port.

### `Listen`

Start listening on a stream socket; refuse a datagram socket with `Invalid`.

### `Accept`

Accept one pending connection with the flags above; skip a connection that was aborted before it was accepted; report the peer, unmapped.

### `Connect`

Start a non-blocking connect: success when it is made at once, `WouldBlock` when it is under way (`EINPROGRESS`), the mapped error otherwise. A datagram socket
"connects" at once.

### `FinishConnect`

After the socket became writable: read the pending error; if there is none, check that a peer exists, else the connect is still under way (`WouldBlock`).

### `Send`

Write what the socket takes; a partial write is a result; `WouldBlock` with zero when it takes nothing.

### `Receive`

Read what has arrived; zero bytes on a stream is the end of the stream and reports `Closed`; an empty buffer is `Invalid`.

### `SendTo`

Send one datagram to the endpoint, converted for the family; a datagram above the limit is `MessageTooLarge`.

### `ReceiveFrom`

Receive one datagram and its sender; if the datagram was larger than the buffer it is dropped and reported as `MessageTooLarge`.

### `Shutdown`

Close one or both halves of a connection; `NotConnected` when there is none.

### `Close`

Release the handle; a socket that is not open answers `Invalid`.

### `LocalEndpoint`

The bound address (after a bind to port 0, the chosen port).

### `PeerEndpoint`

The connected peer, `NotConnected` when there is none.

### `SetReuseAddress`

Allow binding a port that has connections waiting to expire. (Not the Windows `SO_REUSEADDR`, which allows hijacking: Windows sockets are opened with
`SO_EXCLUSIVEADDRUSE`, which is "off"; "on" clears it, which is Windows' own default, where a port whose old connections wait to expire can be bound again.)

### `SetNoDelay`

Turn off Nagle's delay on a stream socket; `Invalid` on a datagram socket.

### `SetKeepAlive`

Turn keep-alive probes on or off.

### `SetKeepAliveTiming`

Set the idle time, probe interval and probe count of keep-alive on a stream socket (Linux `TCP_KEEPIDLE`/`TCP_KEEPINTVL`/`TCP_KEEPCNT`; macOS `TCP_KEEPALIVE` for the
idle time; Windows 10 1709 and later have the three Linux options, in seconds, and the backend uses them rather than the older `SIO_KEEPALIVE_VALS`, which takes
milliseconds and has no count).

### `SetLinger`

`SO_LINGER`: on with 0 seconds makes the close abortive (a reset is sent, unsent data dropped); off restores the default.

### `SetReceiveBuffer`

Request a receive buffer and report what the system granted.

### `SetSendBuffer`

Request a send buffer and report what the system granted.

### `SetDualStack`

For an IPv6 socket: `on` accepts IPv4 peers as mapped addresses (`IPV6_V6ONLY` off); `Invalid` for an IPv4 socket.

### `SetBroadcast`

Allow sending to broadcast addresses; off for every new socket.

## The poller

`net::Poller` (`engine/net/poller.h`) is the one platform class of the readiness code: the reactor (`net/reactor.cpp`), the timers, the cancel logic and the
asynchronous operations are neutral and sit on it. A new platform implements these four functions in `engine/net/<platform>/` (Linux: `linux/poller_epoll.cpp`; macOS: `macos/poller_kqueue.cpp`).

- **One-shot, by level.** A socket is armed for one notification and reported once; if it is ready when armed it is reported at once, so a registration after the
  event is not lost. After a report the socket stays known to the poller but is disarmed until armed again (the reactor re-arms the direction that still has a waiter).
- **`Arm` and `Disarm` run while another thread is in `Wait`**, and `Wake` makes a `Wait` in progress (or the next one) return. Hangup and error are reported as readiness bits;
  the operation that follows reports the real error.
- **The timeout of `Wait` never ends early** (round up to the system's resolution).

### `Arm`

Add the socket or modify its registration to one notification of the given interest (read, write or both); epoll: `EPOLL_CTL_MOD` with `EPOLLONESHOT`, falling back to
`EPOLL_CTL_ADD`. Windows: check the handle (`getsockopt(SO_TYPE)`, as `epoll_ctl` refuses a closed descriptor), add or update the socket in the poller's own armed set
under its lock, and wake a wait in progress so that its next copy has it. macOS: kqueue keeps reading and writing as two filters; each wanted direction is
added `EV_ONESHOT` (checked by level when added) and an unwanted one deleted, with `EV_RECEIPT` so a missing filter's delete is not an error and a closed
descriptor's add is (`EBADF`, Invalid). The direction that did not fire stays armed until it does or the socket is armed again; the reactor arms what
is still waited for after every report and ignores a report nobody waits for.

Windows uses `select`, not `WSAPoll` (the Day 266 plan; changed on Day 278 after a measurement): `WSAPoll` reports `POLLHUP` for the peer's half-close whatever was
asked for, so a socket waited on only for writing with a full send buffer would make every poll return at once; `select` reports it for writing only when it can be
written or was reset, and the end of the stream and a reset make it readable, which is epoll's behavior. The set is built for each wait from the armed set (an
`fd_set` of the needed size: Winsock reads `fd_count` entries, so a set larger than `FD_SETSIZE` is laid out the same way); a socket armed for writing is also in
the exception set, where a failed connect appears. IOCP would need a different shape, see the spec.

### `Disarm`

Forget the socket; ignore one that is unknown or already closed (macOS: delete both filters; closing a descriptor removes its filters). Windows: remove it from the armed set; a wait in progress may still see it, and its report is dropped
(each registration carries a generation, so a new socket that reuses a closed handle's value is not reported for the old one).

### `Wait`

Block for ready sockets, a wake-up or the timeout, and report the ready sockets (not the wake-up) with their readiness bits. Windows: `select` over a copy of the armed
set taken under the lock, the timeout rounded up to a microsecond. A handle in the copy that is closed before or during `select` (the reactor disarms and closes
from another thread while it waits) makes `select` fail with `WSAENOTSOCK`; that is not a broken poller: a closed handle still armed in the same registration is
reported once as an error, a disarmed one is dropped, and the next wait takes a new copy (`tests/net/poller_test.cpp`, found by the Day 278 review). macOS: `kevent`, the
timeout in nanoseconds and at most a million seconds per wait (kevent refuses centuries); a socket's two filters in one wait are merged into one event;
`EV_EOF` on the write filter is a hangup, and with a pending error (`fflags`) an error.

### `Wake`

Make a wait return: an `eventfd` write on Linux; on macOS `NOTE_TRIGGER` on an `EVFILT_USER` event of the same kqueue (`EV_CLEAR`); on Windows, which has no `eventfd`, one byte from a UDP socket connected to a second one bound to loopback, which
the wait watches for reading and drains.

## Name resolution

`net::SystemResolve` (`engine/net/resolver.h`) is the one place a host name becomes endpoints; the thread that calls it, the queue, the timeouts and the cancel logic are neutral
(`resolver.cpp`, `resolve.cpp`). A new platform implements the one function in `engine/net/<platform>/` (POSIX: `posix/resolve_posix.cpp`, `getaddrinfo`).

- **Bounded and deduplicated:** at most `EndpointSet::kCapacity` endpoints, in the system's order, an address once; a system that returns nothing is `NotFound`.
- **Errors by meaning:** a name that does not exist is `NotFound`, a name server that did not answer is `TimedOut`, and the rest map to `Unreachable`, `LimitExceeded`, `Unsupported` or `Other`.
- **It blocks and cannot be cancelled** (Windows: `GetAddrInfoExW` could be, and may be used to; the interface does not rely on it): the resolver thread absorbs that, and a resolution whose coroutine gave up is finished and dropped.

### `SystemResolve`

Resolve a name and a port to endpoints with the operating system's resolver, asking for stream sockets so that each address appears once. Windows: `GetAddrInfoW`
with the name in UTF-16 (the ANSI `getaddrinfo` would pass an internationalized name through the process's code page); its result codes (`WSAHOST_NOT_FOUND`,
`WSATRY_AGAIN`, ...) map to the same meanings as POSIX's `EAI_` codes (`windows::ErrorFromResolveError`).

