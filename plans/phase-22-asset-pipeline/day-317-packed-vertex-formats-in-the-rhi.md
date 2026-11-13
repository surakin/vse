# Phase 22 / Day 317 — Packed Vertex Formats in the RHI

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 313-316; Phase 4 (`rhi::VertexFormat`, the backends), Phase 13 (DX12), Phase 21 (Metal).

Added on 2026-11-13 (the user asked to consider skeletons and animation before the mesh format). The RHI has only `Float1-4`, `UInt1-4` and `UNorm8x4`, so a mesh blob could not hold the compact attributes a GPU-ready format needs (16-bit texture coordinates, 8-bit normals and tangent frames, 8-bit joint indices, 8-bit skin weights) without a load-time conversion, which the phase forbids. This day adds the formats; Day 319 (mesh) and Day 322 (skinning) use them.

## Tasks

- [ ] **Inventory (read first):** `rhi::VertexFormat` and `VertexFormatSize` (`engine/rhi/enums.h`), `VertexAttributeDesc::Validate`, every backend mapping table (Vulkan `VkFormat`, DX12 `DXGI_FORMAT`, Metal `MTLVertexFormat`, the null backend), the shader reflection check that decides whether a vertex input type fits an attribute format (`renderer::shader_reflection`), and the places that switch over `VertexFormat` (`Count` users). Record the list in this file before changing anything.
- [ ] **The new formats** (one enumerator each, appended before `Count`, sizes in `VertexFormatSize`): `Float16x2`, `Float16x4`, `UNorm16x2`, `UNorm16x4`, `SNorm16x2`, `SNorm16x4`, `SNorm8x4`, `UInt8x4`, `UInt16x4`. `UNorm8x4` stays. Each is a vertex-buffer format the three APIs must support without a capability bit (Vulkan core lists `R8G8B8A8_SNORM`, `R8G8B8A8_UINT`, `R16G16_{UNORM,SNORM,SFLOAT}`, `R16G16B16A16_{UNORM,SNORM,UINT,SFLOAT}` as mandatory vertex formats; D3D12 and Metal likewise): if the Day 317 inventory finds one that is not, drop it from the list and say so here.
- [ ] **Backend mappings:** a table entry per backend and format, with the Vulkan table checked by a test that asks the physical device for `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` on every format the enum lists (so a wrong mapping or a missing feature fails at once), the DX12 table written to the same shape (`engine/rhi/dx12/`, Windows) and the Metal table (`engine/rhi/metal/`, Mac); the null backend accepts them. Alignment rules (a 6-byte `SNorm16x2` pair next to a 4-byte attribute) are checked by `Validate`, with a message naming the attribute.
- [ ] **Shader reflection fit:** a `float4` (or `float2`) shader input accepts the float, half, UNorm and SNorm formats of the same width, a `uint4` input accepts `UInt8x4`, `UInt16x4` and `UInt4`; any other pairing is the existing pipeline-build mismatch error, with a test for each accepted and one refused pair per format.
- [ ] **GPU readback test (Vulkan on Linux):** a vertex shader that passes each attribute through to a storage buffer; the test draws one point per format with known bytes and reads back the decoded values (UNorm16 0xFFFF reads 1.0, SNorm8 0x81 reads -1.0, half 0x3C00 reads 1.0, UInt8 0xFF reads 255); the same test runs on DX12 and Metal on their machines (`[.]`-free, skipped without the backend). The shader is a new Slang source with committed outputs like the others.
- [ ] **Compatibility:** the existing `StandardVertex` and `ColorVertex` layouts are unchanged and their tests untouched; a layout test shows `VertexFormatSize` for every enumerator is non-zero and `VertexFormat::Count` is the new bound; `renderer::MeshData::Set` validates the new formats (its stride and offset checks) and its tests cover them.
- [ ] **Three machines:** the DX12 and Metal tables and the readback test are written on the Linux machine, checked on the Windows PC and the Mac no later than Day 335 (`KNOWN-ISSUES.md` lists them as waiting); the Mac needs the interim `.metallib` route for the readback shader.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 335; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every new vertex format maps on all four backends, the Vulkan device confirms the feature bits, the readback test decodes each format to the expected values, and the existing vertex layouts are unchanged.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
