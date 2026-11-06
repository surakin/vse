# Phase 8 / Day 104 — The Settings Model: `RenderSettings`, Cost Classes, Diff

**Phase:** [Phase 8 — RHI Reset](../phase-08-rhi-reset.md)
**Depends on:** Day 103 (spec).

Pure data and pure functions, no GPU: what a setting is, what changing it costs, and how two settings become the cheapest
sufficient action. New files `engine/renderer/render_settings.h/.cpp`, tests in `tests/renderer`.

## Tasks

- [x] `SettingId` and a **setting registry**: each setting declares an id (stable string and small integer), a type (bool, enum with names, integer with range, float with range, string), a default, its **cost class** (`None`, `Dynamic`, `Swapchain`, `PipelineRebuild`, `DeviceReset`) and the capability it needs (none, or a named `DeviceCaps`/window capability). The registry is open: a later phase registers its own settings; no switch over a closed list in the diff.
- [x] The built-in settings, each with a class chosen from the spec: `backend` (DeviceReset), `adapter` (DeviceReset), `validation` and `sync_validation` (DeviceReset), `frames_in_flight` (DeviceReset), `present_mode` (Swapchain), `width`/`height`/`fullscreen` (Swapchain), `swapchain_format`/`hdr` (Swapchain), `shader_quality` (PipelineRebuild), `gbuffer_debug_layer` (Dynamic), `render_scale` (Dynamic). Settings that exist only to prove the mechanism are marked as such in the registry.
- [x] `RenderSettings`: a value type holding one value per registered setting (fixed-capacity storage, no heap per copy, no STL); `Get`/`Set` by id with range clamping or rejection (decide and document which); `operator==`; a `version` field (format version of the serialized form) and `generation` (incremented on each applied change, not part of equality).
- [x] `DiffSettings(const RenderSettings& from, const RenderSettings& to, const Capabilities&)` returns a `SettingsChange`: the highest cost class among the changed settings (the cheapest *sufficient* action, because a higher class includes the lower ones), the list of changed ids, and the list of requests that are **unsupported** (capability missing) with a reason; an unsupported request is dropped to its default or rejected as the registry says, never silently ignored.
- [x] Serialization: `SerializeSettings` to a small text format (`key = value` lines, stable order, comments allowed) and `ParseSettings` back; unknown keys are reported as warnings and skipped (not kept: later phases register their settings at start-up, so an unknown key is stale), malformed values are errors with a line number; a `version` mismatch is handled by a documented migration hook. Round trip is byte-stable (minted-output rule: same input, same output).
- [x] `Capabilities` description: what the device and window can do (present modes, HDR formats, fullscreen, adapter count) as plain data filled by Day 105-107; here only the type and a fake for tests.
- [x] Tests (phase 8 in `tests/phases.txt`): the diff picks the right class for each built-in setting and for combinations; a later-registered setting with its own class takes part without code changes; unsupported requests; equality ignores `generation`; clamp/reject behavior; serialize/parse round trip, unknown key, bad value, version migration; fixed-capacity overflow reports an error instead of crashing; 1000 random setting pairs: `Diff(a,b)` class equals the maximum class of the individually changed settings, `Diff(a,a)` is `None`.
- [x] **Documentation:** `///` comments with `Fails:`/`Aborts:` labels; a new `docs/guide/render-settings.md` (what a setting is, cost classes, how to register one, the file format) with a tagged sample that compiles; `render_settings.h` is covered by the `renderer` module in `docs/reference/modules.txt`; `docgen lint` at zero; `cmake --build build --target docs` and the generated reference committed; `docs/guide/README.md` lists the page.
- [x] Run `scripts/test-phase.sh --affected` and quote the line; sanitizer flavors if the settings storage does any manual memory work.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it under "Post-implementation code review".
- [x] Update `STATUS.md`; commit.

**Done:** the registry, `RenderSettings`, `DiffSettings` and the text format exist with the tests above passing; a setting registered outside the module changes the diff result with no edit to the diff code; the guide page `render-settings.md` is committed and `ctest -L docs` passes.


## Result

New: `engine/renderer/render_settings.h/.cpp` (registry, `RenderSettings`, `Capabilities`, `DiffSettings`, `ResolveSettings`, text format),
`tests/renderer/render_settings_test.cpp` (20 test cases, 14473 assertions, phase 8; `flavors 7 plain,asan,tsan` is in `tests/phases.txt`),
`docs/guide/render-settings.md` (two compiled and run samples). `tests/renderer/` is now listed file by file in `tests/phases.txt`
(the phase map refuses two phases for one file, so a directory line cannot be overridden).

Deviations from the task list: unknown keys are skipped with a warning rather than kept; a number outside its range is clamped
(a warning in a parse) unless the setting uses `RangePolicy::Reject`; an unsupported change is left out of `changed` and
`ResolveSettings` restores the current value (instead of "dropped to its default"). Putting a gated setting back to its default
never needs the capability.

Scoped runs: plain `ran 1090 of 2444 tests (phases: all; flavors: plain; display: included)`, 100% passed; `ctest -L docs` 4/4;
ASan `ran 810 of 2444 tests (phases: all; flavors: asan; display: included)` and TSan `ran 544 of 2444 ... tsan`, both 100% passed; docgen samples 29 compiled, 25 run, 0 failed; the non-unity build compiles.

## Post-implementation code review

`/code-review high` found 7 issues.

1. A capability-gated setting could not be turned back to its default once the capability was gone (the diff called every change
   unsupported). **Fixed** (only a change to a non-default value needs it); regression test added and confirmed to fail without the fix.
2. `SetText` accepted leading/trailing blanks that the file format trims, so a round trip changed the value. **Fixed** (`BadText`); test added, fails without the fix.
3. Enum names with blanks, `=`, `#`, empty or null could not be read back or crashed. **Fixed** in `Register`; test added.
4. The `Get*` comment contradicted the code. **Fixed**.
5. `DiffSettings` repeated the per-type comparison of `operator==`. **Fixed** (one `SameValue` helper).
6. The G-buffer layer names are listed here and in the demo. **Declined for now**: Day 113 wires the demo to the setting and
   removes the duplicate.
7. No test for the abort on comparing settings of different registries. **Declined**: a death test needs the phase 7 helper; the
   contract is documented as `Aborts:`.
