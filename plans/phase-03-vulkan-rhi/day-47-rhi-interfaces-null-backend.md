# Phase 3 / Day 47 — Device Interface, Handle Pool & Null Backend

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 46.

The abstract interfaces plus a backend that validates everything and draws nothing — it lets the upper layers and all validation logic be tested with no GPU, and proves the interface is usable before Vulkan is written.

## Tasks

- [ ] `engine/rhi/device.h`: `Device` abstract class — resource creation/destruction (`CreateBuffer/Texture/Sampler/Shader/BindGroupLayout/BindGroup/GraphicsPipeline/ComputePipeline` and `Destroy*` taking handles), `Caps()`, `CreateCommandList()`, `Submit(CommandList**, count, SubmitInfo)` with fence/semaphore parameters (types declared here, implemented Day 54), `WaitIdle()`, `DeviceLost()`, `BeginFrame()/EndFrame()` frame index hooks, `Map/Unmap` for CPU-visible buffers.
- [ ] `engine/rhi/command_list.h`: `CommandList` abstract class — `Begin/End`, `CopyBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer`, `Barrier(resource, before, after)`, `BeginRendering(RenderingDesc)/EndRendering`, `SetPipeline`, `SetBindGroup(slot, group)`, `SetPushConstants`, `SetViewport/Scissor`, `SetVertexBuffer/IndexBuffer`, `Draw/DrawIndexed`, `Dispatch`, debug-label push/pop. Declarations only.
- [ ] `engine/rhi/swapchain.h`: `Swapchain` abstract class (`Acquire`, `Present`, `Resize`, `Format`, `Extent`, `CurrentTexture`) — declarations; native-surface input type declared (`NativeWindowInfo`, implemented Day 48).
- [ ] `engine/rhi/handle_pool.h`: `HandlePool<T, Tag>` — slot array + generation counters + free list; `Allocate`, `Free`, `Get(handle)` returning null for a stale/invalid handle, capacity growth through the allocator; `Retire(handle, frame)` + `CollectRetired(completed_frame)` implementing the deferred-destruction queue of the design spec. Not thread-safe by itself; the device serializes creation/destruction behind a mutex (decision recorded).
- [ ] `engine/rhi/null/null_device.{h,cpp}` (`rhi` library): `CreateNullDevice()`; validates every descriptor and every command-list call against the interface contract (use-after-destroy, wrong state, draw without pipeline, bind group/layout mismatch, mapping a GPU-only buffer) and reports through `LogRhi` plus a retrievable error counter; stores buffer contents in host memory so copy/map round trips work.
- [ ] Tests: handle pool stale-handle detection, reuse bumps the generation, retired slots are not recycled before their frame completes; null device accepts a legal sequence (create → record → submit) and flags each illegal one (one test per rule) exactly once.
- [ ] Make the null device the default for non-`[gpu]` tests; add `rhi_tests_asan`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `Device`, `CommandList`, `Swapchain` and `HandlePool` are documented with `Thread-safety:`/`Ownership:`/`Aborts:` per the strict rules; the guide gains "Devices, command lists and lifetimes".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a complete legal command sequence runs on the null device; each documented misuse is caught and counted; stale handles never dereference; ASan clean.
