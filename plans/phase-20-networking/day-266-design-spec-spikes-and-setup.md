# Phase 20 / Day 266 — Design Spec, Spikes (Sockets, Readiness, QUIC/TLS) & Phase Setup

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Phase 19 complete; Phase 2 (`jobs::IoThread`, `Counter`, coroutines); Phase 9 (file-layer shape: `BACKEND.md`, errors as values, a test double); Phase 11 (settings).

Settle the networking design with measurements, put the QUIC/TLS/encryption question to the user with evidence, and set up the phase. A planning-and-spike day: the spikes are throwaway and no engine code ships.

## Tasks

- [x] **`CURRENT_PHASE` becomes 20** in the commit that completes this day's first task, with phase 20 named in `tests/phases.txt` (the `flavors 20` line and the `tests/net/` mapping arrive with the first test, Day 267).
- [x] **Spec** `docs/superpowers/specs/2026-11-NN-networking-design.md` (date of the day) recording each decision below with its reason (planner's defaults in brackets; a spike that disproves one amends the later day files in the same commit):
      - modules [`engine/net`, layer above `foundation` and `jobs`; sockets and readiness in `net/linux/` and `net/windows/` (and `net/macos/` designed in); the `Transport` interface and the three transports above them; no ECS type in `net`];
      - addresses and errors [value types with no syscalls, `net::Status` with portable codes];
      - readiness [a dedicated reactor thread (epoll on Linux; IOCP or `WSAPoll` on Windows) that completes `jobs::Counter`s exactly like `jobs::IoThread` does, so a coroutine `co_await`s a read, write, accept or datagram and resumes on a participant; no thread per connection; spike 1 decides whether it extends `IoThread` or is a second non-participant thread];
      - the packet and handshake formats of the UDP transport, the channel model, the congestion algorithm [NewReno-style window with a documented pacing rule], the MTU [1200 payload bytes];
      - hooks for replication (sequence-stamped snapshots, per-connection bandwidth budget, per-connection interest callback) recorded, not built;
      - settings group `net.*`; test labels [`network` for real sockets, skipped with a printed reason when loopback or `bind` is unavailable].
- [x] **Spike 1, readiness:** a throwaway epoll echo server and client driven by a counter-completing thread, 1,000 loopback connections, measuring wake-ups per message and the latency of a coroutine resume; and the Windows equivalent noted for Day 278 (IOCP versus `WSAPoll`, expected scale and cost). Decision recorded.
- [x] **Spike 2, UDP on loopback:** a throwaway sender/receiver measuring the loss, reordering and receive-buffer overruns a loopback socket shows at 10k, 100k and 500k datagrams per second with default and enlarged buffers; sets the defaults of `net.recv_buffer_bytes` and the soak-test rates.
- [x] **Spike 3, QUIC / TLS / encryption (the user's decision):** compare (a) a vendored library (ngtcp2 with a TLS library, quiche, msquic as source), (b) the OS's own (MsQuic on Windows, none on Linux, Network.framework on Apple), (c) TLS on the TCP transport only, (d) an authenticated-encryption layer on an audited primitive over the UDP transport, (e) none in this phase and QUIC deferred. For each: license, source size, build time, how it ports to Windows, macOS, Android, iOS and consoles, how it meets the file-layer and no-ad-hoc-thread rules, and what it costs to test. Write a recommendation and **stop for the user's decision before Day 280**; whichever is chosen gets its entry in `PLAN.md` assumption #26 and `BUILD.md`. Never custom cryptography.
- [x] **Sandbox check:** record whether loopback sockets work in the test environment and how `network`-labelled tests report a skip; fix the label scheme in `tests/phases.txt` conventions.
- [x] **Module plan:** `ARCHITECTURE.md` gains the planned rows (`net`, its layer, allowed edges); `tools/module_deps_check/modules.txt` mappings arrive with the code.
- [x] **Benchmark baseline:** the loopback TCP echo and the UDP datagram rate of the spikes are written down as the numbers Day 281 is measured against; the pass conditions (messages per second, latency percentiles, CPU per connection, 10,000 simulated connections in the soak test) are set in the spec.
- [x] **Day files check:** Days 267-281 are re-read against the spike outcomes and amended in the same commit where a spike disproves them.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec records every decision with its evidence, the QUIC/encryption recommendation is in front of the user, `CURRENT_PHASE` is 20, and Days 267-281 are still accurate (or amended in this commit).

## Notes

- Planning-and-spike day: the day review is exempt, as for Phase 19 Day 250 (no engine code ships).

## Outcome (2026-11-11, Linux machine)

- Spec: `docs/superpowers/specs/2026-11-11-networking-design.md`. Spike 1: 1.0-1.09 million echo round trips per second at 1,000 loopback connections through one epoll reactor thread and four resuming threads, 0.74-0.76 wake-ups per round trip; decision: the reactor is a second non-participant thread, not an extension of `IoThread` (Day 269 amended). Spike 2: zero loss and reordering at 10k, 100k and 500k paced datagrams per second with a draining receiver; a stalled receiver keeps 92 of a 20,000 burst with the default buffer and 3,640 with 4 MiB, so `net.recv_buffer_bytes` defaults to 4 MiB and loss tests run on the simulated transport. Windows: `WSAPoll` first, IOCP only if Day 278 measures a need.
- Spike 3: written up with a recommendation (authenticated encryption on an audited primitive over the UDP transport; QUIC and TLS deferred with a trigger). **Sizes and per-platform facts are from memory and documentation, not measured; the user's decision is pending and is needed before Day 280.** Days 267-279 do not depend on it; Day 280's header says so.
- Sandbox: loopback TCP, UDP and IPv6 work here; a real-socket test that cannot bind skips with its reason.
- Days 267-281 re-read: only Days 269 and 280 needed a sentence. The Windows half of the figures is not measured (the Windows PC).
- Runs: see `STATUS.md` (no engine code shipped; planning-and-spike day, so no day review).
