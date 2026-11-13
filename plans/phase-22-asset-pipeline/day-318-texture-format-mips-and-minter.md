# Phase 22 / Day 318 — The Texture Format, Mip Generation & the Texture Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-317; Phase 5 (`renderer::TextureImage`).

Textures without compression yet: formats, mips, color space, and the minter around them.

## Tasks

- [ ] **Texture blob v1:** dimension (2D, cube, array), width, height, depth/layers, `rhi::Format`, color space (sRGB or linear), the mip chain tightly packed per level, bounds on sizes and mip counts enforced by the loader. **Every mip level is stored** (there is no "generate at load" flag: mip generation, sRGB handling and premultiplication all happen in the minter), and each level starts at an aligned offset in the layout the upload wants (row pitch and slice alignment of the target API recorded by the minter from the RHI's rules, so the loader copies nothing).
- [ ] **Source form `.tex.src`:** text parameters (`source`, `format`, `srgb`, `mips`, `filter`, `alpha`) naming a raw image file in a trivial custom container (`.raw`: a small header plus RGBA8 rows) that the tool's test generator and the Phase 29 importer produce; no PNG (the user's answer).
- [ ] **Mip generation:** a deterministic filter (box and a fixed-tap Kaiser-windowed filter, in linear light for sRGB data, with alpha handled premultiplied) in integer or exactly-specified float arithmetic so the output is byte-identical across compilers and machines (no `-ffast-math`, no FMA contraction: tested on Clang and GCC and, on the other machines, at the closing run).
- [ ] **Formats in this day:** R8, RG8, RGBA8 (UNORM and sRGB), R16F, RGBA16F; compressed formats arrive on Days 319 and 320 behind the same blob layout.
- [ ] **Loader `LoadTextureBlob(bytes)`:** returns a view of the buffer's levels for the upload with `desc.mip_levels` from the blob and `generate_mips = false` always (the zero-copy path of Day 313's spike; a `TextureImage`-compatible copying overload exists only for tests).
- [ ] **Tests:** minted mip chains of a gradient, a checker and a noise image against golden hashes; sRGB round-trip error bounds; odd sizes (non-power-of-two, 1xN); a cube map; hostile blobs; minting twice and on two thread counts identical.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** raw images mint to texture blobs with deterministic mips, load into `TextureImage`, and hostile blobs are refused.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
