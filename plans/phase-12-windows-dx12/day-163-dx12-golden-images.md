# Phase 12 / Day 163 — DX12 Golden Images: the Phase 4 Suite

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 162.

Run the whole Phase 4 RHI suite through DX12 and match every golden image. Where a test is Vulkan-shaped (`tests/rhi/vulkan_*`), it is split into a
backend-neutral test run against each registered backend and a Vulkan-only remainder.

## Tasks

- [ ] **Backend-parameterized fixtures:** `gpu_fixture.h`, `cube_fixture.h`, `render_target.h` take a backend from the registry; each golden test runs once per
      available backend (Vulkan and DX12 on Windows, Vulkan on Linux) with the backend in the test name so a failure names it. Vulkan-only checks (loader,
      instance, `vk_memory` policy, SPIR-V validation) stay in `vulkan_*` files.
- [ ] **Goldens on DX12:** triangle, compute, cube, textured cube, lit cube against the existing images and tolerances (decision 11: investigate, never
      re-bless; a tolerance change carries measured numbers and is recorded in the test and `KNOWN-ISSUES.md`); also compared against the Windows Vulkan output of
      Day 153 and the difference recorded, which separates driver from backend differences.
- [ ] **WARP:** the same goldens on the WARP adapter (a second, deterministic D3D12 implementation); record whether they pass the same tolerance — useful later
      as a GPU-free check.
- [ ] **Validation:** the debug layer silent for the whole suite; one full run with GPU-based validation enabled, recorded (time and messages).
- [ ] **The RHI suite** (phase 4 label) passes on Windows for both backends; per-test results recorded in this file.
- [ ] **Documentation:** `docs/guide/building-and-testing.md` "GPU tests per backend" (how the backend parameter works, how to run one backend); `docs/guide/rhi.md`
      notes the DX12 differences found; reference regenerated if headers changed.
- [ ] **Runs:** Windows and Linux `--affected` (shared test fixtures changed) with each machine's sanitizer flavors; quote all.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: a parameterized test that silently runs only one backend (assert the count of backends a test
      ran against), tolerance changes.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every Phase 4 golden image matches through DX12 (and Vulkan) on Windows with the debug layer silent, the GPU tests run per backend, and any tolerance
change is justified by recorded measurements.
