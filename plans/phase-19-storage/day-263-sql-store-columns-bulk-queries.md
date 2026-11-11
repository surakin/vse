# Phase 19 / Day 263 — SqlStore II: Column Layout, Bulk Paths & Queries

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 262; Day 250 spike (b).

Make the SQL store fast, and let tools and servers ask questions of stored data.

## Tasks

- [ ] **Column layout (queryable components, optional; spike (b): about 2x the insert cost, field queries in milliseconds):** a component flagged `Queryable` in its description also gets a table `c_<name>(key BLOB PRIMARY KEY REFERENCES entity(key) ON DELETE CASCADE, one column per scalar or vector field)` generated from the registry's field list (strings and arrays stay in the blob), written alongside the entity row in key order; the entity blob stays the source of truth for loading, so both layouts load the same world; an index per queryable field is created on request.
- [ ] **Component schema changes:** a new component version updates the generated table (`ALTER TABLE ... ADD COLUMN` where `Supports`, else rebuild from the blobs) inside the migration transaction; tested with a field added.
- [ ] **Bulk paths:** multi-row inserts (a prepared statement of N rows, N a tuned constant) inside one transaction per save, a prepared statement per component kind reused across the batch, loads that read by key range with one cursor rather than one query per entity, the `WITHOUT ROWID` primary key where `Supports`.
- [ ] **Queries:** `SqlStore::FindKeys(component, field, op, value)` returns entity keys matching a field comparison, only for queryable components, and `Count`; results ordered by key; an unqueryable component is `Unsupported`.
- [ ] **Tests** (`tests/storage/sql_layout_test.cpp`): a world saved with and without the column layout loads identically; `FindKeys` equals a brute-force scan of the world for every operator over 10,000 entities; the added-field migration; the bulk path equals the one-row path (same stored state); the columns equal the blob values for every queryable component (a consistency checker used by the tests).
- [ ] **Measurements** to the day file: 100,000 entities saved and loaded, blob only and with columns, against the Day 250 spike numbers.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the SQL store has a measured fast path, an optional queryable layout, and answers field queries without loading the world.
