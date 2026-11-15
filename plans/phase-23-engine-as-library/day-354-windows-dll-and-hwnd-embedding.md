# Phase 23 / Day 354 — Windows: the DLL & Embedding into an `HWND`

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 344-353 built on Linux; Phases 13, 21. **Done on the Windows PC.**

The shared build and the host-window embedding on Windows, Vulkan and Direct3D 12.

## Tasks

- [x] `ENGINE_BUILD_SHARED` on `windows-msvc` and `windows-clang-cl`: `engine.dll`, PDBs, the `ENGINE_API` `dllexport`, no import library used by hosts (the loader uses `LoadLibraryW`/`GetProcAddress`), one CRT rule written in the guide (no `FILE*`, no allocation crosses); `abi_check` through `dumpbin /exports`; asan flavor (MSVC has no tsan).
- [x] `DllMain` does no work; verify with a test that loading the DLL runs nothing but the loader's checks; `FreeLibrary` after shutdown works (the reload cycle test of Day 348 on Windows, 50 cycles under asan).
- [x] A test host built with a **different CRT setting** (`/MT` against the engine's `/MD`) loads the DLL and runs the headless frame, the memory test and the resource test (Day 350).
- [x] Foreign surface: Vulkan and Direct3D 12 on a host `HWND` (`SurfaceDescriptor` kind `HWND`), resize, device loss and recovery, readback; the Win32 C host (`host_win32.c`) with its own window and message loop forwards input and implements the cursor and clipboard callbacks. *(Device loss and recovery not tested, as on Linux: the C API cannot inject a loss; `KNOWN-ISSUES.md`.)*
- [x] Run the whole Linux-written C host suite on Windows; fix what differs; record in `KNOWN-ISSUES.md` what could not be done; delay-loading is not used.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Windows PC; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the DLL builds and loads, the reload cycles and the C hosts (headless, resources, import, embedded window) pass on the Windows PC with both backends.

## Outcome

Done 2026-11-15 on the Windows PC (commits `3f6ab16e`, part 1, and this one).

- **The C API on Windows.** `engine/engine_c` builds on Windows (plain, asan); the declaration of `engine_get_api` in `engine_c.h` carries the `dllexport` its
  definition has in the library's own sources (`ENGINE_C_API`; MSVC refuses a mismatch, C2375). The root `CMakeLists.txt` adds `engine/engine_c`,
  `tools/abi_check` and `tools/shared_state_check` to the Windows build.
- **`engine.dll`.** `-DENGINE_BUILD_SHARED=ON` builds `engine.dll` and `engine_asan.dll` (MSVC) and `engine.dll` (clang-cl), whole-archived; the export list is a
  `.def` file generated from `abi/engine.exports`. `dumpbin /exports`: exactly `engine_get_api` in each. `abi_check` reads `dumpbin`'s output (with
  recorded fixtures, tested on every platform) and compiles `engine_c.h` as C11 with `cl /std:c11 /W4 /WX`. No `DllMain` of our own: the unload work
  (the frame pool's memory) is a static object's destructor, run when the DLL detaches. No delay-loading.
- **The loader.** `engine_loader.h` loads with `LoadLibraryExW` (UTF-8 path, `LOAD_WITH_ALTERED_SEARCH_PATH` for a path with a folder), `GetProcAddress`,
  `FreeLibrary`, with the system's message and error number on a refusal.
- **The tests on Windows.** `tests/engine_library/host_os.h` gives the C hosts their operating system (POSIX side: what they called directly before; Windows
  side: Win32, Toolhelp, psapi); `host_quiet_failures()` sends the debug C runtime's asserts and `abort()` to standard error (dialog boxes had appeared on
  the desktop while the hosts ran). Every C host of the Linux days runs on Windows: the static hosts (`host_c`, `host_runtime`, `host_resources`,
  `host_import`, `host_import_off`, `status_map_test`), the loaded ones (`host_dl` and the three refused fakes, `dup_singletons` and its planted plugin,
  `owner_host`, `unload_host`, `crash_host`, `host_reload` with 50 cycles under asan, `host_inside`, `host_import` through the loader) and the ABI snapshot
  (Windows x64 prints exactly the committed `abi/engine_api.snapshot`). New: `host_dllmain.c` (loading starts no thread), `host_crt.c` (a host built `/MT`,
  depending on `KERNEL32.dll` alone, runs frames, the memory test and the resource test through the loader).
- **Embedding into an HWND.** `ENGINE_SURFACE_WIN32` (`display` the `HINSTANCE`, `surface` the `HWND`), `ENGINE_CAP_SURFACE_WIN32`, and
  `ENGINE_SURFACE_FLAG_D3D12` / `ENGINE_SURFACE_FLAG_VULKAN` to choose the graphics API (neither: the platform's own, Direct3D 12 on Windows); a surface keeps
  its API across `surface_rebind`. `windows/host_win32.c` (plain and asan): its own window class, procedure and message loop, run with Direct3D 12 and with
  Vulkan: arguments, the golden frame, sRGB grey, resize, hidden, the window hidden by the host, destroyed under the engine, rebind, cancelled read-back,
  forwarded input, the cursor callback. `surface_input_test.cpp` runs on a Win32 window (the UI's click, text, cursor and clipboard requests through the
  host's callbacks).
- **Fixed on the way.** The crash handler left its gate as the top-level exception filter after `crash_handler_uninstall` (in a DLL that would point into
  unloaded code): `FinishCrashEventHookRemoval` gives the host's filter back after `sentry_close` (`host_runtime` and `crash_host` check it). The ABI
  snapshot's update wrote CR LF on Windows (`file(WRITE)` in text mode): both scripts compare and write LF. The `abi_check` header check wrote one file for
  every parallel case (a race): each its own name.
- **Found and filed** (`KNOWN-ISSUES.md`): a fault inside a job ends the process as an abort on MSVC (the coroutine's `unhandled_exception`), with the dump
  written; `host_runtime` hung twice in the `build-msvc` full runs (stacks recorded); device loss on a host surface is not tested; the install consumer runs on
  Linux only.

Runs: `scripts/test-phase.sh --affected` (every phase: the root `CMakeLists.txt` and an `engine/foundation` header changed): `build-msvc` `ran 3150 of 6062 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 1375 s`, `build-windows-msvc-nounity` `ran 3150 of 6062 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 338 s`, `build-clang-cl` `ran 3180 of 3336 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 321 s`, `build-windows-clang-cl-nounity` `ran 3180 of 3336 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 332 s`; asan: `build-msvc` `ran 2629 of 6062 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 1693 s`, `build-windows-msvc-nounity` `ran 2629 of 6062 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 175 s`. Failures: `engine_c_runtime` and `engine_c_runtime_asan` in `build-msvc` (each a hang, stopped by hand: `KNOWN-ISSUES.md`) and the filed resolver-queue flake once in `build-clang-cl` (3 of 3 reruns passed). The shared trees (`-DENGINE_BUILD_SHARED=ON`): `ctest -L engine_library` 86 of 86 in `build-msvc-shared` (plain and asan) and 78 of 78 in `build-clang-cl-shared`. All six trees build clean (MSVC and clang-cl, unity and non-unity, and both shared trees). `ctest -L docs` 7 of 7. **After the rebase onto Day 355** (the Mac's commit `1bb6222f`, merged on the PC: `host_os.h` takes in its process probes, the Win32 values move past Cocoa's to `ENGINE_SURFACE_WIN32` 4 and `ENGINE_CAP_SURFACE_WIN32` 0x20, the ABI snapshot regenerated with both): all six trees build clean; `ctest -L engine_library` 89 of 89 in `build-msvc` and `build-msvc-shared`, 81 of 81 in `build-clang-cl-shared` and `build-windows-clang-cl-nounity`; the gates 113 of 113; `ctest -L docs` 7 of 7 (the full `--affected` runs above are from before the rebase).

## Post-implementation code review

`/code-review high` over the day's diff (from `3f6ab16e~1`, both commits), done inline: 6 findings. **Fixed (2):** (1) the checks that the library left the
address space only ever asserted that `host_lib_is_loaded` returned false, so a probe that never matched would pass them all: it was verified to see the DLL
by its forward-slash path (a throwaway probe: 0 before, 1 while loaded, 0 after `FreeLibrary`), and `host_dl.c` and `unload_host.c` now also check it is seen
while loaded; (2) two static non-inline callbacks in `posix/host_os_posix.h` could draw GCC's `-Wunused-function` in a host that does not use them: made
`static inline` (not built on Linux here). **Found during the day and fixed (1):** the Windows crash child entered before `main` had the function table, so
it crashed on a null pointer and the "crashed as planted" check passed for the wrong reason (moved after `engine_get_api`; the envelope count showed it).
**Declined or recorded (3):** `engine_loader.h` includes `<windows.h>` into a host's translation unit with its macros (a host-facing header has no wrapper to
hide them; the guide's loader paragraph is the place to say so if a host trips on it); `host_run_crash_child` on Windows takes exit code 3 as a crash, which an
unrelated abort would also give (the callers also require the dump the engine wrote, so a crash without one still fails); `FinishCrashEventHookRemoval` could
race another thread setting a filter at the same moment (uninstall is not concurrent with the host's own filter changes by the contract of the C API).

