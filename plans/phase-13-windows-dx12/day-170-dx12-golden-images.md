# Phase 13 / Day 170 — DX12 Golden Images: the Phase 4 Suite

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 169.

Run the whole Phase 4 RHI suite through DX12 and match every golden image. Where a test is Vulkan-shaped (`tests/rhi/vulkan_*`), it is split into a
backend-neutral test run against each registered backend and a Vulkan-only remainder.

## Tasks

- [x] **Backend-parameterized fixtures:** `gpu_fixture.h`, `cube_fixture.h`, `render_target.h` take a backend from the registry; each golden test runs once per
      available backend (Vulkan and DX12 on Windows, Vulkan on Linux) with the backend in the test name so a failure names it. Vulkan-only checks (loader,
      instance, `vk_memory` policy, SPIR-V validation) stay in `vulkan_*` files.
- [x] **Goldens on DX12:** triangle, compute, cube, textured cube, lit cube against the existing images and tolerances (decision 11: investigate, never
      re-bless; a tolerance change carries measured numbers and is recorded in the test and `KNOWN-ISSUES.md`); also compared against the Windows Vulkan output of
      Day 160 and the difference recorded, which separates driver from backend differences.
- [x] **WARP:** the same goldens on the WARP adapter (a second, deterministic D3D12 implementation); record whether they pass the same tolerance — useful later
      as a GPU-free check.
- [x] **Validation:** the debug layer silent for the whole suite; one full run with GPU-based validation enabled, recorded (time and messages).
- [x] **The RHI suite** (phase 4 label) passes on Windows for both backends; per-test results recorded in this file.
- [x] **Documentation:** `docs/guide/building-and-testing.md` "GPU tests per backend" (how the backend parameter works, how to run one backend); `docs/guide/rhi.md`
      notes the DX12 differences found; reference regenerated if headers changed.
- [x] **Runs** (Windows done; the Linux run of the shared fixtures is pending until Day 174): Windows and Linux `--affected` (shared test fixtures changed) with each machine's sanitizer flavors; quote all.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: a parameterized test that silently runs only one backend (assert the count of backends a test
      ran against), tolerance changes.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every Phase 4 golden image matches through DX12 (and Vulkan) on Windows with the debug layer silent, the GPU tests run per backend, and any tolerance
change is justified by recorded measurements.

## Notes

- **Fixtures.** `tests/rhi/backend_fixture.h` (new) holds the per-backend machinery: `GpuBackends` (the registered backends but null that are
  available), `FOR_EACH_GPU_BACKEND(backend)` (one Catch2 section per backend, named `vulkan` / `dx12`), `BackendValidation` (Vulkan's
  validation errors; Direct3D 12's debug-layer errors and warnings) and `CreateTestDevice` (validated, skips when it cannot be made, fails
  under `ENGINE_RHI_REQUIRE_GPU=1`). `cube_fixture.h` takes a backend and gained `CheckGolden` (the comparison, a named diff image, and the
  `ENGINE_RHI_GOLDEN_REPORT=1` report); `render_target.h` was generic already. `gpu_fixture.h` stays the Vulkan instance fixture of the
  Vulkan-only tests (the plan named it among those to parameterize; its users test Vulkan internals).
- **Files.** `vulkan_golden_test.cpp`, `vulkan_cube_test.cpp`, `vulkan_textured_cube_test.cpp` and `vulkan_lit_cube_test.cpp` became
  `golden_test.cpp`, `cube_test.cpp`, `textured_cube_test.cpp` and `lit_cube_test.cpp`; the fill/saxpy and storage-texture tests moved from
  `vulkan_compute_test.cpp` to `compute_test.cpp` (with the layouts the shaders declare: a read-only storage buffer, strides); the SPIR-V
  module check stays Vulkan's. Shaders come from the minted blob sets. The touched files were converted off the standard containers
  (`tests/rhi` 286 to 252 offending lines; `image_compare.*` still converts with all its users).
- **Goldens on DX12 (Radeon 860M):** every one matches with the tolerances unchanged. Largest difference from the golden / from Vulkan's
  image of the same test on this PC: triangle 1 / 0, cube 1 / 0, textured cube 40 (an edge pixel; 9 interior pixels over the tolerance, as
  allowed) / 7, lit cube 41 (13 interior) / 6. Vulkan on this PC: triangle 1, cube 1, textured 40 (10 interior), lit 41 (15 interior), as
  on Day 160. So DX12 and Vulkan agree to within 7 levels, and the distance to the NVIDIA-made goldens is the driver's.
- **WARP:** every golden matches with the same tolerances (triangle 1, cube 1, textured 44 with 13 interior pixels over, lit 44 with 16): a
  GPU-free check for later. This needed `ENGINE_RHI_GPU` on DX12 (review finding below).
- **Validation:** the debug layer and Vulkan's validation silent over the per-backend suite; GPU-based validation over it on DX12: 29 test
  cases passed, 8 s, no messages.
- **The RHI suite (phase 4) on Windows, both backends:** `ctest -L '^phase4$'`: 454 of 454 passed, 18 of them skipped (the Wayland and X11
  window tests and other Linux-only cases).

## For the Linux run

Shared: `tests/rhi/backend_fixture.h`, `cube_fixture.h`, the renamed and converted golden tests and `compute_test.cpp`, `vulkan_compute_test.cpp`,
`tests/rhi/CMakeLists.txt`, `tests/phases.txt`, `tools/std_container_check/allowlist.txt`, the testing guide. Run: both unity modes (GCC too:
the fixture's macro and the conversions), `--affected` with asan and tsan, `rhi_tests "[golden]"` (the Vulkan section alone), `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1267 of 2473 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 144 s; the last full run took 13 s` (`Throughput scales with participant count`, phase 2, failed once: the filed flake).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 941 of 2473 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 73 s; the last full run took 13 s`.
- All three trees build clean; docs 51 samples, 0 failed; lint 4489 of 4489; `ctest -L docs` 6 of 6.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 3 findings, 2 fixed, 1 declined.

- **Fixed (regression test, fails without the fix):** the DX12 backend ignored `ENGINE_RHI_GPU`, which Vulkan reads (the descriptor's adapter
  wins, then the variable), so the first WARP run used the Radeon; found by the WARP numbers matching the Radeon's. Allow-listed in
  `config_access_check` as Vulkan's is.
- **Fixed:** with no GPU backend the per-backend tests ran nothing and passed; they skip now (fail under `ENGINE_RHI_REQUIRE_GPU=1`). No test:
  the registry cannot be emptied for one test without affecting the rest of the process.
- **Declined:** `ENGINE_RHI_UPDATE_GOLDEN=1` writes only from the Vulkan section, so a DX12-only update run compares nothing; the goldens
  are Vulkan's by decision 11, and the analytic checks still run.
