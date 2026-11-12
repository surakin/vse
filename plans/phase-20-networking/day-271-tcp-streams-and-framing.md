# Phase 20 / Day 271 — TCP Streams: Framing, Backpressure, Close & Timeouts

**Phase:** [Phase 20 — Networking](../phase-20-networking.md)
**Depends on:** Days 267-270.

The TCP building blocks above raw sockets, used by the TCP transport (Day 277) and by tools.

## Tasks

- [x] **`net::TcpStream`**: a connected socket plus a bounded receive buffer and a bounded send queue; `Send` reports `LimitExceeded`/full instead of growing; a writer coroutine drains the queue with `Send` awaitables; backpressure visible to the caller as `SendQueueBytes()` and a "writable again" awaitable.
- [x] **Length-prefixed framing** `FrameReader`/`FrameWriter` (4-byte big-endian length, **the limit checked before any allocation**, a frame larger than `net.max_frame_bytes` closes the stream with `LimitExceeded`); partial frames across any read split, tested byte by byte.
- [x] **Close:** graceful (`Shutdown(write)`, drain, close, with a linger timeout) and abortive (`RST` via `SO_LINGER 0`); reasons reported (`PeerClosed`, `Reset`, `TimedOut`, `LocalClose`, `ProtocolError`).
- [x] **Keep-alive and timeouts:** connect timeout, idle timeout, optional TCP keep-alive options from settings; a half-open connection is detected by the idle timer.
- [x] **Tests** (`tests/net/tcp_test.cpp`, label `network`): echo of 10,000 random frames with every possible split point of the byte stream (injected by a test socket wrapper that returns 1..N bytes per call); backpressure with a slow reader never grows memory past the cap; oversized and zero-length frames; reset mid-frame; idle timeout; graceful close drains in-flight data; 1,000 streams under asan/tsan.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (sockets, readiness and connection state are concurrency and lifetime code); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. Real-socket tests are labelled `network` and use loopback with ephemeral ports only. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is portable and checked on the Windows PC no later than Day 281 (Day 278 for the socket backend itself); until then the day is "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/net.md` is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 20).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a TCP stream sends and receives bounded frames with backpressure, partial reads and writes of any size, and every close and timeout is a reported reason.

## Outcome

New: `net::FrameWriter`/`net::FrameReader` (`engine/net/frame.h`: 4-byte big-endian length; zero is `Invalid`, above the limit `LimitExceeded`, both checked on the length before any payload memory; the payload buffer grows with the bytes that arrived), `net::TcpStream` (`engine/net/tcp_stream.h`: bounded ring send queue with `Send`/`Flush(keep_queued)`/`SendFrame`, `ReceiveFrame`, `Close` with linger, `Abort`, `CloseReason`, idle and connect timeouts), and `Socket::SetKeepAliveTiming`/`SetLinger` (POSIX backend; `BACKEND.md` sections). Deviation from the task wording: there is no background writer coroutine; the owner drives the queue with `Flush` (the "writable again" wait is `Flush(capacity - n)`), which the TCP transport (Day 277) does from its own job. `TcpStreamOptions` also carries `max_read_bytes` (asks the socket for at most that many bytes per read, which is the "test socket wrapper returns 1..N bytes" of the task) and the socket buffer sizes. Documentation: `docs/guide/net.md` ("TCP streams and frames", one compiled sample), `docs/reference/net.md`; `docgen lint` at zero, `ctest -L docs` 7 of 7.

Tests: `tests/net/frame_test.cpp` (10,000 frames byte by byte, in random pieces and whole; the stream cut at every single point; hostile lengths; a header promising 1 MiB holds 14 bytes) and `tests/net/tcp_test.cpp` (echo of 1,500 / 4,000 / 10,000 / 10,000 frames with reads of 1 / 3 / 7 / unlimited bytes; a slow reader fills the queue to its capacity and nothing is lost; oversized, zero-length and good-then-bad frames; reset and end of stream mid-frame and at a boundary; idle timeout and its restart on every byte; the per-call limit leaves the stream open; graceful close of 800 in-flight frames; linger expiry; abort; refused connect; 500 pairs = 1,000 streams).

Runs (Linux machine): plain `ran 2624 of 6889 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2046 of 6889 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1722 of 6889 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)` (only the known tsan scene test failed). GCC and Clang non-unity: `net_tests` and `net_tests_sockets` build clean (warnings are errors) and pass.

## Post-implementation code review

`/code-review high`: 3 findings, 2 fixed, 1 deferred. (Before the review, self-checks found and fixed: the idle timeout covered a whole 16 KiB piece instead of one stall, and operations on a stream that was never opened dereferenced a null reactor; the second has a test.)

- Fixed: a kernel `ETIMEDOUT` (keep-alive probes unanswered) was taken for the call's own timeout, so the stream stayed open with no reason unless an idle time was set (a wait that ran out of time carries no system error, a kernel timeout does); on the send side every failure other than reset became `Reset`, now `TimedOut` for `ETIMEDOUT`. No regression test: a kernel connection timeout cannot be provoked on loopback.
- Deferred: each stream allocates its buffers up front (send queue 256 KiB, read buffer 64 KiB, scratch 16 KiB at the defaults); `net.connection_budget_bytes` and lazy growth belong to Day 279 (`KNOWN-ISSUES.md`).
