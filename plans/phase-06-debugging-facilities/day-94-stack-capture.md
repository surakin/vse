# Phase 6 / Day 94 — Stack Capture

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 91 and 92.

Capture the call stack of the current thread, and of a faulting thread from a signal context, without allocating or
locking, and without ever faulting itself.

## Tasks

- [ ] Add `engine/foundation/debug/stack_trace.h`: `StackTrace` (fixed capacity of 64 frame addresses, a count, a `truncated` flag), `CaptureStackTrace(StackTrace&, skip_frames)` for the current thread, and `CaptureStackTraceFromContext(StackTrace&, const void* ucontext)` for a signal handler; both are async-signal-safe and allocate nothing.
- [ ] Linux backend `engine/foundation/os/linux/debug/stack_walk.cpp`: walk frame pointers (`rbp` on x86-64, `x29` on AArch64) with the bounds checks Spike A settled: the frame address lies inside the thread's stack range (obtained once per thread at first use and cached in a `thread_local` that needs no destructor, or from `/proc/self/maps` inside a signal), is aligned, and strictly increases; a return address must fall inside a mapped executable range. A frame that fails any check ends the walk without touching memory.
- [ ] Make frame pointers available where the walker needs them: add `-fno-omit-frame-pointer` to `ENGINE_PRODUCTION_COMPILE_OPTIONS` and to the test targets of this phase (cost measured on `renderer_bench` and recorded; if it is not negligible, keep it for Debug and RelWithDebInfo only and document that Release traces may be short). Libraries without frame pointers (driver, libc) end a walk early; record that, and decide with Spike A whether a `.eh_frame` fallback is worth it (proposed: not in this phase, filed in `KNOWN-ISSUES.md`).
- [ ] Skip logic: `skip_frames` drops the capture function itself and the macro's frames so the first frame is the caller of the failing code; test it against an `__attribute__((noinline))` chain of known depth.
- [ ] Context walk: take the starting `pc`, `sp` and frame pointer from `ucontext_t` (`REG_RIP`/`REG_RSP`/`REG_RBP`), put `pc` first, then follow the chain from the faulting frame; handle the case where the fault was a null call (pc 0) by taking the return address from the stack top.
- [ ] Tests (in `tests/debug`): depth and order of a known call chain; a trace from a `std::thread`; from a job-system coroutine resumed on a worker (frames above the resume point are present, or the limit is documented); capture inside a signal handler for `SIGUSR1` sent to self; a fault inside a function with a smashed frame pointer ends the walk without a second fault (child process: the stack is deliberately corrupted, the walker returns a short trace, the child exits 0); a stack of 10000 frames is truncated at capacity and flagged; the walker is run 1e6 times in a loop for a rough cost, recorded in the day file.
- [ ] Run the walker under ASan and TSan: instrumented frames are larger and fake stacks (`detect_stack_use_after_return`) move frames onto the heap; record how the walker behaves and which checks need `__asan_*` awareness or a documented limit.
- [ ] **Documentation:** `///` comments with `Thread-safety:` ("async-signal-safe") and `Fails:` labels; `docs/guide/foundation-debugging.md` gets "Capturing a stack trace" with a tagged compiled sample; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, `--flavor asan`, `--flavor tsan`; the day changes `ENGINE_PRODUCTION_COMPILE_OPTIONS`, so the script runs everything) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff, with extra attention to every memory read the walker does; record the outcome here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** both capture functions exist, are async-signal-safe by construction (no heap, no lock, no stdio, checked by reading the code and by running them inside a handler), never fault on corrupted frames, and their tests pass in plain, ASan and TSan; the frame-pointer flag cost is recorded.
