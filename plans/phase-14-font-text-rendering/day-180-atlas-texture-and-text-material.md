# Phase 14 / Day 180 — The Atlas Texture and the Text Pipeline

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 177-179; Phase 5 (materials, render graph); Phase 8 (device resources and reset); Phase 13 (the DXIL path, backend-parameterized GPU tests).

The GPU side of the atlas: textures that survive a reset, and a pipeline that samples them as coverage.

**Plan amendment (2026-11-09, Day 180):** the "text material in `MaterialSystem`" became a dedicated `renderer::TextRenderer` on the raw RHI (own pipeline, bind group layout, sampler and per-frame vertex buffer). The material system's set 1 / per-object conventions fit meshes, not a stream of pixel-space quads, and the renderer's other passes (G-buffer, lighting, tonemap) already own their pipelines the same way. The atlas page is sent whole when it changed (the RHI has no sub-rectangle upload; `KNOWN-ISSUES.md`). The shaders are `text.vert` / `text.frag` with SPIR-V only (no `dxc` on the Linux machine; the DXIL is minted on the Windows PC). Day 181 builds the render-graph pass and the draw API on this `TextRenderer`.

## Tasks

- [x] **`renderer::GlyphAtlasTexture`**: owns one RHI texture per CPU page (R8 unorm), sends a changed page whole through `UploadTexture` (the RHI uploads a full mip level), registers with
      `renderer::DeviceResources` at its tier (a `ScopedRegistration` member), implements `Release`/`Recreate` by re-uploading every page from the CPU copy, and never caches a handle past a frame
      (it exposes the owner and the material reads `Handle()` when recording).
- [x] **Text shaders** `shaders/text.vert.slang` and `text.frag.slang`: vertex input is a per-glyph quad (position, uv, color) built on the CPU or from the per-object storage buffer as Phase 5 Day 84 suggests —
      the spec (Day 175) decided; the fragment samples the atlas, outputs `color * coverage` as premultiplied alpha; compiled to SPIR-V by `shader_mint`, committed with their reflection tables
      (`*.spv.inc`, `*.refl.inc`), `shaders_check` reproduces them with `slangc` v2026.18.2; DXIL waits for the Windows PC (`KNOWN-ISSUES.md`).
- [x] **`renderer::TextRenderer`** (replaces the planned material template, see the amendment): pipeline for a given target format (premultiplied alpha blend, no depth), sampler, bind group per atlas page
      per draw, a CpuToGpu vertex buffer with a region per frame in flight, `BeginFrame` / `Add(layout, style, x, y)` / `Record(list, ring, w, h)`, a per-frame glyph budget with a counted drop, Release/Recreate through the registry.
- [x] **Clip space and orientation:** the shader and the quad builder use the engine's clip conventions (`ENGINE_CLIP`, Phase 13 Day 163) so text is upright and unmirrored on Vulkan and DX12; a test
      renders an asymmetric glyph ("F") and reads it back.
- [x] **Tests** (`tests/renderer/text_renderer_test.cpp`, GPU, per backend through the Day 170 fixtures): upload a page, draw one glyph quad, read back pixels (coverage values land where expected);
      a reset through `DeviceHost::Recreate` and the registry leaves resource counts equal (`Device::Resources()`) and the same pixels; device loss recovery re-uploads the pages; adding a page mid-frame sequence works.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux and Windows (Vulkan and DX12); then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** an atlas page uploads, the text material draws a coverage quad correctly oriented on Vulkan and DX12, the atlas texture survives a reset and a device loss with the resource counts unchanged, and `shaders_check` reproduces the new shaders.

## Notes

- New: `engine/renderer/glyph_atlas_texture.h/.cpp` (`GlyphAtlasTexture`: one R8 texture per atlas page, `Sync` creates new pages and sends changed ones whole, `Release`/`Recreate` rebuild every page from the CPU atlas, registered at the texture tier); `engine/renderer/text_renderer.h/.cpp` (`TextRenderer`, `TextRendererDesc`: pipeline with premultiplied blend, sampler, bind group per page, CpuToGpu vertex buffer with a region per frame in flight, `BeginFrame`/`Add`/`Record`, glyph budget with counted drops, registered at the material tier); `shaders/text.vert.slang`, `text.frag.slang` (SPIR-V and reflection minted; the std140 block is checked against the reflection by `static_assert`). The `renderer` library now links `text` (`ARCHITECTURE.md`, docgen sample link line).
- Plan amendments (also at the top of the day file and in the Day 181 file, the phase index and the spec): no `MaterialSystem` template, a dedicated `TextRenderer` on the raw RHI like the other passes; a changed page is sent whole (RHI uploads full mip levels; `KNOWN-ISSUES.md`); no DXIL yet (no `dxc` on this machine), so the GPU tests run on Vulkan only and Direct3D 12 refuses `Create` by name (`KNOWN-ISSUES.md`, open until the Windows PC).
- Colors: written as sRGB bytes; for an sRGB target the vertex shader decodes them, for UNORM they are used as they are. Quads sit on whole pixels (corner = floor(x + 0.5)).
- Tests: `tests/renderer/text_renderer_test.cpp` (7 cases, label phase 14): null device (pages follow the atlas; registry rebuild; queueing, budget drops, Record order), Vulkan (every pixel of an "F" equals the atlas bitmap, with orientation checks; premultiplied blend over blue at alpha 1 and 0.5; sRGB round trip; a device reset and an injected device loss give the same pixels with equal resource counts). A flipped-UV mutation fails the tests.
- Runs on Linux (Clang), reduced: `--affected` plain `ran 1466 of 3773 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 92 s` (one failure: the known X11 session flake); asan `ran 1054 of 3773 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 51 s`; tsan `ran 759 of 3773 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 10 s`. `ctest -L docs` 6 of 6. GCC and Clang non-unity builds clean; the text tests pass in both. Windows: open (`KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, 2 fixed, 2 declined.
- Fixed: `Record` kept drawing after an atlas page failed to sync (a missing or never-uploaded page could be sampled); it now returns false before drawing.
- Fixed: a second `Record` in one frame would overwrite the frame's vertex region while the first draw still reads it; now an `ENGINE_ASSERT` (documented under `Aborts:`; reset on `Release` because a new device counts frames again). No death-test regression test: the project's assert tests need the child-process helper and the contract is documented; declined for cost.
- Declined: whole-page upload of a changed page (the RHI has no sub-rectangle upload; `KNOWN-ISSUES.md`).
- Declined: no DXIL for the text shaders (no `dxc` on this machine; `KNOWN-ISSUES.md`, open until the Windows PC).
