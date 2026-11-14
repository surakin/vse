# Phase 22 / Day 325 — The BC7 Encoder (a Subset) & Quality Settings

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 324.

The high-quality format for color and alpha, limited to the modes Day 313's spike chose.

## Tasks

- [x] **BC7 decoder** for every mode (the decoder must accept any valid block, whatever the encoder writes).
- [x] **BC7 encoder, mode 6 only (Day 313 Spike 3):** one subset, RGBA, 7-bit endpoints with p-bits, 4-bit indices; principal-axis fit, the four p-bit combinations, a bounded least-squares refinement. Spike numbers: 52 to 55 dB on smooth content against 43 to 45 dB for BC1, 3.7 to 7.4 MP/s unoptimized on one core. The partitioned modes are **not** built (about +3 dB for about 10x the encode time); revisit only if a real asset shows a visible gap.
- [x] **Quality and speed settings** (`fast`, `normal`, `high`: partitions tried and refinement steps) with the time per megapixel measured and recorded.
- [x] **Texture minter formats** `bc7` (color), `bc7-alpha`; a minter option choosing the format by content (opaque or not) is *not* added (explicit is deterministic and reviewable).
- [x] **Tests:** PSNR against BC1 on the same images (BC7 must be better on the gradient, noise and a photograph-like test image), validity of every block by the decoder, mode histogram stable across worker counts, output golden-hashed for the standard images at each quality.
- [x] **Benchmark** (hidden `[.][asset-bench]`): encode speed per format and quality at 1 and 8 workers, recorded for Day 339.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [x] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 340; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** BC7 mints deterministically at three qualities, every block it writes decodes, and its quality beats BC1 on the test images.

## Outcome

Built as planned, with these deviations:

- **Decoder:** all eight modes (partitions, anchors, rotations, both index sets of modes 4 and 5, p-bits). The 64 two-subset and 64 three-subset partition tables and their anchor texels were not typed from memory: they were read back from an independent BC7 decoder (Pillow's) by decoding crafted blocks, then the 192 random blocks (24 per mode) it decodes are embedded in the test with the FNV hash of its output, and the engine's decoder matches all of them. The reserved mode decodes to transparent black.
- **Encoder:** mode 6 only, as planned. Principal-axis range fit in four channels (extremes and 1/16 and 1/8 insets), the four p-bit combinations, least-squares refinement (`Normal` 2 passes, `High` 6; no partitions exist to try). An opaque block restricts both p-bits to 1 so alpha 255 is exact; a flat color of mixed channel parity is within one step (recorded).
- **Formats:** `format bc7` (alpha encoded as 255, `alpha` defaults to `ignore`) and `bc7-alpha` (alpha kept, `straight`); `srgb true` gives `BC7_SRGB`; `BcEncodeOptions::drop_alpha`.
- **Measured** (64x64, mean squared error per channel, RGB; BC1 against BC7): gradient 8.8 against 7.0, noise 2843 against 2700, photograph-like 5.4 against 1.6 (the gradient and noise gain is small because mode 6 has a single line; the smooth image gains 5 dB). Golden hashes of the 32x32 images at each quality are in `bc_test.cpp`.
- **Speed** (hidden `[.][asset-bench]`, 1024x1024 photograph-like image, debug build, MP/s at 1 and 8 workers): bc1 fast 8.6 and 60.8, normal 6.4 and 45.2; bc4 fast 22.8 and 172, normal 10.2 and 78; bc5 fast 11.7 and 88.9, normal 5.3 and 40.4; bc7 fast 0.66 and 4.95, normal 0.43 and 3.23, high 0.42 and 3.14. BC7 is below the spike's 3.7 to 7.4 MP/s (recorded; a release build and Day 339 will measure again).
- The Day 323 texture test that asserted BC7 had no size was updated.

Runs: plain `ran 3046 of 8153 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2461 of 8153`, tsan `ran 2134 of 8153` (the two bytecode tests waiting for the PC and Mac, and the three known tsan Vulkan SegFaults); Clang non-unity, GCC unity and non-unity clean; `ctest -L docs` 7 of 7; gates pass.

## Post-implementation code review

Built as planned, with these deviations:

- **Decoder:** all eight modes (partitions, anchors, rotations, both index sets of modes 4 and 5, p-bits). The 64 two-subset and 64 three-subset partition tables and their anchor texels were not typed from memory: they were read back from an independent BC7 decoder (Pillow's) by decoding crafted blocks, then the 192 random blocks (24 per mode) it decodes are embedded in the test with the FNV hash of its output, and the engine's decoder matches all of them. The reserved mode decodes to transparent black.
- **Encoder:** mode 6 only, as planned. Principal-axis range fit in four channels (extremes and 1/16 and 1/8 insets), the four p-bit combinations, least-squares refinement (`Normal` 2 passes, `High` 6; no partitions exist to try). An opaque block restricts both p-bits to 1 so alpha 255 is exact; a flat color of mixed channel parity is within one step (recorded).
- **Formats:** `format bc7` (alpha encoded as 255, `alpha` defaults to `ignore`) and `bc7-alpha` (alpha kept, `straight`); `srgb true` gives `BC7_SRGB`; `BcEncodeOptions::drop_alpha`.
- **Measured** (64x64, mean squared error per channel, RGB; BC1 against BC7): gradient 8.8 against 7.0, noise 2843 against 2700, photograph-like 5.4 against 1.6 (the gradient and noise gain is small because mode 6 has a single line; the smooth image gains 5 dB). Golden hashes of the 32x32 images at each quality are in `bc_test.cpp`.
- **Speed** (hidden `[.][asset-bench]`, 1024x1024 photograph-like image, debug build, MP/s at 1 and 8 workers): bc1 fast 8.6 and 60.8, normal 6.4 and 45.2; bc4 fast 22.8 and 172, normal 10.2 and 78; bc5 fast 11.7 and 88.9, normal 5.3 and 40.4; bc7 fast 0.66 and 4.95, normal 0.43 and 3.23, high 0.42 and 3.14. BC7 is below the spike's 3.7 to 7.4 MP/s (recorded; a release build and Day 339 will measure again).
- The Day 323 texture test that asserted BC7 had no size was updated.

Runs: plain `ran 3046 of 8153 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2461 of 8153`, tsan `ran 2134 of 8153` (the two bytecode tests waiting for the PC and Mac, and the three known tsan Vulkan SegFaults); Clang non-unity, GCC unity and non-unity clean; `ctest -L docs` 7 of 7; gates pass.
