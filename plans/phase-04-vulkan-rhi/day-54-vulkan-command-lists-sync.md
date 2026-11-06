# Phase 4 / Day 54 — Command Lists, Submission & Synchronization

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 53.

Recording and submitting work, with the portable fence/semaphore abstractions and multi-threaded recording through the job system.

## Tasks

- [x] `VulkanCommandList` implementing the Day 47 interface for copies, barriers, debug labels and dispatch-free recording; one `VkCommandPool` per (jobs participant, frame in flight) so recording never locks; `CreateCommandList()` hands out a list from the calling thread's pool (participant index via `jobs::CurrentParticipantIndex()`, a fallback pool for non-participant threads); pools reset at the start of their frame.
- [x] RHI synchronization types finalized: `Fence` (CPU-waitable, built on the device timeline semaphore: `FenceValue` monotonically increasing), `GpuSemaphore` (binary, for swapchain acquire/present only, hidden behind the swapchain API); `SubmitInfo{ wait fences/values, signal fences/values }`; `Device::SignalFence/WaitFence(value, timeout)`.
- [x] `Barrier(resource, before, after)` implementation: the `ResourceState` → (stage, access, layout) table in one place, expressed as `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2`; a unit-tested pure function; `Undefined → X` for images discards contents.
- [x] `Submit` implementation with `vkQueueSubmit2`, timeline waits/signals, and a submission mutex (single submission thread rule enforced + documented).
- [x] Device-lost handling: `VK_ERROR_DEVICE_LOST` from any call sets the sticky `DeviceLost()` state, logs once with context, and makes later submits no-ops returning an error instead of crashing.
- [x] Multi-threaded recording test: record M command lists in parallel as jobs on the Phase 2 scheduler (`ParallelFor`), each doing a distinct buffer copy, submit them in order from the main thread, verify all destinations (TSan target `rhi_tests_tsan` added today).
- [x] Tests: barrier table cases against hand-derived expectations; fence value monotonicity; `[gpu]` timeline wait with timeout returns on completion and times out when never signaled; device-lost path exercised through a test-only fault-injection hook on the fake submit.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** command-list, fence and submit APIs documented with Thread-safety (recording `thread-compatible`, submit `owner-thread-only` or mutex-guarded as decided) and Aborts labels; guide section "Recording and submitting".
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** command lists are recorded on many threads concurrently and submitted in order, fences work, barriers come from one table, and TSan/ASan/validation are clean.

## Result

Added `vk_barrier` (`GetBarrierScopes`, the single state to stage/access/layout table), `VulkanCommandList` (copies, buffer barriers, debug labels; texture, pipeline and draw commands log "not implemented" until their resources exist) and `vulkan_device_commands.cpp` (per-thread command pools in a 3-frame ring, `CreateCommandList`, `Submit` with `vkQueueSubmit2` on the timeline semaphore, sticky device-lost handling, `InjectSubmitFailure` fault hook). Tests: 8 barrier-table sections, record/submit/verify, fence monotonicity and timeout, 20-frame pool reuse, 64 lists recorded on a 4-participant scheduler, plain threads with slot recycling past the slot limit, injected device loss, and a GPU-free multithreaded null-device test. All pass plain and under ASan with validation (`ENGINE_RHI_REQUIRE_GPU=1`); the TSan flavor `rhi_tests_tsan` passes all non-GPU tests. Docs 1528/1528; guide section "Recording and submitting".

Deviations from the plan, decided here:
- **Pool slots.** Slots come from a process-wide table of 320 entries that each thread takes on first use and returns on exit, not from `jobs::CurrentParticipantIndex()`: participant indices are only unique within one `WorkerPool` and pools may coexist.
- **`SubmitInfo` / `SignalFence` / `GpuSemaphore`.** Not added. With one queue and in-order submission every wait on an earlier submit is redundant, so explicit wait/signal lists add nothing yet; the binary semaphores belong to the swapchain (Day 56) and stay hidden behind it. Revisit when a second queue exists.
- **TSan vs GPU tests.** `rhi_tests_tsan` registers `~[gpu]` only: the proprietary NVIDIA driver starts threads ThreadSanitizer cannot track and crashes inside its allocator interceptor (`CHECK failed: tsan_interceptors_posix.cpp:2206`), even for a single-threaded GPU test. GPU threading is covered by ASan plus validation and by the null-device multithreaded test under TSan. With a Mesa software driver the whole binary can be run by hand.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed:
1. Pool slots keyed by participant index collided across coexisting worker pools: every thread now takes a slot from the table (see Deviations).
2. `Begin`/`End` ignored Vulkan failures: a failed list is flagged, its commands are skipped, and `Submit` refuses the batch and returns 0.
3. `CopyBuffer` allowed overlapping ranges in one buffer: now an assert.
4. `CreateCommandList` between `EndFrame` and `BeginFrame` used a pool still in flight: it now aborts without an open frame (the null device reports an error; regression test added, confirmed failing without the fix).
5. Contradictory list-lifetime docs: unified as "use it only within the frame it was created in".
6. `BeginFrame` reset pools under `resource_mutex_` and also after a failed wait on a lost device: the reset is now outside the lock and skipped when the wait failed.
8. `Submit` checked list phases outside `submit_mutex_`, so a list submitted twice from two threads would race: validation now runs under the mutex.
9. `PushDebugLabel(nullptr)` reached the driver: now an assert.
10. Stray CMake comment and a mid-class `public:` block removed; STATUS.md and this section updated.

Declined:
7. `LookupBuffer` takes `resource_mutex_` for each copy and barrier. The critical section is a handle-pool lookup and a struct copy, and the pool design makes a lock-free read a change to `HandlePool` that benefits every backend. Revisit with a profile once recording volume is real (Phase 5).
