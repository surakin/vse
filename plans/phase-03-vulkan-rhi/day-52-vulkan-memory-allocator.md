# Phase 3 / Day 52 — GPU Memory Allocator

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 51.

No VMA: a small purpose-built suballocator, because `vkAllocateMemory` count is limited and per-resource allocation does not scale.

## Tasks

- [ ] `engine/rhi/vulkan/vk_memory.{h,cpp}`: memory-type selection from `MemoryLocation` and the resource's `memoryTypeBits` (prefer device-local for `GpuOnly`, host-visible+coherent for `CpuToGpu`, host-cached for `GpuToCpu`, with documented fallbacks, e.g. resizable-BAR / unified memory).
- [ ] Block allocator: per memory type a list of large blocks (default 64 MiB; 256 MiB for device-local on large heaps; capped by `maxMemoryAllocationCount` headroom), each managed by an offset free-list with coalescing; alignment and `bufferImageGranularity` respected (linear vs optimal resources never share a granularity page unless safe).
- [ ] Dedicated allocations for large resources (> a threshold) and when the driver asks (`VkMemoryDedicatedRequirements`).
- [ ] Persistent mapping of host-visible blocks (mapped once per block); `Allocation` struct (block, offset, size, mapped pointer); `Free` returns space and releases empty blocks lazily (keep one spare).
- [ ] Statistics: bytes allocated/used per heap, block count, fragmentation estimate; budget check via `VK_EXT_memory_budget` when present; log on exhaustion with the heap breakdown.
- [ ] Failure policy: allocation failure returns an empty `Allocation` (the resource creation then fails with an invalid handle and a log line) — never an abort; out-of-device-memory is distinguished from out-of-host-memory.
- [ ] Tests (pure, no GPU): the free-list/block logic runs against a fake memory backend — alignment, coalescing, exhaustion, double free assert, 100k random alloc/free cycles keep invariants (no overlap, total bytes conserved). `[gpu]`: real allocations of each location kind; a 1 GiB-ish sweep of small allocations stays within a handful of `vkAllocateMemory` calls (assert the count).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** allocator stats and the failure policy are documented under "GPU memory" in the guide; the internal types are documented as `rhi/vulkan` members.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** suballocation works against both the fake and the real driver with coalescing and correct alignment, and a flood of small allocations needs only a few real memory objects.
