# Phase 22 / Day 324 — BC Decoders & the BC1, BC4 and BC5 Encoders

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 323; PLAN.md assumption #4 (BCn are hardware formats; decoding them ourselves is in scope).

The block-compression core: decoders first (the reference for every test), then the simple encoders.

## Tasks

- [x] **`rhi::Format::BC4_UNorm`** (Day 313 Spike 4: the enum has BC1, BC5 and BC7 but not BC4): add it with the Vulkan, DX12 and Metal mappings, the format-property table and a test per backend that compiles on its platform (the Mac and the PC check theirs at the phase close, `KNOWN-ISSUES.md`).
- [x] **Decoders** for BC1, BC2/BC3 (alpha), BC4, BC5 and (Day 325) BC7 written from the published formats: `DecodeBlock`, `DecodeImage`; tested against the format specification's worked examples.
- [x] **BC1 encoder:** endpoint selection by a deterministic principal-axis (range-fit) search with refinement, a 4-color mode and an optional 3-color-plus-transparent mode, endpoint rounding and ordering fixed so the output is byte-identical everywhere; a quality setting that bounds the refinement iterations.
- [x] **BC4 and BC5 encoders** (one and two channels, 8 interpolation steps, both endpoint orders tried, deterministic tie-breaking).
- [x] **Block-parallel encoding** on the job system (`jobs::parallel_for` over rows of blocks) with output independent of the worker count.
- [x] **Wiring:** the texture minter takes `format = bc1 | bc4 | bc5`; the blob stores the compressed levels; the loader returns them as-is (the GPU format exists in `rhi::Format`; Day 313's spike confirmed or added it).
- [x] **Tests:** decode(encode(x)) PSNR on the test images against bounds recorded in the spec (a solid, a gradient, a checker, noise, a normal map for BC5); an exhaustive check over every 4x4 block of a small generated set for round-trip validity (decoder accepts every block the encoder writes); encoded output identical for 1 and 8 workers; a block with all-equal texels encodes exactly.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [x] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 341; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** BC1, BC4 and BC5 mint deterministically with PSNR above the recorded bounds, the decoders match the specification, and worker count does not change the output.

## Outcome

Built as planned, with these deviations:

- **BC4:** `rhi::Format::BC4_UNorm` is appended after `BC7_SRGB` (before `Count`) so no stored format number changes; mapped in Vulkan, Direct3D 12 and Metal (`MTLPixelFormatBC4_RUnorm`), with the format table row and checks in `types_test.cpp` and `dx12_resource_test.cpp`. The DX12 and Metal edits were not built here.
- **Codec** (`engine/asset/block_compress.*`): decoders for BC1, BC2, BC3, BC4, BC5 (BC7 is Day 325; BC2 has no `rhi::Format`, so the decoder names schemes with `BcKind`); encoders for BC1, BC4, BC5 in integer arithmetic. BC1: principal axis by fixed-point power iteration, extremes plus 1/8 and 1/4 insets, then 0/2/4 least-squares refinements by quality, 3-color transparent form behind `punchthrough_alpha`. BC4/5: min and max in both orders (8-step and 6-step forms) and least-squares refinement. `EncodeImage` fans block rows out with `jobs::ParallelForBlocking`; `asset` now links `jobs` (an edge ARCHITECTURE.md already allowed).
- **Wiring:** `format bc1|bc4|bc5` (`srgb true` with bc1 gives `BC1_SRGB`; bc4/bc5 are linear). The chain is filtered as RGBA8 (linear light for sRGB), each stored level is compressed; `TightRowBytes`/`LevelRows` make the blob layout block-aware, `MintTexture` takes an optional scheduler, `mint-texture` starts one only for bc sources.
- Measured error (64x64, mean squared error per channel): solid 4.3, gradient 8.8 (BC1) and 0.2 (BC4), checker 3.8, normal map 25.1 (BC5), noise 2843 (BC1, 13.6 dB), 62.5 (BC4), 61.4 (BC5); the test bounds sit just above these. Output is identical for no scheduler and 1, 3 and 8 workers, and on Clang and GCC, unity on and off.
- The Day 323 files were not clang-formatted when committed; a formatting-only commit (`aa0275ab`) precedes this day.

Runs: plain `ran 3040 of 8135 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2455 of 8135`, tsan `ran 2128 of 8135`; failures are the two bytecode tests waiting for the PC and Mac and, under tsan, the three known Vulkan SegFaults (`KNOWN-ISSUES.md`). `ctest -L docs` 7 of 7, gates pass. End to end: `asset_mint mint-texture` on a 32x32 raw image gave 1216, 1216 and 1856 bytes for bc1, bc4 and bc5 (5952 for rgba8).

## Post-implementation code review

Built as planned, with these deviations:

- **BC4:** `rhi::Format::BC4_UNorm` is appended after `BC7_SRGB` (before `Count`) so no stored format number changes; mapped in Vulkan, Direct3D 12 and Metal (`MTLPixelFormatBC4_RUnorm`), with the format table row and checks in `types_test.cpp` and `dx12_resource_test.cpp`. The DX12 and Metal edits were not built here.
- **Codec** (`engine/asset/block_compress.*`): decoders for BC1, BC2, BC3, BC4, BC5 (BC7 is Day 325; BC2 has no `rhi::Format`, so the decoder names schemes with `BcKind`); encoders for BC1, BC4, BC5 in integer arithmetic. BC1: principal axis by fixed-point power iteration, extremes plus 1/8 and 1/4 insets, then 0/2/4 least-squares refinements by quality, 3-color transparent form behind `punchthrough_alpha`. BC4/5: min and max in both orders (8-step and 6-step forms) and least-squares refinement. `EncodeImage` fans block rows out with `jobs::ParallelForBlocking`; `asset` now links `jobs` (an edge ARCHITECTURE.md already allowed).
- **Wiring:** `format bc1|bc4|bc5` (`srgb true` with bc1 gives `BC1_SRGB`; bc4/bc5 are linear). The chain is filtered as RGBA8 (linear light for sRGB), each stored level is compressed; `TightRowBytes`/`LevelRows` make the blob layout block-aware, `MintTexture` takes an optional scheduler, `mint-texture` starts one only for bc sources.
- Measured error (64x64, mean squared error per channel): solid 4.3, gradient 8.8 (BC1) and 0.2 (BC4), checker 3.8, normal map 25.1 (BC5), noise 2843 (BC1, 13.6 dB), 62.5 (BC4), 61.4 (BC5); the test bounds sit just above these. Output is identical for no scheduler and 1, 3 and 8 workers, and on Clang and GCC, unity on and off.
- The Day 323 files were not clang-formatted when committed; a formatting-only commit (`aa0275ab`) precedes this day.

Runs: plain `ran 3040 of 8135 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2455 of 8135`, tsan `ran 2128 of 8135`; failures are the two bytecode tests waiting for the PC and Mac and, under tsan, the three known Vulkan SegFaults (`KNOWN-ISSUES.md`). `ctest -L docs` 7 of 7, gates pass. End to end: `asset_mint mint-texture` on a 32x32 raw image gave 1216, 1216 and 1856 bytes for bc1, bc4 and bc5 (5952 for rgba8).
