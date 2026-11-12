# Phase 26 / Day 326 — Networking on macOS: the Socket Hooks, the kqueue Poller, Name Resolution

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 325, and the main line's Phase 20 closed (Day 281: the socket, poller and transport interfaces settled).

Added on 2026-11-12 (Day 310's inventory, the user's decision): `engine/net` (Phase 20) arrived on the main line after this phase was planned. It is
window-free, but unlike the other modules of Day 310 it has platform code: Linux's backend is `net/posix/` (shared) plus `net/linux/` (hooks and the
`epoll` poller). This day adds `net/macos/` against the interface as Phase 20 leaves it. If Phase 20 has not closed by the time this day comes, it waits,
and the rest of the phase does not depend on it.

## Tasks

- [ ] **Inventory at the start:** `engine/net/BACKEND.md` as Phase 20 left it (operations, the poller, name resolution), `net/linux/` and `net/posix/`
      (which calls are Linux's own), `tests/net` and its `network` label, and what the main line's closing day recorded as open for macOS.
- [ ] **`net/macos/` socket hooks** (the POSIX backend's counterpart of `net/linux/linux_hooks.cpp`): no `accept4` and no `SOCK_NONBLOCK`/`SOCK_CLOEXEC`
      (create, then `fcntl` both flags at once, with the window documented); `SO_NOSIGPIPE` on every socket instead of `MSG_NOSIGNAL`; datagram truncation
      through `recvmsg`'s `MSG_TRUNC` flag as macOS reports it; keep-alive timing through `TCP_KEEPALIVE` (idle), `TCP_KEEPINTVL`, `TCP_KEEPCNT`; the errno
      table for Darwin's codes (`ENOTSUP` and `EOPNOTSUPP` differ, `EPROTOTYPE` on a dead connection's send).
- [ ] **The poller on `kqueue`** (`net/macos/poller_kqueue.cpp`, the counterpart of `poller_epoll.cpp`): `Arm`, `Disarm`, `Wait`, `Wake` (an
      `EVFILT_USER` event), read and write interest as two filters, edge or level as the interface states; closing an armed socket.
- [ ] **Name resolution:** `SystemResolve` through `getaddrinfo` as Linux's `resolve_posix.cpp`, with whatever macOS returns differently (`AI_ADDRCONFIG`
      without a configured address, IPv4-mapped results) recorded and handled.
- [ ] **Build and suites:** `engine/net` and `tests/net` in plain, asan and tsan; `net_backend_lists` passes; the real-socket tests (label `network`)
      pass or skip with a reason when loopback is unavailable; the reactor and timer tests under TSan are the concurrency checks of the day; the
      simulated transport and codec tests match Linux's golden bytes.
- [ ] **Documentation:** `engine/net/BACKEND.md` "macOS, as built"; the networking guide's platform list; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: descriptors leaking into children in the `socket`-then-`fcntl` window, a `kqueue` and its
      events after a socket is closed, `SIGPIPE` on every send path.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (remove the Day 310 entry that points here); commit and push.

**Done:** `engine/net` and `tests/net` build and pass on macOS in three flavors and both unity modes, with `net/macos/` documented in `BACKEND.md`.
