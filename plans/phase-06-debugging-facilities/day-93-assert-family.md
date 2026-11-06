# Phase 6 / Day 93 — Assert, Ensure, Verify & the Handler Chain

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Day 92 (the helper tests the abort paths).

Replace the single `ENGINE_ASSERT` with a family of macros and one handler chain that tests, the crash library and a
debugger can all hook, without breaking the 334 existing call sites.

## Tasks

- [x] Define the failure record (platform-neutral: no `errno`, no signal numbers): `AssertInfo { kind, file, line, function, condition_text, message }` (all `const char*`, no allocation) and the kinds `Assert`, `Ensure`, `Verify`, `Unreachable`, `NotImplemented`.
- [x] Add `SetAssertHandler(handler) -> previous` and the handler type `AssertAction (*)(const AssertInfo&)` returning `Abort`, `Continue` or `Break`; the default handler prints one line (`ASSERT FAILED: file:line function: condition: message`, truncated at 512 bytes, to the shared write sink as before), then returns `Break` if a debugger is attached and `Abort` otherwise. Thread-safe: the handler pointer is an atomic; calling it takes no lock.
- [x] Compiler and architecture differences live in one header, `engine/foundation/debug/compiler.h`: `ENGINE_DEBUG_BREAK()` (`int3` on x86-64, `brk #0xf000` on AArch64, `__debugbreak()` on MSVC, `__builtin_debugtrap()` where clang has it), `ENGINE_UNREACHABLE_HINT()` (`__builtin_unreachable()` / `__assume(0)`), `ENGINE_NOINLINE`, and the printf-format attribute; nothing else in the phase names a compiler builtin. Platform differences for debugger detection sit behind `IsDebuggerAttached()` (Linux and Android: `TracerPid` in `/proc/self/status` read with raw syscalls, cached for at most a second; the sketches for Windows `IsDebuggerPresent` and macOS/iOS `sysctl` `P_TRACED` go in the spec). The break macro stops the debugger at the failing line in the caller, not inside the handler. Test the pieces separately; the break itself is exercised only in the child of a death test with a stub that records it.
- [x] Macros in `engine/foundation/log/assert.h` (one include for all of them): `ENGINE_ASSERT(cond, msg)` unchanged in meaning (always on, fatal; messages stay string literals, there are no format strings), `ENGINE_ENSURE(cond, msg)` (evaluates `cond` once, returns it as a `bool`, reports once per call site with the stack trace of Day 94 (libbacktrace) once it exists and a counter, then continues), `ENGINE_VERIFY(expr, msg)` (always evaluates `expr`; checked only when `ENGINE_DEBUG_CHECKS` is set), `ENGINE_CHECK_SLOW(cond, msg)` (compiled out unless `ENGINE_SLOW_CHECKS`), `ENGINE_UNREACHABLE(msg)` (fatal in debug, `__builtin_unreachable()` in release), `ENGINE_NOT_IMPLEMENTED()`. Define how `ENGINE_DEBUG_CHECKS`/`ENGINE_SLOW_CHECKS` follow the CMake build type, and an option to force them on in a release build.
- [x] Per-site ensure state: a function-local `static` atomic flag and counter per call site (no registry allocation); a global registry of sites would need the heap, so keep an intrusive linked list of the static records, linked on first failure with a compare-and-swap, so `EnsureReport()` can list every site that failed with its count. Test concurrent first failures from 8 threads: exactly one report per site.
- [x] Keep the old test hooks working: `SetExitSinkForTesting` and `SetWriteSinkForTesting` stay, marked deprecated for new tests; migrate `container_vector_test.cpp` to `SetAssertHandler` (handler throws or `longjmp`s as that test needs) and delete the exit sink when no test uses it. `AssertFail` now ends in `abort()` after the handler returns `Abort` (so the crash handler and a debugger see `SIGABRT`); document the change from exit code 1.
- [x] Sweep the call sites: no mechanical rewrite of the 334 existing asserts, but classify the ones that guard recoverable runtime conditions (for example allocator failure after `TryReserve`) and list them in the design notes as `ENSURE` candidates; convert only those in `engine/foundation` where the caller can really continue, with a test for each converted site.
- [x] Tests (in `tests/debug`, using the death-test helper): a failing `ENGINE_ASSERT` in a child dies with `SIGABRT` and prints the one-line report; the handler that returns `Continue` lets an assert pass in-process (the capture test); `ENGINE_ENSURE` returns the condition, reports once for 1000 failures at one site and once each for two sites, and counts correctly; `ENGINE_VERIFY` evaluates its expression in release configuration (compile the same test with the check macro off, as a separate small target or a `#define` before the include); `ENGINE_CHECK_SLOW` does not evaluate its operand when off (a counter proves it); `ENGINE_UNREACHABLE` and `ENGINE_NOT_IMPLEMENTED` abort in a child; a handler installed on one thread is seen by another; messages longer than the buffer are truncated without overrun (ASan).
- [x] **Documentation:** `///` comments on every new symbol with `Aborts:` and `Thread-safety:` labels; update `docs/guide/foundation-logging-and-assert.md` with the family, when to use which, and an example of a capture handler in a test (tagged sample that compiles); rewrite the `Fails:`/`Aborts:` convention paragraph in `docs/DOC-CONVENTIONS.md` to name `ENGINE_ASSERT` versus `ENGINE_ENSURE`; docgen reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, then `--flavor asan`, `--flavor tsan`) and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; fix or decline each finding with a reason; record it here under "Post-implementation code review" and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the six macros and `SetAssertHandler` exist with the semantics above, all existing tests pass unchanged except the migrated one, the new tests pass in plain, ASan and TSan, `AssertFail` aborts with `SIGABRT`, and the guide, conventions page and reference are updated.

## Result

Code: `engine/foundation/log/assert.h` (the macro family, `AssertInfo`/`AssertKind`/`AssertAction`, `SetAssertHandler`,
`DefaultAssertHandler`, the ensure-site list API), `log.cpp` (handler chain, `AssertFail`), `debug/compiler.h`
(`ENGINE_DEBUG_BREAK`, `ENGINE_UNREACHABLE_HINT`, `ENGINE_NOINLINE`, `ENGINE_COLD`), `debug/debugger.h` with the Linux
`os/linux/debug/debugger.cpp`; CMake options `ENGINE_FORCE_DEBUG_CHECKS` / `ENGINE_FORCE_SLOW_CHECKS`. Tests:
`tests/debug/assert_family_test.cpp`, `assert_linux_test.cpp`, and a separate executable `debug_checks_off_tests` built with
the checks defined off. Docs: guide `foundation-logging-and-assert.md` (which macro to use, a compiled capture-handler
sample), `DOC-CONVENTIONS.md` (`Aborts:` versus `Fails:` for assert versus ensure), reference regenerated
(`foundation/log` 62/62, `foundation/debug` 66/66).

Deviations from the plan above:

- `ENGINE_ENSURE` is an expression macro built on an immediately invoked lambda holding a function-local static record, so
  it works as `if (!ENGINE_ENSURE(...))`; it is not usable in a `constexpr` function. The function name is passed in
  as an argument because `__func__` inside the lambda would name the lambda.
- No re-entrancy guard in the assert path: production code is built `-fno-exceptions`, so a handler that throws (the
  migrated tests do) skips cleanup of any guard state. A handler that fails an assertion recurses until the stack ends.
- The exit sink was deleted (the plan says to delete it once no test uses it): `log_test.cpp` and `container_vector_test.cpp`
  moved to `SetAssertHandler`. `AssertFail` stays as a function (the RHI null device calls it) and always aborts.
- `ENGINE_DEBUG_CHECKS` and `ENGINE_SLOW_CHECKS` default from `NDEBUG`; the CMake options force them on globally. They are
  per-translation-unit settings, so a new rule of the platform-neutral gate (renamed in spirit, same tool) bans
  `ENGINE_VERIFY`, `ENGINE_CHECK_SLOW` and `ENGINE_UNREACHABLE` in headers.
- `ENGINE_UNREACHABLE` always ends in `abort()`: a handler's `Continue` is turned into `Abort`, and a debugger that resumes
  after the break is followed by `UnreachableAbort()`.
- The `debug_checks_off_tests` executable is not built when either FORCE option is on (no translation unit can have the
  checks off then).
- Phase 6 now has 152 tests: 48 plain, 48 ASan, 48 TSan, 4 `[checks off]`, 4 gate cases.

Call-site survey (the plan asks for `ENGINE_ENSURE` candidates): 335 `ENGINE_ASSERT` sites outside the macro definition:
151 in `engine/rhi`, 118 in `engine/renderer`, 28 in `engine/jobs`, 19 in `tools/docgen`, 17 in `engine/foundation`, 2 in
`game/common`. The 17 foundation sites are all contracts the caller cannot survive: allocator out of memory in
`Vector::Reserve`/`Resize` (the functions return void; continuing would write out of bounds), `Vector::Pop` on an empty
vector, `Normalized()`/`Inverse()` of a zero-length vector or quaternion (documented `Aborts:`; continuing yields NaN),
`CommandLine` index and setup-order checks, `RunInChild` null arguments. **None was converted to `ENGINE_ENSURE`**:
none can continue without changing a documented contract. The sites in `rhi`, `renderer`, `jobs` and `tools` are outside
the day's conversion scope (the plan limits it to `engine/foundation`); they were not individually classified.

Verification (final): `scripts/test-phase.sh --affected` plain `ran 925 of 2024 tests (phases: all; flavors: plain; display: included)`
with one failure, `X11Window::Create opens a mapped window on the real X server` (display, phase 1), which also fails
with a binary built before the day's changes and is recorded in KNOWN-ISSUES; asan `ran 682 of 2024 tests (... flavors: asan ...)`
and tsan `ran 417 of 2024 tests (... flavors: tsan ...)`, all pass. `ctest -L phase6 -j16 --repeat until-fail:20`, four rounds:
no failure (80 runs of 152 tests). Also passing: `-DENGINE_UNITY_BUILD=OFF`, clang 23.1.4, a Release build
(152 of 152; the jobs/foundation Release tests ran too, see KNOWN-ISSUES for one pre-existing Release failure), and Release with
both FORCE options. `ctest -L docs` 4/4, `docgen lint` at zero known findings.

Release builds found one bug in Day 92's code: `FaultKind::DivideByZero` did not fault when optimized, because GCC turns
`1 / x` into compares and emits no divide; both operands are volatile now.

Mutation checks (each reverted, `cmp` verified): `AssertFailed` no longer turns an unreachable `Continue` into an abort
(test fails); the default handler aborts for ensure (2 tests fail); the truncated line loses its newline (test fails);
the unreachable macro without the abort after the break (test fails); the first failure decided by the wrapping count
(test hangs on the cyclic site list and times out). A first attempt at one mutation was blocked by the harness because it
included `git checkout` on a file with uncommitted work; nothing ran, and it was redone with copies.

## Post-implementation code review

`/code-review high` on the day's diff, 9 findings:

1. `ENGINE_UNREACHABLE` could fall through when the handler answered Break and the debugger resumed. **Fixed**: the macro
   ends in `UnreachableAbort()`; test: `A debugger that resumes after ENGINE_UNREACHABLE breaks is followed by the abort`.
2. The ensure "first failure" test used a `u32` that wraps, which could link a site twice and make the list cyclic. **Fixed**:
   a separate `reported` flag decides; test with a directly constructed site whose count is set to wrap.
3. The debugger cache was two relaxed atomics (time and answer). **Fixed**: one word holds both.
4. Any tracer (`strace`, `rr`) counts as a debugger, so an assert under them breaks (SIGTRAP) instead of aborting.
   **Declined**: telling debuggers from other tracers needs the tracer's name, which is brittle. Documented on
   `IsDebuggerAttached`.
5. `ENGINE_ASSERT` is no longer known to the compiler as non-returning, and a few call sites assume it never returns.
   **Declined**: with the default handler it never returns; only a test handler that answers Continue can resume, and the
   `AssertAction::Continue` documentation now says code after a failed assert may assume the condition held.
6. The per-translation-unit macros could cause ODR differences in header-inline code. **Fixed**: the gate bans them in
   headers (planted-violation fixture added); nothing uses them in a header today.
7. The exit sink was deleted although the plan says keep-then-delete, and the day record, `STATUS.md` and the verification
   were missing. **Fixed**: the plan says to delete it when no test uses it, and none does; this section, `STATUS.md` and
   the numbers above are the rest.
8. The `ENGINE_ASSERT` expansion is large at 335 sites. **Fixed in part**: the failure functions are `noinline` and `cold`;
   no size measurement was taken.
9. `DefaultAssertHandler` crashed on null strings. **Fixed**: null counts as empty; test added.
