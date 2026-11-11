# Phase 20 / Day 270 — Name Resolution on a Job, Listeners & Dual Stack

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 267-269.

Turning names into endpoints without ever blocking a frame, and accepting connections on IPv4 and IPv6.

## Tasks

- [ ] **`net::Resolve(scheduler, host, port, family_hint)`** awaitable: runs the OS resolver (`getaddrinfo`, in the Linux backend only) on a job executed by a non-participant resolver thread (a bounded queue, one thread, results delivered through a counter) so it never occupies a compute worker; bounded result count; a timeout (the OS call cannot be cancelled, so a timed-out resolve is abandoned and its result dropped safely); numeric hosts are parsed without calling the OS.
- [ ] **`net::Listener`**: bind + listen with a backlog from settings, `AcceptLoop` awaitable producing connected sockets, per-accept error handling (`EMFILE`/`ENFILE` back off instead of spinning, `ECONNABORTED` skipped), a cap on accepted-but-unhandled connections.
- [ ] **Dual stack:** a listener on `::` accepts IPv4-mapped clients where the OS allows (`IPV6_V6ONLY` off), reported honestly where it does not; clients try the resolved endpoints in order with a per-attempt timeout (Happy Eyeballs light: sequential, documented).
- [ ] **Tests** (`tests/net/resolve_test.cpp`, `listener_test.cpp`, label `network`): resolve `localhost` and numeric hosts; a hostile resolver result list is bounded; abandoned resolve at shutdown; accept 5,000 connections from a looping client; descriptor exhaustion is survived (limit lowered with `setrlimit` in a child process) and recovers; IPv6 loopback skipped with a reason where absent.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** names resolve off the frame path, listeners accept on IPv4 and IPv6 without spinning on errors, and every wait is cancellable.
