# Phase 26 / Day 323 — Metal Renderer: the Phase 5 Suite

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 322.

The Phase 5 scene tests through Metal: the render graph, materials and permutations, the deferred G-buffer and lighting, and `renderer_demo.ppm`.
Modelled on DX12 Day 171.

## Tasks

- [ ] **Run the suites** with a Metal factory: `render_graph_gpu_test`, `gbuffer_gpu_test`, `lighting_gpu_test`, `renderer_demo_gpu_test`, the material
      and permutation tests, `reset_golden_gpu_test`; record differences as on Day 322.
- [ ] **Graph on Metal:** the graph's barriers become what Day 320 decided; passes that write and then sample a G-buffer target land in separate render
      encoders (checked by a test counting encoders per frame); the HDR target `RGBA16Float`.
- [ ] **Depth sampling:** the depth shim the renderer uses for D3D12 (typed view of a depth texture) has its Metal counterpart; the lighting pass samples
      `Depth32Float` correctly.
- [ ] **`renderer_demo` on Metal:** runs its script, writes `renderer_demo.ppm`, matches the golden within tolerance; F1-F10 live keys checked by hand.
- [ ] **Baselines:** `rhi_bench` and `renderer_bench` on Metal recorded beside the other machines' (no target; the integrated GPU and the Retina size are
      named).
- [ ] **Documentation:** `renderer.md` names Metal where it lists backends; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: handle lifetime across frames in parallel recording, the encoder-count test's strength.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Phase 5 scene suites and `renderer_demo.ppm` pass through Metal with validation silent in three flavors, and the benchmark baselines are
recorded.
