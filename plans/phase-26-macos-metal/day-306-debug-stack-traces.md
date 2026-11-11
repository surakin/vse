# Phase 26 / Day 306 — Debugging I: Stack Traces, Symbols, Crash Context

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 305.

The first third of `engine/foundation/os/macos/debug/BACKEND.md`: libbacktrace on Mach-O with dSYMs, the module list, demangling, thread ids and the
crash context. Linux's `os/linux/debug/` is the reference; what is the same POSIX code moves to `os/posix/debug/`.

## Tasks

- [x] **Build libbacktrace for Mach-O:** the CMake source list replaces the macOS stop with `macho.c` (and the `config.h` values for Darwin:
      `HAVE_MACH_O_DYLD_H`, no `dl_iterate_phdr`); ~~the vendored version's macOS support is confirmed by building its own tests once (not committed)~~
      (amended on Day 306: not done; the engine's stack-trace suite in three flavors exercises the library instead, `KNOWN-ISSUES.md`).
- [x] **dSYM step:** every executable of ours built with debug info gets a post-link `dsymutil` (a CMake function beside the existing split-debug
      option, `ENGINE_SPLIT_DEBUG_INFO`'s macOS meaning documented); the dSYM sits next to the binary, where libbacktrace looks. Without `dsymutil` the
      configure says so and traces fall back to symbol names only (tested by building one test with the step off).
- [x] **`ResolveStackTrace` on macOS** (`stack_trace_backtrace.cpp`, built for real for the first time: the Day 101 cross-syntax gap in
      `KNOWN-ISSUES.md`): function, file and line for a frame in the main executable, a frame in a dylib of ours, and a frame in a system library (name
      only); ASLR slide handled.
- [x] **`InitDebugFacilities` and `ReloadDebugModules`:** the module list from `_dyld_image_count`/`_dyld_get_image_name`/`_dyld_get_image_header` and
      `_dyld_register_func_for_add_image`; a test `dlopen`s a library and finds it in the list.
- [x] **`DemangleSymbol`** (`abi::__cxa_demangle`, through the existing wrapper rules) and **`CurrentThreadId`** (`pthread_threadid_np`).
- [x] **`InitCrashContext`:** build id from the main image's `LC_UUID` as hex, `kern.osproductversion`, `machdep.cpu.brand_string` (absent on Apple silicon:
      `hw.model`, and the field says so), `hw.logicalcpu`, `hw.memsize`, the executable from `_NSGetExecutablePath`; the crash-context tests compare with
      `sysctl -n` output.
- [x] **`crash_extract`** (amended: moved to Day 308, which has the first macOS crash reports; today `crash_extract` builds on macOS and its tool
      test passes) (`tools/crash_extract/resolve.cpp`'s platform branch): resolves a macOS report's frames through the dSYM, with `atos` as a
      cross-check in the test when present.
- [x] **`debug_alloc_tests`** link with GNU ld's `--wrap=malloc` (`tests/debug/CMakeLists.txt:201`), which ld64 lacks: interpose with `DYLD_INTERPOSE`,
      count through a malloc zone, or keep it Linux-only with the reason in `KNOWN-ISSUES.md` (decided here; Day 299 inventory).
- [x] **Conformance groups** *stack trace* and *crash context* of `tests/debug/BACKEND-CONFORMANCE.md` pass or skip with a reason.
- [x] **Documentation:** `os/macos/debug/BACKEND.md` sections get "as built (Day 306)" notes; `docs/guide/foundation-debugging.md` and
      `porting-debugging.md` macOS sections (dSYM placement, `atos`); `debug_backend_lists` and `debug_cross_syntax_macos` pass; `docgen lint` zero.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote); shared-file runs filed.
- [x] **One thread id on macOS** (added on Day 305 by its review): `debug::CurrentThreadId` and `jobs::detail::CurrentThreadId` (Day 305: the low 31 bits
      of `pthread_threadid_np`, never 0) must print the same number for a thread, or the crash report names the job system's form beside its own, so a
      worker reads the same in a crash report and in the job logs.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the module list's lifetime against images added from another thread, the resolver's
      behavior without a dSYM, anything that allocates on a path the crash report uses.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** stack traces on macOS resolve function, file and line through the dSYM (tested in the main executable and a dylib), the crash context
matches `sysctl`, the stack-trace and crash-context conformance groups pass in three flavors, and the debugging guides describe the dSYM rules.

## Result (2026-11-11, the Mac)

- **libbacktrace on Mach-O** (`third_party/libbacktrace/CMakeLists.txt`): a Darwin branch with `macho.c`, `HAVE_MACH_O_DYLD_H`, no data-symbol lookup
  (upstream's own configure turns it off for Mach-O); the vendored sources are unchanged. Its own tests were not built (the plan's "once, not committed"):
  the engine's stack-trace suite exercises it in three flavors instead.
- **dSYMs:** `engine_dsym(<target>)` and `ENGINE_DSYM` (root `CMakeLists.txt`, default on): `dsymutil` after linking, `<binary>.dSYM` beside the
  binary; `engine_split_debug_info` calls it on macOS. Applied to `debug_tests*` and the test plugin. `debug_no_dsym_tests` (built without it) shows
  the fallback: function names, no file or line.
- **The debug layer on macOS:** the shared sources plus `os/macos/debug/module_list.cpp` (dyld's add-image and remove-image callbacks into a
  registry under a mutex, after the review) and `crash_context_macos.cpp`; **temporary**
  `os/macos/debug/not_yet_ported.cpp` (Days 307-308's functions answer "unsupported"; `crash_handler_sentry.cpp` left out until Breakpad's macOS
  headers are vendored; `KNOWN-ISSUES.md`).
- **Found: frames of the dyld shared cache were named by the wrong image.** An image's range as the span of all its segments covers its neighbours in
  the shared cache (`__LINKEDIT` is shared), so `libobjc` claimed `libsystem_pthread`'s frames. The range is the `__TEXT` segment now; the
  system-frame test checks that no frame is attributed to `libobjc` on that path (it failed before).
- **Found: libbacktrace does not read `MH_BUNDLE`**, which a CMake `MODULE` is on macOS: the late-loaded plugin's frames were unnamed after
  `ReloadDebugModules`. The test plugin is `SHARED` on macOS (`KNOWN-ISSUES.md`: plug-ins should be dylibs).
- **Found: archive name collisions lose debug information in non-unity builds.** A static archive keeps only an object's base name, and `dsymutil`
  skipped the second `directory.cpp.o`, `error.cpp.o` and `crash_context.cpp.o` (shared and platform files of the same name). Renamed
  `io/posix/posix_directory.cpp`, `posix_error.cpp`, `os/macos/debug/crash_context_macos.cpp`; `engine_add_foundation_layer` now stops the
  configure on Apple when two sources of one library share a file name (it fired on a too-broad first version, `session.cpp` beside `session.mm`,
  which do not collide).
- **Thread ids:** `debug::CurrentThreadId` returns the job system's number (the low 31 bits of `pthread_threadid_np`, never 0); tested.
- **Crash context** matches `sysctl -n kern.osproductversion hw.logicalcpu hw.memsize machdep.cpu.brand_string`; build id = `LC_UUID` (32 hex digits).
- **`debug_alloc_tests`:** decided Linux-only (ld64 has no `--wrap`; `KNOWN-ISSUES.md`).
- **Tests on macOS:** `tests/debug` builds the shared test files in three flavors with `resource_guard_macos.cpp` (`os::OpenHandleCount`) and
  `tests/debug/macos/stack_trace_macos_test.cpp` (the dylib frame's function, file and line from its dSYM and its offset against the slide; a system
  frame by module, the TSan runtime's under TSan, which intercepts `pthread_once`; thread ids; the crash context). The death-test, fault and dump
  cases skip (unsupported until Days 307-308).
- **Runs (macOS, `/bin/bash scripts/test-phase.sh`):** `--affected --crashes` plain `ran 610 of 1253 tests (phases: all; flavors: plain; display:
  included; crash tests: included)`, asan `ran 321 of 1253`, tsan `ran 322 of 1253`, passed (before the last fixes); after them `--all --flavor all`
  `ran 1253 of 1253 tests (phases: all; flavors: all; ...)`, all passed in unity; non-unity the same with the flaky throughput test the one failure.
  After the review's fixes, the same: unity `ran 1253 of 1253`, all passed; non-unity `ran 1253 of 1253` with the flaky throughput test the one
  failure. No warnings, no `dsymutil` warnings. (Rebuilt and rerun once more after renumbering onto the main line's Phase 20 plan: below.)

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, **all 10 fixed**.

1. **The module list walked dyld's list by index without a lock against `dlclose`** (indices shift: a header with another image's slide or name, or
   load commands read from an unmapped image). Rebuilt on `_dyld_register_func_for_add_image` and `..._remove_image` (the plan's own design): the
   callbacks keep a registry of header, slide and path under a mutex, the remove callback runs before the image is unmapped, and a refresh copies the
   registry. No dyld call is made while holding the mutex.
2. **The crash-context test hard-coded `x86_64`**: it compares with `uname` now.
3. **The dylib test failed without a dSYM** (`ENGINE_DSYM` off is supported): it finds the frame by address and skips without file and line. Checking
   that showed a libbacktrace limit: without the dSYM it does not name an image's last symbol (the plugin's only one); recorded in `KNOWN-ISSUES.md`.
4. **`tests/module_map.txt` had no line for `os/macos/debug`**, so `--affected` skipped phase 7 for those files: `engine/foundation/os/macos/debug 7,26`.
5. **Apple silicon does have `machdep.cpu.brand_string`** (`Apple M1`): the `hw.model` fallback was dead code on a wrong premise; removed, and
   `BACKEND.md` and the porting guide corrected.
6. **A stale dSYM could survive a rebuild**: `engine_dsym` removes the old bundle after each link, and with the option off removes it only. The option
   text no longer promises every program.
7. **The `KNOWN-ISSUES.md` entry for Linux was incomplete** (the renamed sources, `BACKEND.md`, the guides and `ctest -L docs`; the allow list's reason
   text): completed.
8. **`dsymutil` ran in builds without debug information**: only Debug and RelWithDebInfo make a dSYM now.
9. **Conventions:** `::time` became `os::WallClockUnixMillis()`; `waitpid` is `::waitpid`.
10. **Two tasks were ticked as written but not done so**: the libbacktrace self-test task is amended (not built; `KNOWN-ISSUES.md`), and the
    registration task is now done (finding 1).

**Renumbered:** while this day was being committed, the main line planned Phase 20 (networking, Days 266-281) and moved Phase 26 to Days 298-327
(`c8c1ce7a`). This day was done as Day 290 and is Day 306; its text was renumbered (every Phase 26 day number +16) and merged onto that commit.
