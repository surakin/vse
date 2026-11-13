# Phase 26 / Day 324 — Text, UI and the Scene Bridge on Metal

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 323.

What Phases 14, 15 and 17 put on the GPU: the glyph atlas and text pass, `UiRenderer`, UI surfaces rendered to textures, the viewport widget, and the
scene bridge drawing ECS entities, all through Metal; plus any GPU path a main-line phase added before today (the spatial culling of Phase 18 Day 247).

## Tasks

- [x] **Inventory at the start:** (made from the code below; the runs themselves are Day 325's) every GPU test on `master` outside the Phase 4 and 5 suites (`text_golden_gpu_test`, `ui_golden_gpu_test`,
      `ui_demo_gpu_test`, scene and culling GPU tests, anything newer), each run with a Metal factory.
- [x] **Text:** (Day 325: the text tests pass through Metal) ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (the Day 325 run: the text tests run once per backend) the atlas texture (`R8` pages, partial uploads), the text pass, the large-text stand-in; the text golden images within tolerance;
      Retina scale (text rasterized at the backing scale).
- [x] **UI:** (Day 325: the UI goldens pass through Metal) ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (the Day 325 run) `UiRenderer`'s draw list (scissors, nine-slice, images by `ImageId`), the viewport widget hosting a rendered world, UI surfaces to textures
      sampled in the 3D scene; `ui_golden_gpu_test` and `ui_demo_gpu_test` pass.
- [x] **Scene bridge and culling:** (Day 325: the scene tests pass through Metal) ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (the Day 325 run) the ECS scene through `RenderSync` draws the same images as on Linux; the renderer's frustum culling through spatial
      (if built by today) gives the same visible set.
- [x] **`ui_demo` on Metal:** (Day 325: the ctests pass; checked by the user with the trackpad, keyboard, clipboard, a Finder drop and fullscreen, after the Command-key fix) ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (the ctests are set up; the run and the hand checks with the Day 325 run) runs its script; checked by hand with the trackpad, keyboard, clipboard and a file dropped from Finder (recorded).
- [x] **Documentation:** `text.md`, `ui.md` name Metal; `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` with GPU and display tests (quote). **Deferred to Day 325 (the user's choice; the user is asked first).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: partial atlas uploads against in-flight frames, scissor rectangles in pixels at Retina
      scale.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every GPU test on `master` beyond the Phase 4 and 5 suites passes through Metal with validation silent in three flavors, and `ui_demo` runs on
Metal with the hand checks recorded.

## Inventory (2026-11-13, `master` at `143a3b07`)

Every `[gpu]` test outside `tests/rhi/macos/`, by how it picks its device:

- **Once per GPU backend (`backend_fixture.h`, `FOR_EACH_GPU_BACKEND`): 26 files, so through Metal on macOS with no change.**
  - the Phase 4 and 5 suites (Days 322-323);
  - `text_golden_gpu_test`, `ui_golden_gpu_test`, `ui_demo_gpu_test`;
  - the renderer's `material`, `shader_permutation`, `renderer`, `recreatable_renderer`, `apply_settings`, `ui_renderer`, `scene`,
    `text_renderer`, `text_pass`, `ui_surface_target`, `viewport_target` and `device_loss_recovery` tests;
  - the scene bridge: `tests/scene/scene_render_test.cpp` (`RenderSync` drawing ECS entities), the test the "Scene bridge and culling" task
    rests on.
- **Vulkan-only (`gpu_fixture.h`): they skip on macOS.**
  - the `vulkan_*` files;
  - `adapter_test`, `shader_test`, `device_host_test`, `swapchain_reconfigure_test` and `lifecycle_conformance_test`. Their backend-neutral
    cases already run on the null device; Metal's counterparts are `tests/rhi/macos/metal_*`, and `lifecycle_conformance_test`,
    `device_host_test` and `swapchain_reconfigure_test` get their Metal cases on Day 325 (its plan).
- **Direct3D 12 only:** the `dx12_*` files, `adapter_switch_gpu_test` (Day 325: it skips on a Mac with one adapter) and
  `device_resources_test`'s Vulkan case.
- **No spatial culling GPU test exists on `master` yet** (Phase 18's culling is on the CPU).

## Implementation notes

- **`ui_demo` and `rhi_demo` on Metal:**
  - the macOS scripted `ui_demo` run uses `--rhi metal` (on null from Day 314);
  - new named Metal ctests: `metal_ui_demo_screenshot` (with validation; named outside `ui_demo_*` so it carries phase 26), and
    `rhi_demo_device_loss_trace_metal`, `rhi_demo_oom_device_metal` and `rhi_demo_list_gpus_metal`, as D3D12's (phase 26);
  - the scripted demo runs also forbid Metal's "failed assertion" output.
  - every demo ctest has Metal's validation environment (Day 323).
- **Docs:** `docs/guide/text.md` and `ui.md` name Metal (and the Retina pixel rule), as first run on Day 325; `ctest -L docs` for the two
  pages is deferred with the runs (the reference regenerated and `docgen lint` at zero here).
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 325.**

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings, all fixed.

1. **The `ui_demo` Metal screenshot carried phase 15** (the `ui_demo_*` line), so a phase-26 scoped run would leave it out. Fixed: renamed
   `metal_ui_demo_screenshot`, phase 26. The scripted Cocoa run stays phase 15, as it was on null.
2. **`STATUS.md`, `KNOWN-ISSUES.md` and this section were missing.** Added in this commit.
3. **No `KNOWN-ISSUES.md` entry for the day's pending checks.** Added: the `ui_demo` hand checks and the shared files' checks.
4. **The inventory read as a run, and missed the scene-bridge test.** Fixed: worded as an inventory, `scene_render_test` added.
5. **`rhi_demo_list_gpus_metal` passed with zero adapters.** Fixed: it needs a count of one or more.
6. **The guides stated the Metal text and UI paths as checked.** Fixed: worded as first run on Day 325.
7. **`ctest -L docs` was neither run nor deferred.** Deferred to Day 325 with the runs, recorded above.
8. **The scripted runs did not forbid a Metal validation message.** Fixed: "failed assertion" is forbidden too.
9. **A duplicated comment line in `rhi_demo`'s CMake file.** Fixed.
