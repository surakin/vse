# Phase 27 / Day 473 — Windows: LuaJIT on MSVC and clang-cl, the Tests and the Hashes

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 472; Phase 13.

On the PC: the vendored library builds, every script test passes, and the hashes equal Linux's.

## Tasks

- [x] Pull and build `windows-msvc` and `windows-clang-cl`; LuaJIT through its MSVC build route driven from CMake (the toolchain files find MSVC; a plain PowerShell or Git Bash, never a Developer Command Prompt); `BUILD.md`'s Windows section updated.
- [x] **File layer: the Windows directory watcher, `ReadDirectoryChangesW`** (`io/windows/directory_watcher.cpp`, the backend of the Day 464 `io::DirectoryWatcher`; section in `io/windows/BACKEND` notes and `io/BACKEND.md`): the directory opened with `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED` and the share flags the layer always uses, one outstanding overlapped read into a buffer of 64 KiB (`FILE_NOTIFY_CHANGE_FILE_NAME | LAST_WRITE | SIZE | CREATION`), re-armed after each completion, `Wait` on the event handle, `FILE_ACTION_RENAMED_OLD_NAME`/`NEW_NAME` paired, `ERROR_NOTIFY_ENUM_DIR` (buffer overflow) becomes `Overflow`, names converted from UTF-16 at the boundary with the layer's rules, `CancelIoEx` and a drain of the pending request in `Close`/the destructor so the buffer is never written after it is freed. Win32 only through `os/windows/win32.h`. The Day 464 conformance suite (`tests/io/directory_watcher_conformance.h`) runs against it, plain and asan, unity and not, with both compilers; `Open` selects it instead of the polling fallback; the hot-reload test then runs through it. Remove its `KNOWN-ISSUES.md` entry.
- [x] The script tests, plain and asan (no tsan on Windows), unity and not; the sandbox and budget tests (the hostile corpus) on Windows; the debugging console and hot reload (file watching through the Windows file layer).
- [x] The cross-machine check: the script scenes' ECS state hashes and event logs equal Linux's (the scripts use no transcendental math in the reference scenes, so bit equality holds; scenes that do use it are compared within a tolerance and listed); thread-count independence at 1, 2, 7, 16 threads on Windows.
- [x] The C API script tests with the DLL (`engine.dll`) and the host sample.
- [x] Record the final `ran X of Y` lines and the hash table in this file; commit and push from the PC.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this is the Windows PC's day; Linux and the Mac are checked on Days 472 and 474.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** LuaJIT builds on MSVC and clang-cl, every script test passes on Windows, and the reference hashes equal Linux's.

## Notes (done 2026-11-19 on the Windows PC)

- **LuaJIT on Windows** (`third_party/luajit/CMakeLists.txt`): the same CMake route as Linux, with `msvcbuild.bat`'s steps: DynASM with `-D WIN -D ENDIAN_LE -D P64 -D FFI -D FPU`
  (`-D JIT` unless `ENGINE_LUAJIT_JIT=OFF`), `buildvm -m peobj` writes `lj_vm.obj`, which CMake links in as an object; `/O2 /w` in every configuration (the directory's Debug
  `/Od /Ob0` removed). The host tools are run through `$<TARGET_FILE:...>`: the Windows toolchain files make CMake treat the build as cross-compiling, and then it does not
  replace a target name in a custom command (the same fix in `tools/script_api_doc`'s `script_api_page`). Errors unwind through `RtlUnwindEx` on Windows x64 always.
- **The script modules on Windows:** `engine/script`, `script_physics`, `script_media`, their tests, `game/script_demo`, `game/script_bench` and `tools/script_api_doc` join the
  Windows build; the modules build plain and asan there (no tsan; `foreach(suffix "" ${extra})`, since `foreach(... IN LISTS ...)` skips the empty plain suffix). The C API is
  built with `ENGINE_C_WITH_SCRIPT=1`.
- **Fixed on the way:** the no-script branch of `engine_c/script_api.cpp` never defined `ScriptHost::Impl`, so its defaulted destructor deleted an incomplete type (cl refuses it;
  the Mac's no-script build has the same code); `host_script.c` shadowed a local (`found`, C4456). `tests/phases.txt`: `flavors 27 windows: plain,asan` (the
  `test_phase_labels` failure filed on 2026-11-18 is gone).
- **The directory watcher** (`io/windows/directory_watcher.cpp`, `io/BACKEND.md`): as the task says, plus `FILE_NOTIFY_CHANGE_DIR_NAME` (directories are names too, as on
  Linux), a directory marked for deletion (`FileStandardInfo.DeletePending`) ends the watch, and **`ERROR_OPERATION_ABORTED` is an `Overflow` and a new read**: Windows cancels
  the I/O of a thread when the thread ends, so a read armed inside `Poll` on a worker that then exited ended the watch. Found while writing; regression test `directory watcher:
  a watcher last polled by a thread that has ended keeps watching` (fails without the fix: `Poll` returns `Invalid`), and a rename case (`Renamed` with its old name on the
  native backends). The hot-reload tests run through it.
- **Cross-machine check:** no Linux scene hashes were on record except the C API sample's (Day 472), which the PC matches. The demo's hash is now a committed reference
  (`Script demo: the state hash after 150 frames is the same on every machine`, `0xCD4A4433E698FAD7`), equal on the PC with MSVC and clang-cl, Debug and Release, JIT and
  no JIT; Linux and the Mac check it on their next run (`KNOWN-ISSUES.md`).

| Scene | Linux | Windows PC |
|---|---|---|
| `script_sample` (C API, 1,000 entities, 600 frames), partitions 1, 2, 4, 8 | `e3e3ca13a45686f9` (Day 472) | `e3e3ca13a45686f9` at each count, plain and asan |
| `script_demo --frames 120` (2,000 bodies, 16 partitions), threads 1, 2, 4, 8, 16 | `e1f5c33055210a41` at 1, 2, 8 and 16 threads (recorded 2026-11-19, `ce90ed09`) | `e1f5c33055210a41` at each count, Debug and Release |
| `tests/script_demo` small game, 150 frames | `0xCD4A4433E698FAD7` (recorded 2026-11-19: Clang with the JIT, plain, asan and tsan; the test passes) | `0xCD4A4433E698FAD7` (MSVC, clang-cl, no JIT) |
| thread-count tests (World view 1, 2, 7, 16; parallel system 1, 2, 3, 7, 16; replay) | equal | equal (in the runs below) |

- **Speed (MSVC Release, 16 hardware threads):** `script_demo` scripts 260.7 / 152.7 / 92.9 / 50.2 / 28.6 ms at 1 / 2 / 4 / 8 / 16 threads, physics about 9.6 ms;
  `script_bench --scene check` passes (9,972 ns per entity, limit 24,000; 717 ns per call, limit 1,400). Debug: 1,145 ms at 1 thread and 851 ms at 16 (the `/MDd` heap,
  `KNOWN-ISSUES.md`). `engine_c_script_sample_asan` takes 589 s alone; its Windows asan timeout is now 1800 s.

## Runs

`scripts/test-phase.sh --affected` selects every phase today (CMake and `third_party/` changed) and cannot leave out the phase 8 reset-loop test, which froze this PC, so the
runs are ctest directly over every test but `Resets in a loop on a real device leave the counts, memory and descriptors where they were` (and its asan twin), crash tests left out
as on the earlier PC days, display tests included (console session active):

- `build-msvc` plain: **3977 of 3980 passed** (ctest: "99% tests passed, 3 tests failed out of 3980"; every phase, flavor plain, display included, crash tests and the reset loop excluded) in 279 s. Failed: the Debug voice-cost bound
  (filed), the Metal library test (the Mac's re-mint of `gbuffer.frag`, filed), `docs_check` (the reference was stale after the header comment changed; regenerated, `ctest -L docs`
  8 of 8).
- `build-msvc` asan: **3439 of 3441 passed** ("2 tests failed out of 3441"; flavor asan, crash tests and the reset loop excluded) in 820 s. Failed: the Metal library test; `engine_c_runtime_asan` (timeout, the Phase 23 hang; 0.4 s
  alone).
- `build-clang-cl` plain: **4006 of 4010 passed** ("4 tests failed out of 4010"; flavor plain, display included, crash tests and the reset loop excluded) in 500 s. Failed: the voice-cost bound, the Metal library test, the mouse window test and
  `engine_c_acceptance_scene` (each 3 of 3 alone).
- Phase 27 alone: plain 307 of 307 in both trees; the no-JIT tree (`-DENGINE_LUAJIT_JIT=OFF`, MSVC) 249 of 249 script tests and the cross-machine case; `io_tests` and
  `io_tests_asan` `[watch]` 4 of 4 cases. Shared trees: `engine_library` 95 of 95 (MSVC) and 104 of 104 (clang-cl).
- Built clean: `build-msvc`, `build-clang-cl`, both unity-off trees, both shared trees, `-DENGINE_BUILD_SHARED=ON -DENGINE_LIBRARY_WITH_IMPORTER=OFF` (`engine.dll`), the
  no-JIT tree and a Release tree. No tsan (Windows has none).

## Post-implementation code review

`/code-review high` over the day's diff, inline: **5 findings, none fixed in code.** Recorded in `KNOWN-ISSUES.md` (2): every Lua error on Windows is an SEH exception that the
crash hook's vectored observer records, so a flood of them can push a fault out of its 128-entry ring (the report then names the thread by stack pointer); the longer asan timeout
of `engine_c_script_sample` treats the Debug heap's cost, not its cause. No change needed (2): a rename whose two records land in two reads is a `Removed` and a `Created`
(events are hints; inotify behaves the same across reads); the LuaJIT Windows branch passes `cl` flags, and only MSVC and `clang-cl` are supported on Windows (`BUILD.md`).
Declined (1): a failed `FileStandardInfo` query on the watched directory's own handle ends the watch; no transient cause is known, and the consumer treats the `Overflow` and
`Invalid` as "look again". The thread-exit bug above was found while writing, before the review, and fixed with its regression test.
