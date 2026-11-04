# Phase 3 / Day 51 — Physical Device Selection, Queues & Logical Device

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 50.

Pick a GPU, find its queues, create the device, and report capabilities in RHI terms.

## Tasks

- [x] Day 49 review carry-over: `CheckInstanceVersion` only checks the loader's instance version, so selection must also reject any device whose `VkPhysicalDeviceProperties::apiVersion` is below 1.3 with a clear logged message (part of the pure selection function and its tests).
- [x] Enumerate physical devices; query properties, features (1.1-1.3 structs), memory properties, queue families, and required device extensions (`VK_KHR_swapchain` for presentable devices).
- [x] Selection policy as a pure, unit-testable function over plain-data device descriptions: reject devices lacking 1.3 / dynamic rendering / synchronization2 / timeline semaphores / a graphics+compute queue; rank discrete > integrated > virtual > CPU; honor `ENGINE_RHI_GPU=<index|name substring>` override; for presentable devices require a queue family able to present to the actual surface (`vkGetPhysicalDeviceSurfaceSupportKHR`). Log the full ranking.
- [x] Queue plan: one graphics+compute+present queue family; a dedicated transfer family if one exists (else share); expose `QueueFamilies` plus queue handles; document the single-submission-thread rule from the design spec.
- [x] Create `VkDevice` enabling exactly the features used (dynamic rendering, synchronization2, timeline semaphore, `samplerAnisotropy` if available, `shaderInt64` not requested); load `VkDeviceFns`.
- [x] Fill `DeviceCaps` from the real limits (max image dimension, push-constant size, uniform/storage buffer offset alignments, timestamp period, best supported depth format); log them.
- [x] `CreateVulkanDevice(const DeviceCreateInfo&)` factory behind `engine/rhi/rhi.h` (`CreateDevice(Backend::Vulkan, ...)`), headless by default (no surface).
- [x] Tests: the selection function with synthetic device lists (discrete beats integrated, missing feature rejected, override honored, no suitable device → error with reasons); `[gpu]`: a real device is created headless, caps are plausible, `WaitIdle` succeeds, destruction is validation-clean.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `CreateDevice`, `DeviceCreateInfo`, `DeviceCaps` and the `ENGINE_RHI_GPU` switch are documented in the reference and guide.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a headless Vulkan device is created through the RHI factory with correct caps, and the selection policy is covered by synthetic-input unit tests.

## Result

`rhi.h` (`Backend`, `ValidationSetting`, `DeviceCreateInfo`, `CreateDevice`, defined in the `rhi_vulkan` library), `vk_device_select` (pure `RejectionReason`/`SelectPhysicalDevice`/`PlanQueues`), `vk_format` (the full `Format`↔`VkFormat` table, also covering the format-mapping task of Day 55), `VulkanDevice` (instance, selection, queue, features, caps, `WaitIdle`). Queue decision: every submission, uploads included, goes to the single graphics+compute queue; the dedicated transfer family is only recorded in the plan, so no ownership transfers exist in Phase 3. Resource creation, command lists and fences are logged "not implemented yet" stubs that Days 53–57 replace; `WaitFence(0)` already reports complete. 97 RHI test cases pass plain and under ASan with `ENGINE_RHI_REQUIRE_GPU=1` on the dev NVIDIA GPU; docs 1356/1356.

## Post-implementation code review

`/code-review high`, 8 findings:

1. **The default `CreateDevice` returns a stub** — partly fixed (`WaitFence(0)` is complete); the rest declined as intended transitional state: the stubs log an error when used and are replaced by Days 53–57 of this same phase, before anything depends on them.
2. **Second GPU test skipped instead of failing under `ENGINE_RHI_REQUIRE_GPU=1`** — fixed.
3. **Timestamps claimed without checking `timestampValidBits`** — fixed: recorded per queue family; the period is 0 if the graphics family cannot write timestamps.
4. **`ENGINE_RHI_GPU=3060` treated as an out-of-range index** — fixed: a number that is not a valid index is matched against names (test added).
5. **Depth format silently defaulted** — fixed: an error is logged when no candidate works.
6. **Duplicated extension enumeration; `VK_INCOMPLETE` treated as failure** — `VK_INCOMPLETE` fixed; sharing one helper declined (instance- vs device-level enumeration use different entry points and handles; the duplicated part is a dozen lines).
7. **Linear scan in `ToVkFormat`** — fixed: indexed by enum with a `static_assert` on the table size.
8. Bookkeeping — done in this commit.
