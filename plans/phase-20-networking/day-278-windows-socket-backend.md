# Phase 20 / Day 278 — Windows Socket Backend (Windows PC)

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 268-277; Phase 13 (the Windows platform layer).

The Windows implementation of `BACKEND.md`, done on the Windows PC, proving the interface fits a second backend. Planned as the day the Windows PC builds and tests all of Days 267-277.

## Tasks

- [x] **Winsock backend** in `engine/net/windows/` (`WSAStartup` once through the platform layer, `WSA*` calls only there; `winsock2.h` only through `engine/net/windows/winsock.h` as `windows.h` goes through `win32.h`): non-blocking sockets (`ioctlsocket`), error mapping from `WSAGetLastError` through the Day 267 table, `SO_EXCLUSIVEADDRUSE` instead of `SO_REUSEADDR` semantics, dual stack, `WSAECONNRESET` on UDP handled (ignored via `SIO_UDP_CONNRESET`).
- [x] **Readiness:** the Day 266 decision (IOCP via `AcceptEx`/`WSARecv` overlapped, or `WSAPoll` first) implemented behind the same `Reactor` interface and awaitables; the same race rules and tests as Day 269.
- [x] **Resolver:** `getaddrinfo` on the resolver thread; numeric hosts without the OS.
- [x] **Gate and docs:** `net_access_check` Windows entries; `BACKEND.md` Windows notes; `docs/guide/windows.md` and `BUILD.md` updated if a prerequisite changes (none expected: `ws2_32` ships with the OS).
- [x] **Build and test on the Windows PC:** `windows-msvc` (plain, asan) and `windows-clang-cl`, both unity modes; the whole of `tests/net` including the conformance suite on TCP and UDP and the codec golden bytes (the quantization determinism check of Day 272); fix what breaks or list it in `KNOWN-ISSUES.md`.
- [x] **Records:** every Day 267-277 entry "open until the Windows PC runs it" is cleared or restated with the failure.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Windows backend passes the same `tests/net` as Linux on both Windows compilers, or the differences are listed with their reasons.

## Outcome (2026-11-12, Windows PC)

- **The backend** (`engine/net/windows/`): `winsock.h` (the one include of `<winsock2.h>`, `<ws2tcpip.h>`, `<mswsock.h>`, `<mstcpip.h>` and
  `<iphlpapi.h>`, after `win32.h`, inside the unity enable/disable pair), `wsa_error.h`/`.cpp` (the Winsock table and the `GetAddrInfoW` table, no
  Windows header in the `.h`, so the test needs none), `windows_net.h` (`EnsureWinsock`, a `CallOnce` around `WSAStartup` 2.2 called by the first
  socket, poller or resolution; no `WSACleanup`, the reason in `BACKEND.md`; the sockaddr conversion, the Windows twin of the POSIX one),
  `socket_windows.cpp`, `poller_select.cpp`, `resolve_windows.cpp`. Links `ws2_32` and `iphlpapi`, both part of Windows (no `BUILD.md` change).
- **Sockets:** `WSASocketW` with `WSA_FLAG_NO_HANDLE_INHERIT`, then `FIONBIO`; an accepted socket takes the listener's "not inherited" (probed) and
  gets both again. The interface's defaults over Windows' own: IPv6 sockets dual stack (`IPV6_V6ONLY` off), every port exclusive
  (`SO_EXCLUSIVEADDRUSE`; `SetReuseAddress(true)` clears it, never `SO_REUSEADDR`); a bind refused with `WSAEACCES` is AddressInUse. UDP:
  `SIO_UDP_CONNRESET` off (`tests/net/windows/socket_flags_windows_test.cpp` checks a datagram to a closed port does not reset the socket);
  `WSAEMSGSIZE` is the cut datagram. Keep-alive timing with `TCP_KEEPIDLE`/`TCP_KEEPINTVL`/`TCP_KEEPCNT` (Windows 10 1709 and later), not
  `SIO_KEEPALIVE_VALS`, so the probe count is supported. Scope names through `if_nametoindex`.
- **Measured and changed (the plan said `WSAPoll`):** `WSAPoll` reports `POLLHUP` (0x2) for the peer's half-close whatever was asked for, and never
  `POLLRDNORM`; a reset is `POLLHUP | POLLERR` (0x3, with `POLLWRNORM` 0x13). A socket waited on only for writing with a full send buffer after the
  peer's half-close makes every poll return at once (`The peer's half-close does not wake a coroutine that waits to write` failed). `select` reports
  that socket for writing only when it can be written or was reset; the end of the stream and a reset make it readable (all six cases probed). The
  poller is `select` over `fd_set`s sized to the armed set (`poller_select.cpp`), with a wake-up between two loopback UDP sockets; the spec, `BACKEND.md`
  and the guide say so. IOCP stays the replacement if Day 281's measurement says so.
- **Refusals on loopback:** Windows answers a reset to its SYN by sending the SYN again (a refused loopback connect took 2,019-2,070 ms, measured),
  beyond the 2 s the tests allow; a connect to a loopback address turns SYN retransmission off (`SIO_TCP_INITIAL_RTO`), which makes it immediate.
  Other destinations keep Windows' behavior (`KNOWN-ISSUES.md`).
- **A non-blocking `send` takes a whole 8 MiB write** when the send buffer has any room: the "taken in parts" check of `socket_test.cpp` is POSIX-only,
  with the reason, and its other checks (every byte, in order) run.
- **Resolver:** `GetAddrInfoW` with the name in UTF-16 (`os::windows::Utf8ToUtf16`; an invalid UTF-8 name is Invalid); numeric hosts never reach it
  (`resolve.cpp`).
- **Tests on Windows:** the whole of `tests/net` builds on both compilers: `socket_test`, `reactor_test`, `resolve_test`, `listener_test`, `tcp_test`,
  `udp_loopback_test`, `tcp_loopback_test` (the conformance suite on TCP and UDP over real loopback), the codec's golden bytes, plus
  `wsa_error_map_windows_test.cpp`, `windows/socket_flags_windows_test.cpp` and the review's `poller_test.cpp` (portable). The two descriptor-limit
  tests of `listener_test.cpp` skip on Windows (no per-process limit to lower); `tcp_test`'s thousand streams runs without the `rlimit` step.
- **Found by the full run: the five-thousand-connection listener test hung** under the scoped run's load (674 s, killed). The connect loop failed with
  `WSAEADDRINUSE` (10048): both sides closed at once, so both stayed in TIME_WAIT (32,442 entries seen for 16,384 ephemeral ports, kept two minutes),
  and the accept loop waited for ever for the connections that never came. Fixed in the test: every accept and the client's wait have a 20 s
  timeout (a failing client ends the test with a failure), and the client closes only after the server's close, so TIME_WAIT stays on the listener's
  port (24 copies in two rounds of twelve then passed, where twelve had hung).
- **Gates:** `net_access_check` names `select`, `FreeAddrInfoW`, `FreeAddrInfoExW`, `WSAAddressToStringW` and `WSAStringToAddressW` (a planted line
  for two of them); `platform_neutral_check` rule 4 admits `engine/net/windows/` and `tests/net/windows/` and knows `mswsock.h`, `mstcpip.h`,
  `iphlpapi.h`, `netioapi.h` and `ws2ipdef.h`; `module_deps_check` lets `net/windows/` and `tests/net/windows/` use `os/windows/` (`ARCHITECTURE.md`
  section 4); the docs samples link `ws2_32` and `iphlpapi` on Windows.
- **Docs:** `engine/net/BACKEND.md` (the Windows notes, the poller, `SetReuseAddress`, `SetKeepAliveTiming`, the resolver), the `Backend:` labels of
  `socket.h`, `poller.h` and `resolver.h`, `docs/guide/net.md` ("On Windows"), `docs/guide/windows.md`, the networking spec (the measured change);
  `docs/reference/` regenerated on the Windows PC.
- **Builds:** `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity` and `build-windows-clang-cl-nounity` clean (warnings are errors). `net_tests`
  (206 cases) and `net_tests_sockets` (73 cases: 71 passed, and the 2 descriptor-limit tests skip) pass in all four trees, plain, and asan in the two
  MSVC trees.
- **Runs (Windows has no tsan):** the first scoped run, `ran 4802 of 5079 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: excluded) in 384 s`, failed only the twelve locked-session display tests and the known
  `Throughput` flake (3 of 3 on rerun), after the listener test's fix; the run after the review's fix: `ran 4806 of 5083 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: excluded) in 376 s`, failed only the twelve
  locked-session display tests.
  `ctest -L docs` 7 of 7.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, 2 fixed, 2 declined.

- **Fixed: a socket closed while the reactor waits broke the reactor for good.** The Windows wait polls a copy of the armed set; `Reactor::Close`
  disarms and closes from another thread, and a handle closed after the copy is taken made `select` fail with `WSAENOTSOCK`, which `Wait` returned as
  a failure, and the reactor ends every wait on a failed wait. Now a closed handle still armed in the same registration is reported once as an
  error and a disarmed one is dropped. Regression tests `tests/net/poller_test.cpp` (a waiter against 400 rounds of arm, disarm and close; a socket
  closed without being disarmed): both failed before the fix (264 to 289 failed waits in three runs) and pass after it (five runs plain, one asan).
- **Fixed:** two comments of `poller.h` still named `WSAPoll` and the replaced `poller_wsapoll.cpp`.
- **Declined: turning off SYN retransmission on loopback also refuses a full accept queue at once.** Measured: 300 loopback connects to a listener with
  a backlog of 1 that accepted 20 every 100 ms gave 299 refusals with the setting and 291 without it, so Windows refuses a full queue either way (it
  answers with a reset where Linux drops the SYN); keeping a refused local connect at two seconds costs more. Written in `BACKEND.md` and
  `KNOWN-ISSUES.md`.
- **Declined: mapping `select`'s results back costs O(armed x ready)** (each copy entry looked up in the three result sets). `select` itself is O(n),
  and the thousand-connection echo test passes; Day 281's measurement decides whether the poller (or IOCP) needs better, recorded in `KNOWN-ISSUES.md`.

## Notes

- This day is done on the Windows PC; if no Windows machine is available when the phase reaches it, it stays open and Day 281 records it as open (as Phase 19 did).
