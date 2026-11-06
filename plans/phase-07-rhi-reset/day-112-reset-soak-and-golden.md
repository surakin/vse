# Phase 7 / Day 112 — Reset Loops, Golden Equality & Sanitizers

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 108-111.

The phase's acceptance tests: the definition of done, stated as tests, plus planted regressions to prove they catch breakage.

## Tasks

- [ ] **Reset loop:** a test that applies a `DeviceReset` 100 times (null backend, in every flavor) and 100 times on Vulkan (GPU label; 20 in ASan/TSan flavors if 100 is too slow, recorded) with a scene of objects, lights and materials; after each iteration `ResourceCounts` equals the first iteration's, device memory is equal, the registry holds the same owners, `RendererStats` shows no failures; after the loop resident memory and open fds are within the tolerance measured in Day 103.
- [ ] **Golden equality:** render the `renderer_demo` offscreen scene, reset, render again: the two images are pixel-identical on the same adapter (zero tolerance, same GPU and driver), and both match the existing blessed golden within its existing tolerance. A cross-adapter variant (iGPU vs dGPU) compares with the golden tolerance only and skips on a single-adapter machine.
- [ ] **Reset during a frame sequence:** 50 frames with a changing camera, a reset requested at frames 7, 8 (a newer request replacing it), 20 (swapchain class) and 31 (device class); the scene state (object transforms, light list, material parameters) is identical before and after by value, and frame indexes continue.
- [ ] **Every cost class under load:** one test per class asserting what *did not* happen (a `Dynamic` change creates no device object; a `Swapchain` change keeps the device epoch), using device statistics, not timing.
- [ ] **Concurrency:** a scheduler-backed renderer resetting while worker jobs are queued (TSan flavor); the reset joins the workers' recording before touching the device; a job submitted during the reset waits.
- [ ] **Planted regressions** (restored afterwards), recording which are caught: skip one owner's `Release` (leak count fails); keep a stale handle in a material (stale-handle abort or the equality test fails); skip the dirty flag of `FrameData` (golden equality fails); skip the registry's tier order (a test of order fails); skip the `Quiesce` before destruction (validation or TSan catches it); make rollback a no-op (the rollback test fails); make recovery ignore the retry limit (the limit test fails). Uncaught ones go to `KNOWN-ISSUES.md` with the test gap.
- [ ] Add each new test file to `tests/phases.txt` (phase 7) and verify `scripts/test-phase.sh --affected` selects them from a change to `engine/renderer/material.cpp` and to `engine/rhi/device_host.cpp`.
- [ ] **Documentation:** the guide's "Testing a reset" paragraph (how to write a test that survives a reset, how to use the counts); `docs/guide/building-and-testing.md` lists the new GPU tests and the multi-adapter skip; `ctest -L docs`.
- [ ] Run the scoped tests and the asan and tsan flavors; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff (mostly tests and any fixes they forced); record it.
- [ ] Update `STATUS.md` and `KNOWN-ISSUES.md`; commit.

**Done:** the loop, golden, mid-sequence and concurrency tests pass in the plain, ASan and TSan flavors, the seven planted regressions are caught or filed, and nothing leaked across 100 resets on either backend.
