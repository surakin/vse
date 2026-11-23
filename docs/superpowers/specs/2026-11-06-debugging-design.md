# Debugging facilities — library selection and design (Phase 7, Day 91)

Status: decided 2026-11-06 from the spikes below. Scope: Linux implemented, Windows (Phase 13) and macOS (Phase 21) made
ready, mobile and consoles designed in. `PLAN.md` assumption #20 is the policy exception this spec supports.

## 1. Decision

| Job | Choice | Pinned |
|---|---|---|
| Stack traces, symbols, file and line | **libbacktrace** | commit `0b9b49cf4a2c9229fc052d6716e1528b2f23e91a` (2026-09-03; the project makes no releases) |
| Crash capture and minidumps | **sentry-native** with the **Breakpad** backend and transport `none` | release **0.17.6** (2026-11-04), the `sentry-native.zip` asset (submodules included) |

The earlier proposal named the **Crashpad** backend. The spikes rejected it for this project (section 4): on Linux
sentry's Crashpad needs **libcurl** at build time and its `crashpad_handler` executable needs libcurl and about 25 more
shared libraries at run time, so a shipped game would fail to start its handler on a machine without them, and "no
network code" would not be true. The Breakpad backend has neither problem.

Stays hand-written: the assert/ensure/verify family, the crash context and recent-log ring buffer, the death-test
helper, the core-dump setup and scripts, and one small dev tool that extracts the minidump from sentry's envelope.

Fallbacks, in order: (a) sentry-native's **native** backend once it stops being experimental (out-of-process, libc-only
daemon, writes a `.dmp` directly; worked in the spike); (b) the **Crashpad** backend with a patch that removes
`http_transport_libcurl.cc` and the libcurl link (an `[engine patch]`, one file plus CMake lines), if out-of-process
capture is wanted. Switching is a CMake option plus the two lines in the facade's implementation file.

## 2. Evaluation criteria (written before testing)

License compatible with shipping in a game binary; builds from vendored source with CMake, offline; size and warnings
under GCC 16 and clang 23; C interface; no network or telemetry code; dumps every thread; crash path async-signal-safe or
out of process; works for a fault on any thread, a stack overflow and a corrupted heap; coexists with the sanitizers;
lets us attach our own report and recent log lines; lets us choose the dump directory; Linux, Windows, macOS and Android
with the same API and a console path; maintained.

## 3. Inventory (what exists today)

- `AssertFail` (`engine/foundation/log/log.cpp`) prints `ASSERT FAILED: file:line message` to fd 1 and calls
  `os::Exit(1)`: it does not abort, so no debugger or crash handler sees a signal. 334 `ENGINE_ASSERT` sites
  (`engine`, `game`, `tools`); one test redirects the exit (`tests/foundation/container_vector_test.cpp`, through
  `SetExitSinkForTesting`).
- No signal handler anywhere; sanitizer options only in the two `lsan_options.cpp` files.
- Vendoring pattern: Catch2 and nlohmann/json under `third_party/` with a `VENDORED.md` (pinned release, license,
  what was pruned), no submodule, no `FetchContent`.
- Tools on the development machine: `lldb` 23.1.4 (reads minidumps), `gdb`, `coredumpctl`, `addr2line`,
  `llvm-symbolizer`, `readelf`, clang 23.1.4, GCC 16.2.1, CMake 4.4.5, Ninja. Not installed: `minidump-stackwalk`,
  `dump_syms`, `strace`, lld.
- Upstream activity on 2026-11-06: sentry-native 0.17.6 published 2026-11-04; google/breakpad last commit 2026-10-15;
  Crashpad commits daily; libbacktrace last commit 2026-09-03.

## 4. Spike results

All spikes ran in a scratch directory from downloaded sources (nothing under `engine/` or `third_party/` changed), on
x86-64 Linux, GCC 16.2.1, with the same crash program: segv (null write), abort, illegal instruction, divide by zero,
stack overflow, a fault on a `std::thread`, and a fault with four other threads alive. Transport `none` and no DSN.

### Spike A — sentry-native, Crashpad backend

- Builds in 14 s (311 ninja steps), no warnings; static `libsentry.a` 3.97 MB plus Crashpad libraries; 299 source files /
  70.6 kLoC compiled. Needs `find_package(CURL REQUIRED)` (`external/crashpad/util/CMakeLists.txt`, Linux branch).
- All 7 modes: exactly one `.dmp` in `<db>/pending/`, original signal in the exit status (139, 134, 132, 136), 14.8-33 KB
  (stack overflow 141 KB). `lldb -c` shows the faulting thread, source file and line, and all 5 threads in the
  multi-thread case. A dump is written without any DSN configured.
- `on_crash` ran in the crashing process; a file given to `sentry_options_add_attachment` was copied next to the dump.
- **`crashpad_handler`** (987 KB stripped, 22 MB unstripped) links `libcurl.so.4` and, through it, libssl, libcrypto, krb5,
  nghttp2/3, ngtcp2, ssh2, brotli, zstd, idn2 and others (about 25 libraries) and contains HTTPS strings even with
  `SENTRY_TRANSPORT=none`. It must be placed next to the program and found at run time.
- Verdict: works, robust (out of process), but fails the no-network and ship-without-extra-libraries criteria unless
  patched.

### Spike B — the alternatives

- **Breakpad backend (chosen).** Builds in 4 s, no warnings, 135 source files / 37.5 kLoC compiled, `libsentry.a` 3.45 MB +
  `libbreakpad_client.a` 2.75 MB + libunwind 0.9 MB; the linked program depends only on libstdc++, libm, libgcc and libc.
  No handler executable. All 7 modes produce a valid minidump (`MDMP`) of every thread (45-165 KB) that `lldb` opens with
  file and line, original signal in the exit status, `on_crash` and attachments work, 76 ms from start to exit. **The
  dump is inside sentry's `.envelope` file** (an attachment item, `attachment_type: event.minidump`); the `.crash` file next
  to it is empty. Extracting it is about ten lines (envelope = JSON header line, then item-header line + payload of the
  stated length, repeated). Stripped program grows by 576 KB.
- **native backend.** Builds in 6 s, no warnings; needs its daemon `sentry-crash` (3.7 MB, libc only) passed as the handler
  path; writes `__sentry-crash.dmp` (290-390 KB) that `lldb` opens, all threads; 187 ms to exit. Marked experimental in
  the docs, so not chosen now.
- **inproc backend.** Not tested for dumps: it writes no minidump (documented; it is the Android default).
- **Breakpad client directly.** It is C++ with `std::string` in its public interface and has no C API, so the facade file
  would need the STL; sentry-native's C API avoids that. The CMake wrapper it needs already exists in sentry's
  `external/breakpad`. Not pursued.
- **Crashpad directly.** Evaluated on paper only: GN build and `mini_chromium`; sentry's fork is what the CMake build above
  uses, and has the libcurl dependency.

### Spike C — libbacktrace

- Autotools build, 0 warnings, `libbacktrace.a` 537 KB, 16.8 kLoC. `config.h` needs `dl_iterate_phdr`, `mmap`, atomics
  (`HAVE_ATOMIC_FUNCTIONS`/`HAVE_SYNC_FUNCTIONS`), `clock_gettime`, `getipinfo` and the optional `zlib`/`zstd`/`liblzma`
  (compressed debug sections); `BACKTRACE_USES_MALLOC` is 0 (mmap allocator). A CMake wrapper has about ten feature
  checks; leaving zlib/zstd/lzma out only loses compressed DWARF sections.
- Traces from a normal call, a `std::thread` and a `dlopen`ed library resolve function, file and line, including an
  **inlined** frame (two names at one address); a stripped binary falls back to symbol names without file:line; frames
  built with `-fomit-frame-pointer` unwind correctly (it uses the compiler's unwinder, not frame pointers); a trace taken
  inside a `SIGSEGV` handler on an alternate stack crosses the signal frame and starts at the faulting line.
- **Zero `malloc`/`calloc`/`realloc` calls during the trace inside a handler** (counted with `--wrap`), with a warm-up before
  and also with the first call inside the handler. The first call walks `dl_iterate_phdr`, so the facade makes it at startup.
- Quirks the facade must handle: libc frames come back with no function name from `backtrace_full` (fall back to
  `backtrace_syminfo`); the unwinder appends a bogus last frame with pc `0xffffffffffffffff` (drop it).
- A resolved trace costs 0.6 µs once the object files are cached. Works unchanged in programs built with
  `-fsanitize=address,undefined` and `-fsanitize=thread` (library built plain).
- Paper comparison: **cpptrace** is signal-safe only on Linux with libunwind and glibc 2.35+, not on Windows or macOS;
  **backward-cpp** is not signal-safe (its handlers call `malloc`).

### Spike D — coexistence

- With the Crashpad backend a handler of ours runs in either install order (it chains to the previous handler) and the
  dump is still written, also in an ASan program.
- With the Breakpad backend a handler installed **before** `sentry_init` is replaced and never runs; installed **after**, it
  runs and chains to Breakpad's. Rule for anything of ours: install after the library, always chain.
- **Neither is needed:** the library's `on_crash` hook runs in the crashing process on the Crashpad, Breakpad and native
  backends, and attachments are carried along. The report is written in `on_crash` into a file that was added with
  `add_attachment` at install time. Sentry prints `[sentry] DEBUG ...` lines to stderr by default; its logger must be
  routed to our log (`sentry_options_set_logger`) or silenced.
- Sanitizers: the uninstrumented library inside an ASan program installed over ASan's SEGV handler and wrote the dump.
  TSan was not run with the library: Day 95 tests it.

### Spike E — core dumps

`core_pattern` is `|systemd-coredump`, `ulimit -c` unlimited; every crash of the spikes was captured (`coredumpctl list`),
`coredumpctl dump -o core` produced a core that `gdb` opens ("Core was generated by ..."), and a program can raise its soft
`RLIMIT_CORE` from 0 to the hard limit with `setrlimit`. The library's handler re-raises the original signal, so a minidump
**and** a core are both produced. (The spikes left about 25 small cores in the systemd-coredump store; they age out.)

## 5. Decisions

1. **Crash capture:** sentry-native 0.17.6, Breakpad backend, transport `none`, static library, no tests or examples.
2. **Stack traces:** libbacktrace at the pinned commit, CMake wrapper written by us (about ten feature checks).
3. **Hand-written:** assert family, crash context + ring buffer + providers, death-test helper, core-dump setup and scripts,
   the envelope extraction tool (`tools/crash_extract`, a dev tool like `docgen`, never linked into the engine).
4. **Facade:** neutral headers in `engine/foundation/debug/`; exactly two implementation files include a library header
   (`stack_trace_backtrace.cpp` for `backtrace.h`, `crash_handler_sentry.cpp` for `sentry.h`, both C headers so they compile
   under the production flags, no STL); `tools/platform_neutral_check` enforces it with planted-violation fixtures.
5. **No handler executable.** The Breakpad backend is in-process, so there is nothing to ship next to the program and no
   `engine_add_crash_handler`; the options struct still has a handler-path field for backends that need one.
6. **Dump location and extraction:** `sentry_options_set_database_path` is `os::GetCrashDumpDirectory()`; the minidump of a
   crash is the `event.minidump` item of `<db>/<run>.run/<id>.envelope`; `tools/crash_extract` writes it as `<id>.dmp` and
   `scripts/crash-report.sh` does that first.
7. **Report path:** one `on_crash` hook writes the report (to the report sink, and to `<db>/crash-report.txt` that was
   attached at install). No signal handler of ours. The library's logger is routed to `LogCore`.
8. **No upload:** transport `none`, no DSN field in our options, and a ctest that fails if a binary links libcurl, a TLS
   library or winhttp. Verified in the spike: the linked program has only libstdc++/libm/libgcc/libc.
9. **Sanitizers:** the crash handler is installed only when asked in the sanitizer flavors (their own reports are the
   artifact); a smoke test installs and crashes under ASan; the TSan run is a Day 95 task. One uninstrumented build of each
   library is linked into every flavor (ASan program + plain libraries worked in the spikes).
10. **Release builds:** handler off by default unless `ENGINE_CRASH_HANDLER=1` or the flag is given.

## 6. Risks and mitigations

| Risk | Mitigation |
|---|---|
| sentry-native changes license, direction or API | MIT, vendored and pinned; the facade is one file; fallbacks (a) and (b) above; the C API surface used is about 10 functions |
| The Breakpad backend is in-process, so a corrupted heap or a fault inside the handler can lose the dump | Breakpad dumps from a `clone`d child with its own allocator; the report hook uses no heap and is guarded against re-entry; the death tests include a second fault; fallback (a)/(b) for out-of-process |
| The dump is wrapped in sentry's envelope | `tools/crash_extract` (about 100 lines, tested); the envelope format is documented and the item is typed |
| sentry-native writes more than we need (session files, breadcrumbs, event JSON) | `auto_session_tracking` off, no DSN, no scope data; the database directory is documented as owned by the library |
| Vendored tree size (135 files / 37 kLoC for the chosen backend; 11 MB of Breakpad tree before pruning) | prune to what the build uses, record it in `VENDORED.md`; both libraries built once |
| Vendored code's warnings under our flags | built as separate targets with their own flags, like Catch2; kept out of unity batches |
| libbacktrace reads DWARF only | known gap for Phase 13 (clang/DWARF builds, or a DbgHelp resolver behind `ResolveStackTrace`) |
| A sanitizer swallows the signal | handler off by default in sanitizer flavors; `AssertFail` prints its own report and aborts |
| `on_crash` runs in a signal context | the report writer is allocation-free, lock-free and gated by `tools/signal_safety_check` |

## 7. Draft vendoring records (finalized on Days 94 and 95)

**`third_party/libbacktrace/VENDORED.md`:** upstream `https://github.com/ianlancetaylor/libbacktrace`; pin
`0b9b49cf4a2c9229fc052d6716e1528b2f23e91a`; license BSD-3-Clause (`LICENSE`, unmodified); kept: the 20 `.c`/`.h` files
named in Day 94 and `backtrace-supported.h`; added: `CMakeLists.txt` replacing autotools; no source edits.

**`third_party/sentry_native/VENDORED.md`:** upstream `https://github.com/getsentry/sentry-native`, release **0.17.6** from
the `sentry-native.zip` asset (submodules included, no network at build time); license MIT (`LICENSE`) plus Breakpad's
BSD-3-Clause (`external/breakpad/LICENSE`) and libunwind's MIT license (`vendor/libunwind`); kept: `src/`, `include/`,
`external/breakpad` (client and common sources the build compiles), `vendor/libunwind`, their CMake files; pruned: tests,
examples, benchmarks, `external/crashpad`, `external/libunwindstack-ndk`, `ndk/`, fuzzing; built with
`-DSENTRY_BACKEND=breakpad -DSENTRY_TRANSPORT=none -DSENTRY_BUILD_SHARED_LIBS=OFF -DSENTRY_BUILD_TESTS=OFF
-DSENTRY_BUILD_EXAMPLES=OFF -DSENTRY_ENABLE_INSTALL=OFF`; upgrade: re-download the asset, re-apply the prune list.

## 8. Platform notes (desk research, for Windows, macOS, Android, iOS, consoles)

- **Windows (Phase 13):** sentry-native supports x86/x64/arm64 (MSVC 2022+, LLVM-MinGW) with its Crashpad (default),
  Breakpad and inproc backends; the Breakpad backend is "desktop OSes only", which includes Windows; the Windows Crashpad
  handler uses winhttp, not libcurl. libbacktrace reads DWARF only, so MSVC/PDB builds need either clang with DWARF or a
  DbgHelp resolver behind `ResolveStackTrace`; `IsDebuggerPresent`, `CreateProcess` for death tests, NTSTATUS decode for
  `CrashReason`, WER `LocalDumps` as the core-dump equivalent.
- **macOS (Phase 21):** supported (macOS 15+, Apple Clang or LLVM Clang 15+); Breakpad backend available; libbacktrace
  needs the dSYM next to the binary; code signing and entitlements for the handler apply to the Crashpad backend only;
  `sysctl` `P_TRACED` for debugger detection; `posix_spawn` death tests shared with Linux.
- **Android / iOS (Phase 29):** Android API 21+, NDK 23+, default backend `inproc` (no minidump); iOS is not in the
  supported list (the support matrix marks Breakpad untested there): designed in, `Unsupported` where needed.
- **Consoles (Phase 30):** separate sentry repos for PlayStation, Xbox and Switch, under NDA.

The neutral-interface design (a `CrashReason` enum plus the raw platform code, an opaque native context, `u64` thread ids,
a sink type instead of a descriptor, `Unsupported` results) is unchanged from the plan and fits all of the above.

## 9. Changes this spec makes to the later days

- Day 94: libbacktrace facade handles the missing-function and bogus-last-frame cases; the first call at startup.
- Day 95: breakpad backend (no handler executable, no `engine_add_crash_handler`); route the library's logger; TSan smoke
  test; add `tools/crash_extract`.
- Day 96: the report is written from the `on_crash` hook into the attached file (no handler of ours).
- Day 97: no signal handler of ours; the install-order rule is moot; the safety gate covers the hook.
- Day 99: `crash-report.sh` calls `crash_extract` first.
- Day 101: the Windows/macOS audit records the above notes; `BACKEND.md` lists no handler-executable entry.

## 10. Platform audit (Phase 7 Day 101; read from the vendored sources, not from memory)

Checked in `third_party/sentry_native/` (0.17.6) and `third_party/libbacktrace/` (commit `0b9b49c`).

| | Linux (built and tested) | Windows (Phase 13) | macOS (Phase 21) | Android / iOS (Phase 29) | Consoles (Phase 30) |
|---|---|---|---|---|---|
| sentry-native Breakpad backend | supported | supported (README matrix) | supported (README matrix) | "(✓)": builds, untested upstream | separate repos, NDA |
| Transport `none` | supported | supported | supported | supported | n/a |
| Default backend upstream | Crashpad | Crashpad (Breakpad for Xbox/WINGDK) | Crashpad | `inproc` (writes no minidump) | n/a |
| What we set | `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`, static library | the same; no handler executable with Breakpad (Crashpad would need one and uses winhttp, not libcurl); `SENTRY_THREAD_STACK_GUARANTEE_*` exist for stack overflow | the same | `breakpad` is untested there; a Phase 29 decision | not decided |
| `on_crash` hook arguments | `uctx` is **null**, hook runs in a **helper thread** (read from `sentry_backend_breakpad.cpp` and Breakpad's `exception_handler.cc`) | `uctx` carries the `EXCEPTION_POINTERS` | `uctx` carries the user context (non-system Breakpad) | untested | n/a |
| libbacktrace object format | ELF (`elf.c`, built) | PE/COFF (`pecoff.c` + `alloc.c`, vendored, not built) | Mach-O (`macho.c`, vendored, not built) | ELF (Android), Mach-O (iOS) | n/a |
| Symbols | DWARF | **DWARF only**: PDB is not read | DWARF in a dSYM next to the binary | DWARF | n/a |

What each platform needs that Linux does not:

- **Windows:** a build id reader (PE CodeView GUID and age; `-Wl,--build-id` has no effect), the unhandled-exception filter and
  vectored handler are installed by the library, fast-fail crashes bypass SEH and are not seen by `on_crash` (a documented
  gap), `SetThreadStackGuarantee` for a stack overflow, a job object for death-test children, PowerShell equivalents of the
  scripts, and the DWARF-or-PDB decision for symbols.
- **macOS:** the library takes Mach exceptions, so the Linux signal recorder is not used and the report is written from
  `on_crash` (which receives the context); dSYM bundles next to the binary; `/cores` is normally disabled; the hardened runtime
  and notarization restrict handlers; `PR_SET_PDEATHSIG` does not exist (death-test children need another way to die with the
  parent).
- **Android / iOS:** Breakpad is untested upstream on both; Android's default backend writes no minidump; iOS has no supported
  list entry. Both are "designed in" (`Unsupported`, skipped death tests) and decided in Phase 29.
- **Consoles:** the crash libraries are under NDA; the neutral facade forwards to them; decided in Phase 30.

Known gaps and the decision each needs: **(1)** PDB symbols on Windows (Phase 13: clang with DWARF, or a DbgHelp resolver behind
`ResolveStackTrace`); **(2)** file and line on macOS need the dSYM (Phase 21: ship or locate it); **(3)** the Windows and macOS
crash report is written from `on_crash`, so the Linux recorder (`os/linux/debug/crash_signal.cpp`) has no counterpart and
`detail::InstallCrashEventHook` is not implemented there; **(4)** Android and iOS minidumps (Phase 29); **(5)** consoles
(Phase 30). The lists per function are `engine/foundation/os/windows/debug/BACKEND.md` and
`engine/foundation/os/macos/debug/BACKEND.md`, kept in step with the headers by `debug_backend_lists`.

**Correction to section 9 (as built).** Days 96 and 97 do write the report from a small signal recorder placed in front of
Breakpad's handler on Linux, not from the `on_crash` hook: on Linux the hook gets no signal information and runs on a helper
thread, so the fault address, the faulting thread's name and its stack are not available there (day 97 file). On Windows and
macOS the hook does receive the context, so the report moves back into the hook there.

**What the cross-target syntax check found (Day 101).** `ENGINE_NOINLINE_LAMBDA` expanded to `[[msvc::noinline]]` for every
compiler that defines `_MSC_VER`, which clang rejects on a lambda when it targets Windows. It is now `__declspec(noinline)`.

