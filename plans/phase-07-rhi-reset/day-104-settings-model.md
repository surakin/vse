# Phase 7 / Day 104 — The Settings Model: `RenderSettings`, Cost Classes, Diff

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 103 (spec).

Pure data and pure functions, no GPU: what a setting is, what changing it costs, and how two settings become the cheapest
sufficient action. New files `engine/renderer/render_settings.h/.cpp`, tests in `tests/renderer`.

## Tasks

- [ ] `SettingId` and a **setting registry**: each setting declares an id (stable string and small integer), a type (bool, enum with names, integer with range, float with range, string), a default, its **cost class** (`None`, `Dynamic`, `Swapchain`, `PipelineRebuild`, `DeviceReset`) and the capability it needs (none, or a named `DeviceCaps`/window capability). The registry is open: a later phase registers its own settings; no switch over a closed list in the diff.
- [ ] The built-in settings, each with a class chosen from the spec: `backend` (DeviceReset), `adapter` (DeviceReset), `validation` and `sync_validation` (DeviceReset), `frames_in_flight` (DeviceReset), `present_mode` (Swapchain), `width`/`height`/`fullscreen` (Swapchain), `swapchain_format`/`hdr` (Swapchain), `shader_quality` (PipelineRebuild), `gbuffer_debug_layer` (Dynamic), `render_scale` (Dynamic). Settings that exist only to prove the mechanism are marked as such in the registry.
- [ ] `RenderSettings`: a value type holding one value per registered setting (fixed-capacity storage, no heap per copy, no STL); `Get`/`Set` by id with range clamping or rejection (decide and document which); `operator==`; a `version` field (format version of the serialized form) and `generation` (incremented on each applied change, not part of equality).
- [ ] `DiffSettings(const RenderSettings& from, const RenderSettings& to, const Capabilities&)` returns a `SettingsChange`: the highest cost class among the changed settings (the cheapest *sufficient* action, because a higher class includes the lower ones), the list of changed ids, and the list of requests that are **unsupported** (capability missing) with a reason; an unsupported request is dropped to its default or rejected as the registry says, never silently ignored.
- [ ] Serialization: `SerializeSettings` to a small text format (`key = value` lines, stable order, comments allowed) and `ParseSettings` back; unknown keys are kept and reported as warnings, malformed values are errors with a line number; a `version` mismatch is handled by a documented migration hook. Round trip is byte-stable (minted-output rule: same input, same output).
- [ ] `Capabilities` description: what the device and window can do (present modes, HDR formats, fullscreen, adapter count) as plain data filled by Day 105-107; here only the type and a fake for tests.
- [ ] Tests (phase 7 in `tests/phases.txt`): the diff picks the right class for each built-in setting and for combinations; a later-registered setting with its own class takes part without code changes; unsupported requests; equality ignores `generation`; clamp/reject behavior; serialize/parse round trip, unknown key, bad value, version migration; fixed-capacity overflow reports an error instead of crashing; 1000 random setting pairs: `Diff(a,b)` class equals the maximum class of the individually changed settings, `Diff(a,a)` is `None`.
- [ ] **Documentation:** `///` comments with `Fails:`/`Aborts:` labels; a new `docs/guide/render-settings.md` (what a setting is, cost classes, how to register one, the file format) with a tagged sample that compiles; `render_settings.h` is covered by the `renderer` module in `docs/reference/modules.txt`; `docgen lint` at zero; `cmake --build build --target docs` and the generated reference committed; `docs/guide/README.md` lists the page.
- [ ] Run `scripts/test-phase.sh --affected` and quote the line; sanitizer flavors if the settings storage does any manual memory work.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it under "Post-implementation code review".
- [ ] Update `STATUS.md`; commit.

**Done:** the registry, `RenderSettings`, `DiffSettings` and the text format exist with the tests above passing; a setting registered outside the module changes the diff result with no edit to the diff code; the guide page `render-settings.md` is committed and `ctest -L docs` passes.
