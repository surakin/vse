# Phase 26 / Day 289 — The Job System on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 288.

`engine/jobs` is built on the `sync` wrappers (threads, atomics, mutexes, condition variables), coroutines and the file layer, plus one platform file,
`jobs/linux/thread_platform.cpp` (CPU list, pinning, thread ids, naming; corrected on Day 283: the first version of this file said jobs had no platform
code), which `engine/jobs/CMakeLists.txt:18` picks for every system that is not Windows. Today it builds, runs and is measured on macOS.

## Tasks

- [x] **`jobs/macos/thread_platform.cpp`** and an explicit macOS branch in `engine/jobs/CMakeLists.txt`: allowed CPUs `0..hw.logicalcpu-1`, `PinCurrentThread`
      returns false (no affinity masks on macOS; `THREAD_AFFINITY_POLICY` is only a hint, decided by measurement), thread id from `pthread_threadid_np`,
      naming as below; `tests/jobs/worker_pool_test.cpp` reads pinning back from `/proc/self/task` on Linux: on macOS it checks that pinning reports false.
- [x] **`sync` on macOS:** `Thread` names through `pthread_setname_np(name)`, which on macOS names only the calling thread (the wrapper sets the name from
      inside the new thread; a test reads it back with `pthread_getname_np`); name length limit 63 bytes (cut on a UTF-8 boundary, tested); stack size
      through `pthread_attr_setstacksize` (the default secondary-thread stack on macOS is 512 KiB: check the scheduler's workers ask for what they need).
- [x] **CPU count and affinity:** the scheduler's worker count from `sysctlbyname("hw.activecpu")` (or what `Thread::HardwareConcurrency` already returns:
      check it is the logical count); macOS has no thread affinity API, so any affinity request is a documented no-op returning false; the main thread's
      QoS class stays the default; workers get `QOS_CLASS_USER_INITIATED` if the spec decides it (measured).
- [x] **Configure and build `engine/jobs`** and `tests/jobs` on macOS, linking `foundation_io` until the `foundation` aggregate exists on macOS
      (Day 293), as `engine/jobs/CMakeLists.txt` already does for Windows (merge the two branches into one "aggregate not yet ported" branch rather
      than copying it); the coroutine wrappers (`jobs/coroutine.h`) compile with Apple clang's
      `<coroutine>` (libc++), and the frame allocator hooks behave as on Linux.
- [x] **Suites:** the job-system tests, including the stress tests and the async I/O tests through the file layer of Day 288, pass in plain, asan and
      tsan; record any TSan report and fix its cause (a report is a bug until shown otherwise; a false positive is suppressed only with a written reason
      in the suppressions file).
- [x] **Measure:** the Phase 2 scheduler benchmark (spawn and steal latency, an empty-job throughput run) on this 4-core i5; recorded beside the Linux and
      Windows numbers in the jobs guide, no target to meet (different hardware).
- [x] **Documentation:** `docs/guide/jobs.md` and `foundation-sync.md` name the macOS behavior (thread names set from inside, no affinity, QoS);
      `docgen lint` zero.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote the three lines); shared-file runs filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: worker start-up and shutdown ordering, the thread-name race, anything TSan flagged.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `engine/jobs` and its suites pass on macOS in plain, asan and tsan and both unity modes (quoted), the benchmark numbers are in the jobs guide,
and the thread naming, stack size and affinity behavior on macOS are documented and tested.

## Result (2026-11-11, the Mac)

- **`jobs/macos/thread_platform.cpp`:** the CPU list is `0` to `hw.activecpu - 1` (no per-process mask on macOS); thread ids are `pthread_threadid_np`'s
  low 31 bits (19 million here; masked so a long uptime cannot make the `i32` negative); names through `pthread_setname_np`, which names only the
  calling thread (the worker pool already names each worker from inside it), cut to 63 bytes on a UTF-8 boundary. **Pinning:** none. macOS has no
  affinity API, so a new hook `detail::CanPinThreads()` (true on Linux and Windows) is false and the pool does not try (no "could not pin" warning per
  worker); `worker_pool_test.cpp` checks that no worker reports a CPU there.
- **Thread naming lives in `jobs`, not `sync`** (the plan put it in `sync`; `sync/thread.h` says names are not its business, and the one caller is the
  worker pool). Stack size: the default secondary-thread stack (512 KiB on macOS) is kept: coroutine frames come from the frame pool, and the stress
  suite passes with it in three flavors. QoS: the default, not changed (no measurement asked for a change). `Thread::HardwareConcurrency()` is the
  logical count (8), equal to the CPU list (tested).
- **CMake:** one "platform being ported" branch for Windows and macOS in `engine/jobs/CMakeLists.txt` (link `foundation_io` until the aggregate exists;
  macOS gets the tsan flavor too); `tests/jobs` takes the general path on macOS plus `tests/jobs/macos/thread_platform_macos_test.cpp` (in a `macos/`
  folder: it includes `<sys/sysctl.h>`, which `platform_neutral_check` rule 7 caught in `tests/jobs/`). `tests/io` now builds `async_file_test.cpp`
  with `jobs` on macOS.
- **`jobs::ReadFileAsync` and size-less sources: tried and reverted.** It refuses a pipe or FIFO on every POSIX system (they answer `Size` with
  `Unsupported`). Reading them as streams was tried today and **reverted after the review**: a pipe whose writer stays open, a terminal or
  `/dev/stdin` would block the job system's one I/O thread for good, on Linux as well. Behavior is unchanged from the main line. Its `/proc` case
  skips on macOS (as on Windows: only Linux has a file whose reported size is wrong); the size-less FIFO read is covered by the io-level
  `ReadFile` tests (Days 287-288).
- **Found: the TSan canary failed on macOS** although TSan reported the race: the sanitizers' `abort_on_error` defaults to 1 on macOS (0 on Linux), so
  the report ends in `abort()` (exit 134; 66 with `abort_on_error=0`) and ctest counts an abort as a failure. The canary's test sets
  `TSAN_OPTIONS=abort_on_error=0` on Apple; its pass condition is unchanged.
- **TSan:** no report in the jobs or io suites.
- **Measured** (Debug, plain, load average 3): throughput 1.00x, 1.99x, 3.79x, 6.07x for 1, 2, 4, 8 participants; idle-worker wake latency median
  27.3 / 26.2 us, p99 60.8 / 219.8 us, max 358 / 1562 us over two runs of 10,000. In `docs/guide/jobs.md` beside the Windows numbers.
- **Flaky:** `Throughput scales with participant count` failed in 3 parallel runs of 4 and passed 6 of 6 alone (the timing test already recorded as
  flaky on Windows; `KNOWN-ISSUES.md`).
- **Runs (macOS, `/bin/bash scripts/test-phase.sh`, after the review's fixes):** `--affected` (everything: CMake changed) plain `ran 501 of 946 tests
  (phases: all; flavors: plain; ...)`, asan `ran 222 of 946`, tsan `ran 223 of 946`, all passed; non-unity `ran 946 of 946 tests (phases: all;
  flavors: all; ...)` with the flaky throughput test the one failure (it passed 3 of 3 alone there). No warnings.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings. **8 fixed (4 of them by one revert), 1 deferred to Day 290.**

1. **Fixed by reverting: reading size-less sources as streams could block the single I/O thread for good** (a pipe whose writer stays open, a
   terminal, `/dev/stdin`) or buffer 64 MiB of `/dev/zero`, on Linux too. The day did not need it; `ReadFileAsync` is back to the main line's code.
2. **Fixed by the same revert: the public contract changed without its docs.**
3. **Fixed by the same revert: the macOS FIFO case could hang** when the feeder gave up before the I/O thread opened the FIFO, and left `SIGPIPE`
   ignored on a failed `REQUIRE`. The case is gone (the `/proc` test skips off Linux).
4. **Fixed by the same revert: the stream path's made-up `OutOfMemory` and throwaway arena.**
5. **Fixed by the same revert: the stream path was barely tested on Linux** (and the `/proc` name was wrong on macOS).
6. **Fixed: `Participant::tid`'s doc** names the macOS value (31 bits, for logs only, not unique after 2^31 threads).
7. **Fixed: the masked id could be 0, and the call's result was ignored**: it is never 0, and 1 if the call fails.
8. **Fixed: the pinning test branched on the platform**: it branches on `jobs::detail::CanPinThreads()`; the pool computes `pin` once.
9. **Deferred to Day 290 (a task added there): two thread ids on macOS.** `debug::CurrentThreadId`'s macOS backend is Day 290's; the two must print the
   same number for a thread.
