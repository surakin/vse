# Phase 26 / Day 273 — The Job System on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 272.

`engine/jobs` has no platform directory: it is built on the `sync` wrappers (threads, atomics, mutexes, condition variables), coroutines and the
file layer. Today it builds, runs and is measured on macOS, and the platform pieces the wrappers hide (thread names, priorities, CPU count) get their
macOS answers.

## Tasks

- [ ] **`sync` on macOS:** `Thread` names through `pthread_setname_np(name)`, which on macOS names only the calling thread (the wrapper sets the name from
      inside the new thread; a test reads it back with `pthread_getname_np`); name length limit 63 bytes (cut on a UTF-8 boundary, tested); stack size
      through `pthread_attr_setstacksize` (the default secondary-thread stack on macOS is 512 KiB: check the scheduler's workers ask for what they need).
- [ ] **CPU count and affinity:** the scheduler's worker count from `sysctlbyname("hw.activecpu")` (or what `Thread::HardwareConcurrency` already returns:
      check it is the logical count); macOS has no thread affinity API, so any affinity request is a documented no-op returning false; the main thread's
      QoS class stays the default; workers get `QOS_CLASS_USER_INITIATED` if the spec decides it (measured).
- [ ] **Configure and build `engine/jobs`** and `tests/jobs` on macOS, linking `foundation_io` until the `foundation` aggregate exists on macOS
      (Day 277), as `engine/jobs/CMakeLists.txt` already does for Windows (merge the two branches into one "aggregate not yet ported" branch rather
      than copying it); the coroutine wrappers (`jobs/coroutine.h`) compile with Apple clang's
      `<coroutine>` (libc++), and the frame allocator hooks behave as on Linux.
- [ ] **Suites:** the job-system tests, including the stress tests and the async I/O tests through the file layer of Day 272, pass in plain, asan and
      tsan; record any TSan report and fix its cause (a report is a bug until shown otherwise; a false positive is suppressed only with a written reason
      in the suppressions file).
- [ ] **Measure:** the Phase 2 scheduler benchmark (spawn and steal latency, an empty-job throughput run) on this 4-core i5; recorded beside the Linux and
      Windows numbers in the jobs guide, no target to meet (different hardware).
- [ ] **Documentation:** `docs/guide/jobs.md` and `foundation-sync.md` name the macOS behavior (thread names set from inside, no affinity, QoS);
      `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote the three lines); shared-file runs filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: worker start-up and shutdown ordering, the thread-name race, anything TSan flagged.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `engine/jobs` and its suites pass on macOS in plain, asan and tsan and both unity modes (quoted), the benchmark numbers are in the jobs guide,
and the thread naming, stack size and affinity behavior on macOS are documented and tested.
