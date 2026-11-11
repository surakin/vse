# Phase 20 / Day 275 — UDP Transport II: Channels, Reliability & Fragmentation

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Day 274.

The part games need and TCP does not give: per-message reliability choices and large messages over small packets.

## Tasks

- [ ] **Channels:** unreliable, unreliable-sequenced (older messages dropped), reliable-ordered, reliable-unordered; each channel number independent so a stalled reliable-ordered channel never blocks another; per-channel send queues with caps and per-message priority within a packet budget.
- [ ] **Reliable delivery:** resend from the ack state (not a per-message timer alone), resend timeout from the RTT estimate (Day 276 refines), a message acknowledged exactly once, a bounded window of unacknowledged data per connection, **ordered delivery with a bounded reorder buffer** (a peer that opens a gap beyond the buffer is disconnected, not buffered forever).
- [ ] **Message packing:** several small messages share a packet up to the MTU; a message too large for one packet is fragmented (fragment id, count, index) and reassembled with a total-size limit declared up front, a per-connection reassembly memory budget, a timeout for incomplete messages, and rejection of overlapping or inconsistent fragments.
- [ ] **Tests** (`tests/net/udp_channels_test.cpp`): the conformance suite on the UDP transport over the simulated network at 0, 5, 30 and 60 percent loss with reordering and duplication; a 1 MiB message delivered intact in every impairment; a million random messages across channels with exactly-once ordered delivery on reliable channels; fragment bombs (declared 1 GiB, thousands of 1-fragment messages, overlapping fragments) stay within the budget; per-channel isolation under a stalled channel.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** all four reliability kinds work on UDP, large messages are fragmented and reassembled within hard budgets, and the conformance suite passes under heavy impairment.
