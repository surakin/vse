# Phase 16 / Day 219 — The Windows PC: Windows-Only Files and Both Compilers

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-218 pushed.

The files Linux cannot build (`engine/platform/windows`, `engine/rhi/dx12`, `engine/foundation/os/windows`, `win32_*`, `tests/windows`, `tests/rhi/dx12`) are ported on the Windows PC; shared code changed on Days 208-218 is built there for the first time.

## Tasks

- [x] `git pull` on the Windows PC; build `windows-msvc` (plain and asan) and `windows-clang-cl` (plain) in unity and `-DENGINE_UNITY_BUILD=OFF`; fix what Days 208-218 broke on MSVC (coroutine traits specialization, `constexpr` differences, `/W4 /WX` findings in the new headers); no warning suppressions.
- [x] Port the Windows-only files (list from Day 207): Win32 and COM calls stay (they are the OS boundary, not the standard library); only `std::` and standard includes move.
- [x] MSVC specifics: `Atomic<T>` for 64-bit types, `Mutex` over SRWLOCK versus `std::mutex` (keep the standard type inside `sync`, record the choice), coroutine frame allocation behavior, `swprintf` signature differences inside `cstr/format.h` (the wrapper hides them).
- [x] Run `scripts/test-phase.sh --all` on Windows (plain and asan) and the Direct3D 12 and Vulkan demos with validation.
- [x] Move the entries "Windows files still hold std (Phase 16)" out of `KNOWN-ISSUES.md`; record anything that still fails there with the observed fact.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the Windows-only files contain no `std::` or standard includes, both Windows compilers build clean in both unity modes, the Windows test run's final line is recorded, and `KNOWN-ISSUES.md` lists only what was observed to fail.

## Notes

- **Build after the pull** (Days 208-218 built on Windows for the first time): six problems, all fixed. `Atomic<T>::FetchAdd`/`FetchSub` were templates on the
  delta type, so `hit[i].FetchAdd(1)` on an `Atomic<u8>` narrowed an `int` inside the wrapper (C4244 under `/WX`); they now take `Atomic<T>::Delta` (`T`, or
  `isize` for a pointer) as `std::atomic` does, so a narrowing argument converts at the call. `tests/rhi/device_host_test.cpp` threw a non-copyable
  `engine_test::Text` (C5272); it now throws a copy of the message in a fixed buffer. `dx12_commands_test.cpp` still called `.c_str()` and streamed a
  `Text` (KNOWN-ISSUES had both). The rest was the port below.
- **The port:** 56 files (`engine/foundation/{os,io,settings}/windows/`, `engine/rhi/dx12/`, `engine/platform/windows/`, `engine/jobs/windows/`,
  `tools/{crash_extract,shader_mint}/windows/`, `tests/windows/`, the `*_windows*` tests of `tests/{debug,io,jobs,settings}`, `tests/rhi/dx12_*`): 601
  `std::` names and 109 standard includes replaced by the wrappers (`Atomic`/`MemoryOrder`, `Mutex`/`LockGuard`, `Thread`/`ThisThread`, `OnceFlag`/`CallOnce`,
  `MemCopy` and the `CStr*`/`WStr*` functions, `FormatTo`/`WFormatTo`/`ScanFrom`, `os::Print`/`os::GetEnvironment`/`os::Abort`/`os::ExitNow`/
  `os::SetSignalHandler`, `os::MonotonicTimeNanos` for the two `std::chrono` timings). By hand: `crash_report_script_windows_test.cpp` reads the script's
  output through `CreatePipe`/`CreateProcessW` instead of `_popen`/`fread`; `crash_end_to_end_windows_test.cpp` formats exception codes with `FormatTo`
  instead of `std::hex`; `os::Exit` on Windows calls the C library's `::exit` (an allow-list entry, as the Linux file has). New wrapper overloads:
  writable `CStrFindLastChar`, `WStrFind` and `WStrFindLastChar` (as `strrchr`/`wcsstr`/`wcsrchr` have), and `os::FlushAll()` (`fflush(nullptr)`, for the
  death-test child's exit). Win32 and COM calls are unchanged: they are the operating system's.
- **The allow list** loses its 22 Windows entries; it gains `engine/foundation/os/windows/os.cpp` (`::exit`, as Linux) and `tests/windows/discovery/` (a
  project of its own, built without the engine's include paths, that checks a program using the C++ library links with each compiler). `std_usage_check`
  passes with them, as do `platform_neutral_check`, `module_deps_check`, `file_access_check`, `config_access_check` and `raw_new_check`.
- **MSVC specifics:** `Atomic<u64>` and the pointer atomics are lock-free on x64 (the sync tests pass on cl and clang-cl); `Mutex` stays `std::mutex` inside
  `sync` (the MSVC library builds it on an SRW lock; no need for a Windows-specific mutex); coroutine frames are allocated through the promise's
  `operator new` on both compilers (the jobs tests pass); `WFormatTo` forwards to the conforming `swprintf(buffer, count, format, ...)`, so no caller sees
  MSVC's older signature.
- **A test bug the Windows run found:** `Number conversion matches the C library` (`tests/cstr/cstr_test.cpp`) compared the end pointers of two parses of
  two separate `"0x1F rest"` literals; GCC and Clang pool identical literals, MSVC's debug build does not. Both parses now read one string.
- **Builds:** `build-msvc` (plain and asan), `build-clang-cl`, `build-windows-msvc-nounity` and `build-windows-clang-cl-nounity` build clean.
- **Runs:** `scripts/test-phase.sh --affected` (everything, an `engine/foundation` header changed): `ran 1786 of 3477 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 276 s`,
  two failures (the cstr test above, fixed; `docs_check`, the reference not yet regenerated, fixed); asan `ran 1424 of 3477 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 180 s`, the
  same cstr test only. After the fixes the closing run (recorded in the Day 221 file) covers everything: `ran 3477 of 3477 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 300 s`; one failure, a flaky crash test (`Windows: while the crash handler is installed, abort() shows no dialog and calls no error reporting`, plain; it passed 35 of 35 runs alone and its asan twin passed; filed in `KNOWN-ISSUES.md`).
  `rhi_demo`, `renderer_demo` and `ui_demo` 1000 frames with `--validation` on Direct3D 12 (GPU-based validation) and Vulkan: exit 0, no error or warning.
  `ctest -L docs`: 7 of 7.

## Post-implementation code review

`/code-review high` over the day's changes (the working tree against Day 221's commit): 2 findings, both fixed.

1. **Fixed:** the port replaced the death-test child's `fflush(nullptr)` with flushes of standard output and standard error only, so a library's other C
   stream would no longer be flushed before `TerminateProcess`. New wrapper `os::FlushAll()` (`os/console.h`, documented), used there; its test
   (`tests/cstr/os_console_test.cpp`, `os::FlushAll empties every C stream's buffer, not only the console's`) is POSIX and runs on Linux only (open in
   `KNOWN-ISSUES.md` until the Linux machine runs it); on Windows the death tests run through the call.
2. **Fixed:** two trailing comments lost their column when `std::mutex` became `Mutex` (`registry_store.h`, `stack_trace_dbghelp.cpp`).

The problems the compilers and the runs found (above) are not review findings: each was fixed when it was seen.

