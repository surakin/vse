# Phase 10 / Day 131 — Keys, Values and the Registry

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Day 130 (the spec).

The model, with no storage yet: what a setting is, how it is named and declared, and how code reads one cheaply. New files `engine/foundation/settings/key.h`, `value.h`, `registry.h` and `.cpp`s; tests in `tests/settings/`.

## Tasks

- [ ] Phase setup that waited for this day: `flavors 10 plain,asan,tsan` in `tests/phases.txt`, `tests/settings/` (own executables per flavor, like `tests/io`) with its line, the `settings` module in `docs/reference/modules.txt`, `engine/foundation/settings` in `tests/module_map.txt`.
- [ ] `settings::Key`: a validated dotted name (`renderer.present_mode`, at most 3 segments of lowercase letters, digits and `_`, 63 bytes total), a value type with `Parse(StringView, Key*)`, comparison, hash, `Segments()`, and the section/name split the file format uses; invalid text fails with a documented status.
- [ ] `settings::Value`: a small tagged value (`Bool`, `Int` as `i64`, `Float` as `f64`, `Enum` as an index, `Text` as an owned UTF-8 string, plus `List` if Day 130 decided on lists), equality, a canonical text form (`ToText`) and a parser from text for a declared type (`FromText`) with line-free errors (`BadNumber`, `OutOfRange`, `NotInEnum`, `TooLong`, ...).
- [ ] `settings::SettingDesc`: type, default, range with a `RangePolicy` (clamp or reject, as the renderer's), enum names, label, description, group, `restart_required`, an **apply class** (`u8` plus a name table owned by the registrant — the renderer's `CostClass` becomes one such table), a capability name, optional `env` alias and optional `flag` alias (used on Days 134 and 138), a `deprecated_names` list (Day 136).
- [ ] `settings::Registry`: `Register(desc)` returning a `SettingId`; lookup by key and by id; iteration in registration order and sorted by key; the registry is filled at start-up and then read-only (`Freeze()`; registering after it aborts); a `Registry::Global()` and constructible private registries for tests; every registration failure is a status with the reason (duplicate key, bad default, range, enum count, ...).
- [ ] `settings::Setting<T>` typed handles (`Setting<bool>`, `<i64>`, `<f32>`, `<f64>`, `<Text>`, `Enum<E>`): created from a registered id, `Get()` with the cost the Day 130 Spike B chose, `Default()`, `Desc()`; reading a handle of the wrong type is a compile error or an abort at creation, not a silent conversion.
- [ ] Tests (phase 10): key grammar table (60 rows: empty, uppercase, dots at the ends, too many segments, long, Unicode); `Value` text round trips for every type including float edge cases (`-0`, `1e-30`, NaN and infinity refused); every registration failure; id stability; a registry of 500 settings; handles read their default; the wrong-type handle; TSan: readers while the registry is only read.
- [ ] **Documentation:** `///` comments with labels; the first version of `docs/guide/settings.md` (Keys and values, Declaring a setting, Reading a setting, with tagged samples); reference regenerated; `docgen lint` at zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** settings are declared in a registry with typed handles that read at the cost the spec chose, keys and values parse and print canonically, and the guide has its first sections.
