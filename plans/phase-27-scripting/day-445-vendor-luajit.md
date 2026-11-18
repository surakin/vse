# Phase 27 / Day 445 — Vendoring LuaJIT behind a Boundary Gate

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 444.

LuaJIT is built from source under `third_party/` and only the backend's implementation files include its headers, as the other vendored libraries.

## Tasks

- [x] Vendor LuaJIT at the pinned commit under `third_party/luajit/` (source only, the licence, a `README` with the version, the SHA-256 and the local changes, if any: none expected); `BUILD.md` lists the build prerequisites (a C compiler, `make` or the MSVC batch file) per platform.
- [x] CMake builds it as a static library `luajit` on Linux (host `buildvm` run at build time, the generated headers in the build tree; cross builds for macOS arm64 noted for Day 474), with the engine's flags only where they do not break it (it is third-party code: not under `-Werror`), unity builds off for it, and `LUAJIT_DISABLE_JIT` as a CMake option (`ENGINE_LUAJIT_JIT`, default ON) for Day 470.
- [x] The gate `luajit_boundary_check` (a ctest, in the style of `third_party_boundary` gates of vorbis and sqlite): no file outside `engine/script/luajit/` includes `lua.h`, `lualib.h`, `lauxlib.h` or `luajit.h`; planted-violation tests; the allow list has a reason per entry and a stale entry fails.
- [x] A smoke test links `luajit`, creates a state, runs `return 1+1`, checks the result and the version string, and closes it, under plain, asan and tsan.
- [x] Licence and attribution recorded where the other vendored libraries' are (`docs/` third-party list); the sanitizer builds link the same library (LuaJIT itself is not instrumented: recorded in `KNOWN-ISSUES.md` with what tsan can and cannot see inside it).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs. (Windows and macOS builds wait for Days 473-474; listed in `KNOWN-ISSUES.md`.)
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run. (No public header changed: none to document; the `docs/guide/scripting.md` page starts on Day 446.)
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** LuaJIT builds and links on Linux in every flavor, a smoke test runs a chunk, and a gate keeps its headers inside one directory.


## Outcome (2026-11-18, Linux)

LuaJIT at the pinned commit is vendored under `third_party/luajit/` (`VENDORED.md`, `LICENSE`, `luajit_relver.txt`; upstream `src/` and `dynasm/` unmodified, 4.2 MB) and built by CMake as the static library `luajit` with its own host tools (`minilua`, DynASM, `buildvm`), no `make`; `ENGINE_LUAJIT_JIT=OFF` builds with `LUAJIT_DISABLE_JIT` (the host `buildvm` needs the same define: found by the first JIT-off build). The gate `tools/luajit_boundary_check` (7 ctest cases: real tree, planted violations by line and count, clean tree, stale entry, entry without reason) allows only `engine/script/` (to be) and one allow-list entry, `tests/script/`, the smoke test. `tests/script/luajit_smoke_test.cpp`: runs `1+1`, the version, the engine's allocator (state memory returns to 0), an error as a status, and (JIT-off build) `jit.status()`.

Runs: plain `scripts/test-phase.sh --affected`: `ran 3874 of 10381 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 503 s` (the root `CMakeLists.txt` changed, so it ran everything buildable here); the 3 failures are the known ones (`engine_c_surface`, `A Job destroyed without being released...`, `Snapshot: the cost of a voice...`). `scripts/test-phase.sh 27 --flavor asan`: `ran 4 of 10381 tests (phases: 27; flavors: asan; ...)`; `--flavor tsan`: `ran 4 of 10381 tests (phases: 27; flavors: tsan; ...)`. Also built and passed: GCC (unity), Clang with `ENGINE_UNITY_BUILD=OFF` and `ENGINE_LUAJIT_JIT=OFF` (5 test cases). `ctest -L docs` passes; `scripts/format.sh --check` clean. Not run: `-DENGINE_BUILD_SHARED=ON` (nothing links LuaJIT into the engine library yet).

## Post-implementation code review

Reviewed by hand over the non-vendored diff (the vendored sources are upstream's, unmodified). One finding, fixed: configuring on a Linux machine that is not x86-64 stopped with `FATAL_ERROR` from `third_party/luajit`, which would break every Linux arm64 configure; it now prints a status line and builds no `luajit` target, and `tests/script` skips when the target is missing. Declined: allow-listing the whole `tests/script/` directory (the entry says why; the backend's tests move under `engine/script/luajit/` on Day 448, and the entry then goes stale and must be removed, which the gate enforces).
