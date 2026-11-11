# Phase 20 / Day 279 — Security Basics: Budgets, Rate Limits & Fuzzing

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 274-277.

Nothing is trusted before the handshake completes; every length is bounded before allocation; hostile input is fuzzed.

## Tasks

- [ ] **Budgets:** a per-connection memory budget (receive buffers, reassembly, reorder buffer, send queues) and a global one, enforced by the transports with `LimitExceeded` closes and counted in statistics; no allocation sized by a peer-supplied number without checking it against the budget first (an audit pass over Days 271-277 with a checklist in this file).
- [ ] **Rate limits:** handshakes per second per endpoint and in total, accepts per second, new connections per endpoint; a token bucket with a documented clock; excess is dropped silently (UDP) or refused (TCP); a ban list with expiry for repeat offenders, bounded in size.
- [ ] **Fuzzing:** deterministic fuzz tests (a seeded generator and mutation of valid packets and frames) through the UDP packet parser, the handshake, the fragment reassembler, the TCP frame reader, `NetReader` with random schemas, and the address parser, 1 million cases each, plain and asan, no crash, no allocation above the limits, no state before the handshake; a libFuzzer-style entry point is **not** added (no new dependency), the corpus is generated.
- [ ] **Threat notes** `docs/guide/net.md` section "What the network layer does not protect against": spoofing on an untrusted path, eavesdropping and tampering (until Day 280's outcome), amplification, state exhaustion; what is mitigated and how.
- [ ] **Tests** (`tests/net/security_test.cpp`): half-open floods, handshake floods, fragment bombs, slow-loris on TCP, reorder-gap attack, ack-bitfield lies (acking packets never sent), sequence-number games; each ends within its budget and the server stays responsive to a good client.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** hostile peers and malformed packets cannot exhaust memory or state, the limits are measured, and the parsers survive the fuzz.
