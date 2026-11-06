# Phase 9 / Day 119 — Asynchronous Operations and the Network-Like Test Double

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Day 118; Phase 2 (the scheduler and `jobs::IoThread`).

Async read and write on the interface (needed so a network-backed file can implement it natively), the executor seam from the spec, the coroutine adapters, and a test double that proves the
interface fits a file that is not local.

## Tasks

- [x] In the foundation: `io::AsyncResult` (status plus bytes transferred), `io::Completion` (a function pointer plus user pointer, called exactly once, on an unspecified thread), `io::IoRequestId`, and the `File` operations
      `ReadAsync(buffer, length, offset, completion)`, `WriteAsync(...)`, `FlushAsync`, `Cancel(id)` (best effort; a cancelled request still completes, with `Interrupted`); the lifetime rule written down (the buffer must stay valid until the completion runs; destroying
      a `File` with requests in flight waits for them or completes them with `Interrupted`, as the spec decided).
- [x] `io::IoExecutor` (an abstract class in the foundation: `Submit(work)`), the default `File` implementation of the async operations on top of the synchronous ones and an executor (so any backend gets async for free), and `SetDefaultExecutor`; `LocalFile` uses it.
- [x] In `jobs`: `jobs::IoThread` implements `io::IoExecutor` (one thread today; the interface allows a pool); awaitable adapters `co_await ReadAsync(file, ...)`, `WriteAsync`, `ReadAllAsync` returning the bytes in an arena-backed result; the old `ReadFileAsync(scheduler, path, ...)` is
      re-implemented on `File` (the full port is Day 121, here only the implementation behind the same signature); resuming on the right scheduler thread as today.
- [x] **The network-like double** `tests/foundation/slow_file.h`: a `File` that keeps its bytes in memory, completes every request on its own thread after a configurable delay, reports `size known = false` until the first read finishes, delivers short reads (at most a chunk), can fail
      the Nth request, and implements the synchronous operations only by blocking on the asynchronous ones. It passes `FileConformance` unchanged, which is the proof that the interface assumes nothing local.
- [x] Async tests (phase 9): ordering of completions per file, many concurrent reads (1000) of one file, cancellation before and after completion, a completion that destroys the file, an error in the middle of a chain, `co_await` of a read inside a job on a 4-participant scheduler,
      `ReadAllAsync` over the slow double, the executor shut down with requests queued; TSan clean; no allocation in the completion path for the local case (counted with the test allocator).
- [x] **Documentation:** labels; the guide gets "Asynchronous I/O" with a tagged sample (the coroutine form, run on a scheduler) and a section "Writing a File backend" built on the double; `jobs.md` links to it; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors (async lifetime code); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: completion exactly once, destruction with requests in flight, the buffer lifetime rule.
- [ ] Update `STATUS.md`; commit.

**Done:** files read and write asynchronously through callbacks and coroutines, the same conformance suite passes on `LocalFile` and on the network-like double, the TSan flavor is clean, and the "Writing a File backend" guide section is committed.

## Result

- Foundation (`io/file.h`, `io/file.cpp`): `IoWork`, `IoExecutor` (a rejected `Submit` is completed by the caller; accepted work always runs), `SetDefaultExecutor`/`DefaultExecutor` with a built-in single-thread executor created on first use, `AsyncResult`, `Completion`, caller-owned `AsyncRequest`, and `File::ReadAsync`/`WriteAsync`/`FlushAsync`/`Cancel` plus the backend hooks `StartAsync`, `ClaimAsync`, `CompleteAsync`, `WaitForAsyncRequests`. Any backend gets asynchronous I/O from its synchronous operations; a natively asynchronous one overrides `StartAsync`.
- Jobs: `jobs::IoRequest` is now an alias of `io::IoWork` and `jobs::IoThread` implements `io::IoExecutor`; `jobs/file_async.h` has `co_await jobs::ReadAsync/WriteAsync/FlushAsync` and `ReadAllAsync` (yields a `Vector<u8>`, not an arena block). The existing `jobs::ReadFileAsync` is untouched here and is ported to `io::File` on Day 121 with the other callers.
- `io::FilePtr` (`UniquePtr<File>`) added at the user's request and used across the layer, tests and guide.
- Decisions: the async API is request-struct based (`AsyncRequest*`) instead of returning ids (spike A: zero allocation, +1.7% over today's path); `File` has no key function (every virtual is inline) so test code compiled with RTTI can derive from it against the RTTI-free library; the request is counted out before its completion runs, so a completion may destroy the file.
- Tests: `tests/io/slow_file.h` (the network-like double: own thread, delay, chunked reads, unknown size, Nth-request failure, sync ops that block on async ones), `tests/io/async_file_test.cpp`: both conformance suites on the double (two chunk sizes), unknown size, ordered completions, 1000 concurrent reads (local file and double), cancellation, a completion that destroys the file, destruction with requests in flight, a failure mid-chain, executor shutdown with queued work, zero heap allocations on the local completion path (plain flavor; skipped under the sanitizers whose runtimes own `operator new`), and `co_await` reads, writes, flushes and `ReadAllAsync` over a 4-participant scheduler including the double. The io executables link `jobs` and run 43 cases / 17535 assertions in plain, ASan and TSan (TSan clean).
- Docs: guide sections "Asynchronous I/O" (a run sample on a scheduler) and "Writing a File backend", `jobs.md` link, strict-label fixes for the jobs headers, baseline 3345/3345.
- Scoped runs: `ran 1080 of 2867 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, `ran 797 of 2867 ... flavors: asan`, `ran 517 of 2867 ... flavors: tsan`, all pass (the X11 window test passed this time, also after the review fixes); `ctest -L docs` 4/4. An early run took 64 s under TSan because the double conformance used 7-byte chunks over a 3 MiB read; the chunk sizes were raised and the suite runs in about a second.

## Post-implementation code review

`/code-review high`: 10 findings, 8 fixed (some by changing the contract), 2 declined.

Fixed:
1. A completion that destroyed the file while other requests on it were queued behind it would wait for work that could not run (the destructor spins, the single executor thread is the one spinning). The contract now says a completion may destroy the file only when no other request on it is in flight (header, guide); the wait backs off with a sleep instead of burning a core. The double's self-join case is covered by the same rule (guide: "Writing a File backend").
2. The built-in executor thread is now registered with the crash handler like `jobs::IoThread`. Declined: removing it, because the foundation must do asynchronous I/O without the job system; the guide tells users to install the scheduler's I/O thread.
3. `ReadAllAsync` could recurse once per chunk if a backend completed inline: the `StartAsync` contract now says a backend completes inline only to report a failure (which ends a chain) and otherwise from another thread; documented in the header and the backend guide.
4. `ReadAllAsync` ignored a known size: it reserves from `Size()`, and its chunk is 64 KiB. It still yields a `Vector<u8>` rather than an arena block (recorded in the Result section).
5. The 1000-reads test checked `statuses[0]` for every request: now mapped by request id.
6. A failed `REQUIRE` could leave the default executor dangling: tests use a `ScopedDefaultExecutor`, and `IoThread` now clears itself as the default executor on shutdown and destruction (`io::ClearDefaultExecutorIf`), which also covers the scheduler-destroyed-first case. The tests changed accordingly (a stopped executor falls back to the built-in one; a refusing executor is tested separately).
7. `AsyncRequest::state` used magic numbers: a named `io::AsyncState` enum.
8. `Cancel` on a request whose completion may have freed it: the contract says to call it only while the request cannot have completed.

Declined: none beyond item 2's removal; bookkeeping (STATUS.md, this section) is this commit.
