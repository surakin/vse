# Phase 20 / Day 277 — The TCP Transport & the Cross-Transport Conformance Pass

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 271, 273.

The same interface on TCP, so a store over the network, tools and remote debugging can use it.

## Tasks

- [ ] **`net::TcpTransport`:** one stream per connection; a message is a frame (Day 271); the channel number rides in the frame header; every reliability kind maps to the stream (unreliable kinds degrade, flagged by `Supports`); a handshake frame exchanges protocol version and limits before any message is accepted; messages bounded; per-connection memory budget.
- [ ] **Events and statistics** through the Day 273 interface (RTT from the kernel where the platform offers it, else from an in-band ping).
- [ ] **Conformance:** the Day 273 suite passes on TCP over real loopback sockets (label `network`) and on a TCP-semantics simulation (stream model with the simulated network's latency), with the "unreliable may be lost" cases adapted by the transport's declared support.
- [ ] **One interface, three backends:** a test that runs the same scripted conversation (connect, 100 messages on 4 channels, close with a reason) against simulated, UDP and TCP transports and compares the application-visible event sequences.
- [ ] **Tests** (`tests/net/tcp_transport_test.cpp`): hostile peers (garbage before the handshake, a frame header declaring 4 GiB, a stall mid-frame, 10,000 half-open connections) stay within budgets and time out; 1,000 connections of mixed traffic under tsan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the TCP transport passes the same conformance suite as the simulated and UDP transports and hostile peers cannot exhaust it.
