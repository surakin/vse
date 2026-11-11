# Phase 19 / Day 251 — Vendoring SQLite & the Boundary Gate

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 250 (the pinned version, the compile options, the VFS decision).

Bring SQLite in as source behind a fence: it builds on both platforms and in every sanitizer flavor, and only one directory may include its header.

## Tasks

- [ ] **Vendor** the amalgamation under `third_party/sqlite/` at the pinned version (`sqlite3.c`, `sqlite3.h`, the public-domain notice, a `README.engine` with the source URL, version and SHA-256, and the options used), the same pattern as `third_party/freetype/`; the vendored target keeps its own warning flags.
- [ ] **CMake target `sqlite3`** (C, static) with the Day 250 compile options; excluded from unity builds; built with `-fPIC` where needed; no shell, no extension loading; the same target on Windows MSVC and clang-cl (the vendored C compiles without our `/WX`).
- [ ] **Library `engine/database/sqlite/`** with its CMake target `database_sqlite` (and `_asan`/`_tsan` flavors) containing one function, `database::SqliteVersion()`, in `sqlite_version.cpp`, the first (and for now only) file allowed to include `sqlite3.h`.
- [ ] **Boundary gate** `tools/sqlite_boundary_check` (ctest `sqlite_boundary_check`, the pattern of `tools/file_access_check`): fails when any file outside `engine/database/sqlite/` and `third_party/sqlite/` includes `sqlite3.h` or names an `sqlite3_*` function; an allow list with a reason per entry and a stale entry fails; the gate has its own tests (a planted violation is caught).
- [ ] **Module rows:** `engine/database` (empty interface directory for now) in `ARCHITECTURE.md` section 1 and `modules.txt`; the `database_sqlite` target allowed to link `sqlite3` and `foundation` only.
- [ ] **`BUILD.md`:** the vendored SQLite and its version in the prerequisites table (no new system package); the C compiler need on each platform is already listed.
- [ ] **Test** (`tests/database/sqlite_smoke_test.cpp`): `SqliteVersion()` returns the pinned version string; the library links in plain, asan and tsan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** SQLite builds from source on Linux (and is listed for the Windows PC), only `engine/database/sqlite/` can include it, and a gate keeps it that way.
