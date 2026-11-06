# Phase 4 / Day 52 — GPU Memory Allocator

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 51.

No VMA: a small purpose-built suballocator, because `vkAllocateMemory` count is limited and per-resource allocation does not scale.

## Tasks

- [x] `engine/rhi/vulkan/vk_memory.{h,cpp}`: memory-type selection from `MemoryLocation` and the resource's `memoryTypeBits` (prefer device-local for `GpuOnly`, host-visible+coherent for `CpuToGpu`, host-cached for `GpuToCpu`, with documented fallbacks, e.g. resizable-BAR / unified memory).
- [x] Block allocator: per memory type a list of large blocks (default 64 MiB; 256 MiB for device-local on large heaps; capped by `maxMemoryAllocationCount` headroom), each managed by an offset free-list with coalescing; alignment and `bufferImageGranularity` respected (linear vs optimal resources never share a granularity page unless safe).
- [x] Dedicated allocations for large resources (> a threshold) and when the driver asks (`VkMemoryDedicatedRequirements`).
- [x] Persistent mapping of host-visible blocks (mapped once per block); `Allocation` struct (block, offset, size, mapped pointer); `Free` returns space and releases empty blocks lazily (keep one spare).
- [x] Statistics: bytes allocated/used per heap, block count, fragmentation estimate; budget check via `VK_EXT_memory_budget` when present; log on exhaustion with the heap breakdown.
- [x] Failure policy: allocation failure returns an empty `Allocation` (the resource creation then fails with an invalid handle and a log line) — never an abort; out-of-device-memory is distinguished from out-of-host-memory.
- [x] Tests (pure, no GPU): the free-list/block logic runs against a fake memory backend — alignment, coalescing, exhaustion, double free assert, 100k random alloc/free cycles keep invariants (no overlap, total bytes conserved). `[gpu]`: real allocations of each location kind; a 1 GiB-ish sweep of small allocations stays within a handful of `vkAllocateMemory` calls (assert the count).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** allocator stats and the failure policy are documented under "GPU memory" in the guide; the internal types are documented as `rhi/vulkan` members.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** suballocation works against both the fake and the real driver with coalescing and correct alignment, and a flood of small allocations needs only a few real memory objects.

## Result

`vk_array.h` (heap `GrowArray` for backend bookkeeping), `vk_memory.{h,cpp}` (`SelectMemoryType`, `FreeList`, `MemoryAllocator`, `MemoryStats`, all independent of Vulkan types), `vk_memory_backend.{h,cpp}` (`VulkanMemoryBackend`), wired into `VulkanDevice` (`Memory()`, `HeapBudget`, `VK_EXT_memory_budget` enabled when available). Pure tests run against a fake backend: preferences and fallbacks, alignment, coalescing, double free and exhaustion, 100k random cycles, spare block, dedicated, failure reasons, count limit. Real-driver tests allocate each location kind and push 512 MiB of 256 KiB suballocations through fewer than 16 `vkAllocateMemory` calls. 114 RHI test cases pass plain and under ASan; docs 1455/1455. `bufferImageGranularity` is satisfied by keeping linear and optimal resources in separate blocks, not by per-neighbor tracking.

## Post-implementation code review

`/code-review high`, 9 findings:

1. **Dedicated objects not freed at teardown** — fixed: every live dedicated object is tracked and freed by the destructor (test).
2. **Double free of a dedicated allocation reached the driver** — fixed: a free is looked up first and aborts if unknown.
3. **`allocation_headroom` contradicted its documentation** — fixed in docs: it is a margin for objects allocated outside the allocator (swapchain images, driver-internal); the allocator itself, dedicated objects included, stops at the reduced limit.
4. **`non_coherent_atom_size` unused** — fixed: ranges in non-coherent host-visible memory are rounded to the atom (test).
5. **"Baseline grew from 137 to 235"** — declined: the baseline stores documented/total symbol counts (235/235 is 100% coverage), not parked findings; lint reports zero known findings.
6. **`__is_pointer` and missing `<cstdint>`** — fixed: `std::is_pointer_v`.
7. **`vkMapMemory` failure ignored device loss and out-of-memory kinds** — fixed.
8. **Failed free accounting continued when asserts are off** — fixed: returns without touching the counters.
9. **A fresh block that could not satisfy the request leaked** — fixed: alignment at or above the dedicated threshold routes to a dedicated object (test), and the impossible-fit path releases the block.
