# Phase 19 / Day 251 — Vendoring SQLite & the Boundary Gate

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 250 (the pinned version, the compile options, the VFS decision).

Bring SQLite in as source behind a fence: it builds on both platforms and in every sanitizer flavor, and only one directory may include its header.

## Tasks

- [x] **Vendor** the amalgamation under `third_party/sqlite/` at the pinned version (`sqlite3.c`, `sqlite3.h`, the public-domain notice, a `README.engine` with the source URL, version and SHA-256, and the options used), the same pattern as `third_party/freetype/`; the vendored target keeps its own warning flags.
- [x] **CMake target `sqlite3`** (C, static) with the Day 250 compile options; excluded from unity builds; built with `-fPIC` where needed; no shell, no extension loading; the same target on Windows MSVC and clang-cl (the vendored C compiles without our `/WX`).
- [x] **Library `engine/database/sqlite/`** with its CMake target `database_sqlite` (and `_asan`/`_tsan` flavors) containing one function, `database::SqliteVersion()`, in `sqlite_version.cpp`, the first (and for now only) file allowed to include `sqlite3.h`.
- [x] **Boundary gate** `tools/sqlite_boundary_check` (ctest `sqlite_boundary_check`, the pattern of `tools/file_access_check`): fails when any file outside `engine/database/sqlite/` and `third_party/sqlite/` includes `sqlite3.h` or names an `sqlite3_*` function; an allow list with a reason per entry and a stale entry fails; the gate has its own tests (a planted violation is caught).
- [x] **Module rows:** `engine/database` (empty interface directory for now) in `ARCHITECTURE.md` section 1 and `modules.txt`; the `database_sqlite` target allowed to link `sqlite3` and `foundation` only.
- [x] **`BUILD.md`:** the vendored SQLite and its version in the prerequisites table (no new system package); the C compiler need on each platform is already listed.
- [x] **Test** (`tests/database/sqlite_smoke_test.cpp`): `SqliteVersion()` returns the pinned version string; the library links in plain, asan and tsan.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** SQLite builds from source on Linux (and is listed for the Windows PC), only `engine/database/sqlite/` can include it, and a gate keeps it that way.

## Notes

- Pinned: SQLite **3.54.1**, `sqlite-amalgamation-3540100.zip` (2,990,966 bytes) downloaded from sqlite.org on 2026-11-11 with the user's yes; SHA3-256 `92281c86...` matches the value sqlite.org publishes; SHA-256 and the options are in `third_party/sqlite/VENDORED.md`.
- Findings: (1) with `SQLITE_OS_OTHER` there is no mutex implementation, so Day 254 installs mutex methods on the engine's `Mutex`; (2) `sqlite3_initialize` fails until a VFS exists because the `memdb` VFS needs a default one, so `SQLITE_OMIT_DESERIALIZE` is defined (the engine does not serialize databases); (3) a static archive is searched once, so `sqlite3_os_init`/`sqlite3_os_end` live in the same file as `SqliteVersion` (the non-unity link failed otherwise; Day 254 amended).
- Gate `sqlite_boundary_check` (7 ctest cases: real tree, planted violations with counts, clean fixture, stale entry, entry without a reason); `module_deps_check`, `config_access_check`, `std_usage_check`, `platform_neutral_check`, `file_access_check` pass.
- Runs (Linux): plain `ran 2200 of 5729 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 142 s` (the two known X11 window tests failed); asan `ran 1675 of 5729 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 156 s`; tsan `ran 1357 of 5729 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 183 s` (the known scene abort). These ran as `third_party/` changed (everything runs); the link-order fix landed during them and was re-checked with `ctest -L database`. Clang and GCC non-unity builds of `database_tests` link and pass; `ctest -L docs` 7 of 7; `docgen lint` baseline has a `database` row (1 symbol).
- Windows PC: not yet built (listed in `KNOWN-ISSUES.md`).
- Day review: `/code-review high` over the hand-written diff (the vendored files excluded): the only finding was the link-order fragility above (fixed before commit, with the non-unity build as its regression check); declined: none.
