# Phase 26 / Day 292 — Text, UI and the Scene Bridge on Metal

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 291.

What Phases 14, 15 and 17 put on the GPU: the glyph atlas and text pass, `UiRenderer`, UI surfaces rendered to textures, the viewport widget, and the
scene bridge drawing ECS entities, all through Metal; plus any GPU path a main-line phase added before today (the spatial culling of Phase 18 Day 247).

## Tasks

- [ ] **Inventory at the start:** every GPU test on `master` outside the Phase 4 and 5 suites (`text_golden_gpu_test`, `ui_golden_gpu_test`,
      `ui_demo_gpu_test`, scene and culling GPU tests, anything newer), each run with a Metal factory.
- [ ] **Text:** the atlas texture (`R8` pages, partial uploads), the text pass, the large-text stand-in; the text golden images within tolerance;
      Retina scale (text rasterized at the backing scale).
- [ ] **UI:** `UiRenderer`'s draw list (scissors, nine-slice, images by `ImageId`), the viewport widget hosting a rendered world, UI surfaces to textures
      sampled in the 3D scene; `ui_golden_gpu_test` and `ui_demo_gpu_test` pass.
- [ ] **Scene bridge and culling:** the ECS scene through `RenderSync` draws the same images as on Linux; the renderer's frustum culling through spatial
      (if built by today) gives the same visible set.
- [ ] **`ui_demo` on Metal:** runs its script; checked by hand with the trackpad, keyboard, clipboard and a file dropped from Finder (recorded).
- [ ] **Documentation:** `text.md`, `ui.md` name Metal; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU and display tests (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: partial atlas uploads against in-flight frames, scissor rectangles in pixels at Retina
      scale.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every GPU test on `master` beyond the Phase 4 and 5 suites passes through Metal with validation silent in three flavors, and `ui_demo` runs on
Metal with the hand checks recorded.
