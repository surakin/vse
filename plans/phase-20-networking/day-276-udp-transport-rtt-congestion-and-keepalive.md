# Phase 20 / Day 276 — UDP Transport III: RTT, Congestion Control, Keep-alive & Timeouts

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 274-275.

Making the UDP transport a good network citizen: it must not flood a path, and it must notice when the other side is gone.

## Tasks

- [ ] **RTT estimation** (smoothed RTT and variance from acked packets, Karn's rule for retransmitted packets), exposed in the statistics; resend timeout derived from it with a floor and ceiling.
- [ ] **Congestion control:** a conservative, documented window-based algorithm (NewReno-style: slow start, congestion avoidance, halve on loss, reset on timeout) with pacing so a burst is not sent at once; an application cap (`net.max_bandwidth_bytes_per_second`) on top; the send queue reports backpressure as the window closes; unreliable messages are dropped (oldest first) before reliable ones are delayed. The algorithm is written down in the spec with its parameters and the reasoning.
- [ ] **Keep-alive and timeout:** an empty acknowledged packet when idle, a connection timeout, and the first-ack-after-silence rule; path MTU fixed at 1200 payload bytes with an optional probe left out (recorded).
- [ ] **Statistics:** per-connection RTT, jitter, loss, throughput, window, queue depth, retransmissions, duplicates, drops by reason.
- [ ] **Tests** (`tests/net/udp_congestion_test.cpp`): over the simulated network with a 1 Mbit/s, 40 ms path the throughput converges within 10 percent of the cap without sustained queue growth; two flows share a bottleneck within a factor of 2; loss bursts recover; the connection survives a 5-second blackout shorter than the timeout and closes with `TimedOut` after a longer one; RTT estimate within 10 percent over a jittery path; a deterministic regression trace pins the algorithm.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the UDP transport estimates RTT, keeps its sending rate under a documented congestion algorithm and a cap, and notices a dead peer, all measured on the simulated network.
