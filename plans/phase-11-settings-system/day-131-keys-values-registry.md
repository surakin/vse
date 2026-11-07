# Phase 11 / Day 131 — Keys, Values and the Registry

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Day 130 (the spec).

The model, with no storage yet: what a setting is, how it is named and declared, and how code reads one cheaply. New files `engine/foundation/settings/key.h`, `value.h`, `registry.h` and `.cpp`s; tests in `tests/settings/`.

## Tasks

- [x] Phase setup that waited for this day: `flavors 11 plain,asan,tsan` in `tests/phases.txt`, `tests/settings/` (own executables per flavor, like `tests/io`) with its line, the `settings` module in `docs/reference/modules.txt`, `engine/foundation/settings` in `tests/module_map.txt`.
- [x] `settings::Key`: a validated dotted name (`renderer.present_mode`, at most 3 segments of lowercase letters, digits and `_`, 31 bytes per segment and 95 in all (the spec's numbers; the 63 first planned were too small for `log.<channel>.verbosity`)), a value type with `Parse(StringView, Key*)`, comparison, hash, `Segments()`, and the section/name split the file format uses; invalid text fails with a documented status.
- [x] `settings::Value`: a small tagged value (`Bool`, `Int` as `i64`, `Float` as `f64`, `Enum` as an index, `Text` as an owned UTF-8 string, plus `List` if Day 130 decided on lists), equality, a canonical text form (`ToText`) and a parser from text for a declared type (`FromText`) with line-free errors (`BadNumber`, `OutOfRange`, `NotInEnum`, `TooLong`, ...).
- [x] `settings::SettingDesc`: type, default, range with a `RangePolicy` (clamp or reject, as the renderer's), enum names, label, description, group, `restart_required`, an **apply class** (`u8` plus a name table owned by the registrant — the renderer's `CostClass` becomes one such table), a capability name, optional `env` alias and optional `flag` alias (used on Days 134 and 138), a `deprecated_names` list (Day 136).
- [x] `settings::Registry`: `Register(desc)` returning a `SettingId`; lookup by key and by id; iteration in registration order and sorted by key; the registry is filled at start-up and then read-only (`Freeze()`; registering after it aborts); a `Registry::Global()` and constructible private registries for tests; every registration failure is a status with the reason (duplicate key, bad default, range, enum count, ...).
- [x] `settings::Setting<T>` typed handles (`Setting<bool>`, `<i64>`, `<f32>`, `<f64>`, `<Text>`, `Enum<E>`): created from a registered id, `Get()` with the cost the Day 130 Spike B chose, `Default()`, `Desc()`; reading a handle of the wrong type is a compile error or an abort at creation, not a silent conversion.
- [x] Tests (phase 11): key grammar table (60 rows: empty, uppercase, dots at the ends, too many segments, long, Unicode); `Value` text round trips for every type including float edge cases (`-0`, `1e-30`, NaN and infinity refused); every registration failure; id stability; a registry of 500 settings; handles read their default; the wrong-type handle; TSan: readers while the registry is only read.
- [x] **Documentation:** `///` comments with labels; the first version of `docs/guide/settings.md` (Keys and values, Declaring a setting, Reading a setting, with tagged samples); reference regenerated; `docgen lint` at zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [x] Update `STATUS.md`; commit.

**Done:** settings are declared in a registry with typed handles that read at the cost the spec chose, keys and values parse and print canonically, and the guide has its first sections.


## Result

- New: `engine/foundation/settings/` `key`, `value`, `desc` (declaration, `ParseValue`, `FormatValue`), `registry`, `setting` (typed handles); `tests/settings/` (3 executables: plain, asan, tsan; 18 cases, 2896 assertions); `docs/guide/settings.md` (Keys and values, Declaring a setting, Reading a setting, two run samples); `settings` module in `modules.txt`; phase setup (`flavors 11`, module map, `test_phase_script`).
- Not done: `Aborts:` paths (register after `Freeze`, wrong-type handle) are documented but not death-tested (the death-test helper lives in `tests/debug`'s support code); floats are parsed and printed with `std::from_chars`/`std::to_chars` (locale-independent); `List` is not a type (spec decision).
- Runs: plain `ran 1186 of 3086 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan `ran 855 of 3086`; tsan `ran 572 of 3086`. Docs: lint 3664/3664, `ctest -L docs` passes.

## Post-implementation code review

`/code-review high`: 10 findings. Fixed: floats printed as `1e+02` for 100.0 (now `to_chars` shortest, plain decimals for ordinary sizes); duplicate deprecated keys inside one declaration; enum names restricted to safe printable ASCII (no `[`, `]`, `\`, controls); locale-dependent `strtod`/`printf` replaced by `from_chars`/`to_chars`; `Value::operator==` compares float bits (`-0.0` differs); `Setting::Default/Desc` assert on an empty handle; `Publish` asserts the enum index; `IsFrozen` is atomic; the 63-byte note in this file. Declined: the 540-byte `Value` (config values are not read through `Value` on hot paths: handles read the 8-byte cell; a text-heap design can follow if a profile asks); the lint-baseline churn comes from regenerating the reference.
