# Phase 7 / Day 106 — Device Lifecycle: `DeviceHost`, Epochs, Leak Accounting

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 103-105.

The RHI half of a reset: one owner that destroys a device cleanly and creates the next, handles that cannot alias across
devices, and counters that let a test assert nothing leaked.

## Tasks

- [ ] **Handle epochs** as decided on Day 103 (default plan: `DeviceCreateInfo`-independent process-wide device serial seeds each handle pool's generation counter so a handle from an earlier device is never valid on a later one). Change `HandlePool` accordingly; handles stay 8 bytes; generation wrap behavior unchanged and tested; null and Vulkan devices both use it.
- [ ] **Live-resource accounting:** `Device::ResourceCounts()` returning live counts per kind (buffers, textures, samplers, shaders, bind group layouts, bind groups, pipelines, swapchains, command lists) plus device memory allocated and descriptor pools; implemented on both backends from the existing pools; the destructor logs and counts anything still alive (a `LogRhi` warning with the kinds), and a test hook makes that an error.
- [ ] **Orderly shutdown contract** documented and implemented: `WaitIdle`, flush the deferred-destruction queues, release upload and readback staging, uniform rings, descriptor pools; then destroy. A new `Device::Quiesce()` (wait idle + drain deferred destruction, device still usable) so the host can check counts *before* destruction.
- [ ] `rhi::DeviceHost` in `engine/rhi/device_host.h`: owns a `DevicePtr` and a `Swapchain*`; `Create(DeviceCreateInfo, SwapchainDesc)`, `Device()`, `Swapchain()`, `Epoch()` (the serial of the current device, 0 when none), `Destroy()`, and `Recreate(const DeviceCreateInfo&, const SwapchainDesc&)` which runs: notify observers (`OnDeviceReleasing`), `Quiesce`, destroy swapchain, destroy device, create device, create swapchain, notify (`OnDeviceCreated`). A failed create returns an error reason and leaves the host empty (the caller rolls back, Day 110).
- [ ] Observer interface `DeviceObserver` (`OnDeviceReleasing(epoch)`, `OnDeviceCreated(device, epoch)`), registered with an ordering tier; the renderer's registry (Days 108-109) plugs into this. Observers are called on the owning thread only.
- [ ] Pipeline cache: the host passes the same cache directory to each new device; a device on a different adapter writes its own file (name carries the identity); verify the cache is saved on destroy so the next device starts warm.
- [ ] Thread rule enforced in debug builds: `Recreate` aborts (`ENGINE_ASSERT`) if called off the owning thread or between `BeginFrame` and `EndFrame`; documented in the RHI thread-safety contract (`docs/guide/rhi.md`).
- [ ] Crash-report state from Phase 6: the host registers the current epoch and reset count as crash-state providers, replacing the adapter provider on each creation.
- [ ] Tests: stale-handle detection across devices (a buffer handle from device A is invalid on device B, every handle kind, null backend and Vulkan); `Recreate` 100 times on the null backend with counts at zero after each; observers called in tier order and once each; a failing create leaves the host empty and reports the reason; counts at zero after destroy on Vulkan (GPU test, skips without GPU, 20 iterations), validation silent; ASan and TSan pass; death test for `Recreate` inside a frame.
- [ ] **Documentation:** `///` comments with labels (`Aborts:` for the frame and thread rules, `Fails:` for creation); `rhi.md` gets "Lifecycle and reset" with a tagged sample; the new header is in `modules.txt`; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected`, then `--flavor asan` and `--flavor tsan` (lifetime code); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on destruction order and handle aliasing; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** a device can be destroyed and re-created through `DeviceHost` any number of times on both backends with zero live resources between iterations, handles from earlier devices are always detected, the thread and frame rules are enforced, and the lifecycle section of the RHI guide is committed.
