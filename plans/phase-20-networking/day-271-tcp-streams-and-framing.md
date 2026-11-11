# Phase 20 / Day 271 — TCP Streams: Framing, Backpressure, Close & Timeouts

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 267-270.

The TCP building blocks above raw sockets, used by the TCP transport (Day 277) and by tools.

## Tasks

- [ ] **`net::TcpStream`**: a connected socket plus a bounded receive buffer and a bounded send queue; `Send` reports `LimitExceeded`/full instead of growing; a writer coroutine drains the queue with `Send` awaitables; backpressure visible to the caller as `SendQueueBytes()` and a "writable again" awaitable.
- [ ] **Length-prefixed framing** `FrameReader`/`FrameWriter` (4-byte big-endian length, **the limit checked before any allocation**, a frame larger than `net.max_frame_bytes` closes the stream with `LimitExceeded`); partial frames across any read split, tested byte by byte.
- [ ] **Close:** graceful (`Shutdown(write)`, drain, close, with a linger timeout) and abortive (`RST` via `SO_LINGER 0`); reasons reported (`PeerClosed`, `Reset`, `TimedOut`, `LocalClose`, `ProtocolError`).
- [ ] **Keep-alive and timeouts:** connect timeout, idle timeout, optional TCP keep-alive options from settings; a half-open connection is detected by the idle timer.
- [ ] **Tests** (`tests/net/tcp_test.cpp`, label `network`): echo of 10,000 random frames with every possible split point of the byte stream (injected by a test socket wrapper that returns 1..N bytes per call); backpressure with a slow reader never grows memory past the cap; oversized and zero-length frames; reset mid-frame; idle timeout; graceful close drains in-flight data; 1,000 streams under asan/tsan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a TCP stream sends and receives bounded frames with backpressure, partial reads and writes of any size, and every close and timeout is a reported reason.
