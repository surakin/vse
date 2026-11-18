# Phase 27 / Day 473 — Windows: LuaJIT on MSVC and clang-cl, the Tests and the Hashes

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 472; Phase 13.

On the PC: the vendored library builds, every script test passes, and the hashes equal Linux's.

## Tasks

- [ ] Pull and build `windows-msvc` and `windows-clang-cl`; LuaJIT through its MSVC build route driven from CMake (the toolchain files find MSVC; a plain PowerShell or Git Bash, never a Developer Command Prompt); `BUILD.md`'s Windows section updated.
- [ ] **File layer: the Windows directory watcher, `ReadDirectoryChangesW`** (`io/windows/directory_watcher.cpp`, the backend of the Day 464 `io::DirectoryWatcher`; section in `io/windows/BACKEND` notes and `io/BACKEND.md`): the directory opened with `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED` and the share flags the layer always uses, one outstanding overlapped read into a buffer of 64 KiB (`FILE_NOTIFY_CHANGE_FILE_NAME | LAST_WRITE | SIZE | CREATION`), re-armed after each completion, `Wait` on the event handle, `FILE_ACTION_RENAMED_OLD_NAME`/`NEW_NAME` paired, `ERROR_NOTIFY_ENUM_DIR` (buffer overflow) becomes `Overflow`, names converted from UTF-16 at the boundary with the layer's rules, `CancelIoEx` and a drain of the pending request in `Close`/the destructor so the buffer is never written after it is freed. Win32 only through `os/windows/win32.h`. The Day 464 conformance suite (`tests/io/directory_watcher_conformance.h`) runs against it, plain and asan, unity and not, with both compilers; `Open` selects it instead of the polling fallback; the hot-reload test then runs through it. Remove its `KNOWN-ISSUES.md` entry.
- [ ] The script tests, plain and asan (no tsan on Windows), unity and not; the sandbox and budget tests (the hostile corpus) on Windows; the debugging console and hot reload (file watching through the Windows file layer).
- [ ] The cross-machine check: the script scenes' ECS state hashes and event logs equal Linux's (the scripts use no transcendental math in the reference scenes, so bit equality holds; scenes that do use it are compared within a tolerance and listed); thread-count independence at 1, 2, 7, 16 threads on Windows.
- [ ] The C API script tests with the DLL (`engine.dll`) and the host sample.
- [ ] Record the final `ran X of Y` lines and the hash table in this file; commit and push from the PC.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this is the Windows PC's day; Linux and the Mac are checked on Days 472 and 474.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** LuaJIT builds on MSVC and clang-cl, every script test passes on Windows, and the reference hashes equal Linux's.
