# Phase 21 / Day 309 — Metal Lifecycle, Device Loss and Resets

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 308.

`engine/rhi/BACKEND-LIFECYCLE.md` on Metal: the fifteen functions, the lifecycle conformance suite, device loss (external-GPU removal and injected), and
the reset loops of Phase 8. Modelled on DX12 Day 172.

## Tasks

- [x] **The fifteen functions:** `CreateDevice`, `EnumerateAdapters`, `Adapter`, `Epoch`, `Resources`, `InFrame`, `Quiesce`, `WaitIdle`, `DeviceLost`,
      `InjectDeviceLoss`, `RecreateSurface`, `Reconfigure`, `ActualPresentMode`, `GetColorSpace`, `Capabilities`, each checked against its section and
      marked "Metal, as built (Day 309)".
- [x] **Device loss:** `MTLRegisterDevicesObserver` (or `MTLCopyAllDevicesWithObserver`) for `MTLDeviceRemovalRequestedNotification` and
      `WasRemoved` feeding `DeviceLost`; command-buffer errors that mean the device is gone (`MTLCommandBufferErrorDeviceRemoved`) likewise;
      `InjectDeviceLoss` makes every later submission fail as a real loss would; destruction after loss never waits forever.
- [x] **Conformance:** a Metal factory in `tests/rhi/lifecycle_conformance_test.cpp`; the whole suite passes.
- [x] **Resets:** (`metal_lifecycle_test`; `device_host_test` and `swapchain_reconfigure_test` stay Vulkan-only, their Metal counterparts are `metal_lifecycle_test` and `metal_swapchain_test`; `device_loss_recovery` and `reset_golden_gpu_test` run per backend; `reset_soak` is on the null backend everywhere; the reset time is printed by the test, recorded with the run) the 100-reset loops (counts, memory, pipeline archive) and the pixel-identical-after-reset test on Metal; `device_host_test`,
      `swapchain_reconfigure_test`, `device_loss_recovery`, `reset_soak` with Metal; measure one device reset (Linux: about 40 ms on its GPU).
- [x] **GPU switch:** (`adapter_switch_gpu_test` is Direct3D 12-only and skips here; the switch logic is covered by the null backend, as on Linux) with one adapter there is nothing to switch to on this Mac: `adapter_switch_gpu_test` skips with that reason; the switch logic is
      covered by the null backend as on Linux (recorded).
- [x] **Validation settings:** `validation`/`sync_validation` documented as advisory on Metal; changing them reports "takes effect at next start".
- [x] **Documentation:** `BACKEND-LIFECYCLE.md` Metal notes; `rhi.md` and `renderer.md` ("Device loss and recovery") Metal notes; `rhi_lifecycle_lists`
      passes; `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` (below, "The run of Days 299-309"). Originally: `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote). **Waiting for the user: Days 299-309's runs, after they say go.**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the device observer's lifetime and thread, tolerant destruction after loss, ARC objects
      surviving a reset that should not.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Metal backend passes the lifecycle conformance suite and the reset loops with unchanged counts and pixel-identical output, recovers from an
injected loss, the reset time is recorded, and the lifecycle checklist is marked as built for Metal.

## Plan amendments (made during the day)

- **`Auto` picks Metal on macOS from today** (rank 30), and the Day 298 fallback (the setting's `auto` as the null backend on macOS) is gone:
  `renderer::BackendForSetting` is the setting's value on every platform. The suites run against this state in the waiting run.
- **"Takes effect at next start" for the validation settings** is a log line from Metal's device creation (which a reset repeats), in both
  directions, not a restart class in the renderer's settings diff: that would teach the diff about backends for one case.

## Implementation notes

- **Device loss:** `MTLCopyAllDevicesWithObserver` per device. `MTLDeviceWasRemovedNotification` and `MTLDeviceRemovalRequestedNotification`
  for this device's registry ID mark it lost through the shared state, never the device. A GPU whose removal was requested is left out of the
  next adapter list (`NoteMetalRemovalRequested`), so the reset releases it. The observer is removed in the destructor; a live-observer count
  is kept for the tests.
- **Conformance:** a Metal case in `tests/rhi/lifecycle_conformance_test.cpp`, mirroring D3D12's.
- **Tests added** (waiting for the run): `tests/rhi/macos/metal_lifecycle_test.mm` covers:
  - 100 resets through `DeviceHost` with an owner of a buffer and a compute pipeline, the cache on: counts, memory and observers unchanged,
    and every pipeline from the archive the last device wrote; the reset time is printed;
  - a stale handle on the next device;
  - a loss after real work: a recorded list refused, no fence, waits answered at once, destruction not waiting.
- **Validation settings:** a request that differs from the process is logged as taking effect at the next start; `sync_validation` has no
  Metal counterpart (logged).
- **Docs:** `engine/rhi/BACKEND-LIFECYCLE.md` ("Metal, as built (Day 309)" for every function), `docs/guide/rhi.md` (loss, `Auto`),
  `docs/guide/renderer.md` (device loss on Metal, the setting), `renderer.h` (`BackendForSetting`); the reference regenerated, `docgen lint`
  at zero.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: waiting for the user.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, 9 fixed, 1 declined.

1. **A GPU whose removal was requested was picked again by the reset.** Fixed: it is left out of later adapter lists.
2. **The demos' `auto` tests failed instead of skipping on a Mac without Metal 3.** Fixed: Metal's "no device" reasons are in the skip patterns.
3. **The loss test could not fail on `Submit`.** Fixed: real work first, a recorded list refused, the fence and the wait checked.
4. **The device observer had no test.** Fixed in part: a live-observer count, checked across the 100 resets (the removal itself cannot be
   made to happen here).
5. **The reset loop had the pipeline cache off.** Fixed: on, with a compute pipeline per device, each one found in the archive.
6. **The observer comment promised more than Apple documents.** Fixed: it says why the handler holds only shared state.
7. **`BackendForSetting`'s doc said it read the registry, and callers kept stale comments.** Fixed.
8. **The Day 298 cache entry and a resolved Day 300 note in `KNOWN-ISSUES.md`.** Fixed.
9. **Each device creation enumerates the GPUs twice.** Declined: one more enumeration is small next to device creation, and merging the list
   with the observer's would tie the adapter listing to a device's lifetime.
10. **Day file and `STATUS.md` not updated.** Done in this commit.

## The run of Days 299-309

Run on 2026-11-13 after the user said go, with `ENGINE_RHI_REQUIRE_GPU=1` and the display tests, the user at the Mac. The first plain run failed
62 of 2376 tests; each failure was traced and fixed below, then the three flavors were run again until only the known failures were left.

**Final run:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s`. The failures left are known: `Every minted DXIL module is a valid container minted beside the committed SPIR-V`
(plain, asan and tsan; the DXIL of `fill.comp`, `saxpy.comp` and now `material_test.frag` is stale until the PC re-mints it,
`KNOWN-ISSUES.md`) and the known flaky `Throughput scales with participant count` (phase 2, plain; failed in three of the four three-flavor runs, passed 3 of 3 reruns each time). Both unity modes build clean (`build-macos`, `build-macos-nounity`, every flavor); `docs/reference/`
regenerated, `docgen lint` at zero, `ctest -L docs` 6 of 6.

### What the run found, and the fixes

1. **A nil sampler label is fatal under Metal's validation** (`-[MTLSamplerDescriptorInternal setLabel:]`): most of the 62 failures. Set only
   when there is a name.
2. **A vertex layout no attribute reads** made the pipeline archive's serialized descriptor fail. Only bindings an attribute uses get a layout.
3. **Metal stops inside a binary archive while shader validation is on** (an unbalanced `dispatch_group_leave` in its compiler's variant
   cache, macOS 15.8.1): the device leaves the archive off while `MTL_SHADER_VALIDATION` is set (logged), and the archive tests (tag
   `[archive]`) run in a ctest discovery of their own with the API validation only. A test checks the archive is off under shader validation.
4. **Autoreleased Metal objects were never drained** when the caller had no pool (a job thread, a C++ test): each kept its device alive, about
   37 MiB over 100 resets. Pools around device creation, `Submit`, the retired-resource collection, the readbacks and copies, draws and
   dispatches, list recycling, and the lists' destruction; `ShaderParts`' out-parameter is `__strong`. New ctest `metal_autorelease_pools`:
   the reset loop with `OBJC_DEBUG_MISSING_POOLS=YES`, failing on any Metal or driver object autoreleased without a pool on the test's thread
   (Metal's own completion threads release what it held and are not counted). The resident drift is now 1.4 to 3.4 MiB without validation.
   With Metal's validation layer in assert mode it is 8 to 13 MiB, inside that layer's completion handlers on Metal's thread, so the reset
   loop's memory check is skipped when `MTL_DEBUG_LAYER` is set (warned) and kept in `metal_autorelease_pools`.
5. **A ThreadSanitizer report on every Metal GPU test:** the block given to `notifyListener:` captured the wait's semaphore, copied on the
   waiting thread and read on Metal's notification thread through IOSurface, a hand-off TSan cannot see. The block now captures nothing and
   finds the waits in a table under a mutex.
6. **`material_test.frag` read its parameters 4 bytes off on Metal:** a `float` after a `float3` is at 28 in std140 and 32 in Metal's layout.
   The block is reordered (both layouts agree; re-minted, a second mint byte-identical) and `shader_mint` now refuses a block field whose
   Metal offset differs from the SPIR-V one (test, which fails without the check). Only that shader had it. `shaders/README.md` has the rule.
7. **The pipeline cache was never written to the user's cache directory:** Metal serializes into a file beside the cache file, before the
   directory exists. The directory is created first (test, which fails without the fix).
8. **Metal refused to re-serialize an archive read from its file with the window's pipelines added** ("missing 'vertex' stage in pipeline
   no. 8"; found in the hand run, reproduced by an offscreen run then a windowed one). The cache keeps the descriptions of the pipelines the
   device made (hits and accepted misses, equal ones once) and writes a fresh archive of them when that happens; if that fails too the device
   stops saving, logged once. Test through a hook that refuses the first serialization.
9. **The command shortcuts used Control on macOS** (found by the user: Ctrl+C copied, Cmd+C did nothing). `os::kModShortcut` (Command on
   macOS, Control elsewhere) for select all, copy, cut, paste, undo and redo in the text field, select all and toggle-click in the list box,
   and the demo's Quit; accelerators match Super; a menu or dialog does not take Cmd+letter as a mnemonic; menus label Command "Cmd+". The
   `ui_demo` script word `shortcut+`. Tests: the other platform's modifier copies nothing; Cmd+A in an open menu chooses nothing.
10. **Tests that were wrong:** a sampler anisotropy of 64 (the description refuses more than 16), a "truncated" Metal library shorter than its
    header, the hand-built material modules without their Metal library, `ApplyMetalSlots` missing a push block reported with two bindings,
    and the encoder-count test's loose barrier count (exactly 4, measured).
11. **The planted validation test** aborts as designed, which ctest counts as a failure whatever the output: it runs through a driver
    script that requires the failed assertion and an abnormal exit.
12. **Gates:** `rhi_boundary_check` missed `@import Metal;` (a line's `;` reads as `<SEMI>`), `std_usage_check` caught two raw integer
    spellings in the timeline wait.
13. **Demos asking for `--rhi vulkan` on macOS** failed under `ENGINE_RHI_REQUIRE_GPU=1`: a test that names a backend its platform cannot have
    is skipped (only when it asked for it by name).
14. The demos' usage lines list `cocoa`.

Day 304's open question is answered: Metal accepts a 16-bit index buffer offset that is not a multiple of 4 (a draw from index 1, with API
validation; new test). Day 298's: `auto` writes its cache to `~/Library/Caches/engine/metal-<adapter id>.bin`.

### Measurements (Intel Iris Plus 645, macOS 15.8.1)

- **The device:** unified memory, low power, not removable; working set 1.5 GiB; argument buffers tier 1, read-write textures tier 1.
- **Golden images** (worst channel difference, interior and edge pixels over tolerance): triangle 1, 0, 0 of 308; cube 1, 0, 0 of 569;
  textured cube 35, 13, 0 of 2741; lit cube 35, 12, 0 of 2652; the demo scene (and before and after a reset) 93, 1, 1 of 33435; the UI
  goldens 8 to 48, at most 3 interior; the text overlay and UI scene 1. All within their tolerances; none changed.
- **A device reset:** 100 resets, mean 43 ms, worst 151 ms, without validation (Linux: about 40 ms). In `renderer_demo` with validation,
  150 to 360 ms per reset from the keys.
- **Validation's cost:** the rhi GPU suite 16 s with validation, 15 s without (the validation run skips the archive tests).
- **`rhi_bench` (Release):** 0.64 to 0.89 us per draw (1000 and 10000 cubes), barriers 0.02 to 0.05 us per call (recorded, no work on
  Metal), uploads 3.8 GiB/s (1 MiB) and 6.6 GiB/s (64 MiB) through the staging ring, the cube pipeline 1.8 to 1.9 ms with or without the
  cache. The pools added per draw cost nothing measurable (0.69 to 0.89 us before them).
- **`renderer_bench` (Release, 1280x720, 300 lights):** a frame of 1000 objects 1.4 to 1.5 ms, 10000 about 7.1 ms, 50000 about 25 ms;
  16000 lights 13.9 ms; the graph 0.07 to 0.09 us per pass.
- **`renderer_demo` start:** 0.89 s with an empty cache, 0.37 to 0.39 s once its archive (539 KiB) is written.
- **The screen's EDR:** potential 2.00, now 1.00.

### Hand checks (the user at the Mac)

- `renderer_demo --rhi metal`: F1 to F10 work and the picture is right after each (F3 has one GPU to pick; F10's loss recovers on the first
  attempt). **F11 is taken by macOS** (Show Desktop); the overlay toggle is covered by `renderer_demo_overlay_cocoa`.
- `ui_demo --rhi metal`: the trackpad, typing, the clipboard with TextEdit both ways (after fix 9), a file dropped from Finder, native
  fullscreen in and out: all right.
- Not checked: a move between screens of different scale (one display), `KNOWN-ISSUES.md`.

### Code review of the run's fixes

`/code-review high` on the run's changes: 10 findings, 8 fixed, 1 in part, 1 declined.

1. **A pipeline the archive refused was still kept for the rebuild**, which would then fail. Fixed: kept only after a hit or an accepted miss.
2. **The kept descriptions grow and hold their shaders.** Fixed in part: equal descriptions are kept once; they live as long as the device,
   which a reset ends.
3. **The retry serialized over the refused attempt's file.** Fixed: removed first.
4. **Cmd+letter in an open menu or dialog was a mnemonic.** Fixed (Super in the masks), with a test.
5. **Any "not built for" output skipped a demo test**, which would hide `auto` choosing Vulkan. Fixed: only when the test asked for that
   backend by name.
6. **The pool test's class list missed other GPUs' driver classes.** Fixed: AGX, AMD, IOGPU, IOAccel added.
7. **Cmd+click did not toggle a list row; Cmd+Y does not redo.** Cmd+click fixed. Cmd+Y declined: the macOS redo is Cmd+Shift+Z, which works;
   Ctrl+Y is the Windows convention and stays.
8. **A pool per draw instead of one per thread.** Declined: measured, the per-draw cost did not change (above); a pool per job and thread
   would be the cleaner shape and is filed in `KNOWN-ISSUES.md`.
9. **A helper between `ApplyMetalSlotsImpl` and its comment.** Fixed.
10. **Stale notes** (the `config_access_check` reason for `MTL_SHADER_VALIDATION`, `ui_script.h`, `KeyChord`'s docs). Fixed.
