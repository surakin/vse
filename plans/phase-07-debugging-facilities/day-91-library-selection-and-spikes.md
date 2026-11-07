# Phase 7 / Day 91 — Library Selection & Spikes

**Phase:** [Phase 7 — Debugging Facilities](../phase-07-debugging-facilities.md)
**Depends on:** Phase 6 complete.

Phase 7 uses existing libraries (`PLAN.md` assumption #20). This day chooses them by measurement, not by reputation,
and settles how they sit behind our facade. Planning and throwaway spikes only: nothing under `engine/` or
`third_party/` changes, no new test.

## Tasks

- [x] Inventory what exists: `AssertFail` and the two test sinks in `engine/foundation/log/log.cpp` (prints to fd 1 and exits with code 1 through `os::Exit`, does not abort); the number of `ENGINE_ASSERT` sites (334 at planning time) and the tests that redirect the exit (`container_vector_test.cpp`); every signal handler and sanitizer option already in the tree (`lsan_options.cpp`, none else); how third parties are vendored today (`third_party/catch2/VENDORED.md`, `third_party/nlohmann_json/`). Put the table in the design spec.
- [x] Write the evaluation criteria into the spec before testing anything, so the result is not argued backwards: license compatible with shipping in a game binary; builds from vendored source with CMake, offline, with no submodule or `FetchContent`; size (files, lines, build seconds) and warnings under our compiler (GCC 16 and clang 23 here); a C interface (no STL type in our headers); no network or telemetry code compiled in; dumps every thread; the crash path is documented async-signal-safe or out of process; works with a fault on any thread, a stack overflow and a corrupted heap; coexists with ASan/UBSan/TSan; lets us attach our own report text and recent log lines to a crash; lets us choose the dump directory; platforms (Linux now, Windows, macOS and Android with the same API, a console path for Phase 23); maintenance (last release, open crash-handling bugs, checked on the day).
- [x] Spike A, crash capture with sentry-native and the Crashpad backend (scratchpad, source tarball downloaded into its own empty directory): build with CMake (`SENTRY_BACKEND=crashpad`, `SENTRY_TRANSPORT=none`, no tests or examples), initialize with a local database directory, crash a test program (null write, `abort`, stack overflow, fault on a `std::thread`), find the `.dmp`, open it with `lldb -c` (`image list`, `thread list`, `bt all`), and record: files and lines vendored, build time, binary size growth, whether `crashpad_handler` is built and how the program finds it, how many threads the dump holds, whether anything links `libcurl` or a TLS library (`ldd`, `nm`), and what the dump directory layout is.
- [x] Spike B, the alternatives with the same program and the same questions: sentry-native with the `breakpad` backend, with `inproc`, and Breakpad's Linux client used directly (its autotools build is the cost: how many files would a CMake wrapper need). Crashpad directly is evaluated on paper from its documentation only (its GN build and `mini_chromium` dependency are the cost) unless Spike A fails.
- [x] Spike C, stack traces with libbacktrace (scratchpad; it has autotools only, so note what a CMake wrapper needs: the `config.h` checks): from a normal call, from a `SIGSEGV` handler, through a `std::thread`, through a job-system coroutine, through a frame with no frame pointer (libc, the Vulkan driver), for a `-g` binary and a stripped one, with inlined frames, after `dlopen`; record that the first call is not async-signal-safe (`dl_iterate_phdr`) and must be made at startup, the cost per trace, and the behavior under ASan and TSan. Compare against `cpptrace` and `backward-cpp` on paper (the first is signal-safe only on Linux, the second is not signal-safe).
- [x] Spike D, coexistence: our own message on a failed assert and the library's crash handler on the same process: which `sigaction` runs first, whether the library's handler chains or re-raises, what happens when we install ours before and after, whether the library offers an on-crash hook or attachments (for the Day 96 report text and the ring buffer), and what each sanitizer does (`allow_user_segv_handler`, `handle_segv`, `handle_abort`). Also a fault on a worker thread and a stack overflow.
- [x] Spike E, core dumps: record this machine's `core_pattern` (`systemd-coredump`), `ulimit -c`, that `coredumpctl` and `gdb` are present; run a crashing program with `RLIMIT_CORE` raised by the program itself and open the core in `gdb`.
- [x] Decide and write down, each with its reason: (1) the crash-capture library (proposed: sentry-native with the Crashpad backend and `transport none`; the Breakpad client directly is the fallback if Spike A/B show Sentry-specific coupling or weight that outweighs its CMake build and platform coverage); (2) the stack-trace library (proposed: libbacktrace); (3) what stays hand-written (assert family, crash context and ring buffer, death-test helper, core-dump setup); (4) the facade: neutral headers in `engine/foundation/debug/`, exactly two implementation files that include the libraries' headers, and the gate that enforces it; (5) how the handler executable (Crashpad) is located and shipped (next to the program, found from the executable path); (6) the dump directory rule and the no-upload rule (`transport none`, a test that no network library is linked); (7) the sanitizer policy from Spike D; (8) how a vendored library is built per flavor (one uninstrumented build linked everywhere, or three builds) from Spikes A and C.
- [x] Write the `PLAN.md` assumption #20 text final (names the chosen libraries and versions), update the `CLAUDE.md` bullet to match, and draft the two `VENDORED.md` files (upstream URL, pinned release or commit, license, what is pruned, how it is built) that Days 94 and 95 complete.
- [x] Write the design spec `docs/superpowers/specs/2026-11-06-debugging-design.md`: the inventory, the criteria, every spike's result with command lines, the decisions above with their reasons, and a risk list (library abandoned or changing license, the handler executable missing at run time, a handler that itself faults, the sanitizers swallowing the signal, a vendored library's warnings under our flags, PDB-only Windows builds) with the mitigation for each.
- [x] Update the later day files where a spike result changes a task (edit the plan before continuing, per `CLAUDE.md`); correct `plans/phase-07-debugging-facilities.md`.
- [x] **Documentation:** no public API. The spec is linked from `docs/README.md`.
- [x] Update `STATUS.md`; commit (planning and spec only; no code review is required for a planning-only commit).

**Done:** the spec exists with the criteria, the spike results and every listed decision with its reason; the two libraries are chosen (or replaced by the alternatives) with measured numbers; `PLAN.md` assumption #20 and the `CLAUDE.md` bullet name them; later day files are corrected where the spikes disagreed with the plan; nothing under `engine/` or `third_party/` changed.

## Result

Spec: `docs/superpowers/specs/2026-11-06-debugging-design.md` (criteria, inventory, every spike with numbers, decisions,
risks, draft `VENDORED.md` records, platform notes). Nothing under `engine/` or `third_party/` changed; the spikes ran in
the scratchpad from downloaded sources.

**Chosen:** libbacktrace at commit `0b9b49cf4a` and sentry-native 0.17.6 with the **Breakpad** backend and transport `none`.
The proposed Crashpad backend was **rejected**: on Linux it needs libcurl at build time, and `crashpad_handler` links
libcurl plus about 25 shared libraries and contains HTTPS code even with transport `none`, so it fails the no-network and
ship-without-extra-libraries criteria without a patch.

Spike findings that changed later days: the Breakpad backend has no handler executable (Day 95 drops
`engine_add_crash_handler`); its dump arrives inside sentry's `.envelope` (Day 95 adds `tools/crash_extract`, Day 99's script
calls it); the library's `on_crash` hook runs in the crashing process on every backend, so the report is written there and
the engine installs no signal handler of its own (Days 96 and 97); a handler installed before `sentry_init` is replaced by
Breakpad's (the guide says to install after and chain); libbacktrace needs a `backtrace_syminfo` fallback for libc frames,
drops a bogus last frame, and makes its first call at startup (Day 94); the library's `[sentry] DEBUG` stderr lines must be
routed to the engine log (Day 95); TSan with the library was not run (Day 95).

Measured: Breakpad backend builds in 4 s, 0 warnings, 135 source files / 37.5 kLoC, +576 KB stripped, 76 ms from start to
exit including the dump, 7/7 crash modes dump every thread and open in `lldb` with file and line; libbacktrace 0.6 µs per
resolved trace and zero allocations inside a handler. Tool gaps recorded: no `minidump-stackwalk`, `dump_syms`, `strace` or
lld on this machine. The spikes left about 25 small cores in the systemd-coredump store.
