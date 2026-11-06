# Phase 7 / Day 113 — Demo Seams, Settings File & Cost Measurements

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 112.

Give a human a way to use it, and measure what a reset costs so the Phase 7 notes ("what does the app show meanwhile?")
are answered with numbers.

## Tasks

- [ ] `renderer_demo` and `rhi_demo` build their device through `DeviceHost`/`Renderer::ApplySettings`; command-line options `--gpu <id|index|name>`, `--list-gpus`, `--set key=value` (repeatable, any registered setting), `--settings <file>` (Day 104 format) and `--save-settings <file>`; the existing flags (`--no-vsync`, `--validation`, `--backend`) become shorthand for settings; the usage text lists them; an invalid key or value exits with the existing usage code and names it.
- [ ] Live keys in the windowed demos (printed on startup, logged when pressed): `F1` toggle vsync (Fifo/Immediate), `F2` toggle validation (device reset), `F3` cycle GPU (device reset), `F4` cycle resolution/scale, `F5` fullscreen, `F6` cycle swapchain format, `F7` cycle shader quality (pipeline rebuild), `F9` force a device reset with unchanged settings, `F10` inject device loss. Keys are disabled in `--screenshot` mode. Each key logs the cost class applied and the time it took.
- [ ] While a reset runs the demo shows what the Day 110 decision says (window keeps its last picture; a text line in the log); document and verify there is no flash of garbage on a swapchain recreation (screenshot test: the first frame after is a clear, not garbage).
- [ ] Automated seam test (no human): `--script "frame:10 set:present_mode=immediate frame:20 reset frame:30 gpu:next frame:40"` style option used by ctests so the live paths run in CI without real key presses; ctests per demo, GPU label, on both display backends (resource group `display`), validation on and silent.
- [ ] **Measurements**, recorded in this file: time per cost class on `renderer_demo`'s default scene (540 objects, 300 lights) from the apply call to the first good frame — `Swapchain`, `PipelineRebuild` (lazy and `Prewarm`), `DeviceReset` cold cache, warm cache — in median and worst of 20; the split of a device reset by stage (observers releasing, device destroy, device create, swapchain, texture re-upload, pipeline build, first frame); the same at 10000 objects and 1000 textures-worth of upload if a scene exists; memory and fd drift over 100 resets in the real demo; decide and record: eager vs lazy pipeline rebuild default, and whether a reset needs a progress display.
- [ ] `docs/guide/getting-started.md` and the demo READMEs show the options and keys; `renderer_demo/README.md` explains the reset keys.
- [ ] **Unverified-by-human entry** in `KNOWN-ISSUES.md`: the live key paths (F1-F10) and the visual quality of a reset were exercised by script, not with real keys and eyes, unless the user does it; list exactly what to look at.
- [ ] Run `scripts/test-phase.sh --affected`, then the demos for 1000 frames on Wayland and X11 with `--validation`, silent; quote the lines (a locked session breaks window tests).
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** every registered setting can be set from the command line, a file or a key in both demos, the scripted seam tests run the live paths in CI, the cost table is recorded with numbers, and the lazy-versus-eager and progress-display questions have recorded answers.
