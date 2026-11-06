# Phase 11 / Day 136 — Versioning, Migration and Unknown Keys

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 135.

Files outlive engine versions. New files `settings/migration.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [ ] Format and schema versions: the file's `version = N` (the format) and a per-section schema version registered by the owner (`Registry::SetSchemaVersion("renderer", 3)`); a file newer than the program is read in **compatibility mode** (known keys applied, the rest kept verbatim on save, a warning) and is never rewritten in an older format without `ReplaceNewer`.
- [ ] `Migration` steps registered by the owner: **rename** (`old key -> new key`, also via `SettingDesc::deprecated_names`), **value map** (`old value -> new value` for one key), **split/merge** by a small callback, and **drop** (key no longer exists); steps are ordered by the version they upgrade from, applied on load to the in-memory view, and the file is rewritten (through the document model, keeping comments) only when the user next saves; the report lists each migration applied.
- [ ] Unknown keys: kept verbatim in the file, reported once as warnings with a "did you mean" suggestion (closest registered key), never fatal; unknown keys from a newer version survive a save by an older program (test).
- [ ] Deprecated settings: a registered-as-deprecated key still resolves (with a warning naming the replacement) for the number of versions the owner states.
- [ ] A **fixtures directory** `tests/settings/fixtures/` of real files from each past schema (written as the Phase 8 renderer formats were: the `version = 1` text of `renderer::SerializeSettings`) that must keep loading; adding a schema version without a fixture and a migration test fails a check (a ctest `settings_fixtures`).
- [ ] Tests (phase 11): rename chains (`a -> b -> c`), a value map, split and merge, drop, a file from the future, unknown keys kept on save, suggestions, deprecation warnings, migration idempotence (migrating twice equals once), the report; every fixture loads.
- [ ] **Documentation:** the guide gets "Changing a setting between versions" (the rules for owners: never reuse a key for a different meaning, how to rename, how to remove); labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`; commit.

**Done:** old files keep loading after keys and meanings change, newer files are never damaged by older programs, and a missing migration test fails the build.
