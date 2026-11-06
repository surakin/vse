# Phase 11 / Day 134 — Layered Sources, Environment and Command Line

**Phase:** [Phase 11 — Settings System](../phase-11-settings-system.md)
**Depends on:** Days 131-133.

Several stores read as one, with a defined precedence and a way to ask where a value came from. New files `settings/layers.h/.cpp`, `settings/env_source.h/.cpp`, `settings/command_line_source.h/.cpp`; tests in `tests/settings/`.

## Tasks

- [ ] `settings::LayerStack`: an ordered list of named layers, each a `ConfigStore` plus a role (`Defaults` from the registry, `System`, `User`, `Project`, `Environment`, `CommandLine`, and `Custom` between), lowest to highest as the spec fixed; `Resolve(Key)` returns the value and the **provenance** (layer name, the store's `Describe()`, the line when the store knows it); `ResolveAll` for the listing; an unset value falls through to the next lower layer.
- [ ] Which layer is written: `LayerStack::WriteLayer()` (default `User`); `Set` goes to it and a value equal to what the lower layers already give can be dropped (`MinimalWrites` option) so files do not fill with defaults; `Unset` removes the key from the write layer so the lower layer shows again.
- [ ] `EnvironmentSource` (read-only store over a snapshot of the process environment): `ENGINE_SETTING_<KEY>` with dots as `__` (documented, with a test), plus each setting's declared `env` alias (the old `ENGINE_RHI_GPU` style); values are converted by the registry's type and a bad value is reported with the variable name, never silently dropped.
- [ ] `CommandLineSource` (read-only store over `os::CommandLine`): `--set key=value` (repeatable), `--settings FILE` (adds a `Custom` layer over a `FileConfigStore`), each setting's declared `flag` alias (`--gpu NAME`, `--no-vsync` as a bool flag with its negation), `--list-settings`/`--help` handled by the caller using the registry; unknown `--set` keys are an error with a suggestion (closest key by edit distance); arguments the source does not own are left for the application.
- [ ] Shipped defaults: a `ReadOnlyConfigStore` layer for values a game ships (the `System` layer), loadable from a file next to the executable (`io::ExecutableDirectory`).
- [ ] Tests (phase 11): precedence table (every pair of layers, 8 cases of set/unset combinations); provenance strings; a bad environment value and a bad flag value; `--set` with `=` inside the value, quoted values, repeated keys (last wins), unknown keys with a suggestion; `MinimalWrites`; the environment scrubbed (tests set and restore variables); layers over the failing and read-only doubles (a failing layer is reported and skipped, the rest still resolve).
- [ ] **Documentation:** the guide gets "Layers and where a value comes from" and "Environment and command line" with run samples; labels; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`; commit.

**Done:** values resolve through a documented stack of layers, every value can say where it came from, and the environment and the command line are layers like any other.
