# Phase 10 / Day 137 — Move the Renderer onto the Shared System

**Phase:** [Phase 10 — Settings System](../phase-10-settings-system.md)
**Depends on:** Days 131-136; Phase 7 (`renderer::RenderSettings`, `SettingsRegistry`, `Renderer::ApplySettings`).

The first consumer, with no change in behavior: every Phase 7 settings test passes against the new registry. Files touched: `engine/renderer/render_settings.*`, `renderer.*`, `device_resources`, the tests in `tests/renderer/`.

## Tasks

- [ ] The 14 built-in rendering settings are declared in the shared `settings::Registry` under the `renderer.` section (`renderer.present_mode`, ...), with their types, defaults, ranges and policies; `CostClass` becomes the renderer's **apply class table** (`none`, `dynamic`, `swapchain`, `pipeline-rebuild`, `device-reset`) registered with the settings; `capability` and `mechanism_only` are kept as attributes of the description. Decide in the day (spec rule from Day 130) whether `renderer::SettingsRegistry` remains as a thin facade for the existing API or the call sites move; either way **no test of Phase 7 is weakened** (a test may change its spelling of a key, never its expectation).
- [ ] `renderer::RenderSettings` stays the value type the renderer works with (a plain struct of resolved values with `Set*`/`Get*` by id), now built from and written to a `settings::Settings` snapshot (`RenderSettings::FromSnapshot`, `ToChanges`); `DiffSettings`/`ResolveSettings`/`Capabilities` keep their meaning.
- [ ] The text format: `SerializeSettings`/`ParseSettings` (Phase 7) become thin wrappers over `config_text` with the section `[renderer]`; the version-1 text of Day 104 loads through a migration (keys without the section prefix map to `renderer.*`) and is a fixture (Day 136's check); a Phase 7 `ParseSettings` test file keeps passing unchanged apart from the section header.
- [ ] `Renderer::ApplySettings` is wired as the apply hook for the `renderer.*` changes: `Settings::Apply` hands the renderer the change list, the renderer applies the cheapest sufficient action at the frame boundary on the owner thread as before, and a failure rolls the setting back (Day 135's rollback) with the renderer's existing status; `Renderer::SetSettingsSource(Settings*)` is optional (a renderer still works with a plain `RenderSettings`).
- [ ] Tests (phase 10 and the existing phases): all `tests/renderer` settings, apply, device-loss and reset tests pass; new tests: change a rendering setting through `Settings::Set` and observe the renderer apply it with the right cost class; a rollback through the new path; a settings file with `[renderer]` keys loads, applies at start-up, saves a changed value in place; the version-1 fixture loads.
- [ ] **Documentation:** `docs/guide/render-settings.md` rewritten around the shared system (keys, apply classes, the file section, the migration note); `renderer.md` links updated; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` (this day touches the renderer, so it runs phases 3, 4 and 7 too) and the asan and tsan flavors; quote the lines. The renderer demo runs 1000 frames with `--validation` on Wayland and X11.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: behavior-preservation (diff the settings listing before and after), the owner-thread rule, rollback.
- [ ] Update `STATUS.md`; commit.

**Done:** the rendering settings live in the shared registry and are loaded, layered and saved by the shared system, every Phase 7 test still passes with its expectation unchanged, and the renderer applies changes arriving through `Settings`.
