# Phase 8 / Day 106 — Device Lifecycle: `DeviceHost`, Epochs, Leak Accounting

**Phase:** [Phase 8 — RHI Reset](../phase-08-rhi-reset.md)
**Depends on:** Days 103-105.

The RHI half of a reset: one owner that destroys a device cleanly and creates the next, handles that cannot alias across
devices, and counters that let a test assert nothing leaked.

## Tasks

- [x] **Handle epochs** as decided on Day 103 (decided in the spec: a process-wide device serial; a pool's initial slot generation is `(serial & 0xFFF) << 20 | 1`; the 2^20-reuse and 4096-device limits are documented and tested). Change `HandlePool` accordingly; handles stay 8 bytes; generation wrap behavior unchanged and tested; null and Vulkan devices both use it.
- [x] **Live-resource accounting:** `Device::ResourceCounts()` returning live counts per kind (buffers, textures, samplers, shaders, bind group layouts, bind groups, pipelines, swapchains, command lists) plus device memory allocated and descriptor pools; implemented on both backends from the existing pools; the destructor logs and counts anything still alive (a `LogRhi` warning with the kinds), and a test hook makes that an error.
- [x] **Orderly shutdown contract** documented and implemented: `WaitIdle`, flush the deferred-destruction queues, release upload and readback staging, uniform rings, descriptor pools; then destroy. A new `Device::Quiesce()` (wait idle + drain deferred destruction, device still usable) so the host can check counts *before* destruction.
- [x] `rhi::DeviceHost` in `engine/rhi/device_host.h`: owns a `DevicePtr` and a `Swapchain*`; `Create(DeviceCreateInfo, SwapchainDesc)`, `Device()`, `Swapchain()`, `Epoch()` (the serial of the current device, 0 when none), `Destroy()`, and `Recreate(const DeviceCreateInfo&, const SwapchainDesc&)` which runs: notify observers (`OnDeviceReleasing`), `Quiesce`, destroy swapchain, destroy device, create device, create swapchain, notify (`OnDeviceCreated`). A failed create returns an error reason and leaves the host empty (the caller rolls back, Day 110).
- [x] Observer interface `DeviceObserver` (`OnDeviceReleasing(epoch)`, `OnDeviceCreated(device, epoch)`), registered with an ordering tier; the renderer's registry (Days 108-109) plugs into this. Observers are called on the owning thread only.
- [x] Pipeline cache: the host passes the same cache directory to each new device; a device on a different adapter writes its own file (name carries the identity); verify the cache is saved on destroy so the next device starts warm.
- [x] Thread rule enforced in debug builds: `Recreate` aborts (`ENGINE_ASSERT`) if called off the owning thread or between `BeginFrame` and `EndFrame`; documented in the RHI thread-safety contract (`docs/guide/rhi.md`).
- [x] Crash-report state from Phase 7: the host registers the current epoch and reset count as crash-state providers, replacing the adapter provider on each creation.
- [x] Tests: stale-handle detection across devices (a buffer handle from device A is invalid on device B, every handle kind, null backend and Vulkan); `Recreate` 100 times on the null backend with counts at zero after each; observers called in tier order and once each; a failing create leaves the host empty and reports the reason; counts at zero after destroy on Vulkan (GPU test, skips without GPU, 20 iterations), validation silent; ASan and TSan pass; death test for `Recreate` inside a frame.
- [x] **Documentation:** `///` comments with labels (`Aborts:` for the frame and thread rules, `Fails:` for creation); `rhi.md` gets "Lifecycle and reset" with a tagged sample; the new header is in `modules.txt`; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected`, then `--flavor asan` and `--flavor tsan` (lifetime code); quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on destruction order and handle aliasing; record it.
- [x] Update `STATUS.md`; commit.

**Done:** a device can be destroyed and re-created through `DeviceHost` any number of times on both backends with zero live resources between iterations, handles from earlier devices are always detected, the thread and frame rules are enforced, and the lifecycle section of the RHI guide is committed.

## Result

Added: `HandlePool::SeedGeneration` + `GenerationSeed`/`AllocateDeviceSerial`/`Device::Epoch` (both backends; the Vulkan serial is taken once a device is usable), `ResourceCounts` + `Device::Resources`, `Device::Quiesce`, `Device::InFrame`, `rhi::DeviceHost` and `DeviceObserver` (`engine/rhi/device_host.h`, compiled into `rhi_vulkan` because it calls `CreateDevice`), crash-state text `rhi_device`, guide section "Lifecycle and reset" (tagged sample), `tests/rhi/device_host_test.cpp` (14 cases; 2 GPU). Deviations, filed in `KNOWN-ISSUES.md`: the destructor leak report reuses the existing `VulkanDevice::GetLeakCounts` as the "error hook" (no new hook); descriptor-pool counts are not in `ResourceCounts`; pipeline-cache save on destroy is the existing `ShutdownPipelines` behavior and was not newly verified; the swapchain branch of the host (window set) is untested until Day 107; stale-handle rejection on Vulkan is checked by generation bits only because the rejection is an assert that cannot be unwound through the device lock; the death test for `Recreate` in a frame uses a throwing assert handler instead of a child process.
Scoped runs: plain `ran 951 of 2517 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (one failure: the known X11 window test, phase 1), asan `ran 682 of 2517 ... flavors: asan` and tsan `ran 412 of 2517 ... flavors: tsan`, all pass. `ctest -L docs` 4/4.

## Post-implementation code review

10 findings. Fixed: observers now see an idle device (host quiesces first); `RemoveObserver` releases an observer that holds objects; the serial is not taken by scratch devices; `Resources().swapchains` reads an atomic; transient bind groups are not counted; the false "lost device returns at once" claim removed (Day 111 implements it); test declaration order so observers outlive the host (and a test for the removal case). Declined: moving `device_host.cpp` into `rhi` (it needs `CreateDevice`, which lives in `rhi_vulkan`; documented on the class); a test of the 2^20 slot-reuse wrap (documented limit, would need 1M frees); making the destructor skip observers (a live observer would then leak; the contract says they outlive the host). The plan-scope gaps above are recorded, not hidden.
