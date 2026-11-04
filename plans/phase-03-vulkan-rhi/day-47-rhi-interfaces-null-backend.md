# Phase 3 / Day 47 — Device Interface, Handle Pool & Null Backend

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 46.

The abstract interfaces plus a backend that validates everything and draws nothing — it lets the upper layers and all validation logic be tested with no GPU, and proves the interface is usable before Vulkan is written.

## Tasks

- [x] `engine/rhi/device.h`: `Device` abstract class — resource creation/destruction (`CreateBuffer/Texture/Sampler/Shader/BindGroupLayout/BindGroup/GraphicsPipeline/ComputePipeline` and `Destroy*` taking handles), `Caps()`, `CreateCommandList()`, `Submit(CommandList**, count, SubmitInfo)` with fence/semaphore parameters (types declared here, implemented Day 54), `WaitIdle()`, `DeviceLost()`, `BeginFrame()/EndFrame()` frame index hooks, `Map/Unmap` for CPU-visible buffers.
- [x] `engine/rhi/command_list.h`: `CommandList` abstract class — `Begin/End`, `CopyBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer`, `Barrier(resource, before, after)`, `BeginRendering(RenderingDesc)/EndRendering`, `SetPipeline`, `SetBindGroup(slot, group)`, `SetPushConstants`, `SetViewport/Scissor`, `SetVertexBuffer/IndexBuffer`, `Draw/DrawIndexed`, `Dispatch`, debug-label push/pop. Declarations only.
- [x] `engine/rhi/swapchain.h`: `Swapchain` abstract class (`Acquire`, `Present`, `Resize`, `ImageFormat`, `Width/Height`, `CurrentTexture`) and `SwapchainStatus`. The creation function (which needs the Day 48 `os::NativeWindowInfo`) is added with the window integration on Day 56, so no placeholder window type is declared today.
- [x] `engine/rhi/handle_pool.h`: `HandlePool<T, Tag>` — slot array + generation counters + free list; `Allocate`, `Free`, `Get(handle)` returning null for a stale/invalid handle, capacity growth through the allocator; `Retire(handle, frame)` + `CollectRetired(completed_frame)` implementing the deferred-destruction queue of the design spec. Not thread-safe by itself; the device serializes creation/destruction behind a mutex (decision recorded).
- [x] `engine/rhi/null/null_device.{h,cpp}` (`rhi` library): `CreateNullDevice()`; validates every descriptor and every command-list call against the interface contract (use-after-destroy, wrong state, draw without pipeline, bind group/layout mismatch, mapping a GPU-only buffer) and reports through `LogRhi` plus a retrievable error counter; stores buffer contents in host memory so copy/map round trips work.
- [x] Tests: handle pool stale-handle detection, reuse bumps the generation, retired slots are not recycled before their frame completes; null device accepts a legal sequence (create → record → submit) and flags each illegal one (one test per rule) exactly once.
- [x] Make the null device the default for non-`[gpu]` tests; add `rhi_tests_asan`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `Device`, `CommandList`, `Swapchain` and `HandlePool` are documented with `Thread-safety:`/`Ownership:`/`Aborts:` per the strict rules; the guide gains "Devices, command lists and lifetimes".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a complete legal command sequence runs on the null device; each documented misuse is caught and counted; stale handles never dereference; ASan clean.

## Result

- `engine/rhi/{device,command_list,swapchain,handle_pool}.h`, `null/null_device.{h,cpp}`: `Device`, `CommandList`, `Swapchain` interfaces; `HandlePool` (chunked slots, generation checks, deferred-destruction queue); `NullDevice` that validates descriptors, handles, scoping, bindings and states and reports through `LogRhi` plus an error counter. 71 RHI test cases (341k assertions) pass plain and under ASan; mutation spot-checks (disabling the map and layout-match checks) make the tests fail.
- State checking happens at `Submit` (list order is only known then); CPU-visible buffers are exempt from state tracking (documented in the design spec, section 2).
- Deviations: `Swapchain` has no creation function yet (it needs the Day 48 window handle; added Day 56); `BindGroupDesc`/`BindGroupEntry` were added to `desc.h` because the interface needs them; `Submit` returns a fence value and `WaitFence/CompletedFence` replace the `SubmitInfo` struct (Day 54 can extend with wait values).
- Docs: `rhi` module 516/516, null backend header included in the module; guide section "Devices, command lists and lifetimes" with a runnable sample; `docs` target links `librhi`.
- Docgen fix found on the way: `docgen samples` resolved the compiler path with `realpath`, turning `/usr/bin/clang++` into `clang-23`, which runs the driver in C mode (no C++ runtime) - the first sample that needed `operator new` exposed it. New `AbsoluteKeepingLinks` + test.

## Post-implementation code review

`/code-review high` found 9 issues:

1. `BeginFrame` deleted command lists, so submitting a pointer kept across frames was a use-after-free and the "earlier frame" error was unreachable - **fixed**: lists are pooled and recycled; a stale pointer now reports "not ended" (regression: submit-stale-pointer case, run under ASan). The dead frame check was removed.
2. `CreateBindGroupLayout` summed binding counts in 32 bits, which could wrap past the budget check and overflow the 64-entry group array - **fixed**: the sum is 64-bit and `Validate()` caps array size at `kMaxBindingArraySize` (256); tests for both.
3. `CreateNullDevice` replaced a caller's `max_sampler_anisotropy` of 1.0 with 16 - **fixed**: the 16 is now the `NullDeviceOptions` default, the caller's value is honored (test).
4. A graphics pipeline bound in one pass stayed bound in the next pass, bypassing the format-match check - **fixed**: the graphics binding ends with the pass (documented on `SetPipeline`, test).
5. Buffer and texture barriers treated `before == Undefined` differently - **fixed**: `Undefined` as `before` is always accepted for both (documented, test).
6. `Submit` crashed on `(nullptr, n)` and accepted submissions outside a frame - **fixed** with errors and tests.
7. `Error()` copied uninitialized stack bytes into `last_error` - **fixed** with a bounded copy.
8. Texture copies did not check Vulkan's offset/row-pitch alignment - **fixed** (multiples of the texel size; tests, documented in the `Aborts:` text).
9. STATUS.md and the review record - **fixed** in this commit.

