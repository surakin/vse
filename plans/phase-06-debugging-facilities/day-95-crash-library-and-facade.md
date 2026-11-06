# Phase 6 / Day 95 — Vendoring the Crash Library & the Crash Facade

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 91-94.

Crash capture and minidumps come from the library Day 91 chose (written here as "the crash library"; proposed:
sentry-native with the Crashpad backend and no transport). This day vendors and builds it, and puts the neutral facade in
front of it.

## Tasks

- [ ] Vendor the crash library as source under `third_party/<name>/` at the pinned release: only what the chosen backend needs (for sentry-native with Crashpad: its `src/`, `include/`, `external/crashpad` with `mini_chromium` and the third-party pieces Crashpad needs, their CMake files), every `LICENSE`, and a `VENDORED.md` (upstream, pin, licenses of each nested project, what was pruned, size on disk). No submodule, no `FetchContent`; the vendoring script or the commands used are recorded so the next upgrade is repeatable.
- [ ] CMake integration: `add_subdirectory` with the options Day 91 fixed (backend `crashpad`, transport `none`, no tests, no examples, static library), the `crashpad_handler` executable built and placed next to every program that installs the handler (a `engine_add_crash_handler(<target>)` function that adds the post-build copy), warnings from the third-party code kept out of ours, the library not batched by `CMAKE_UNITY_BUILD`, and the flavor decision of Day 91 (one build linked into plain, ASan and TSan programs, or three).
- [ ] Add `engine/foundation/debug/crash_handler.h`: the neutral `CrashReason` enum (access violation, bus error, illegal instruction, arithmetic fault, stack overflow, abort, assert, ensure), `CrashHandlerOptions` (database/dump directory defaulting to the `os::` crash-dump directory, never the working directory; handler path; attach-report-file; enable/disable), `InstallCrashHandler(options) -> Result`, `UninstallCrashHandler()`, and `Result` with `Unsupported`. No library type, no signal number in the header.
- [ ] Add the one implementation file `engine/foundation/debug/crash_handler_sentry.cpp` (named for the chosen library), the only file that includes its header (a C header, so it compiles under our production flags: no STL, no exceptions): map `CrashHandlerOptions` to the library's options, force the `none` transport, set the database path and handler path, init and close. Extend the gate of Day 94 to ban the library's header everywhere else, with a planted-violation fixture.
- [ ] No-upload guarantee: a ctest that fails if the built test or demo binary links `libcurl`, a TLS library or `winhttp` (`ldd`/`readelf -d` on Linux), and a unit test that the options struct has no URL or DSN field; document the guarantee in the guide.
- [ ] Add `os::GetCrashDumpDirectory()` (Linux: `$XDG_STATE_HOME/<app>/crashes`, falling back to `~/.local/state/<app>/crashes`, created with mode 0700; the app name comes from the executable name or `ENGINE_APP_NAME`), with `Unsupported`-style failure reporting for a platform without a writable location.
- [ ] Smoke test (death-test helper): a child installs the handler and crashes with `InjectFault(NullWrite)`; the parent finds exactly one `.dmp` in the database directory within a deadline, the file starts with the `MDMP` signature, the child's exit status is the original fault, and the temp directory is removed after. Run in plain, ASan and TSan flavors (or recorded as plain-only with the reason from Spike D).
- [ ] Failure paths: the handler executable missing (installation returns an error and the program still runs, with a logged warning); an unwritable dump directory; installing twice; uninstall then crash (default behavior, no dump). Tests for each.
- [ ] **Documentation:** `///` comments; the guide gets "Installing the crash handler" and "Where dumps go, and what is never sent"; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, which runs everything because CMake changed), then asan and tsan flavors, and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (our CMake, facade, tests; vendored sources reviewed only for what was pruned or patched); extra attention to the handler-executable lookup and the dump directory permissions; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the crash library builds from vendored source under our CMake; a child that crashes leaves a `.dmp` in the dump directory and still dies with the original fault; nothing in the binary can upload; the library header appears in exactly one engine file, enforced by the gate; the tests pass in the flavors Day 91 allowed.
