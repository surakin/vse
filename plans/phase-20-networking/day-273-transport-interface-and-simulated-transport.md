# Phase 20 / Day 273 — The Transport Interface, the Simulated Transport & the Conformance Suite

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 267, 269, 272.

The interface everything above networking will use, and the in-process transport that proves it fits more than one backend and that every higher layer's tests use.

## Tasks

- [x] **`net::Transport` / `net::Connection`** as specified in the spec: `Listen(endpoint)`, `Connect(endpoint)`, `Send(connection, channel, bytes, Reliability)`, `Receive`/`Poll` of events (`Opened`, `Message`, `Closed(reason)`), per-connection statistics (RTT, loss estimate, bytes and messages in and out, send queue depth), `Close(connection, reason)`; messages bounded by `MaxMessageBytes()`; channel numbers bounded; ownership of buffers explicit (a received message is a view valid until the next `Poll`, or a moved `Message`).
- [x] **`net::Reliability`**: `Unreliable`, `UnreliableSequenced`, `ReliableOrdered`, `ReliableUnordered`; a transport states which it supports (`Supports(reliability)`; TCP maps all reliable kinds to its stream and degrades unreliable ones to reliable with a documented flag).
- [x] **`net::SimulatedTransport`:** in-process, deterministic (seeded, driven by a fake `Clock`), configurable per direction: latency and jitter, loss (uniform and burst), reordering, duplication, bandwidth cap with a queue, MTU, and corruption; a `Network` object connects any number of endpoints; time advances only by `Advance(dt)`, so a networking bug replays exactly from its seed.
- [x] **Conformance suite** `tests/net/transport_conformance.h`, written once and run against every transport (simulated now; TCP Day 277; UDP Days 274-276; QUIC if Day 280 adds it): connect/accept, close with reasons, ordered reliable delivery under the transport's adverse conditions, unreliable messages may be lost but never corrupted or duplicated into delivery twice (where promised), message size limits, channel isolation, many connections, close during send, events after close, destruction with traffic in flight.
- [x] **Tests** (`tests/net/sim_transport_test.cpp`): same seed gives the same packet trace byte for byte; each impairment measured against its configured rate over 1 million packets; the conformance suite passes on the simulated transport.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** one interface with reliability kinds and events exists, the simulated transport reproduces any impairment deterministically, and the conformance suite is ready for the real transports.

## Outcome (2026-11-12, Linux machine)

- `net::Transport` is polled (`Poll(&event)` with `Opened`/`Message`/`Closed`), with `Listen`, `Connect`, `Send`, `Close`, `Statistics`, `Supports`, `DegradesUnreliable`, `MaxMessageBytes`, `ChannelCount`; there is no separate `Connection` class, a connection is a `ConnectionId`. A received message is a view valid until the next `Poll`.
- `net::SimNetwork` (seeded xorshift, 1 ms ticks, per-direction `LinkConfig`: latency, jitter, uniform and burst loss, reordering, duplication, one flipped bit, bandwidth with a bounded queue, MTU, trace) and `net::SimulatedTransport` (its own checksummed packets, acks, retransmission with backoff, window of 256, keep-alive, time-out, connect nonce and cancel).
- Conformance suite `tests/net/transport_conformance.h`: 13 checks, run on the simulated transport over a clean link and a hostile one (10% loss, 5% reordering, 5% duplication, 2% corruption); `tests/net/sim_transport_test.cpp` also measures each impairment over 1,000,000 packets, bursts, MTU and queue drops, trace determinism by seed, and the four regressions below.
- Not done on this machine: the Windows PC build and run (open in `KNOWN-ISSUES.md`).
- Runs: plain `ran 2680 of 7072 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2102 of 7072 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1778 of 7072 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)` (only the known tsan scene test failed); GCC, Clang non-unity and an optimized tree pass the `[sim]` tests; `ctest -L docs` 7 of 7. Guide: `docs/guide/net.md` ("Transports").

## Post-implementation code review

`/code-review high`: 9 findings, all fixed.

- A transport whose `Attach` failed (endpoint taken) detached the owner of the endpoint in its destructor; it now detaches only what it attached.
- A connect was identified by (peer, id) alone: a client restarted at the same endpoint was re-accepted into the old connection and a late duplicate request after the close made a ghost one; the request now carries a nonce from the network's generator and a server remembers the last 64 closed requests.
- `ReliableUnordered` had no bound on how far ahead a sequence is accepted; both reliable kinds now drop (and do not acknowledge) a sequence more than 1024 ahead. The ordered kind used to acknowledge before the check.
- Closing a connection that was still connecting told nobody; it now sends a cancel (`kCancel`) so the server ends the accepted connection with the code.
- `SimNetwork::Advance` allocated a list per tick (scratch kept); the bandwidth backlog product could overflow (computed in microseconds); `Conn::queued_bytes` was dead state (removed); `(void)` casts on parameters removed.
- Regression tests (each fails without its fix, checked): endpoint collision, restart at the same endpoint, close while connecting, forged far-ahead sequences.
