# Phase 26 / Day 282 — RHI, Renderer and Demos on the Null Backend

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 281.

There is no Vulkan on macOS (decision 8), so the neutral RHI, the renderer, the scene bridge, `ui_window` and the demos are brought up on the null
backend first: everything above the GPU is proven on macOS before the Metal backend exists.

## Tasks

- [ ] **CMake:** `engine/rhi` (neutral), `rhi/null`, `rhi_backends` (registering null only on macOS until Day 284), `renderer`, `renderer_passes`,
      `scene`, `ui_window` and `game/*` configure on macOS; `rhi/vulkan` and `rhi/dx12` are not built there (a configure message says why); the
      Vulkan-only tests are not configured on macOS, listed in the spec.
- [ ] **Backend choice:** `renderer.backend` / `--rhi` accept `metal` (refused with "not built yet" until Day 284) and `auto` resolves to Metal on macOS,
      falling back to null with a logged reason while Metal is missing (removed on Day 284); `--rhi=null` works everywhere.
- [ ] **Tests:** the null-device, handle-pool, render-graph, material, reset (`reset_rig.h` on null), apply-settings and device-resources suites, the scene
      bridge and `ui_window` tests pass on macOS; GPU-tagged tests skip with "no GPU backend" until Day 290.
- [ ] **Demos on null:** `rhi_demo`, `renderer_demo`, `ui_demo` start with `--rhi=null` and a Cocoa window, run their scripts (`--script`) and exit 0;
      `renderer_demo_crash_after_reset` and the `*_list_gpus` / `*_unknown_gpu` demo ctests pass on null.
- [ ] **Pipeline cache and settings files** of the demos land in the macOS standard directories (Day 272) and the settings store (Day 277): checked once.
- [ ] **Documentation:** `docs/guide/rhi.md` and `renderer.md` say which backends exist per platform; `docgen lint` zero; reference regenerated if any
      header changed.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote); shared-file runs filed (the backend-choice change is
      shared).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the `auto` fallback never hiding a real Metal failure once Metal exists (a test that
      `auto` fails loudly, not silently, when a built backend fails).
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every module above the platform builds on macOS, the RHI, renderer, scene and UI-window suites pass on the null backend in three flavors, and
the three demos run their scripts on macOS with `--rhi=null`.
