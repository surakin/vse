# Phase 7 / Day 94 — Vendoring libbacktrace & the Stack-Trace Facade

**Phase:** [Phase 7 — Debugging Facilities](../phase-07-debugging-facilities.md)
**Depends on:** Days 91-93.

Stack capture and symbolization come from libbacktrace (`PLAN.md` assumption #20, confirmed on Day 91). This day vendors
it, builds it under our CMake, and puts our small facade in front of it so the rest of the engine never sees it.

## Tasks

- [x] Vendor libbacktrace as source under `third_party/libbacktrace/` at the commit Day 91 pinned (`0b9b49cf4a2c9229fc052d6716e1528b2f23e91a`, no releases exist; draft `VENDORED.md` is in the spec, section 7): only the files needed (`backtrace.c`, `dwarf.c`, `elf.c`, `fileline.c`, `macho.c`, `mmap.c`, `mmapio.c`, `pecoff.c`, `posix.c`, `print.c`, `read.c`, `simple.c`, `sort.c`, `state.c`, `alloc.c`, `atomic.c`, `unknown.c`, `internal.h`, `backtrace.h`, `backtrace-supported.h`), its `LICENSE`, and a `VENDORED.md` (upstream URL, pin, license, what was pruned, what was added). No submodule, no `FetchContent`.
- [x] Write `third_party/libbacktrace/CMakeLists.txt` replacing its autotools: the `config.h` it needs from `check_symbol_exists`/`check_include_file` (`HAVE_DL_ITERATE_PHDR`, `HAVE_SYS_MMAN_H`, `HAVE_ATOMIC_FUNCTIONS`/`HAVE_SYNC_FUNCTIONS`, `HAVE_CLOCK_GETTIME`, `HAVE_GETIPINFO`, `HAVE_LINK_H` and the others in the Spike C list; `BACKTRACE_USES_MALLOC` stays 0; zlib, zstd and lzma are left off, which only loses compressed DWARF sections: say so in `VENDORED.md`), the object formats enabled per platform (ELF and Mach-O and PE sources compiled everywhere or only where the platform needs them, as Spike C decided), one static library `backtrace` built once and linked into the plain, ASan and TSan flavors (Day 91, Spike C: an instrumented program calling the plain library worked in all three), the library's own warning flags, and no `-Werror`. The vendored sources are never edited; if one must be, the change is listed in `VENDORED.md` as an `[engine patch]` like Catch2's.
- [x] Add `engine/foundation/debug/stack_trace.h`: `StackTrace` (fixed capacity of 64 `u64` addresses, a count, a `truncated` flag), `CaptureStackTrace(StackTrace&, skip_frames)`, `ResolveStackTrace(const StackTrace&, FrameInfo* out, usize count)` giving per frame the module, function name, file and line (inlined frames included; a frame with no function name from `backtrace_full`, such as libc, falls back to `backtrace_syminfo`; the unwinder's bogus last frame with pc `0xffffffffffffffff` is dropped), and `FormatStackTrace(trace, char* out, usize capacity)` with one fixed line layout (`#N  0xADDR  function  file:line`, falling back to `module+offset` or the raw address). Neutral: no libbacktrace type in the header.
- [x] Add the one implementation file `engine/foundation/debug/stack_trace_backtrace.cpp`, the only file that includes `backtrace.h`: one `backtrace_state` created at first use or at `InitDebugFacilities()` (the first call walks the loaded objects with `dl_iterate_phdr` and is not async-signal-safe, so startup makes it; the crash path only reuses it), an error callback that records the message instead of printing, `backtrace_simple` for capture, `backtrace_pcinfo`/`backtrace_syminfo` for resolution. No frame-pointer requirement: the compiler's unwinder is used, so no change to `ENGINE_PRODUCTION_COMPILE_OPTIONS` is needed (confirm against Spike C).
- [x] The library gate: extend the Day 92 platform-neutral gate (`tools/platform_neutral_check`) so a file includes `backtrace.h` only if it is `stack_trace_backtrace.cpp`, with a planted-violation fixture.
- [x] Demangling: the formatter demangles names with the C++ ABI's `__cxa_demangle` only in the non-signal path (it allocates), with a fixed-size output buffer and a fallback to the mangled name; the signal path prints mangled names.
- [x] Wire the assert family: a failing `ENGINE_ASSERT` and the first failure of each `ENGINE_ENSURE` site print the formatted trace under the message (Day 93 left the hook).
- [x] Tests (in `tests/debug`): a call chain of known depth resolves to the right function names and source lines in a `-g` build (a planted function at a known line of a test source); a trace from a `std::thread`; from a job-system coroutine resumed on a worker; a stripped copy of the test binary falls back to `module+offset`; a symbol in a `dlopen`ed library is found; a trace of 10000 frames is truncated and flagged; formatting never overruns (ASan, capacities 0, 1, 80, 4096); the capture-and-resolve path run inside a `SIGUSR1` handler (child process) completes and, with a counting allocator, allocates nothing after the startup call; the test also runs under ASan and TSan, and the behavior with fake stacks (`detect_stack_use_after_return`) is recorded.
- [x] Build checks: the library compiles without warnings we own under GCC and clang in plain, unity and sanitizer builds; `-DENGINE_UNITY_BUILD=ON` does not batch the third-party target (`SKIP_UNITY_BUILD_INCLUSION` or its own directory setting, recorded in `VENDORED.md`).
- [x] **Documentation:** `///` comments with `Thread-safety:` and `Fails:` labels; `docs/guide/foundation-debugging.md` created with "Capturing and reading a stack trace" (tagged compiled sample); the module `foundation/debug "Foundation: Debugging"` added to `docs/reference/modules.txt` with the strict rules; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`; the day changes CMake, so it runs everything), then `--flavor asan` and `--flavor tsan`, and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (our CMake wrapper, the facade, the tests; the vendored sources are reviewed only for what was pruned or patched); record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** libbacktrace builds from vendored source under our CMake in all flavors; `stack_trace.h` captures, resolves and formats a trace without any library type in sight; assert and ensure print traces with file and line; the tests pass in plain, ASan and TSan; the gate keeps `backtrace.h` out of every other file.

## Result

Code: `third_party/libbacktrace/` (the 18 source files, 3 headers and `LICENSE`/`README.md` of the pinned commit, our
`CMakeLists.txt`, `config.h.in`, `VENDORED.md`), `engine/foundation/debug/stack_trace.h` and its only libbacktrace user
`stack_trace_backtrace.cpp`, `debug/module_list.h` with `os/linux/debug/module_list.cpp`, the assert handler printing a trace
(`log.cpp`), `compiler.h` additions. Tests: `tests/debug/stack_trace_test.cpp`, `stack_trace_alloc_linux_test.cpp`,
`plugin.cpp`. Docs: guide section "Capturing and reading a stack trace" with a compiled sample, reference regenerated
(`foundation/debug` 100/100), lint at zero.

Deviations from the plan above:

- libbacktrace carries its own zlib/zstd/lzma decompressors, so there is no system library to decide on (the spec said
  otherwise); the CMake wrapper has about twelve feature checks and only the Linux (ELF) branch.
- New public API: `debug/module_list.h` (`FindLoadedModule`, `RefreshLoadedModules`). libbacktrace has no "module of an
  address" call and the module-plus-offset fallback needs one; the Linux backend snapshots `dl_iterate_phdr` into an
  immutable table swapped in by an atomic pointer (readers, including signal handlers, take no lock). The table is sized by
  counting the loaded objects.
- `ReloadDebugModules()` exists because libbacktrace reads the loaded objects once: a library loaded afterwards is not
  symbolized until the state is rebuilt (the old state is leaked deliberately; the library cannot free one).
- `FormatStackTrace` resolves one address at a time into a small array and streams the text, so a trace of 64 frames needs
  about 8 KB of stack, not the 100 KB a full `FrameInfo` array would.
- The default assert handler prints the trace with names left mangled (demangling allocates, and an assertion can fail
  inside the allocator) and writes message and trace with one `write` call.
- The stripped-binary test is a separate ctest (`debug_stack_trace_stripped`) that runs a `strip`ped copy of `debug_tests`
  with `ENGINE_EXPECT_STRIPPED_BINARY=1` (the test case skips in the normal executable), instead of a death-test child
  (the helper starts only the running executable).
- The signal-handler allocation test is its own executable per flavor (`debug_alloc_tests`, `_asan`, `_tsan`), wrapping
  `malloc`/`calloc`/`realloc` with `--wrap`; it counts `malloc`-family calls only, not `mmap`.
- No `g_trace_error_count`: libbacktrace's error callback is a no-op (a problem shows up as `module+offset` or `??`).
- The plan's "no heap and no lock" is "never calls `malloc`; takes libbacktrace's internal lock; maps memory with `mmap`".
- The Windows/macOS branches of the libbacktrace wrapper stop with a message naming Phases 12 and 13. The docs sample link
  line (`tools/docgen/CMakeLists.txt`) and its `docs` target now include `backtrace`.
- No `-fno-omit-frame-pointer` change was needed (the unwinder is the compiler's). ASan with `detect_stack_use_after_return`
  on or off, and TSan, give the same traces: the unwinder walks the real stack, where return addresses stay.

Verification (final): `scripts/test-phase.sh --affected` plain `ran 942 of 2073 tests (phases: all; flavors: plain; display: included)`
with the one known display failure (`X11Window::Create opens a mapped window`, KNOWN-ISSUES), asan `ran 698 of 2073 tests
(... flavors: asan ...)`, tsan `ran 433 of 2073 tests (... flavors: tsan ...)`, all else passes. Phase 7 has 201 tests; `ctest -L phase7 -j16
--repeat until-fail:10`, six rounds: no failure. Also passing: `-DENGINE_UNITY_BUILD=OFF`, clang 23.1.4, a Release build
(201 of 201), Release with both FORCE options (69). `ctest -L docs` 4/4.

A real bug the new concurrency test found: with the first version of `InitDebugFacilities`, which published the state before
its warm-up finished, eight threads starting together hung about a third of the time in the TSan flavor under parallel load
(many children killed after 12 s). Cause not investigated beyond the ordering; publishing the state only after the warm-up
(in `InitDebugFacilities` and `ReloadDebugModules`) gave 0 hangs in 72 parallel children and 60 loop runs. The Release
build also found that GCC turns `return AssertFailed(...)` in `EnsureFailed` into a tail call, which removed a frame and
made the trace skip the failing function; a compiler barrier after the call keeps the frame (the ensure test checks the first
frame).

Mutation checks (each reverted, `cmp` verified): `skip_frames` off by one (two tests fail); the inlined flag never set (test
fails); the bogus top-of-address-space frame kept (test fails, after adding the check).

## Post-implementation code review

`/code-review high` on the day's diff, 10 findings:

1. Raw `__asm__` in `log.cpp` and a lambda `noinline` placement invalid for MSVC. **Fixed**: `ENGINE_COMPILER_BARRIER()` and
   `ENGINE_NOINLINE_LAMBDA` in `compiler.h`; the tests use the barrier too. (The `ud2`/`udf` asm in the Linux fault injection
   backend stays: it is platform code.)
2. The frame skip counts are fixed (2 for assert, 4 for ensure), wrong behind a wrapper. **Declined**: a wrapper only adds
   harmless frames at the top, a symbol-based skip is fragile, and both real paths are tested in Debug and Release. Documented
   on `DefaultAssertHandler`.
3. The handler demangled, which allocates, inside an assertion failure. **Fixed**: names stay mangled there.
4. The module table stopped at 128 entries. **Fixed**: sized by counting the loaded objects; a test checks that every object
   the loader reports is found.
5. The "no heap, no lock" contract was stronger than libbacktrace gives and the warm-up too small. **Fixed in part**: docs now
   say "never calls `malloc`, takes the internal lock, maps with `mmap`"; the warm-up takes and resolves a trace. `mmap` is not
   counted by the test.
6. No stripped-binary test; the signal test ran only in the plain flavor. **Fixed**: the stripped-copy ctest and the alloc
   executable in all three flavors.
7. `InitDebugFacilities` and `ReloadDebugModules` raced. **Fixed**: a mutex, and the state is published after warm-up (which
   also fixed the hang above).
8. The inlined marker was lost when a chain was cut, and long names were truncated at 191 bytes. **Fixed**: a cut chain keeps
   its last entry marked inlined; names keep 511 bytes and fewer entries per frame (8).
9. Message and trace were two writes. **Fixed**: one buffer, one write (a test checks one sink call).
10. `g_trace_error_count` was dead state. **Fixed** by removing it (see the deviation above).
