# Phase 26 / Day 290 — Debugging I: Stack Traces, Symbols, Crash Context

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 289.

The first third of `engine/foundation/os/macos/debug/BACKEND.md`: libbacktrace on Mach-O with dSYMs, the module list, demangling, thread ids and the
crash context. Linux's `os/linux/debug/` is the reference; what is the same POSIX code moves to `os/posix/debug/`.

## Tasks

- [ ] **Build libbacktrace for Mach-O:** the CMake source list replaces the macOS stop with `macho.c` (and the `config.h` values for Darwin:
      `HAVE_MACH_O_DYLD_H`, no `dl_iterate_phdr`); the vendored version's macOS support is confirmed by building its own tests once (not committed).
- [ ] **dSYM step:** every executable of ours built with debug info gets a post-link `dsymutil` (a CMake function beside the existing split-debug
      option, `ENGINE_SPLIT_DEBUG_INFO`'s macOS meaning documented); the dSYM sits next to the binary, where libbacktrace looks. Without `dsymutil` the
      configure says so and traces fall back to symbol names only (tested by building one test with the step off).
- [ ] **`ResolveStackTrace` on macOS** (`stack_trace_backtrace.cpp`, built for real for the first time: the Day 101 cross-syntax gap in
      `KNOWN-ISSUES.md`): function, file and line for a frame in the main executable, a frame in a dylib of ours, and a frame in a system library (name
      only); ASLR slide handled.
- [ ] **`InitDebugFacilities` and `ReloadDebugModules`:** the module list from `_dyld_image_count`/`_dyld_get_image_name`/`_dyld_get_image_header` and
      `_dyld_register_func_for_add_image`; a test `dlopen`s a library and finds it in the list.
- [ ] **`DemangleSymbol`** (`abi::__cxa_demangle`, through the existing wrapper rules) and **`CurrentThreadId`** (`pthread_threadid_np`).
- [ ] **`InitCrashContext`:** build id from the main image's `LC_UUID` as hex, `kern.osproductversion`, `machdep.cpu.brand_string` (absent on Apple silicon:
      `hw.model`, and the field says so), `hw.logicalcpu`, `hw.memsize`, the executable from `_NSGetExecutablePath`; the crash-context tests compare with
      `sysctl -n` output.
- [ ] **`crash_extract`** (`tools/crash_extract/resolve.cpp`'s platform branch): resolves a macOS report's frames through the dSYM, with `atos` as a
      cross-check in the test when present.
- [ ] **`debug_alloc_tests`** link with GNU ld's `--wrap=malloc` (`tests/debug/CMakeLists.txt:201`), which ld64 lacks: interpose with `DYLD_INTERPOSE`,
      count through a malloc zone, or keep it Linux-only with the reason in `KNOWN-ISSUES.md` (decided here; Day 283 inventory).
- [ ] **Conformance groups** *stack trace* and *crash context* of `tests/debug/BACKEND-CONFORMANCE.md` pass or skip with a reason.
- [ ] **Documentation:** `os/macos/debug/BACKEND.md` sections get "as built (Day 290)" notes; `docs/guide/foundation-debugging.md` and
      `porting-debugging.md` macOS sections (dSYM placement, `atos`); `debug_backend_lists` and `debug_cross_syntax_macos` pass; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote); shared-file runs filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the module list's lifetime against images added from another thread, the resolver's
      behavior without a dSYM, anything that allocates on a path the crash report uses.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** stack traces on macOS resolve function, file and line through the dSYM (tested in the main executable and a dylib), the crash context
matches `sysctl`, the stack-trace and crash-context conformance groups pass in three flavors, and the debugging guides describe the dSYM rules.
