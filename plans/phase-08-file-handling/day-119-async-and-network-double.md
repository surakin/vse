# Phase 8 / Day 119 — Asynchronous Operations and the Network-Like Test Double

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 118; Phase 2 (the scheduler and `jobs::IoThread`).

Async read and write on the interface (needed so a network-backed file can implement it natively), the executor seam from the spec, the coroutine adapters, and a test double that proves the
interface fits a file that is not local.

## Tasks

- [ ] In the foundation: `io::AsyncResult` (status plus bytes transferred), `io::Completion` (a function pointer plus user pointer, called exactly once, on an unspecified thread), `io::IoRequestId`, and the `File` operations
      `ReadAsync(buffer, length, offset, completion)`, `WriteAsync(...)`, `FlushAsync`, `Cancel(id)` (best effort; a cancelled request still completes, with `Interrupted`); the lifetime rule written down (the buffer must stay valid until the completion runs; destroying
      a `File` with requests in flight waits for them or completes them with `Interrupted`, as the spec decided).
- [ ] `io::IoExecutor` (an abstract class in the foundation: `Submit(work)`), the default `File` implementation of the async operations on top of the synchronous ones and an executor (so any backend gets async for free), and `SetDefaultExecutor`; `LocalFile` uses it.
- [ ] In `jobs`: `jobs::IoThread` implements `io::IoExecutor` (one thread today; the interface allows a pool); awaitable adapters `co_await ReadAsync(file, ...)`, `WriteAsync`, `ReadAllAsync` returning the bytes in an arena-backed result; the old `ReadFileAsync(scheduler, path, ...)` is
      re-implemented on `File` (the full port is Day 121, here only the implementation behind the same signature); resuming on the right scheduler thread as today.
- [ ] **The network-like double** `tests/foundation/slow_file.h`: a `File` that keeps its bytes in memory, completes every request on its own thread after a configurable delay, reports `size known = false` until the first read finishes, delivers short reads (at most a chunk), can fail
      the Nth request, and implements the synchronous operations only by blocking on the asynchronous ones. It passes `FileConformance` unchanged, which is the proof that the interface assumes nothing local.
- [ ] Async tests (phase 8): ordering of completions per file, many concurrent reads (1000) of one file, cancellation before and after completion, a completion that destroys the file, an error in the middle of a chain, `co_await` of a read inside a job on a 4-participant scheduler,
      `ReadAllAsync` over the slow double, the executor shut down with requests queued; TSan clean; no allocation in the completion path for the local case (counted with the test allocator).
- [ ] **Documentation:** labels; the guide gets "Asynchronous I/O" with a tagged sample (the coroutine form, run on a scheduler) and a section "Writing a File backend" built on the double; `jobs.md` links to it; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors (async lifetime code); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: completion exactly once, destruction with requests in flight, the buffer lifetime rule.
- [ ] Update `STATUS.md`; commit.

**Done:** files read and write asynchronously through callbacks and coroutines, the same conformance suite passes on `LocalFile` and on the network-like double, the TSan flavor is clean, and the "Writing a File backend" guide section is committed.
