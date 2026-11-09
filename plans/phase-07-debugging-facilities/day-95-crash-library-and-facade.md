# Phase 7 / Day 95 — Vendoring sentry-native & the Crash Facade

**Phase:** [Phase 7 — Debugging Facilities](../phase-07-debugging-facilities.md)
**Depends on:** Days 91-94.

Crash capture and minidumps come from sentry-native 0.17.6 with its Breakpad backend and no transport (`PLAN.md`
assumption #20; chosen on Day 91, see `docs/superpowers/specs/2026-11-06-debugging-design.md`). This day vendors and
builds it, puts the neutral facade in front of it, and adds the small tool that pulls the minidump out of sentry's
envelope.

## Tasks

- [x] Vendor sentry-native as source under `third_party/sentry_native/` from the `sentry-native.zip` asset of release 0.17.6 (downloaded into its own empty directory; submodules are in the asset): keep `src/`, `include/`, `CMakeLists.txt`, `cmake/`, `external/breakpad` (what the build compiles), `vendor/libunwind`, their CMake files and every `LICENSE`; prune tests, examples, benchmarks, fuzzing, `ndk/`, `external/crashpad`, `external/libunwindstack-ndk`. Write `VENDORED.md` from the spec's draft (upstream, pin, the licenses of each nested project, the prune list, size on disk, the exact configure options, the upgrade steps). No submodule, no `FetchContent`.
- [x] CMake integration: `add_subdirectory(third_party/sentry_native)` with `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`, `SENTRY_BUILD_SHARED_LIBS=OFF`, no tests, no examples, no install; the vendored targets get their own warning flags and are kept out of `CMAKE_UNITY_BUILD` batches; one uninstrumented build is linked into the plain, ASan and TSan programs (ASan worked in Day 91 Spike D; **TSan is tested here**: build a crashing program under TSan, record the result, and if it fails document that the handler is off in the TSan flavor).
- [x] Add `engine/foundation/debug/crash_handler.h`: `CrashReason` (already defined by Day 92 in `debug/crash_reason.h`; this header includes it), `CrashHandlerOptions` (dump directory defaulting to `os::GetCrashDumpDirectory()`, never the working directory; the report-file path to attach; an optional handler path for backends that need one (empty for Breakpad); enable/disable; **no URL or DSN field**), `InstallCrashHandler(options) -> Result`, `UninstallCrashHandler()`, and a `Result` with `Unsupported`. No library type, no signal number in the header.
- [x] Add the one implementation file `engine/foundation/debug/crash_handler_sentry.cpp`, the only file that includes `sentry.h` (a C header: no STL, no exceptions, so it compiles under the production flags): map the options to sentry's (database path, release = build id, session tracking off, no DSN, an attachment for the report file), route sentry's logger to `LogCore` (it prints `[sentry] DEBUG` lines to stderr by default), install the `on_crash` hook that Day 96 fills in, `sentry_init`/`sentry_close`. Extend the Day 94 gate so `sentry.h` is allowed only in this file, with a planted-violation fixture.
- [x] Add `tools/crash_extract/` (a dev tool like `docgen`, never linked into the engine): reads a sentry `.envelope` (a JSON header line, then item-header line + payload of the stated length, repeated) and writes the `event.minidump` item as `<id>.dmp`; exits non-zero on a malformed or truncated envelope without reading out of range. Tests: an envelope from a crashing child; a byte-flip and truncation sweep under ASan (no crash, no out-of-range read); an envelope with no minidump item; two minidump items.
- [x] No-upload guarantee: a ctest that fails if a built test or demo binary links `libcurl`, a TLS library or `winhttp` (`ldd`/`readelf -d` on Linux; Day 91 measured only libstdc++, libm, libgcc and libc), and a unit test that `CrashHandlerOptions` has no URL or DSN field (a `static_assert` on `offsetof`/field names via a trait is not possible, so the test checks the documented field list and the header text); document the guarantee in the guide.
- [x] Add `os::GetCrashDumpDirectory()` (Linux: `$XDG_STATE_HOME/<app>/crashes`, falling back to `~/.local/state/<app>/crashes`, created with mode 0700; the app name comes from the executable name or `ENGINE_APP_NAME`), with a failure result for a platform without a writable location.
- [x] Smoke test (death-test helper): a child installs the handler and crashes with `InjectFault(NullWrite)`; the parent finds exactly one envelope in the database directory within a deadline, `crash_extract` turns it into a file that starts with `MDMP`, the child's exit status is the original fault, and the temp directory is removed after. Run in plain, ASan and TSan flavors (or recorded as plain-only with the reason found above).
- [x] Failure paths: an unwritable dump directory (install returns an error and the program still runs, with a logged warning); installing twice; uninstall then crash (default behavior, no dump); a crash before `InstallCrashHandler` (no dump, original signal). Tests for each.
- [x] **Documentation:** `///` comments; the guide gets "Installing the crash handler" and "Where dumps go, and what is never sent"; `third_party/sentry_native/VENDORED.md` linked from the guide; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, which runs everything because CMake changed), then asan and tsan flavors, and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (our CMake, facade, tool, tests; the vendored sources reviewed only for what was pruned or patched); extra attention to the envelope parser and the dump directory permissions; record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** sentry-native builds from vendored source under our CMake; a child that crashes leaves an envelope from which `crash_extract` produces a valid minidump, and still dies with the original fault; nothing in the binary can upload (checked by a test); `sentry.h` appears in exactly one engine file, enforced by the gate; the tests pass in the flavors recorded above.

## Result

Code: `third_party/sentry_native/` (release 0.17.6, pruned to 6.8 MB in 699 files; `VENDORED.md`), the root `CMakeLists.txt`
configuration block, `engine/foundation/debug/crash_handler.h` and its only sentry user `crash_handler_sentry.cpp`,
`os::EnsureDirectory`, `os::EnsurePrivateDirectory`, `os::GetCrashDumpDirectory`, `tools/crash_extract/` (envelope parser,
CLI, fixtures, script test), `tests/debug/check_no_network.cmake`. Tests: `crash_handler_test.cpp`,
`crash_handler_linux_test.cpp`, `crash_extract_test.cpp` and the `debug_no_network_*` ctests; the parser is compiled into every
flavor of the test executables. 283 Phase 7 tests. Docs: guide sections "Installing the crash handler" and "Where dumps go,
and what is never sent", reference regenerated (`foundation/debug` and `platform/os` at 100%), lint at zero.

Deviations from the plan above:

- **The library is configured `SENTRY_TRANSPORT=none`, but the facade gives it a run-time transport that only copies each
  envelope into `<dump directory>/saved/<uuid>.envelope`.** With the null transport the library deletes the envelope of a
  crash on the next start of the program (found by the review and reproduced), which loses the dump. No network code exists
  either way. A crash is in `<uuid>.run/` until the next launch, then in `saved/`.
- `os::EnsurePrivateDirectory` was added (real directory, not a symlink, owned by the user, tightened to 0700, writable); the
  crash handler uses it for every dump directory, default or given.
- The vendored tree keeps all of sentry's `src/`, the whole libunwind source, and for Breakpad the source files named by
  sentry's CMake for every platform plus the headers the Linux build includes (Windows and macOS private headers must be
  re-vendored in Phases 13 and 23).
- The tests run the crash flows in all three flavors: the child's sanitizers are told not to handle faults, so Breakpad's
  handler runs, and the dump is written and valid in the ASan and TSan flavors too.
- The `debug_no_network_*` check covers the test binaries and `crash_extract`; the shipped demos and benches get theirs
  on Day 100 (task added there). It also ignores symbols a sanitizer runtime defines (clang's static runtime defines
  interceptors named `gethostbyname`); a canned-text test covers both the imports and the defines-only case.
- No `engine_add_crash_handler`: the Breakpad backend has no handler executable.
- The docs sample link line and the `docs` target now list `sentry`, `breakpad_client` and `unwind`.

Verification (final): `scripts/test-phase.sh --affected` plain `ran 976 of 2155 tests (phases: all; flavors: plain; display: included)`
with the one known display failure (`X11Window::Create opens a mapped window`, KNOWN-ISSUES), asan `ran 722 of 2155 tests
(... flavors: asan ...)`, tsan `ran 457 of 2155 tests (... flavors: tsan ...)`, everything else passes. `ctest -L phase7 -j16
--repeat until-fail:10`, four rounds: no failure. Also passing: `-DENGINE_UNITY_BUILD=OFF`, clang 23.1.4, a Release build
(283 of 283). `ctest -L docs` 4/4.

Mutation checks (each reverted, `cmp` verified): the parser's payload bound removed (six tests fail); its integer-overflow
check removed (fails only after adding a wrap-to-small case: `18446744073709551619`); symlinks followed, the mode not
tightened, `..` accepted as an application name (each fails its test); the no-DSN message not filtered (the silent-install
check fails). A mutation of the application-name check left a stray `/tmp/crashes` directory that made the next run fail; the
test now keeps its state directory one level inside its own temporary directory, and the stray directory was removed.

## Post-implementation code review

`/code-review high` on the day's diff, 9 findings:

1. **A crash dump was deleted the next time the program installed the handler.** Reproduced, then **fixed** with the saving
   transport; test: `A crash dump is still there after the program is started again` (a crash, then two launches).
2. The library's logger was never called (it needs the `debug` flag on). **Fixed**: `debug` on, level warning; verified by
   raising the level temporarily. That exposed a warning on every start about the empty server address, which is now
   filtered, with a test that a normal install prints nothing.
3. An existing dump directory was accepted without checking owner, mode or symlink. **Fixed** with `EnsurePrivateDirectory`
   (tests for the symlink, tightening, a file, a read-only directory, and the handler refusing a symlinked or read-only dump
   directory).
4. `ENGINE_APP_NAME` could be `.` or `..` and was cut at 255 bytes silently. **Fixed**: refused; test.
5. `GetCrashDumpDirectory` claimed to be thread-safe but reads the environment, and a test changed the environment in the
   multithreaded test process. **Fixed**: the doc says another thread must not call `setenv`; the test runs in a child.
6. `crash_extract` could leave a partial output and leaked the file on a failed write. **Fixed**: written to `.partial` and
   renamed, always closed, removed on failure; script test.
7. `crash_extract` derived an output name for any input and could overwrite its input. **Fixed**: a derived name only for
   `*.envelope`; the output may not be the input; script test.
8. The no-upload gate covers only test binaries. **Partly fixed**: the guide says exactly what is covered; Day 100 adds the
   shipped programs.
9. A read-only existing dump directory passed `EnsureDirectory`. **Fixed** by the same writability check (finding 3).
