# Phase 10 / Day 138 — Command Line, the Demos and the Other Consumers

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Days 134-137.

The user-visible end: the demos finally get the options that were held back (`KNOWN-ISSUES.md`, "Needed: an engine settings system"), and the engine's scattered environment variables and the log configuration become settings. Files touched: `game/renderer_demo`, `game/rhi_demo`, `game/*_bench`, `engine/foundation/log`, `engine/foundation/main.cpp`, `engine/rhi`, `engine/jobs`, `engine/foundation/debug`.

## Tasks

- [ ] A shared `demo::SettingsMain` helper in `game/common` that builds a `Settings` for a demo (application name, registered settings, command line), handles `--settings FILE`, `--save-settings [FILE]`, `--set key=value`, `--list-settings` (every setting: key, type, default, current value, where it came from, description) and prints `--help` **generated from the registry and the flag aliases**; unknown options are errors with a suggestion.
- [ ] `renderer_demo` and `rhi_demo` use it: their existing flags (`--gpu`, `--no-vsync`, `--validation`, `--backend`, `--frames`, `--script`, ...) become flag aliases of settings where they are settings and stay plain launch options where they are not (the Day 130 inventory decides each); behavior of every existing invocation is unchanged (the demo ctests pass unmodified); the F-keys of `renderer_demo` go through `Settings::Set`, so a saved file reproduces a session (`--save-settings out.cfg`, then `--settings out.cfg`).
- [ ] The engine's own environment variables registered as settings with `env` aliases and read through the environment layer where an application has a `Settings`, with the old direct `getenv` kept only where no `Settings` can exist yet (before `main` has built one; each such place listed in the spec): `ENGINE_RHI_GPU`, `ENGINE_RHI_VALIDATION*`, `ENGINE_RHI_CACHE_DIR`, `ENGINE_WINDOW_BACKEND`, `ENGINE_JOB_THREADS`, `ENGINE_CRASH_HANDLER`. `DeviceCreateInfo` is filled from the settings by one function.
- [ ] Logging: channel verbosity and enabled state are settings (`log.<channel>.verbosity`, `log.<channel>.enabled`), applied through a listener; `log::LoadConfig` is reimplemented on the settings system (same file lines still work through a documented migration) or removed with its test ported; the Phase 6 log tests pass.
- [ ] `KNOWN-ISSUES.md`: the "Needed: an engine settings system" entry is removed; entries for what this phase leaves (remote config, a UI binding layer, secrets) are filed.
- [ ] Tests (phase 10 and the demo ctests): `--list-settings` golden output; `--help` golden output; `--set` of every type through the demo's argument parser; a saved file reproduces a scripted session; the old flags still work; the environment variables still work and now show their provenance; log channel verbosity from a settings file.
- [ ] **Documentation:** the demo READMEs (options tables generated or checked against the registry by a ctest `demo_help_current`); the guide gets "Settings in an application" (the standard `main`); reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan and tsan flavors; both demos 1000 frames with `--validation` on Wayland and X11; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`; commit.

**Done:** both demos read, list, set and save settings, the old invocations behave exactly as before, the engine's environment variables and log configuration are settings with visible provenance, and the held-back item is gone from `KNOWN-ISSUES.md`.
