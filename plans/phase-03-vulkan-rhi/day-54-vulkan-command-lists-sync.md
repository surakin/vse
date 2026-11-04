# Phase 3 / Day 54 — Command Lists, Submission & Synchronization

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 53.

Recording and submitting work, with the portable fence/semaphore abstractions and multi-threaded recording through the job system.

## Tasks

- [ ] `VulkanCommandList` implementing the Day 47 interface for copies, barriers, debug labels and dispatch-free recording; one `VkCommandPool` per (jobs participant, frame in flight) so recording never locks; `CreateCommandList()` hands out a list from the calling thread's pool (participant index via `jobs::CurrentParticipantIndex()`, a fallback pool for non-participant threads); pools reset at the start of their frame.
- [ ] RHI synchronization types finalized: `Fence` (CPU-waitable, built on the device timeline semaphore: `FenceValue` monotonically increasing), `GpuSemaphore` (binary, for swapchain acquire/present only, hidden behind the swapchain API); `SubmitInfo{ wait fences/values, signal fences/values }`; `Device::SignalFence/WaitFence(value, timeout)`.
- [ ] `Barrier(resource, before, after)` implementation: the `ResourceState` → (stage, access, layout) table in one place, expressed as `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2`; a unit-tested pure function; `Undefined → X` for images discards contents.
- [ ] `Submit` implementation with `vkQueueSubmit2`, timeline waits/signals, and a submission mutex (single submission thread rule enforced + documented).
- [ ] Device-lost handling: `VK_ERROR_DEVICE_LOST` from any call sets the sticky `DeviceLost()` state, logs once with context, and makes later submits no-ops returning an error instead of crashing.
- [ ] Multi-threaded recording test: record M command lists in parallel as jobs on the Phase 2 scheduler (`ParallelFor`), each doing a distinct buffer copy, submit them in order from the main thread, verify all destinations (TSan target `rhi_tests_tsan` added today).
- [ ] Tests: barrier table cases against hand-derived expectations; fence value monotonicity; `[gpu]` timeline wait with timeout returns on completion and times out when never signaled; device-lost path exercised through a test-only fault-injection hook on the fake submit.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** command-list, fence and submit APIs documented with Thread-safety (recording `thread-compatible`, submit `owner-thread-only` or mutex-guarded as decided) and Aborts labels; guide section "Recording and submitting".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** command lists are recorded on many threads concurrently and submitted in order, fences work, barriers come from one table, and TSan/ASan/validation are clean.
