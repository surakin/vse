# Phase 14 / Day 180 — The Atlas Texture and the Text Material

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 177-179; Phase 5 (materials, render graph); Phase 8 (device resources and reset); Phase 13 (the DXIL path, backend-parameterized GPU tests).

The GPU side of the atlas: textures that survive a reset, and a material that samples them as coverage.

## Tasks

- [ ] **`renderer::GlyphAtlasTexture`**: owns one RHI texture per CPU page (R8 unorm), uploads only the dirty rectangles through `UploadTexture` and the staging ring, registers with
      `renderer::DeviceResources` at its tier (a `ScopedRegistration` member), implements `Release`/`Recreate` by re-uploading every page from the CPU copy, and never caches a handle past a frame
      (it exposes the owner and the material reads `Handle()` when recording).
- [ ] **Text shader** `shaders/text.slang` (vertex + fragment): vertex input is a per-glyph quad (position, uv, color) built on the CPU or from the per-object storage buffer as Phase 5 Day 84 suggests —
      the spec (Day 175) decided; the fragment samples the atlas, outputs `color * coverage` as premultiplied alpha; compiled to SPIR-V and DXIL by `shader_mint`, committed with their reflection tables
      (`*.spv.inc`, DXIL blobs, `*.refl.inc`), `shaders_check` reproduces them with `slangc` v2026.18.2.
- [ ] **Text material template** in `MaterialSystem`: set 0 frame, set 1 material (atlas texture + sampler + a color/params block), blend state premultiplied alpha, no depth test, no depth write,
      pipeline cached by (template, target format). The atlas texture is replaced in place with `MaterialSystem::SetTexture` (Phase 5 Day 74) when a page is added, rebuilding only that material's bind groups.
- [ ] **Clip space and orientation:** the shader and the quad builder use the engine's clip conventions (`ENGINE_CLIP`, Phase 13 Day 163) so text is upright and unmirrored on Vulkan and DX12; a test
      renders an asymmetric glyph ("F") and reads it back.
- [ ] **Tests** (`tests/renderer/text_material_test.cpp`, GPU, per backend through the Day 170 fixtures): upload a page, draw one glyph quad, read back pixels (coverage values land where expected);
      a reset through `reset_rig.h` leaves resource counts equal (`Device::Resources()`) and the same pixels; device loss recovery re-uploads the pages; adding a page mid-frame sequence works.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux and Windows (Vulkan and DX12); then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** an atlas page uploads, the text material draws a coverage quad correctly oriented on Vulkan and DX12, the atlas texture survives a reset and a device loss with the resource counts unchanged, and `shaders_check` reproduces the new shaders.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
