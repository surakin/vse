# Phase 20 / Day 281 — Settings, `net_probe`, Benchmarks, Docs, Closing Full Run & Phase Review

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 266-280.

Integrate the knobs, build the tools, measure against the Day 266 pass conditions, finish the documentation and close the phase.

## Tasks

- [ ] **Settings** `net.*` through `settings::Registry` with `env` and `flag` names (ports, `net.max_connections`, `net.max_frame_bytes`, `net.max_message_bytes`, timeouts, buffer sizes, `net.max_bandwidth_bytes_per_second`, handshake rate limits, simulated conditions for testing) and no ad hoc `getenv` or flag (`config_access_check`); `net::TransportConfig::FromSettings`.
- [ ] **`tools/net_probe`:** a command-line tool for manual checks (listen, connect, send a file, echo, ping with latency percentiles, resolve) over any transport; its own command line is allowed by the settings gate like other tools.
- [ ] **Benchmarks** (`tests/net/net_bench.cpp`, hidden `[.][net-bench]`, Release): loopback TCP and UDP-transport echo (messages per second at 64 B and 1 KiB, latency percentiles p50/p99/p999, CPU per connection at 1,000 connections), the simulated transport's throughput, the codec; results table in the spec against Day 266's pass conditions; misses are listed in `KNOWN-ISSUES.md` with options, not silently accepted.
- [ ] **Soak test** (`tests/net/soak_test.cpp`, label `network`, bounded to 60 s): 10,000 simulated connections with mixed traffic and impairments, memory and queue sizes steady, no leaks under asan, clean shutdown with traffic in flight.
- [ ] **Demo `game/net_demo`:** a headless server and clients in one process pair over each transport, exchanging a scripted conversation and the storage `AsyncLoad` of a small world over TCP, with a `--simulated` mode; run as ctests `net_demo_runs` (loopback, `network` label) and `net_demo_simulated_runs`.
- [ ] **Guides and tutorial:** `docs/guide/net.md` finished (addresses, sockets, the reactor, TCP, the UDP protocol with its diagrams, the Transport interface, the simulated transport and how higher layers test with it, settings, errors, the security notes, how a new transport or platform is added); `docs/tutorials/talking-over-the-network.md`; `docgen lint` at zero; `cmake --build build --target docs`; `ctest -L docs`.
- [ ] **Spec** updated to what was built with the measured tables and every deviation from Day 266, including the replication hooks list.
- [ ] **Windows PC:** the closing run there as well (`windows-msvc` plain and asan, `windows-clang-cl`), or recorded as open if Day 278 is.
- [ ] **Closing full run (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --all --flavor all` on Linux (and the Windows PC's flavors), plus `ctest -L docs` and the demos; quote the final `ran X of Y tests (phases ...)` line here; `CURRENT_PHASE` stays 20 until Phase 21's first day.
- [ ] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 266-281, from the commit before Day 266 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-20-networking.md` status, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 21 needs its day files before it starts), `KNOWN-ISSUES.md` sweep.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** networking is measured, documented, fuzzed and reviewed, runs on Linux and Windows, and Phase 20 is complete.
