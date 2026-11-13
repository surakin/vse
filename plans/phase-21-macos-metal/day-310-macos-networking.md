# Phase 21 / Day 310 — Networking on macOS: the Socket Hooks, the kqueue Poller, Name Resolution

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 309, and the main line's Phase 20 closed (Day 281: the socket, poller and transport interfaces settled).

Added on 2026-11-12 (Day 294's inventory, the user's decision): `engine/net` (Phase 20) arrived on the main line after this phase was planned. It is
window-free, but unlike the other modules of Day 294 it has platform code: Linux's backend is `net/posix/` (shared) plus `net/linux/` (hooks and the
`epoll` poller). This day adds `net/macos/` against the interface as Phase 20 leaves it. If Phase 20 has not closed by the time this day comes, it waits,
and the rest of the phase does not depend on it.

## Tasks

- [x] **Inventory at the start:** `engine/net/BACKEND.md` as Phase 20 left it (operations, the poller, name resolution), `net/linux/` and `net/posix/`
      (which calls are Linux's own), `tests/net` and its `network` label, and what the main line's closing day recorded as open for macOS.
- [x] **`net/macos/` socket hooks** (the POSIX backend's counterpart of `net/linux/linux_hooks.cpp`): no `accept4` and no `SOCK_NONBLOCK`/`SOCK_CLOEXEC`
      (create, then `fcntl` both flags at once, with the window documented); `SO_NOSIGPIPE` on every socket instead of `MSG_NOSIGNAL`; datagram truncation
      through `recvmsg`'s `MSG_TRUNC` flag as macOS reports it; keep-alive timing through `TCP_KEEPALIVE` (idle), `TCP_KEEPINTVL`, `TCP_KEEPCNT`; the errno
      table for Darwin's codes (`ENOTSUP` and `EOPNOTSUPP` differ, `EPROTOTYPE` on a dead connection's send).
- [x] **The poller on `kqueue`** (`net/macos/poller_kqueue.cpp`, the counterpart of `poller_epoll.cpp`): `Arm`, `Disarm`, `Wait`, `Wake` (an
      `EVFILT_USER` event), read and write interest as two filters, edge or level as the interface states; closing an armed socket.
- [x] **Name resolution:** (nothing differs: the shared `getaddrinfo` code passes `resolve_test` on macOS) `SystemResolve` through `getaddrinfo` as Linux's `resolve_posix.cpp`, with whatever macOS returns differently (`AI_ADDRCONFIG`
      without a configured address, IPv4-mapped results) recorded and handled.
- [x] **Build and suites:** `engine/net` and `tests/net` in plain, asan and tsan; `net_backend_lists` passes; the real-socket tests (label `network`)
      pass or skip with a reason when loopback is unavailable; the reactor and timer tests under TSan are the concurrency checks of the day; the
      simulated transport and codec tests match Linux's golden bytes.
- [x] **Documentation:** `engine/net/BACKEND.md` "macOS, as built"; the networking guide's platform list; `docgen lint` zero.
- [x] **Runs:** `ran 7033 of 7413 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1685 s` (only the known stale DXIL test, in three flavors, and the known flaky `Throughput scales with participant count`, which passed 3 of 3 reruns). `scripts/test-phase.sh --affected --flavor all` (quote).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: descriptors leaking into children in the `socket`-then-`fcntl` window, a `kqueue` and its
      events after a socket is closed, `SIGPIPE` on every send path.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (remove the Day 294 entry that points here); commit and push.

**Done:** `engine/net` and `tests/net` build and pass on macOS in three flavors and both unity modes, with `net/macos/` documented in `BACKEND.md`.

## Implementation notes

- **Inventory:** Phase 20 left `engine/net` with the POSIX backend in `net/posix/` and Linux's hooks and `epoll` poller in `net/linux/`; the hooks were
  `OpenSocketFd`, `AcceptFd`, `SendFlags` and `TruncationFlag`. Nothing of `tests/net` was Linux-only beyond the flags test (`fcntl`, shared) and the
  descriptor-limit tests (POSIX). The main line's Day 281 recorded nothing open for macOS.
- **`net/macos/macos_hooks.cpp`:** a socket is created (or accepted) and gets `FD_CLOEXEC`, `O_NONBLOCK` and `SO_NOSIGPIPE` before the call returns. The
  window between `socket` and `FD_CLOEXEC` cannot be closed on macOS; it is documented, with `POSIX_SPAWN_CLOEXEC_DEFAULT` for an application that forks
  (the engine spawns only death-test children). A connection reset before it is accepted refuses the options with `EINVAL`: it is reported as aborted,
  which `Accept` skips. `SendErrorCode` (a new hook) reads `EPROTOTYPE` from a send as Closed, keeping the errno.
- **Shared POSIX changes:** datagrams are received with `recvmsg` and its `MSG_TRUNC` result flag on both systems (the `TruncationFlag` hook is gone:
  `recvfrom`'s input flag is Linux's only); `ENOTSUP` maps to Unsupported where it differs from `EOPNOTSUPP`; `FinishConnect` reads `SO_ERROR` again
  when `getpeername` fails with something other than `ENOTCONN` (a refusal that lands between the two calls, seen under load), and `EINVAL` with no
  pending error is Reset.
- **`net/macos/poller_kqueue.cpp`:** one-shot read and write filters (added with `EV_ONESHOT`, checked by level when added; an unwanted direction's filter
  deleted; `EV_RECEIPT` for each change's result), an `EVFILT_USER` event to wake, a socket's two filters merged into one event, `EV_EOF` on the write
  filter a hangup and with a pending error an error, the timeout capped at a million seconds (kevent refuses more), capacity 0 Invalid.
- **What macOS does differently, found by the suites:** an `accept` that fails for lack of a descriptor drops the waiting connection (Linux leaves it
  queued): `listener_test` connects again on macOS and the back-off accepts it. Loopback moves sent bytes after `send` returns, so a full socket can
  become writable a moment later: the half-close test fills until a pause adds no room, retries until a writer really waits, and accepts a writer that
  ended with room after the half-close.
- **Build:** `engine/net`, `tests/net`, `tools/net_probe` and `game/net_demo` on macOS; phase 20's macOS flavors are plain, asan and tsan. Under load the
  network tests ran clean 10 times in a row after the fixes (the first run before them failed).
- **Docs:** `engine/net/BACKEND.md` ("macOS, as built", the poller's kqueue notes), `poller.h`, `docs/guide/net.md` ("On macOS"); the reference
  regenerated, `docgen lint` at zero.

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, 7 fixed, 1 declined.

1. **A connection reset before `accept` made the accept Invalid** (`EINVAL` from the options on macOS). Fixed: reported as aborted and skipped. Test
   (twenty resets before accept: every one was Invalid without the fix).
2. **The `EPROTOTYPE` rewrite was in the shared send, on every system, and replaced the errno.** Fixed: the `SendErrorCode` hook, macOS only, the errno
   kept (test).
3. **`FinishConnect`'s extra `SO_ERROR` read ran on every poll.** Fixed: only when `getpeername` fails with something other than `ENOTCONN`.
4. **`EINVAL` from `getpeername` with no pending error was Invalid.** Fixed: Reset.
5. **The half-close test could pass without testing.** Fixed: it fills and waits again until a writer really waits, and requires that.
6. **A wait with no room returned at once on macOS** (Invalid on Linux). Fixed: Invalid (test).
7. **The `EPROTOTYPE` case belonged in the hooks.** Fixed with 2.
8. **The merge of a socket's two filters is quadratic in the batch.** Declined: at most 2016 comparisons per full batch of 64, nothing next to the
   `kevent` call.
