# Phase 22 / Day 320 — The BC7 Encoder (a Subset) & Quality Settings

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 319.

The high-quality format for color and alpha, limited to the modes Day 313's spike chose.

## Tasks

- [ ] **BC7 decoder** for every mode (the decoder must accept any valid block, whatever the encoder writes).
- [ ] **BC7 encoder subset:** modes 6 (one subset, RGBA with p-bits) and the two-subset mode the spike picked for opaque color (with a fixed table of partitions, a bounded partition search), endpoint refinement by a bounded least-squares or coordinate descent, p-bit and index-bit selection; choice between modes by error, ties broken by mode number.
- [ ] **Quality and speed settings** (`fast`, `normal`, `high`: partitions tried and refinement steps) with the time per megapixel measured and recorded.
- [ ] **Texture minter formats** `bc7` (color), `bc7-alpha`; a minter option choosing the format by content (opaque or not) is *not* added (explicit is deterministic and reviewable).
- [ ] **Tests:** PSNR against BC1 on the same images (BC7 must be better on the gradient, noise and a photograph-like test image), validity of every block by the decoder, mode histogram stable across worker counts, output golden-hashed for the standard images at each quality.
- [ ] **Benchmark** (hidden `[.][asset-bench]`): encode speed per format and quality at 1 and 8 workers, recorded for Day 329.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** BC7 mints deterministically at three qualities, every block it writes decodes, and its quality beats BC1 on the test images.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
