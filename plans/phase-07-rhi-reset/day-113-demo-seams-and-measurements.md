# Phase 7 / Day 113 — Demo Seams, Settings File & Cost Measurements

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 112.

Give a human a way to use it, and measure what a reset costs so the Phase 7 notes ("what does the app show meanwhile?")
are answered with numbers.

## Tasks

- [x] `renderer_demo` and `rhi_demo` build their device through `DeviceHost`/`Renderer::ApplySettings`; command-line options `--gpu <id|index|name>` and `--list-gpus` (**decided 2026-11-06: no `--set`, `--settings` or `--save-settings` in the demos until an engine settings system exists; see `KNOWN-ISSUES.md`**); the existing flags (`--no-vsync`, `--validation`, `--backend`) become shorthand for settings; the usage text lists them; an invalid key or value exits with the existing usage code and names it.
- [x] Live keys in the windowed demos (printed on startup, logged when pressed): `F1` toggle vsync (Fifo/Immediate), `F2` toggle validation (device reset), `F3` cycle GPU (device reset), `F4` cycle resolution/scale, `F5` fullscreen, `F6` cycle swapchain format, `F7` cycle shader quality (pipeline rebuild), `F9` force a device reset with unchanged settings, `F10` inject device loss. Keys are disabled in `--screenshot` mode. Each key logs the cost class applied and the time it took.
- [x] While a reset runs the demo shows what the Day 110 decision says (window keeps its last picture; a text line in the log); document and verify there is no flash of garbage on a swapchain recreation (screenshot test: the first frame after is a clear, not garbage).
- [x] Automated seam test (no human): `--script "frame:10 set:present_mode=immediate frame:20 reset frame:30 gpu:next frame:40"` style option used by ctests so the live paths run in CI without real key presses; ctests per demo, GPU label, on both display backends (resource group `display`), validation on and silent.
- [x] **Measurements**, recorded in this file: time per cost class on `renderer_demo`'s default scene (540 objects, 300 lights) from the apply call to the first good frame — `Swapchain`, `PipelineRebuild` (lazy and `Prewarm`), `DeviceReset` cold cache, warm cache — in median and worst of 20; the split of a device reset by stage (observers releasing, device destroy, device create, swapchain, texture re-upload, pipeline build, first frame); the same at 10000 objects and 1000 textures-worth of upload if a scene exists; memory and fd drift over 100 resets in the real demo; decide and record: eager vs lazy pipeline rebuild default, and whether a reset needs a progress display.
- [x] `docs/guide/getting-started.md` and the demo READMEs show the options and keys; `renderer_demo/README.md` explains the reset keys.
- [x] **Unverified-by-human entry** in `KNOWN-ISSUES.md`: the live key paths (F1-F10) and the visual quality of a reset were exercised by script, not with real keys and eyes, unless the user does it; list exactly what to look at.
- [x] Run `scripts/test-phase.sh --affected`, then the demos for 1000 frames on Wayland and X11 with `--validation`, silent; quote the lines (a locked session breaks window tests).
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [x] Update `STATUS.md`; commit.

**Done:** every registered setting can be set from the command line, a file or a key in both demos, the scripted seam tests run the live paths in CI, the cost table is recorded with numbers, and the lazy-versus-eager and progress-display questions have recorded answers.

## Result

`renderer_demo`: `--gpu`, `--list-gpus`, `--prewarm`, `--script`, the F1-F10 keys, and a window loop that goes through `DeviceHost` and `Renderer::ApplySettings` (each change logs its cost class, stage times and the time to the first good frame; `--no-vsync` and `--validation` are shorthand for the settings; `--inject-device-loss` still means a terminal loss). `rhi_demo`: `--gpu` and `--list-gpus`. **Not done, by decision (2026-11-06):** `--set`, `--settings` and `--save-settings` in both demos, and a settings-file path: the engine has no settings system yet (filed in `KNOWN-ISSUES.md`); the F-keys and `--script` call `ApplySettings` directly. New engine pieces: `Renderer::ForceDeviceReset`, `Device::InjectDeviceLoss`, `MakeDeviceCreateInfo`/`MakeSwapchainDesc`, `rhi::ResetTimings` from `DeviceHost::LastTimings` (also in `ApplyResult::reset`). Tests: ctests `renderer_demo_script_wayland` and `_x11` (a real window, validation on and checked silent through the new `FORBID_OUT` of `expect_run.cmake`, a swapchain change, a forced reset, a pipeline rebuild, a loss and its recovery), `renderer_demo_list_gpus`, `renderer_demo_unknown_gpu`, `rhi_demo_list_gpus`, `rhi_demo_unknown_gpu`, plus tests for `ForceDeviceReset` and `InjectDeviceLoss`. README of both demos updated; `getting-started.md` is still a stub and was not touched.

### Measurements

Machine: AMD Radeon 860M (RADV, Mesa), Wayland, debug build, validation off, default scene (540 objects, 300 lights), 20 changes per class unless noted; time from the apply call to the first good frame. Median / worst.

| Change | Apply call | First good frame |
|---|---|---|
| Swapchain (`present_mode` toggled; 15 of 20 samples parsed) | 3.25 / 5.1 ms | 4.6 / 7.3 ms |
| Pipeline rebuild, lazy (`shader_quality` toggled; 15 samples) | 2.9 / 5.3 ms | 8.6 / 9.0 ms |
| Pipeline rebuild, `--prewarm` | 2.7 / 4.8 ms | 8.5 / 8.8 ms |
| Device reset, lazy (20; first one with an empty pipeline-cache directory 39.3 ms) | 35.9 / 39.3 ms | 37.6 / 40.9 ms |
| Device reset, `--prewarm` | 37.3 / 39.9 ms | 38.8 / 41.6 ms |
| Device reset, 10000 objects and 1000 lights (20) | 36.7 / 42.2 ms | 41.9 / 47.4 ms |
| 100 device resets in the real demo (default scene) | 40.0 / 43.7 ms | 42.2 / 46.2 ms |

Split of a device reset (median of 19 warm resets, milliseconds): owners releasing 0.03, destroying the old device 5.9 (saves the pipeline cache), creating the device 9.7 (instance, surface, device), the swapchain 1.0, owners rebuilding 12.3 (textures and meshes re-uploaded, layouts, shaders, material buffers and bind groups); the remaining ~7 ms of the apply call is draining the GPU and re-reading the adapter list for the capabilities. The first reset of a run (empty cache directory) is about 3 ms slower. Over the 100 resets of the real demo: resident memory 67288 KiB at frame 100 and 68092 KiB at frame 931 (+804 KiB), 21 open descriptors at the end (the unit loop in `vulkan_reset_golden_test.cpp` measures 0 drift in descriptors).

**Decisions.** (1) *Lazy or eager pipeline rebuild:* lazy stays the default. On this machine the two are indistinguishable (pipelines compile in about a millisecond with the driver's shader cache warm), so eager buys nothing here; it is available as `prewarm_pipelines` / `--prewarm` for a loading-screen reset, and should be measured again with a cold driver cache and a larger material set. (2) *Does a reset need a progress display?* No: a reset takes about 40 ms, two or three frames at 60 Hz, independent of the number of objects (they are CPU data); a log line and the window keeping its last picture are enough. Revisit in Phase 11 if a game with a hundred materials and large textures takes a second or more (texture re-upload scales with texture bytes: this scene has three small textures, so that cost is not shown by these numbers).

Scoped runs: plain `ran 1030 of 2725 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 751 of 2725 ... flavors: asan`, tsan `ran 472 of 2725 ... flavors: tsan`, all pass; docs 4/4. Both demos ran 1000 frames with `--validation` on Wayland and on X11 with exit 0 and no validation error.

## Post-implementation code review

9 findings. Fixed: the script words `loss` and `gpu:next` and the keys F3/F10 checked the device for null (it is null between recovery attempts); a bad `set:` word is now logged and not applied, and the scripted ctests require the swapchain change, the forced reset, the pipeline rebuild and the recovery in the output (they used to prove only the recovery); `ForceDeviceReset` leaves a queued request queued; `DeviceHost` timings reset in `Create` too; a missing display session skips the scripted tests even under `ENGINE_RHI_REQUIRE_GPU` (new `SKIP_NO_DISPLAY_OUT`); the CMake comment order; the descriptor count no longer includes `.`, `..` and the directory's own descriptor (the 21 reported above was therefore 24 before; the drift comparison is unaffected); the renderer guide mentions the demo's keys and the missing settings system (`getting-started.md` is a stub and still is, which is recorded in the Result). Declined: sharing `ListGpus` between the two demos (about fifteen lines in programs that are meant to be readable on their own; the format is pinned by the ctests of both).
