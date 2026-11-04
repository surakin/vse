# Phase 3 / Day 53 — Buffers, Staging & Upload Path

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 52.

First real resources: buffers, CPU access, and the staged upload path that every later asset relies on.

## Tasks

- [ ] Vulkan implementation of `CreateBuffer/DestroyBuffer`: usage mapping, memory from Day 52, debug names via `VK_EXT_debug_utils`; `Map/Unmap` valid only for `CpuToGpu`/`GpuToCpu` buffers (null device already enforces the same rule); invalid descriptors return the invalid handle and log, matching the null device's behavior.
- [ ] Deferred destruction: `DestroyBuffer` retires the buffer with the current frame number; actual `vkDestroyBuffer` + free happens in `CollectRetired(completed)`; shutdown (`WaitIdle` then collect all) is leak-checked.
- [ ] Upload manager: a persistently mapped staging ring (default 16 MiB, grows by block), `UploadBuffer(dst, offset, data, size)` copies into the ring and records a copy on an internal transfer command list submitted on the transfer queue (graphics queue if shared), signaling a timeline semaphore value the caller can wait on (`UploadTicket`); large uploads chunk through the ring; ring space is reclaimed when its timeline value completes.
- [ ] Readback: `ReadbackBuffer(src, offset, size, dst)` via a `GpuToCpu` staging buffer + fence wait (synchronous helper for tests/tools only; documented as such).
- [ ] Queue ownership: when the transfer family differs from graphics, buffers are created `VK_SHARING_MODE_EXCLUSIVE` and the upload path emits the release/acquire pair — or uses `CONCURRENT` sharing; decide, document, and test the chosen path.
- [ ] Tests `[gpu]` (headless, validation-clean): upload N random bytes of several sizes (1 B, 4 KiB, 1 MiB, larger than the ring) to a device-local buffer and read them back identical; interleaved uploads from two threads serialize correctly (device mutex); destroy-while-in-flight does not touch freed memory (TSan/ASan + validation); retired buffers are freed only after their frame completes.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `CreateBuffer`, `UploadBuffer`, `UploadTicket`, `ReadbackBuffer` are documented with Ownership/Fails labels; guide section "Uploading data".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** data survives a round trip CPU → staging → device-local → readback at every tested size, with deferred destruction and no validation messages.
