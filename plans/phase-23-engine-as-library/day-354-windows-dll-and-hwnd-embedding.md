# Phase 23 / Day 354 — Windows: the DLL & Embedding into an `HWND`

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 344-353 built on Linux; Phases 13, 21. **Done on the Windows PC.**

The shared build and the host-window embedding on Windows, Vulkan and Direct3D 12.

## Tasks

- [ ] `ENGINE_BUILD_SHARED` on `windows-msvc` and `windows-clang-cl`: `engine.dll`, PDBs, the `ENGINE_API` `dllexport`, no import library used by hosts (the loader uses `LoadLibraryW`/`GetProcAddress`), one CRT rule written in the guide (no `FILE*`, no allocation crosses); `abi_check` through `dumpbin /exports`; asan flavor (MSVC has no tsan).
- [ ] `DllMain` does no work; verify with a test that loading the DLL runs nothing but the loader's checks; `FreeLibrary` after shutdown works (the reload cycle test of Day 348 on Windows, 50 cycles under asan).
- [ ] A test host built with a **different CRT setting** (`/MT` against the engine's `/MD`) loads the DLL and runs the headless frame, the memory test and the resource test (Day 350).
- [ ] Foreign surface: Vulkan and Direct3D 12 on a host `HWND` (`SurfaceDescriptor` kind `HWND`), resize, device loss and recovery, readback; the Win32 C host (`host_win32.c`) with its own window and message loop forwards input and implements the cursor and clipboard callbacks.
- [ ] Run the whole Linux-written C host suite on Windows; fix what differs; record in `KNOWN-ISSUES.md` what could not be done; delay-loading is not used.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Windows PC; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the DLL builds and loads, the reload cycles and the C hosts (headless, resources, import, embedded window) pass on the Windows PC with both backends.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
