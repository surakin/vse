# Phase 19 / Day 263 — SqlStore II: Column Layout, Bulk Paths & Queries

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 262; Day 250 spike (b).

Make the SQL store fast, and let tools and servers ask questions of stored data.

## Tasks

- [x] **Column layout (queryable components, optional; spike (b): about 2x the insert cost, field queries in milliseconds):** a component flagged `Queryable` in its description also gets a table `c_<name>(key BLOB PRIMARY KEY REFERENCES entity(key) ON DELETE CASCADE, one column per scalar or vector field)` generated from the registry's field list (strings and arrays stay in the blob), written alongside the entity row in key order; the entity blob stays the source of truth for loading, so both layouts load the same world; an index per queryable field is created on request.
- [x] **Component schema changes:** a new component version updates the generated table (`ALTER TABLE ... ADD COLUMN` where `Supports`, else rebuild from the blobs) inside the migration transaction; tested with a field added.
- [x] **Bulk paths:** multi-row inserts (a prepared statement of N rows, N a tuned constant) inside one transaction per save, a prepared statement per component kind reused across the batch, loads that read by key range with one cursor rather than one query per entity, the `WITHOUT ROWID` primary key where `Supports`.
- [x] **Queries:** `SqlStore::FindKeys(component, field, op, value)` returns entity keys matching a field comparison, only for queryable components, and `Count`; results ordered by key; an unqueryable component is `Unsupported`.
- [x] **Tests** (`tests/storage/sql_layout_test.cpp`): a world saved with and without the column layout loads identically; `FindKeys` equals a brute-force scan of the world for every operator over 10,000 entities; the added-field migration; the bulk path equals the one-row path (same stored state); the columns equal the blob values for every queryable component (a consistency checker used by the tests).
- [x] **Measurements** to the day file: 100,000 entities saved and loaded, blob only and with columns, against the Day 250 spike numbers.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the SQL store has a measured fast path, an optional queryable layout, and answers field queries without loading the world.

## Notes (2026-11-11, Linux machine)

- **Column layout.** New flag `kComponentQueryable` (a stored component). `SqlStoreOptions{registry, columns, limits, indexes}`; `SqlStore::Open(connection, &store, options)`. Table `c_<name>(key BLOB PK REFERENCES entity(key) ON DELETE CASCADE, ...)` (`WITHOUT ROWID` where supported) with one column per scalar field (`INTEGER`/`REAL`) and per vector part (`<field>_x`..`_w`); strings, arrays, matrices, boxes and references stay in the blob. Values come from the blob by decoding the component (through its upgrade chain) and re-encoding each field alone, so no field offsets are needed. The table and column names are the one place SQL text is built from run-time text; they are registry names checked to be plain identifiers, listed in `tools/database_sql_check/allowlist.txt`; every value is bound. A NaN is stored as NULL; a `u64` is its bit pattern in a signed column.
- **Schema changes.** The layout of each table is a signature kept in the metadata (`layout.c_<name>`); a registration that appended fields gets `ALTER TABLE ADD COLUMN` where the connection supports it, else the table is dropped and recreated, and in both cases refilled from the blobs inside the transaction `Open` makes (the metadata `layout.active` makes a database that was written with the layout off refill too). Tested with a field added, with and without `ALTER` (the hiding wrapper moved to `tests/storage/no_alter_connection.h`).
- **Bulk paths.** `Store::PutMany` (default: a loop of `Put`); `SqlStore` overrides it with a 16-row `INSERT OR REPLACE`; `WorldStorage::Save` (full, reconcile and incremental) sends records in groups of 256 through it. Schema version 3 rebuilds `entity` as `WITHOUT ROWID` where the backend has it (a migration step like the others). The load already reads the records with one `Scan` cursor.
- **Queries.** `FindKeys` and `CountWhere` (component name, column, `CompareOp`, a `ValueView`), keys in key order, answered from the table; `Unsupported` (no layout, or not queryable), `NotFound`, `Invalid` (wrong value type). `IndexRequest` creates `CREATE INDEX IF NOT EXISTS` on named columns. `VerifyColumns` is the consistency checker. New `storage::ErrorCode::Unsupported`.
- **Tests** (`sql_layout_test.cpp`, 8 cases, 490,839 assertions): both layouts load the same world (3,000 entities) and the columns match the blobs; `FindKeys` and `CountWhere` equal a brute-force scan for 11 columns x 6 operators over 10,000 entities (66 combinations) plus the refusals; rows follow replace, removal, a stripped component and incremental saves, with foreign keys enforced and not; the layout switched off and on; a field added (ALTER and rebuild paths); `PutMany` equals `Put` for group sizes 1, 15, 16, 17, 32, 33, 100; an indexed column answers the same. Mutations that failed the suite: `<` made `<=`; the explicit delete of a column row removed (caught only after adding the foreign-keys-off run).
- **Measurements** (100,000 entities, ten fields each of which seven are columns, file database, debug test build, includes the ECS encode and entity creation): blob only save 449-481 ms, load 594-596 ms; with columns save 931 ms (1.9x, the spike measured about 2x), load 591 ms (unchanged: the load reads blobs); with columns and an index on `hp` save 1,021 ms; query `hp > 100` (18,268 hits) 6.8 ms without an index, 0.5 ms with one (spike: 0.35 ms on 10,076 hits). The spike's raw insert of 100,000 blob rows was 293 ms, 9 ms to read; the difference to the figures above is the world encode and decode.
- Runs: plain `ran 2480 of 6509 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (two known X11 failures); asan `ran 1925 of 6509`; tsan `ran 1607 of 6509` (only the known Day 235 failure). GCC and Clang non-unity builds clean (they found two missing includes in `world_compare.h`, fixed); the `docs` target and `ctest -L docs` pass.

## Windows PC (2026-11-11): Days 251-263

The Windows half of Days 251-263 (Day 265's Windows task covers what comes after): built in all four Windows trees (`build-msvc` plain and asan,
`build-clang-cl`, `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity`), clean after three fixes:

- MSVC C2797 on `ComponentDesc::fields` and `upgrades` (`= {}` on an `InitializerList` member is list initialization in a member default, which MSVC
  does not implement): the defaults are `InitializerList<T>()`.
- MSVC C4702 (unreachable code) in `detail::EncodeValue`: the enum branch returned, so the final `return Status::Success()` was dead for enums; the
  branch now returns only on failure.
- `test_phase_script_affected_side_phase_wrong_flavor` (Mac Day 302) passed `--flavor tsan`, which the script refuses on Windows; it uses asan, with
  the same meaning (phase 6 has plain tests only on every platform).

Runs (`--affected --since aa1fe1fc`, which selects everything): plain `ran 2294 of 4455 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 249 s`, asan `ran 1894 of 4455 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 157 s`. Plain had three
failures: the `field.h` line shift left `docs/reference/symbols.txt` out of date (regenerated), the script test above (fixed), and the known DX12
device-creation flake (passed 3 of 3, filed). The 510 phase 19 tests (243 of them asan) pass, among them the SQLite VFS on the Windows file backend,
the fault-injection crash tests, the threaded database tests and `old_schema`; the conformance cases of the stub fixture and the two-connection cases on
`:memory:` skip by design, as on Linux. The 21 registry fuzz cases take at most 4.5 s each under asan. `ctest -L docs` 7 of 7. Review: `/code-review high`
over the three fixes, no findings.

## Post-implementation code review

`/code-review high`: 3 findings. (1) The in-place `ADD COLUMN` branch counted the old columns wrongly: fixed (counted from the signature's commas; the field-added test covers it). (2) The explicit delete of a column row for a record without the component was hidden by the foreign-key cascade: tested now with foreign keys off (confirmed to fail without the delete). (3) A `u64` column compares as signed above 2^63: declined, documented in the header and guide.
