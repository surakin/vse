# Phase 12 / Day 164 — DX12 Renderer: the Phase 5 Suite

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 163.

The renderer through DX12: render graph, materials and permutations, G-buffer, deferred lighting and the demo golden, plus `renderer_demo` on DX12.

## Tasks

- [ ] **Renderer tests per backend:** the renderer fixtures (`gbuffer_rig.h`, `reset_rig.h`, the scene tests) take the backend parameter of Day 163; the render
      graph's `ResourceState` transitions are consumed by DX12 unchanged (the graph barrier test checks the emitted barriers on both backends).
- [ ] **Materials and permutations:** every permuted shader's DXIL from Day 156 builds its pipeline; the material tests pass on DX12; bind groups by owner
      (the reset rule) work with DX12 descriptors.
- [ ] **G-buffer and lighting:** formats and attachment count (`CheckGBufferFormats` against DX12 caps), sampled depth through the typeless path, the lighting pass;
      `renderer_demo.ppm` within tolerance on DX12 (same rule as Day 163) and compared with the Windows Vulkan image.
- [ ] **`renderer_demo` on DX12:** 1000 frames with `--validation`, `--set renderer.backend=dx12` (or the setting's final key), settings round trip, the `demo`
      ctests per backend.
- [ ] **Performance:** `renderer_bench` on DX12 versus the Windows Vulkan baseline (instanced-draw cost with the root-constant object index, frame time of the
      demo scene); record both; a large gap is investigated (PIX capture if installed) and either fixed or filed with the numbers.
- [ ] **The renderer suite** (phase 5 label) passes on Windows for both backends; results recorded here.
- [ ] **Documentation:** `docs/guide/renderer.md` notes for DX12 (projection, object index, what differs); reference regenerated if headers changed.
- [ ] **Runs:** Windows and Linux `--affected` (shared renderer fixtures) with each machine's sanitizer flavors; quote all.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: per-frame descriptor ring sizing under the demo's 10 000-object frame, objects receiving the
      wrong index (a test that colors by index catches it), state leaks between passes.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Phase 5 renderer suite and `renderer_demo.ppm` pass through DX12 with the debug layer silent, `renderer_demo` runs on DX12, and DX12's renderer
cost is measured against Vulkan on the same machine.
