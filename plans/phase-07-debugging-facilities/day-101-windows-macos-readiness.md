# Phase 7 / Day 101 — Windows & macOS Readiness

**Phase:** [Phase 7 — Debugging Facilities](../phase-07-debugging-facilities.md)
**Depends on:** Days 91-100.

Phase 7 builds and tests Linux only. Because the two libraries already support Windows and macOS, "ready" now means: the
facade and the tests are free of Linux assumptions, the few platform-specific pieces are listed with the API to use, and
the known gaps are written down. Phases 13 and 21 then wire up the build settings and the thin platform functions.
Mobile and consoles are designed in (a neutral interface, `Unsupported` as an answer) and get no work here.

## Tasks

- [x] Audit the facade against the Windows and macOS documentation of the chosen libraries (desk work; no code): that the pinned crash library and libbacktrace list Windows (x64, arm64) and macOS (arm64, x86-64) as supported at the pinned versions; the exact CMake options for each (`SENTRY_BACKEND=breakpad` on Windows and macOS; no handler executable with that backend; the Windows Crashpad handler would use winhttp, not libcurl; a debug build on Windows); and what each platform needs that Linux does not. Record the answer per platform in the spec; where a library does not support something, say so.
- [x] Record the known gaps and the decision each needs, in the spec and the Phase 13/13 files: libbacktrace reads DWARF only, not PDB (Windows builds with clang and DWARF work; MSVC/PDB builds need a DbgHelp resolver behind `ResolveStackTrace`, a Phase 13 decision); libbacktrace on macOS needs the dSYM next to the binary or a debug build; the library handles the Windows exception filter and macOS Mach exceptions itself; sentry-native's Breakpad backend is listed for desktop operating systems only, so Android (default `inproc`, no minidump) and iOS (not in the supported list) are Phase 29 questions; the console libraries are under NDA (Phase 30).
- [x] The platform-specific pieces left for Phases 13 and 21, as checklists `engine/foundation/os/windows/debug/BACKEND.md` and `engine/foundation/os/macos/debug/BACKEND.md`: the build-id reader (PE CodeView GUID+age, Mach-O `LC_UUID`), `GetCrashDumpDirectory`, the death-test spawn (`CreateProcess`; `posix_spawn` shared with Linux), the child-result decode to `CrashReason` (NTSTATUS codes `0xC0000005` and the like; the signal on macOS), `InjectFault` per `FaultKind`, debugger detection (`IsDebuggerPresent`, `sysctl` `P_TRACED`), the system-info functions of the crash context, `RegisterThreadForCrashHandling`, and post-mortem dumps (WER `LocalDumps`, `/cores`). Each entry gives the neutral signature, the API, the trap (what an exception filter may call, stack overflow handling, hardened runtime), and the conformance groups that exercise it. Every neutral function a platform implements carries a `Backend:` doc label; a ctest (`debug_backend_lists`) fails if a labeled function is missing from either checklist.
- [x] Backend conformance list `tests/debug/BACKEND-CONFORMANCE.md`: every test group, the neutral function it exercises, the `CrashReason` each `FaultKind` must produce, which groups may skip with `Unsupported`. Confirm `tests/debug` uses only the neutral API (the Day 92 gate passes; Linux-only details live in `*_linux_*` files).
- [x] Cross-target checks (from the Day 91 research, or Spike G if it was kept: shim headers for `<cstdint>`/`<cstddef>`/`<atomic>`): ctests `debug_cross_syntax_windows` (`x86_64-pc-windows-msvc`), `debug_cross_syntax_macos` (`arm64-apple-macos13`) compile the neutral headers and the facade sources, with the two libraries' headers on the include path, with `-fsyntax-only`; they skip with a reason where clang is missing, and are recorded in `KNOWN-ISSUES.md` if the shim approach proved impossible. They prove the facade has no hidden Linux dependency.
- [x] CMake: the platform selection for the debug sources (Day 92) has Windows and macOS branches that stop with a message naming Phase 13 or 14 and the `BACKEND.md` to implement.
- [x] Porting guide `docs/guide/porting-debugging.md`, a Windows section and a macOS section: the API sequence for each platform-specific piece, what to reuse unchanged (the facade, assert family, report writer, ring buffer, tests, scripts), the gaps above, and where each test expectation differs by platform. Code blocks that cannot compile on Linux are plain fences.
- [x] Replace the bullets in `plans/phase-13-windows-dx12.md` and `plans/phase-21-macos-metal.md` with the real remaining work, copied from the two `BACKEND.md` files; keep the Phase 29 and 30 notes short ("designed in, see Phase 7; the console crash libraries are under NDA").
- [x] **Documentation:** the `///` comments carry `Backend:` labels; the guide page above; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the audit and gaps are recorded per platform; `BACKEND.md` for Windows and macOS and the conformance list exist and are kept in step with the headers by a test; the facade compiles for the Windows and macOS targets (or the reason it cannot is filed); the porting guide is written; the Phase 13 and 21 files list exactly what is left for them.


## Result

Phase 7 has 489 tests (6 new this day: four `debug_backend_lists*`, two `debug_cross_syntax_*`; the docgen suite has one new case).
Scoped runs: `ran 1061 of 2365 tests (phases: all; flavors: plain; display: included)` (no failures this run), `ran 785 of 2365 ... asan`,
`ran 519 of 2365 ... tsan`; also the non-unity, clang, Release and Release-with-checks builds, repeats, `ctest -L docs`; `docgen lint`
2699/2699. The gdb core test (Day 98) failed once more in a loaded repeat run (`KNOWN-ISSUES.md`).

What exists:
- **Audit** (design spec section 10), read from the vendored sources and the libraries' own support tables: per platform, the CMake settings, what each needs that Linux does not, and the five known gaps with the decision each needs (PDB, dSYM, the report moving back into `on_crash` on Windows and macOS, Android/iOS minidumps, consoles). The spec's section 9 is corrected: Days 96-97 do use a recorder on Linux.
- **`BACKEND.md`** for Windows and macOS (`engine/foundation/os/<platform>/debug/`): one section per labeled function with the API, the trap and the conformance groups, plus the pieces with no neutral label.
- **`Backend:` doc label** (a new docgen label, in `DOC-CONVENTIONS.md`, with a parser test) on 13 neutral functions, and the ctest `debug_backend_lists` that fails if a labeled function has no heading in either checklist or a heading names an unlabeled one (a clean and a planted fixture tree).
- **`tests/debug/BACKEND-CONFORMANCE.md`**: the groups, the neutral functions, what may skip, and the `CrashReason` per `FaultKind` for each platform. `tests/debug` keeps to the neutral API (the Day 92 gate passes).
- **Cross-target syntax checks** `debug_cross_syntax_windows` and `debug_cross_syntax_macos` (clang `-fsyntax-only`, `-nostdlibinc`, shim standard headers in `tests/debug/cross_syntax/shim`): all the neutral headers, `os/os.h`, the platform seam headers and seven platform-free sources. They found a real portability bug on the first run: `ENGINE_NOINLINE_LAMBDA` used an attribute clang rejects when it targets Windows. Mutation check: a Linux header in a neutral header fails both.
- **CMake**: the Windows and macOS branches already stop with a message naming the phase and the `BACKEND.md`; both files now exist.
- **Porting guide** `docs/guide/porting-debugging.md`; **Phase 13 and 21 files** now carry the real remaining work; the Phase 29 and 30 notes are short.

## Post-implementation code review

`/code-review high` found 10 issues.

1. `ENGINE_NOINLINE_LAMBDA` was changed for real MSVC too, untested there. **Fixed**: `[[msvc::noinline]]` stays for `cl.exe`; clang (any target) takes the GNU attribute, which the Linux build already proves on lambdas.
2. The `RegisterThreadForCrashHandling` label said macOS uses `sigaltstack`, its checklist said the opposite. **Fixed** (name only; Mach exceptions need no alternate stack).
3. Windows on arm64 does not trap on an integer divide either. **Fixed** in the label, the checklist, the conformance table and the guide.
4. The gate matched a name anywhere in a checklist. **Fixed**: only names on `### ` headings count; a message distinguishes "no section" from "stale name" (the planted-count test checks both).
5. `os/os.h` and the platform seam headers were not in the cross-target translation unit. **Fixed** (they compile).
6. Decoding an abort from exit code 3 alone is ambiguous with `exit(3)`, and `__fastfail` exits with another code. **Fixed** in the checklist: a marker written by a `SIGABRT` handler in the child; `__fastfail` is not a `FaultKind`.
7. The macOS checklist suggested an `atexit` kill, which does not run when the parent dies. **Fixed** (a pipe the child watches).
8. Bookkeeping missing from the diff. Part of this commit.
9. The cross-target checks skip silently without clang. **Fixed**: recorded in `KNOWN-ISSUES.md`.
10. The shim headers are looser than the real libraries. **Accepted and stated**: the checks are called a parse check, not a build, in the guide and in `KNOWN-ISSUES.md`.
