# Phase 3 / Day 53 — Buffers, Staging & Upload Path

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 52.

First real resources: buffers, CPU access, and the staged upload path that every later asset relies on.

## Tasks

- [x] Vulkan implementation of `CreateBuffer/DestroyBuffer`: usage mapping, memory from Day 52, debug names via `VK_EXT_debug_utils`; `Map/Unmap` valid only for `CpuToGpu`/`GpuToCpu` buffers (null device already enforces the same rule); invalid descriptors return the invalid handle and log, matching the null device's behavior.
- [x] Deferred destruction: `DestroyBuffer` retires the buffer with the current frame number; actual `vkDestroyBuffer` + free happens in `CollectRetired(completed)`; shutdown (`WaitIdle` then collect all) is leak-checked.
- [x] Upload manager: a persistently mapped staging ring (default 16 MiB, grows by block), `UploadBuffer(dst, offset, data, size)` copies into the ring and records a copy on an internal transfer command list submitted on the transfer queue (graphics queue if shared), signaling a timeline semaphore value the caller can wait on (`UploadTicket`); large uploads chunk through the ring; ring space is reclaimed when its timeline value completes.
- [x] Readback: `ReadbackBuffer(src, offset, size, dst)` via a `GpuToCpu` staging buffer + fence wait (synchronous helper for tests/tools only; documented as such).
- [x] Queue ownership: when the transfer family differs from graphics, buffers are created `VK_SHARING_MODE_EXCLUSIVE` and the upload path emits the release/acquire pair — or uses `CONCURRENT` sharing; decide, document, and test the chosen path.
- [x] Tests `[gpu]` (headless, validation-clean): upload N random bytes of several sizes (1 B, 4 KiB, 1 MiB, larger than the ring) to a device-local buffer and read them back identical; interleaved uploads from two threads serialize correctly (device mutex); destroy-while-in-flight does not touch freed memory (TSan/ASan + validation); retired buffers are freed only after their frame completes.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `CreateBuffer`, `UploadBuffer`, `UploadTicket`, `ReadbackBuffer` are documented with Ownership/Fails labels; guide section "Uploading data".
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** data survives a round trip CPU → staging → device-local → readback at every tested size, with deferred destruction and no validation messages.

## Result

Buffers, deferred destruction, a 16 MiB persistently mapped staging ring (`RingAllocator`), chunked `UploadBuffer`, synchronous `ReadbackBuffer`, timeline-semaphore fences and frame tracking are implemented on the Vulkan device; `NullDevice` mirrors the API. Queue ownership: everything runs on the single graphics+compute queue with `VK_SHARING_MODE_EXCLUSIVE` buffers, so no ownership transfers are needed (the transfer family is recorded but unused). Round trips pass at 1 B, 4 KiB, 1 MiB and 40 MiB (larger than the ring), plus two-thread uploads and deferred-destruction tests, plain and under ASan with `ENGINE_RHI_REQUIRE_GPU=1`. Docs 1478/1478; guide sections "Uploading data" and "GPU memory" added.

## Post-implementation code review

`/code-review high` returned 10 findings (all plausible); all were verified.

Fixed:
1. `EndFrame` could miss an upload submitted between reading the last fence and closing the frame: it now closes the frame first, then reads the fence.
2. `UploadBuffer` conflated failure with "already done": `UploadTicket` gained `ok`, and a partial failure returns the last submitted fence.
3. The `CpuToGpu` direct write is not ordered after earlier GPU work: documented as the explicit exemption (use a buffer per frame in flight).
4. A failed submit left the ring chunk unretired and could wedge the ring: it is now retired against the last real submission.
5. Concurrent `DestroyBuffer` and use of the same handle: destruction is deferred until the frame completes, and using a handle while destroying it from another thread remains a documented caller error.
7. Null backend misuse is logged and counted, not aborted; the `Device` docs now say so.
8. Ring: removed the redundant condition and the O(N^2) `Reclaim` erase (`GrowArray::EraseFront`, with a 1000-group test).
9. `FlushRange`/`InvalidateRange` share `SyncRange`; the rounded end is clamped to the allocation, which the allocator keeps atom-aligned.
10. `BeginFrame` no longer waits with the resource mutex held.

Declined:
6. Readback's extra timeline wait and per-call temporary buffer: readback is documented as a synchronous tool/test helper, so the cost is accepted. The redundant wait was removed from the GPU-only path.
