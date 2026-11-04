# Phase 3 / Day 51 — Physical Device Selection, Queues & Logical Device

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 50.

Pick a GPU, find its queues, create the device, and report capabilities in RHI terms.

## Tasks

- [ ] Day 49 review carry-over: `CheckInstanceVersion` only checks the loader's instance version, so selection must also reject any device whose `VkPhysicalDeviceProperties::apiVersion` is below 1.3 with a clear logged message (part of the pure selection function and its tests).
- [ ] Enumerate physical devices; query properties, features (1.1-1.3 structs), memory properties, queue families, and required device extensions (`VK_KHR_swapchain` for presentable devices).
- [ ] Selection policy as a pure, unit-testable function over plain-data device descriptions: reject devices lacking 1.3 / dynamic rendering / synchronization2 / timeline semaphores / a graphics+compute queue; rank discrete > integrated > virtual > CPU; honor `ENGINE_RHI_GPU=<index|name substring>` override; for presentable devices require a queue family able to present to the actual surface (`vkGetPhysicalDeviceSurfaceSupportKHR`). Log the full ranking.
- [ ] Queue plan: one graphics+compute+present queue family; a dedicated transfer family if one exists (else share); expose `QueueFamilies` plus queue handles; document the single-submission-thread rule from the design spec.
- [ ] Create `VkDevice` enabling exactly the features used (dynamic rendering, synchronization2, timeline semaphore, `samplerAnisotropy` if available, `shaderInt64` not requested); load `VkDeviceFns`.
- [ ] Fill `DeviceCaps` from the real limits (max image dimension, push-constant size, uniform/storage buffer offset alignments, timestamp period, best supported depth format); log them.
- [ ] `CreateVulkanDevice(const DeviceCreateInfo&)` factory behind `engine/rhi/rhi.h` (`CreateDevice(Backend::Vulkan, ...)`), headless by default (no surface).
- [ ] Tests: the selection function with synthetic device lists (discrete beats integrated, missing feature rejected, override honored, no suitable device → error with reasons); `[gpu]`: a real device is created headless, caps are plausible, `WaitIdle` succeeds, destruction is validation-clean.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `CreateDevice`, `DeviceCreateInfo`, `DeviceCaps` and the `ENGINE_RHI_GPU` switch are documented in the reference and guide.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a headless Vulkan device is created through the RHI factory with correct caps, and the selection policy is covered by synthetic-input unit tests.
