# Phase 27 / Day 445 — Vendoring LuaJIT behind a Boundary Gate

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 444.

LuaJIT is built from source under `third_party/` and only the backend's implementation files include its headers, as the other vendored libraries.

## Tasks

- [ ] Vendor LuaJIT at the pinned commit under `third_party/luajit/` (source only, the licence, a `README` with the version, the SHA-256 and the local changes, if any: none expected); `BUILD.md` lists the build prerequisites (a C compiler, `make` or the MSVC batch file) per platform.
- [ ] CMake builds it as a static library `luajit` on Linux (host `buildvm` run at build time, the generated headers in the build tree; cross builds for macOS arm64 noted for Day 474), with the engine's flags only where they do not break it (it is third-party code: not under `-Werror`), unity builds off for it, and `LUAJIT_DISABLE_JIT` as a CMake option (`ENGINE_LUAJIT_JIT`, default ON) for Day 470.
- [ ] The gate `luajit_boundary_check` (a ctest, in the style of `third_party_boundary` gates of vorbis and sqlite): no file outside `engine/script/luajit/` includes `lua.h`, `lualib.h`, `lauxlib.h` or `luajit.h`; planted-violation tests; the allow list has a reason per entry and a stale entry fails.
- [ ] A smoke test links `luajit`, creates a state, runs `return 1+1`, checks the result and the version string, and closes it, under plain, asan and tsan.
- [ ] Licence and attribution recorded where the other vendored libraries' are (`docs/` third-party list); the sanitizer builds link the same library (LuaJIT itself is not instrumented: recorded in `KNOWN-ISSUES.md` with what tsan can and cannot see inside it).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** LuaJIT builds and links on Linux in every flavor, a smoke test runs a chunk, and a gate keeps its headers inside one directory.
