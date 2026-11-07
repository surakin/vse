# Phase 11 / Day 136 — Versioning, Migration and Unknown Keys

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 135.

Files outlive engine versions. New files `settings/migration.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [x] Format and schema versions: the file's `version = N` (the format) and a per-section schema version registered by the owner (`Registry::SetSchemaVersion("renderer", 3)`); a file newer than the program is read in **compatibility mode** (known keys applied, the rest kept verbatim on save, a warning) and is never rewritten in an older format without `ReplaceNewer`.
- [x] `Migration` steps registered by the owner: **rename** (`old key -> new key`, also via `SettingDesc::deprecated_names`), **value map** (`old value -> new value` for one key), **split/merge** by a small callback, and **drop** (key no longer exists); steps are ordered by the version they upgrade from, applied on load to the in-memory view, and the file is rewritten (through the document model, keeping comments) only when the user next saves; the report lists each migration applied.
- [x] Unknown keys: kept verbatim in the file, reported once as warnings with a "did you mean" suggestion (closest registered key), never fatal; unknown keys from a newer version survive a save by an older program (test).
- [x] Deprecated settings: a registered-as-deprecated key still resolves (with a warning naming the replacement) for the number of versions the owner states.
- [x] A **fixtures directory** `tests/settings/fixtures/` of real files from each past schema (written as the Phase 8 renderer formats were: the `version = 1` text of `renderer::SerializeSettings`) that must keep loading; adding a schema version without a fixture and a migration test fails a check (a ctest `settings_fixtures`).
- [x] Tests (phase 11): rename chains (`a -> b -> c`), a value map, split and merge, drop, a file from the future, unknown keys kept on save, suggestions, deprecation warnings, migration idempotence (migrating twice equals once), the report; every fixture loads.
- [x] **Documentation:** the guide gets "Changing a setting between versions" (the rules for owners: never reuse a key for a different meaning, how to rename, how to remove); labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [x] Update `STATUS.md`; commit.

**Done:** old files keep loading after keys and meanings change, newer files are never damaged by older programs, and a missing migration test fails the build.


## Result

- New: `settings/migration.h/.cpp` (`MigrationSet` with `Rename`, `MapValue`, `Drop` and `Callback` steps, `MigrationReport`, `StoredFormatVersion`/`StoredSchemaVersion`/`StampVersions`, `kFormatVersion`), `Registry::SetSchemaVersion`/`SchemaVersion`, `StoreError::TooNew`, `Settings::Migrations()`, `LoadReport::notes`, `SaveOptions::replace_newer`, `Settings::UserFileIsNewer()` and `LastSaveStatus()`; fixtures `tests/settings/fixtures/` (`demo_v1.cfg`, `demo_v2.cfg`, `SCHEMAS.txt`); the check `tools/settings_fixtures_check` (ctest `settings_fixtures` plus clean, planted and planted-message self-tests); tests `migration_test.cpp`; guide section "Changing a setting between versions".
- Deviations: migrations run on the user's (writable) store only; a read-only layer ships in the current format and gets renames through `deprecated_keys` (the lookup falls back to a layer's deprecated key); the deprecated-key period is the owner's (a key stays resolvable while it is listed, there is no countdown); the fixture check scans `engine/` and `game/` for literal `SetSchemaVersion("section", N)` calls (a test registers made-up schemas freely, and a call with a non-literal argument is itself an error).
- Runs: plain `ran 1267 of 3321 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan `ran 932 of 3321`; tsan `ran 649 of 3321`; settings executables 30531 assertions in 95 cases; docs lint 3992/3992.

## Post-implementation code review

`/code-review high`: 10 findings, all fixed with a test: a `replace_newer` save kept the newer stamps (now overwritten) and `newer_file_` stayed set after it (cleared), and a rejected save is visible through `LastSaveStatus()`; a failed step advanced the schema anyway (now the schema stays so the next load retries, and a non-text value that cannot be renamed is reported); a new install ran every migration against an empty store (now only stamped); an unreadable version number counted as an old one (now as newer, so nothing is migrated or rewritten); a setting could be registered as `version` or `<section>.schema` (refused); an unknown key was mentioned once per layer (once); the fixture check accepted a name in a comment or an unreadable call (code only, literal calls only); the dead `any` flag. An ASan run also found a test passing a pointer to a temporary path (fixed in the test).
