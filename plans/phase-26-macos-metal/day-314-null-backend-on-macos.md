# Phase 26 / Day 314 — RHI, Renderer and Demos on the Null Backend

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 313.

There is no Vulkan on macOS (decision 8), so the neutral RHI, the renderer, the scene bridge, `ui_window` and the demos are brought up on the null
backend first: everything above the GPU is proven on macOS before the Metal backend exists.

## Tasks

- [x] **CMake:** `engine/rhi` (neutral), `rhi/null`, `rhi_backends` (registering null only on macOS until Day 316), `renderer`, `renderer_passes`,
      `scene`, `ui_window` and `game/*` configure on macOS; `rhi/vulkan` and `rhi/dx12` are not built there (a configure message says why); the
      Vulkan-only tests are not configured on macOS, listed in the spec.
- [x] **Backend choice:** `renderer.backend` / `--rhi` accept `metal` (refused with "not built yet" until Day 316) and `auto` resolves to Metal on macOS,
      falling back to null with a logged reason while Metal is missing (removed on Day 316); `--rhi=null` works everywhere. (Amended: the fallback is the
      *setting's* (`renderer::BackendForSetting`, used by the renderer and the demos), not `rhi::PickAutoBackend`'s. Tried first in the RHI, it gave a
      "real device" test that asks for `auto` a null device to check GPU behaviour on; the RHI's `auto` stays a GPU or a failure.)
- [x] **Tests:** the null-device, handle-pool, render-graph, material, reset (`reset_rig.h` on null), apply-settings and device-resources suites, the scene
      bridge and `ui_window` tests pass on macOS; GPU-tagged tests skip with "no GPU backend" until Day 322.
- [x] **Demos on null:** `rhi_demo`, `renderer_demo`, `ui_demo` start with `--rhi=null` and a Cocoa window, run their scripts (`--script`) and exit 0;
      `renderer_demo_crash_after_reset` and the `*_list_gpus` / `*_unknown_gpu` demo ctests pass on null.
- [x] **Pipeline cache and settings files** of the demos land in the macOS standard directories (Day 304) and the settings store (Day 309): checked once.
- [x] **Documentation:** `docs/guide/rhi.md` and `renderer.md` say which backends exist per platform; `docgen lint` zero; reference regenerated if any
      header changed.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote); shared-file runs filed (the backend-choice change is
      shared).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the `auto` fallback never hiding a real Metal failure once Metal exists (a test that
      `auto` fails loudly, not silently, when a built backend fails).
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every module above the platform builds on macOS, the RHI, renderer, scene and UI-window suites pass on the null backend in three flavors, and
the three demos run their scripts on macOS with `--rhi=null`.

## As built (2026-11-12, the Mac)

- **CMake:** the macOS block adds `engine/rhi` (the neutral RHI and the null backend; `rhi_vulkan` is not built, with a configure message), `engine/renderer`,
  `engine/scene`, `engine/ui_window` (before `tests/ui`, which then builds its window-bound tests: 45 cases, all pass), `tests/rhi`, `tests/renderer`,
  `tests/scene` and the whole of `game/`. `rhi_backends` registers null alone on macOS and exports `ENGINE_RHI_HAVE_VULKAN=1` where it links Vulkan, so
  the shared tests keep their Vulkan cases behind that define (the list is in the port spec, section 8.8).
- **Backend choice:** `rhi::Backend::Metal` (before `Auto`, whose value moves from 3 to 4), the setting's names and the `--rhi` parser gain `metal`;
  an unregistered `metal` is refused as "not built yet (Phase 26 Day 316)". On macOS the setting's `auto` is null when no GPU backend is registered,
  with one log line; a registered GPU backend that fails is never replaced (test: a registered, unavailable fake keeps `auto`, and the device
  creation fails with its reason). The renderer's reset pre-check goes through the same function (found: a forced reset asked for `auto` raw).
- **Demos:** `rhi_demo`, `renderer_demo` and `ui_demo` build on macOS. New ctests on macOS: `renderer_demo_script_cocoa`, `renderer_demo_overlay_cocoa`,
  `ui_demo_script_cocoa` (a Cocoa window on null: a swapchain change, a forced reset, a pipeline rebuild, a device loss and its recovery, as on
  Linux); `renderer_demo_crash_after_reset` runs on Cocoa (the macOS exit after the crash handler is 5, Breakpad's `_exit(EXC_SOFTWARE)`). The
  `*_list_gpus` and `*_unknown_gpu` tests pass on null; the GPU-only ones skip; `*_no_vulkan` is not registered. The demos' goldens and READMEs
  changed with the setting's description and pass here.
- **Settings and cache:** `--save-settings` wrote `~/Library/Application Support/renderer_demo/settings.cfg` (a scratch `HOME`); the null backend
  writes no pipeline cache, so that waits for Metal.
- **Found by the build:** a test relied on the unity build for `rhi/rhi.h` (the non-unity build caught it).
- **Runs (macOS):** `scripts/test-phase.sh --affected --flavor all --display`: `ran 5834 of 6214 tests (phases: all; flavors: all; display:
  included; crash tests: excluded) in 1456 s`. Failures: the flaky `Throughput scales with participant count`, and the registry case in the asan and
  tsan builds, whose binaries had not been rebuilt after a last edit (rebuilt: 7 of 7 registry cases in all three flavors). Skipped window tests:
  only the UI surface-target GPU case (no GPU backend). Before that, by suite: `rhi_tests` 131 passed and 43 skipped, `renderer_tests` 239 and 33,
  `scene_tests` 19 and 1, `ui_tests` 339 of 339 (the 45 window-bound cases among them), the demo ctests 68 of 68. Both unity modes build clean;
  `ctest -L docs` 6 of 6; the gates 71 of 71.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings; 8 fixed, 1 in part.

1. **The demos' tests used the developer's own profile on macOS** (`DEMO_PROFILE_ENV` set only `XDG_*`, which macOS ignores): a user
   `settings.cfg` in `~/Library/Application Support` would change their results, and the crash test left dumps there. Fixed: a private `HOME`.
2. **`*_settings_flag_beats_user_file` passed without reading a user file on macOS** (written under `xdg/`, read from
   `$HOME/Library/Application Support`). Fixed: written where macOS reads it; checked by hand that the demo reads it (`from user file (line 2)`).
3. **The "not built yet" reason named Metal only**: `--rhi metal` on Linux promised Day 316, `--rhi vulkan` on macOS gave registration advice.
   Fixed: an unregistered backend this platform does not build is refused as "not built for <platform>" (Metal on macOS: "not built yet");
   tests for Vulkan on macOS and D3D12 off Windows.
4. **`ENGINE_RHI_HAVE_VULKAN` was defined by `rhi_backends`, not by `rhi_vulkan`**, so a target linking Vulkan directly would lose its Vulkan cases
   silently. Fixed: `rhi_vulkan` defines it (PUBLIC).
5. **A plain `static bool` guarded the fallback's log line**, a data race between renderers on two threads. Fixed: `Atomic<bool>`, inside
   `BackendForSetting`.
6. **The fallback note was printed twice** (the demos' `FillDeviceCreateInfo` and the renderer). Fixed: `BackendForSetting` logs it once; the demo
   prints nothing of its own.
7. **`BackendSettingIndex` repeated the enum's order by hand.** Fixed: `static_cast<u32>(backend)` (the order is a `static_assert` in
   `render_settings.cpp`).
8. **The benchmarks resolve `auto` without the fallback and their usage did not list `metal`.** In part: the usage lists `metal`; the
   benchmarks keep failing on macOS until Metal on purpose (a benchmark must not measure the null device unasked), said in `rhi_bench`'s comment.
9. **`MakeDeviceCreateInfo`'s documentation did not mention the fallback.** Fixed; reference regenerated.
