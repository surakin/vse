# Phase 13 / Day 172 — DX12 Lifecycle, Device Loss and the Backend Switch

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 171; `engine/rhi/BACKEND-LIFECYCLE.md`, `tests/rhi/lifecycle_conformance_test.cpp`.

Make DX12 a full participant in the Phase 8 reset model, then switch backends at run time.

## Tasks

- [ ] **The fifteen lifecycle functions** of `BACKEND-LIFECYCLE.md` on DX12 (`CreateDevice`, `EnumerateAdapters`, `Adapter`, `Epoch`, `Resources`, `Quiesce`,
      `RecreateSurface`, `InjectDeviceLoss`, `Swapchain::Reconfigure`, and the rest as listed); each section of the checklist gets its DX12 note.
- [ ] **Orderly shutdown and tolerant destruction:** the destructor waits for the GPU, releases deferred destruction, rings and heaps, saves the pipeline cache, then
      the device and factory; after loss every destruction path returns without hanging (bounded waits from Day 164); `ReportLiveDeviceObjects` empty afterwards.
- [ ] **Device loss:** `InjectDeviceLoss` through `ID3D12Device5::RemoveDevice`; detection from `Present`, fence waits and `GetDeviceRemovedReason`; recovery
      through `DeviceHost` and `Renderer::ApplySettings` unchanged; `device_loss_recovery_test` on DX12.
- [ ] **Conformance:** a DX12 factory in `lifecycle_conformance_test.cpp` (and WARP as a second one); the whole suite passes; `rhi_lifecycle_lists` passes.
- [ ] **GPU switch at run time:** Radeon 860M ↔ WARP through the `gpu` setting while `renderer_demo` runs (the first time a real GPU switch is exercised on hardware
      the project owns; Linux has one adapter): the picture before and after matches the golden on each adapter, counts unchanged.
- [ ] **Backend switch at run time** (deferred from Phase 8): Vulkan ↔ DX12 through the `backend` setting in a running `renderer_demo` and in a test: every owner
      registered with `renderer::DeviceResources` rebuilds from its CPU-side source with the other backend's shader blobs; 20 alternating switches leave resource
      counts and process memory flat (record the numbers); the picture matches the golden after each switch. `KNOWN-ISSUES.md` entry removed.
- [ ] **Soak:** `reset_soak_test` on DX12 (100 resets: counts and device memory unchanged); reset time recorded beside Vulkan's on this machine.
- [ ] **Documentation:** `BACKEND-LIFECYCLE.md` DX12 notes; `docs/guide/renderer.md` "Surviving a reset" mentions the backend switch; `docs/guide/render-settings.md`
      lists `dx12` and the switch's cost class; reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan (lifetime code: run the asan reset and switch tests 10 times); Linux `--affected` plus asan and tsan if shared
      reset code changed; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny (lifetime and ownership): a handle from one backend used after the switch, shader blobs of the
      wrong format reaching a pipeline, a hang on destruction after loss, the swapchain's window reference across the switch.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** DX12 passes the lifecycle conformance suite, recovers from an injected device loss, switches GPUs between the Radeon and WARP, and a running program
switches between Vulkan and DX12 through the `backend` setting with counts and memory flat.
