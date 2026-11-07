# Phase 11 / Day 134 — Layered Sources, Environment and Command Line

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Days 131-133.

Several stores read as one, with a defined precedence and a way to ask where a value came from. New files `settings/layers.h/.cpp`, `settings/env_source.h/.cpp`, `settings/command_line_source.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [x] `settings::LayerStack`: an ordered list of named layers, each a `ConfigStore` plus a role (`Defaults` from the registry, `System`, `User`, `Project`, `Environment`, `CommandLine`, and `Custom` between), lowest to highest as the spec fixed; `Resolve(Key)` returns the value and the **provenance** (layer name, the store's `Describe()`, the line when the store knows it); `ResolveAll` for the listing; an unset value falls through to the next lower layer.
- [x] Which layer is written: `LayerStack::WriteLayer()` (default `User`); `Set` goes to it and a value equal to what the lower layers already give can be dropped (`MinimalWrites` option) so files do not fill with defaults; `Unset` removes the key from the write layer so the lower layer shows again.
- [x] `EnvironmentSource` (read-only store over a snapshot of the process environment): `ENGINE_SETTING_<KEY>` with dots as `__` (documented, with a test), plus each setting's declared `env` alias (the old `ENGINE_RHI_GPU` style); values are converted by the registry's type and a bad value is reported with the variable name, never silently dropped.
- [x] `CommandLineSource` (read-only store over `os::CommandLine`): `--set key=value` (repeatable), `--settings FILE` (adds a `Custom` layer over a `FileConfigStore`), each setting's declared `flag` alias (`--gpu NAME`, `--no-vsync` as a bool flag with its negation), `--list-settings`/`--help` handled by the caller using the registry; unknown `--set` keys are an error with a suggestion (closest key by edit distance); arguments the source does not own are left for the application.
- [x] Shipped defaults: a `ReadOnlyConfigStore` layer for values a game ships (the `System` layer), loadable from a file next to the executable (`io::ExecutableDirectory`).
- [x] Tests (phase 11): precedence table (every pair of layers, 8 cases of set/unset combinations); provenance strings; a bad environment value and a bad flag value; `--set` with `=` inside the value, quoted values, repeated keys (last wins), unknown keys with a suggestion; `MinimalWrites`; the environment scrubbed (tests set and restore variables); layers over the failing and read-only doubles (a failing layer is reported and skipped, the rest still resolve).
- [x] **Documentation:** the guide gets "Layers and where a value comes from" and "Environment and command line" with run samples; labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [x] Update `STATUS.md`; commit.

**Done:** values resolve through a documented stack of layers, every value can say where it came from, and the environment and the command line are layers like any other.


## Result

- New: `settings/layers.h/.cpp` (`LayerStack`, `LayerRole`, `Resolution` with provenance, `LayerProblem`, `SuggestKey`), `settings/env_source.h` with `env_source.cpp` and `command_line_source.cpp` (`EnvironmentSnapshot`, `EnvironmentSource`, `CommandLineSource`, their shared `TextEntryStore`); `ConfigStore::Origin` and `IsPresent`; `FileStoreOptions::read_only` (shipped defaults from a file); `ConfigDocument::LineOf`; tests `layers_test.cpp` (all 64 combinations of six layers holding a value, role ordering, fall-through past bad, failing and failed layers, provenance, `MinimalWrites`/`Unset`, environment names and the real process environment, the command line); guide sections "Layers and where a value comes from" and "Environment and command line".
- Deviations: `CommandLineSource` reads `argc`/`argv` directly, not `os::CommandLine` (that class keeps only the last of a repeated flag and has no `--name value` form, so `--set` twice and `--gpu NAME` cannot be expressed in it); `--save-settings` takes its file only as `--save-settings=FILE`; a read-only `FileConfigStore` is the "file next to the executable" layer (the caller builds the path with `io::ExecutableDirectory`).
- Runs: plain `ran 1236 of 3236 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 test fails); asan `ran 905 of 3236`; tsan `ran 622 of 3236`; settings executables 30196 assertions in 68 cases; docs lint 3870/3870.

## Post-implementation code review

`/code-review high`: 10 findings. Fixed (each with a test): a value over 511 bytes was silently cut (now `TooLong`, like the file store); a bare `--` was not honoured; `--save-settings FILE` swallowed an application positional; a bool flag with a separate value and `--no-flag=x` passed silently; `SuggestKey` allocated per candidate and suggested unrelated keys for short input; a deprecated key in a lower layer was ignored (now read, with a note); an empty environment variable shadowed lower layers (now unset); the two sources shared copy-pasted code (a `TextEntryStore` base now). Declined: a read-only store's `Commit` returning `ReadOnly` with nothing staged (the Day 132 contract; `Settings::Save` commits only the write layer); the quadratic `EnvironmentSnapshot::Add` (a few hundred variables at most).
