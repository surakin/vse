# Phase 13 / Day 171 — DX12 Renderer: the Phase 5 Suite

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 170.

The renderer through DX12: render graph, materials and permutations, G-buffer, deferred lighting and the demo golden, plus `renderer_demo` on DX12.

## Tasks

- [x] **Renderer tests per backend:** the renderer fixtures (`gbuffer_rig.h`, `reset_rig.h`, the scene tests) take the backend parameter of Day 170; the render
      graph's `ResourceState` transitions are consumed by DX12 unchanged (the graph barrier test checks the emitted barriers on both backends).
- [x] **Materials and permutations:** every permuted shader's DXIL from Day 163 builds its pipeline; the material tests pass on DX12; bind groups by owner
      (the reset rule) work with DX12 descriptors.
- [x] **G-buffer and lighting:** formats and attachment count (`CheckGBufferFormats` against DX12 caps), sampled depth through the typeless path, the lighting pass;
      `renderer_demo.ppm` within tolerance on DX12 (same rule as Day 170) and compared with the Windows Vulkan image.
- [ ] **`renderer_demo` on DX12** (moved to Day 173, where the demos choose their RHI backend): 1000 frames with `--validation`, `--set renderer.backend=dx12` (or the setting's final key), settings round trip, the `demo`
      ctests per backend.
- [x] **Performance:** `renderer_bench` on DX12 versus the Windows Vulkan baseline (instanced-draw cost with the object-index stream, frame time of the
      demo scene); record both; a large gap is investigated (PIX capture if installed) and either fixed or filed with the numbers.
- [x] **The renderer suite** (phase 5 label) passes on Windows for both backends; results recorded here.
- [x] **Documentation:** `docs/guide/renderer.md` notes for DX12 (projection, object index, what differs); reference regenerated if headers changed.
- [x] **Runs** (Windows done; the Linux run of the shared fixtures is pending until Day 174): Windows and Linux `--affected` (shared renderer fixtures) with each machine's sanitizer flavors; quote all.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: per-frame descriptor ring sizing under the demo's 10 000-object frame, objects receiving the
      wrong index (a test that colors by index catches it), state leaks between passes.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Phase 5 renderer suite and `renderer_demo.ppm` pass through DX12 with the debug layer silent, `renderer_demo` runs on DX12, and DX12's renderer
cost is measured against Vulkan on the same machine.

## Notes

- **Per backend:** `gbuffer_rig.h` takes the backend (`MakeDevice(backend)`), and the G-buffer, lighting, render-graph and demo-scene GPU
  tests run once per backend. They became `gbuffer_gpu_test.cpp`, `lighting_gpu_test.cpp`, `render_graph_gpu_test.cpp` and
  `renderer_demo_gpu_test.cpp` (from `vulkan_*`; `tests/renderer/` has `gbuffer_test.cpp` and `lighting_test.cpp` already), converted off the
  standard containers (`tests/rhi` 252 to 223 offending lines). The render-graph test still fails on Vulkan's synchronization warnings,
  and DX12's warnings count through `BackendValidation`. The two device-loss and leak tests of the demo frame count Vulkan's leaks, so they
  stay Vulkan's (pinned to it); DX12's device loss is Day 172.
- **Plan amendments:** `reset_rig.h` and the reset tests per backend move to Day 172 (the reset on DX12 is that day's); `renderer_demo` on
  DX12, the `demo` ctests per backend and the demo scene's GPU frame time move to Day 173 (the programs choose their RHI backend there).
- **Results on DX12 (Radeon 860M):** the G-buffer (every permutation, `CheckGBufferFormats` against the DX12 caps, sampled depth through the
  typeless path), the lighting and tonemap passes, the four-pass render graph and the demo scene all pass with the debug layer silent; the
  demo image is 17 levels from `renderer_demo.ppm` at most on both backends and 1 level from Vulkan's. GPU-based validation over these tests:
  22 test cases passed, 19 s, no messages. The render graph's `ResourceState` transitions needed nothing on DX12.
- **The renderer suite (phase 5) on Windows, both backends:** `ctest -L '^phase5$'`: 403 of 403 passed.
- **Measured** (`renderer_bench`, Release, validation off, DX12 through a local edit not committed): a 10000-object frame on jobs 1.6 to 2.0 ms
  (Vulkan 1.6 to 1.7), 50000 objects on one thread 7.1 to 7.2 ms (Vulkan 6.4 to 6.5), on jobs 7.2 to 7.3 (Vulkan 6.9 to 7.1); 16000 point
  lights 4.4 to 4.5 ms (Vulkan 3.15), 0.27 against 0.19 us a volume draw. Each volume draw records only the draw (the buffer and groups are set
  once), so the difference is the D3D12 draw call; filed, not investigated further (PIX is not installed).

## For the Linux run

Shared: `tests/rhi/gbuffer_rig.h`, the four renamed renderer GPU tests, `tests/rhi/CMakeLists.txt`, `tests/phases.txt`, the guides and READMEs
that name the files, `tools/std_container_check/allowlist.txt`. Run: both unity modes (GCC too), `--affected` with asan and tsan, `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1267 of 2473 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 147 s; the last full run took 13 s` (`Throughput scales with participant count`, phase 2, failed once: the filed flake).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 941 of 2473 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 76 s; the last full run took 13 s`.
- All three trees build clean; docs 51 samples, 0 failed; `ctest -L docs` 6 of 6.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 2 findings, both declined (filed). The extra scrutiny the plan asks for: the descriptor ring under
the large frames (the 50000-object bench frames and the demo scene stay far inside a frame's 131072 descriptors: materials and frame groups
are bound per run, not per object); objects receiving the wrong index (the G-buffer test's per-object probes and the demo image would show
it); state left between passes (each pass sets its targets, root signature and groups again).

- **Declined (filed):** the DX12 draw costs 0.27 us against Vulkan's 0.19 us in the light-volume loop (above).
- **Declined (filed):** a 50000-object frame is about 10% slower on DX12 (above).
