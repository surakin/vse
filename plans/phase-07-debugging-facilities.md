# Phase 7 — Debugging Facilities

**Depends on:** Phase 6 (Test scoping), so this phase's own days re-run only their tests. Uses Phases 0-2 (logging and
assertions, process and file wrappers, threads).
**Goal:** when the engine or a game crashes, fails an assertion, or hits a state that should not happen, the developer
gets a readable stack trace with function, file and line, a dump file to open in a debugger, and macros that distinguish
"abort here" from "report and carry on". Built on existing libraries (below) instead of hand-written crash machinery.
Linux is implemented; Windows (Phase 13) and macOS (Phase 23) are left ready; every interface is shaped so no later
platform forces a redesign.
**Definition of done:** a crash (signal, `ENGINE_ASSERT`, unhandled error) prints a symbolized stack trace and the
engine's context (build id, platform, last log lines) and leaves a minidump holding every thread; an `ENGINE_ENSURE`
that fails logs once with a trace and execution continues; a test process that crashes on purpose (in a child process)
produces both, and the test reads the report and opens the dump with `lldb`; a script turns a dump into a readable trace;
nothing is ever uploaded; all of it is documented and covered by tests in plain, ASan and TSan flavors, with the handler
off by default in release builds unless enabled.

Every day of this phase has its own file under `plans/phase-07-debugging-facilities/` (see `CLAUDE.md`, planning
policy). **Status: complete (2026-11-06).** Days 91-102 are done; the closing full run (`ran 2371 of 2371 tests (phases: all; flavors: all; display: included)`) and the measurements are in [Day 102](phase-07-debugging-facilities/day-102-docs-and-closing-run.md). Phase 8 needs its own day files before it starts; `CURRENT_PHASE` changes in the commit that starts it.

## Days

Each day has its own file of individually checkable sub-tasks, a docs task, an end-of-day `/code-review high` task, and a
concrete Done condition. Day numbers continue from Phase 6 (which ended at Day 90).

91. [Library Selection & Spikes] (done)(phase-07-debugging-facilities/day-91-library-selection-and-spikes.md)
92. [Death-Test Helper & the `tests/debug` Suite] (done)(phase-07-debugging-facilities/day-92-death-test-helper.md)
93. [Assert, Ensure, Verify & the Handler Chain] (done)(phase-07-debugging-facilities/day-93-assert-family.md)
94. [Vendoring libbacktrace & the Stack-Trace Facade] (done)(phase-07-debugging-facilities/day-94-libbacktrace-and-stack-traces.md)
95. [Vendoring the Crash Library & the Crash Facade] (done)(phase-07-debugging-facilities/day-95-crash-library-and-facade.md)
96. [What a Crash Report Contains] (done)(phase-07-debugging-facilities/day-96-crash-report-content.md)
97. [Crash Handling End to End] (done)(phase-07-debugging-facilities/day-97-crash-handler-integration.md)
98. [Core Dumps] (done)(phase-07-debugging-facilities/day-98-core-dumps.md)
99. [Dump Tooling & Symbols] (done)(phase-07-debugging-facilities/day-99-dump-tooling-and-symbols.md)
100. [Wiring the Engine In] (done)(phase-07-debugging-facilities/day-100-subsystem-integration.md)
101. [Windows & macOS Readiness] (done)(phase-07-debugging-facilities/day-101-windows-macos-readiness.md)
102. [Documentation Sweep, Closing Full Run & Wrap-up] (done)(phase-07-debugging-facilities/day-102-docs-and-closing-run.md)

Order reasoning: Day 91 chooses the libraries by spike, because everything after depends on it. The death-test helper
comes next because every later day tests code that aborts or crashes; the assert family follows because every other phase
uses it; each library is then vendored and put behind a facade on its own day; the report content, the end-to-end crash
path, core dumps and dump tooling build on those; integration with the existing subsystems comes when the pieces are
stable; the Windows and macOS readiness day comes after the Linux code it mirrors is final.

## Libraries (`PLAN.md` assumption #20; chosen on Day 91)

Decided 2026-11-06: do not hand-write what well-known libraries already do. Two libraries, vendored as source under
`third_party/` with a pinned release, a `VENDORED.md` and the license, built from source by our CMake, used only through a
facade in `engine/foundation/debug/` (exactly one implementation file includes each library's header, enforced by a gate):

| Job | Library | Why |
|---|---|---|
| Stack capture, symbol names, file and line (ELF, Mach-O, PE with DWARF) | **libbacktrace** (BSD, C) | documented async-signal-safe; unwinds through code without frame pointers; one library on every desktop platform; C interface |
| Crash capture and minidumps (every thread) | **sentry-native 0.17.6** (MIT, CMake, C API) with its **Breakpad** backend and the `none` transport | chosen by Day 91's spikes: CMake build; in-process, so no handler executable to ship; no network libraries linked; every thread in the dump; `on_crash` hook and attachments work. The Crashpad backend was rejected (its Linux handler needs libcurl and ~25 shared libraries); the experimental `native` backend is the fallback |

**Nothing is uploaded.** Dumps stay in a directory on the developer's disk; a test fails if a network library is linked.

**What stays ours** because it is engine-specific or small: the assert/ensure/verify family and its handler chain; the
crash context (build id, platform summary), the recent-log ring buffer and the state-provider registry; the death-test
helper (Catch2 has none); the core-dump setup and the scripts that open a core or turn a dump into a trace.

**Dropped from the earlier plan** (the libraries do it): an own frame-pointer stack walker, an own ELF symbol reader, an
own minidump writer and reader, Mach-O and PE readers, an own crash signal handler, and `tools/symbolize`
(`llvm-symbolizer` and `addr2line` already exist). Known gap carried to Phase 13: libbacktrace reads DWARF only, not PDB.

## Platform design (PLAN.md assumption #11)

**Scope (decided 2026-11-06):** this phase implements **Linux only**. It leaves **Windows (Phase 13) and macOS (Phase 23)
ready**: the libraries already support them, so "ready" means a facade and tests free of Linux assumptions, per-backend
checklists (`BACKEND.md`) for the few platform-specific functions, a backend conformance list the same test sources
satisfy, cross-target syntax checks, and a porting guide (Day 101). Android, iOS and the consoles (Phases 24 and 25) are
**designed in** (the interfaces do not preclude them and `Unsupported` is a valid answer) and get no work here.

Rules applied in every day file:

- **Neutral headers.** No public header under `engine/foundation/debug/` names `ucontext_t`, a signal number, `pid_t`,
  `/proc`, a library type or a platform header. A crash is a `CrashReason` enum plus the raw platform code; thread ids
  are `u64`; output goes through a small sink type. File and process access reuse the `os::` seam. A gate
  (`tools/platform_neutral_check`) enforces this from Day 92.
- **Death tests re-execute the test binary** (no `fork`, which Windows lacks and which is unsafe in a multithreaded
  process); platforms that cannot spawn (iOS, consoles) report `Unsupported` and the tests skip.
- **Tests speak the neutral API** (`InjectFault(FaultKind)`, `ChildResult::reason`), so Phases 13 and 23 build the same
  sources and make the same groups pass.
- **No assumption of a command line, a filesystem layout or a process model.** Dumps go to an `os::`-provided directory.
- **Compiler differences stay in one header** (`debug/compiler.h`: debug break, unreachable hint, noinline).

| Facility | Linux (this phase) | Windows (8) | macOS (9) | Android / iOS (17) | Consoles (18) |
|---|---|---|---|---|---|
| Stack capture and symbols | libbacktrace (unwinder, DWARF) | libbacktrace with DWARF builds; PDB needs a DbgHelp resolver (Phase 13 decision) | libbacktrace + dSYM | libbacktrace (Android); iOS dSYM | SDK |
| Crash capture and dump | crash library, out-of-process handler | same library (exception filter inside it) | same library (Mach exceptions inside it) | the library's `inproc` backend (Android); iOS TBD | console repos of the library (NDA) |
| Core-dump equivalent | cores / `systemd-coredump` | WER `LocalDumps` | `/cores`, crash reports | tombstones; none on iOS (`Unsupported`) | SDK |
| Child process for death tests | re-exec (`posix_spawn`) | `CreateProcess` re-exec | `posix_spawn` | no spawn on iOS: tests skip | skipped |

## Key deliverables

- **Stack traces.** `CaptureStackTrace`, `ResolveStackTrace`, `FormatStackTrace` over libbacktrace; function, file and line;
  inlined frames; async-signal-safe after a startup call; demangling outside signals.
- **Crash capture.** `InstallCrashHandler` over sentry-native: dumps with every thread, a text report with the engine
  context written from the library's `on_crash` hook, the original fault still delivered, no upload.
- **Assert, ensure and verify macros.** `ENGINE_ASSERT` (always on, fatal, ends in `abort()`), `ENGINE_ENSURE` (reports
  once per site with a trace, returns the condition, counted), `ENGINE_VERIFY`, `ENGINE_CHECK_SLOW`,
  `ENGINE_UNREACHABLE`, `ENGINE_NOT_IMPLEMENTED`, a pluggable `SetAssertHandler`, and a debugger break at the failing line.
- **Crash context.** Build id, platform summary, a lock-free ring of the last log lines, state providers (GPU, frame,
  window backend), all read-only in the crash path.
- **Core dumps.** `EnablePostMortemDumps`, scripts to find the latest core and open it in `gdb` with a build-id check.
- **Dump tooling.** `scripts/crash-report.sh` (lldb and, when present, `minidump-stackwalk`), a symbol store for release
  builds, and automated `lldb` checks of the dumps.
- **Death-test support.** `RunInChild`, `InjectFault`, `ChildResult` with a neutral `CrashReason`.
- **Integration.** The logger stamps a build id; worker threads register for crash handling; RHI device loss and
  validation errors attach a trace; the demos install the handler (`--crash-handler`, `--crash-test`).
- **Windows and macOS readiness.** `BACKEND.md` checklists, the conformance list, cross-target checks, the porting guide.
- **Closing run.** The phase's last day runs `scripts/test-phase.sh --all --flavor all` (every test in plain, ASan and
  TSan), `ctest -L docs` and the demos on both display backends, and records the `ran X of Y tests` line in its day file
  (Phase 6 policy; day-to-day runs are scoped to the current phase).

## Notes / open questions

- **Which crash library** was settled by Day 91's spikes (`docs/superpowers/specs/2026-11-06-debugging-design.md`):
  sentry-native's Breakpad backend. Its risks: the handler is in-process (a corrupted heap can lose the dump; Breakpad dumps
  from a `clone`d child with its own allocator), the minidump arrives wrapped in sentry's envelope (a small tool extracts it),
  and the API is Sentry-shaped (hidden behind our facade). Fallbacks: sentry's `native` backend, or Crashpad with a patch
  that removes libcurl.
- ASan/TSan install their own handlers; the crash library must cooperate (chain to the previous handler) and may be
  switchable off in sanitizer builds. Spike D settles it.
- Wayland/X11 and the GPU driver run their own threads; a fault inside a driver should still produce a dump with every
  thread and a report naming the faulting thread.
- Privacy and size: dumps contain memory of the process; document where they are written; they are never uploaded.
- libbacktrace has autotools only: our CMake wrapper (Day 94) is the one piece of build glue we own.
