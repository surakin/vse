# Phase 20 / Day 270 — Name Resolution on a Job, Listeners & Dual Stack

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 267-269.

Turning names into endpoints without ever blocking a frame, and accepting connections on IPv4 and IPv6.

## Tasks

- [x] **`net::Resolve(scheduler, host, port, family_hint)`** awaitable: runs the OS resolver (`getaddrinfo`, in the Linux backend only) on a job executed by a non-participant resolver thread (a bounded queue, one thread, results delivered through a counter) so it never occupies a compute worker; bounded result count; a timeout (the OS call cannot be cancelled, so a timed-out resolve is abandoned and its result dropped safely); numeric hosts are parsed without calling the OS.
- [x] **`net::Listener`**: bind + listen with a backlog from settings, `AcceptLoop` awaitable producing connected sockets, per-accept error handling (`EMFILE`/`ENFILE` back off instead of spinning, `ECONNABORTED` skipped), a cap on accepted-but-unhandled connections.
- [x] **Dual stack:** a listener on `::` accepts IPv4-mapped clients where the OS allows (`IPV6_V6ONLY` off), reported honestly where it does not; clients try the resolved endpoints in order with a per-attempt timeout (Happy Eyeballs light: sequential, documented).
- [x] **Tests** (`tests/net/resolve_test.cpp`, `listener_test.cpp`, label `network`): resolve `localhost` and numeric hosts; a hostile resolver result list is bounded; abandoned resolve at shutdown; accept 5,000 connections from a looping client; descriptor exhaustion is survived (limit lowered with `setrlimit` in a child process) and recovers; IPv6 loopback skipped with a reason where absent.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

## Outcome

New: `net::Signal` (cross-thread wake-up; `Reactor::WaitSignal`), `net::Resolver` (one thread, bounded ring queue, abandoned requests skipped, `Shutdown` cancels the queue), `net::SystemResolve` (getaddrinfo, `engine/net/posix/resolve_posix.cpp`), `net::Resolve`/`ConnectAny`/`ConnectTo` (sequential attempts with a per-attempt timeout), `net::Listener` (dual stack, descriptor-exhaustion back-off 5-200 ms, cap on unhandled connections via `Reserve`/`Handled`), `ErrorCode::NotFound`, `TimeBudget`. Documentation: `docs/guide/net.md`, `docs/reference/net.md`, `engine/net/BACKEND.md` ("Name resolution"), spec resolution row; `docgen lint` at zero (baseline updated), `ctest -L docs` 7 of 7.

Runs (Linux machine): plain `ran 2598 of 6811 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2020 of 6811`, tsan `ran 1696 of 6811` (the whole of the phase is in range because of the style pass). Only the two known X11 window tests failed (plain). GCC non-unity: `net_tests_sockets` builds clean and passes (49 test cases). `scripts/tidy.sh` on the changed files: no findings.

## Post-implementation code review

`/code-review high`: 10 findings, 9 fixed, 1 declined.

- Fixed (each with a regression test checked against the old code where practical): an abandoned resolve request was still looked up (the thread now skips it; test sleeps 500 ms before shutdown and expects one slow call); `Listener::Handled` could wrap the count below zero when called from two threads (now a compare-exchange loop; the test releases two threads together 20,000 rounds and fails against the old code); queue shutdown fired signals under the lock; embedded-NUL host names; `Sleep`/deadline overflow in the wait budget; the resolver flood was unbounded (queue cap); a hostile result list was unbounded (`EndpointSet`); `Resolve` did not mark the request abandoned when its wait failed; `Reserve` was private so a caller taking connections itself could not use the cap (made public).
- Declined: `Signal::Fire` racing with the destruction of its reactor: the contract (documented on `Signal`) is that the reactor outlives every signal fired into it, the same rule as for `CancelToken` users.

**Done:** names resolve off the frame path, listeners accept on IPv4 and IPv6 without spinning on errors, and every wait is cancellable.
