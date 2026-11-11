# Socket layer: what a platform backend implements

`net::Socket` (`engine/net/socket.h`) is shaped so Windows, macOS, Android, iOS and the consoles can each implement it without changing `Address`,
`Endpoint`, `Status` or any caller. This list is kept in step with the header by the test `net_backend_lists`: every function whose documentation
carries a `Backend:` label has a `###` section here, and every section names such a function. Design and reasons:
`docs/superpowers/specs/2026-11-11-networking-design.md`.

Linux and macOS share the POSIX backend in `engine/net/posix/` (`socket_posix.cpp`, `posix_error.cpp`), each adding its own hooks (`net/linux/linux_hooks.cpp`;
`net/macos/` when Phase 26 reaches the network). A new platform adds `engine/net/<platform>/` and selects it in `engine/net/CMakeLists.txt`. Only these
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

- **Windows (Phase 20 Day 278):** Winsock 2 (`WSAStartup` once, before the first socket, `WSACleanup` at shutdown), `WSASocketW` with `WSA_FLAG_NO_HANDLE_INHERIT`
  and `ioctlsocket(FIONBIO)`; `WSAGetLastError` mapped through a table (`WSAEWOULDBLOCK`, `WSAECONNRESET`, ...); `closesocket`; `SOCKET` is an unsigned
  handle, held in `NativeHandle()` as an `i64`; no `accept4`, so set the flags after `accept`; `MSG_TRUNC` does not exist, use `WSAEMSGSIZE` from `recvfrom`
  (the datagram is cut and the error is reported).
- **macOS (Phase 26):** the POSIX backend; no `accept4`, no `SOCK_NONBLOCK`: create with `socket`, then `fcntl` the two flags immediately; `SO_NOSIGPIPE`
  instead of `MSG_NOSIGNAL`; `MSG_TRUNC` is not available on `recvfrom` for datagrams, use `recvmsg` and its `MSG_TRUNC` result flag.
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

Allow binding a port that has connections waiting to expire. (Not the Windows `SO_REUSEADDR`, which allows hijacking: use `SO_EXCLUSIVEADDRUSE` for off and leave
"on" as the default there.)

### `SetNoDelay`

Turn off Nagle's delay on a stream socket; `Invalid` on a datagram socket.

### `SetKeepAlive`

Turn keep-alive probes on or off.

### `SetReceiveBuffer`

Request a receive buffer and report what the system granted.

### `SetSendBuffer`

Request a send buffer and report what the system granted.

### `SetDualStack`

For an IPv6 socket: `on` accepts IPv4 peers as mapped addresses (`IPV6_V6ONLY` off); `Invalid` for an IPv4 socket.

### `SetBroadcast`

Allow sending to broadcast addresses; off for every new socket.
