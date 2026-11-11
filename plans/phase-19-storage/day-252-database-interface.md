# Phase 19 / Day 252 — The Database Interface & the Conformance Suite

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 251 (the module exists); Phase 10 (`String`, `Span`, `Vector`).

Write the interface every database backend implements, and the conformance suite that defines what a correct backend does. No backend runs yet.

## Tasks

- [x] **`database::Status`:** a portable `ErrorCode` (`Ok`, `Busy`, `Locked`, `Constraint`, `NotFound`, `Corrupt`, `Full`, `ReadOnly`, `Misuse`, `Io`, `TooBig`, `Unsupported`, `Interrupted`, `Other`) plus the backend's own code and message text; errors are values, nothing aborts on a database failure.
- [x] **`database::Value` and `ValueView`:** null, 64-bit integer, 64-bit real, text and blob; an owning form and a non-owning view; text is UTF-8 with an explicit length (embedded NUL kept); a blob and an empty blob differ from null; conversions are explicit, never silent.
- [x] **`database::Database`, `Connection`, `Statement`, `Transaction`:** `Database::Open(OpenOptions)` gives a `Connection` (read-write, read-only, create; busy timeout and journal options as plain fields); `Connection::Prepare(SqlText)`, `Execute(SqlText)` for statements with no parameters, `Begin`/`Commit`/`Rollback`, `Savepoint`/`Release`/`RollbackTo`, `LastInsertRowId`, `ChangedRows`, `SchemaVersion()`/`WriteSchemaVersion()` (so no `PRAGMA` text appears outside the backend); `Statement::Bind` by index and by name, `Step` (row, done or error), column count, type and typed getters, `Reset`, `ClearBindings`; `Transaction` is a guard that rolls back on destruction unless committed.
- [x] **`database::SqlText`:** the only type `Prepare` and `Execute` accept; constructible only from a string literal (a `consteval` constructor), so building SQL from user data by concatenation or formatting does not compile; a second line of defence is the gate below.
- [x] **`database::Feature` and `Supports(Feature)`:** `Savepoints`, `WithoutRowId`, `AddColumn`, `NamedParameters`, `ConcurrentReaders`, `WriteAheadLog`, `ForeignKeys`; code asks before relying on one.
- [x] **Gate** `tools/database_sql_check` (ctest `database_sql_check`): fails on a `SqlText` made from anything but a literal outside `tests/` (a `SqlText` constructed through a documented unsafe escape hatch needs an allow-list entry with a reason).
- [x] **Conformance suite** `tests/database/conformance.h` (a Catch2 template over a backend factory): about 40 cases written now and run against SQLite on `:memory:` on Day 253 and on files on Days 254-255: every value type round trips (integer limits, real extremes and `-0`, empty and 1 MiB blobs, text with an embedded NUL and with multi-byte characters); bind errors (out of range, unknown name); `Step` and `Reset` semantics; null versus empty; primary-key, unique, not-null and foreign-key `Constraint` errors; transaction commit, rollback and the guard; savepoints nested three deep; a failed statement inside a transaction leaves the transaction usable; two connections see committed data only; a busy writer returns `Busy` after its timeout; `Supports` agrees with behaviour (a feature reported absent returns `Unsupported`, a reported feature works).
- [x] **Test of the suite itself** (`tests/database/interface_test.cpp`): `Value`/`ValueView`/`SqlText`/`Status` behave as documented (a compile-fail case for a non-literal `SqlText` is checked by the gate's tests).
- [x] **Guide** `docs/guide/database.md` started: the model, the portable code, the rule against built SQL.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the interface and its conformance suite are written and documented, building SQL from data does not compile, and nothing yet implements the interface.

## Notes

- `SetSchemaVersion` became `WriteSchemaVersion`: the `settings_fixtures` gate reads every `SetSchemaVersion(` as the settings system's call. `OpenOptions::journal` and `sync` became `journal_mode` and `sync_mode` (a `[[sync]]` link in `foundation/sync/thread.h` resolved to the field).
- `SqlText::Unchecked` is allowed under `engine/database/` (backends build savepoint statements from a name the interface validates) and, by the gate scanning only `engine/`, `game/` and `tools/`, in `tests/`.
- The conformance suite has 32 cases (`tests/database/conformance.h`); `conformance_stub_test.cpp` instantiates it for an unimplemented backend, so every case skips until Day 253 (which became SQLite on `:memory:`; see that day's file). The suite itself is unproven until a backend runs it.
- `database_sql_check`: 7 ctest cases (real tree, planted violation, message and count, clean, stale entry, entry without reason).
- Runs (Linux, Clang): plain `ran 2244 of 5847 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (CMake files changed, so the script ran every phase); asan `ran 1712 of 5847`; tsan `ran 1394 of 5847`. Failures, all recorded in `KNOWN-ISSUES.md` already or caused by the locked session: the two X11 window tests, and `An entity-built scene renders the same pixels ... [tsan]` (Day 235 entry). `settings_fixtures` failed on the first plain run because of the `SetSchemaVersion` name; fixed by the rename. GCC and Clang non-unity: `database_tests` builds clean and passes 52 of 52. `ctest -L docs` 7 of 7.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, all in `tests/database/conformance.h`.
- Fixed: `Echo` took a connection only to hide it with `(void)` (CLAUDE.md forbids hiding a defect); the parameter is removed.
- Fixed: `SelectWhereOrderLimit` used `<>`, which is outside the portable subset the in-memory database implements; now `>`.
- Fixed: `TransactionGuard` did not check `BeginStatus` in its second block.
- Fixed: a misleading `INFO` that printed a message length instead of the message.
Nothing declined. No regression test applies to the fixes (they change the suite, which no backend runs yet; Day 253 is its first real run, on SQLite).
