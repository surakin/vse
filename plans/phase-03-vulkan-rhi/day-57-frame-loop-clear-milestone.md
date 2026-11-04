# Phase 3 / Day 57 — Frame Loop, Frames in Flight & the Clear-Color Window

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 56.

The first on-screen result and the frame pacing machinery everything else sits on.

## Tasks

- [x] `FrameContext`/`FrameRing` in the RHI: N = 2 frames in flight (configurable 1-3), each owning a command-pool set, upload ring slice, acquire/render-finished semaphores and a timeline fence value; `Device::BeginFrame()` waits for the frame N-back to finish, resets its pools, runs `CollectRetired(completed_frame)`, advances the frame number; `EndFrame()` submits and presents.
- [x] The retire queue is wired for real: resources destroyed during frame F are freed when frame F's fence completes (unit-tested on the null device with a fake fence, then `[gpu]`).
- [x] `game/rhi_demo/` (new dogfood app, built as `rhi_demo`): opens a window (backend from the platform layer), creates a device + swapchain, and each frame clears the swapchain image to a color that animates with time using `BeginRendering` (clear load op) / `EndRendering`; closes cleanly on window close; logs frame time every second.
- [x] Resize and minimize handled in the loop using Day 56 statuses; shutdown order (`WaitIdle` → destroy resources → swapchain → device → instance) asserted by a leak counter that reports any unreleased Vulkan object at exit (debug builds) via per-type live counts.
- [x] Tests `[gpu][window]`: run 120 frames headless-ish (real window if available) with validation fatal, assert zero validation messages and that at most N frames were ever in flight; leak counters are zero after teardown; a test with frames-in-flight = 1 and = 3 behaves identically.
- [x] Manual check recorded in the day file: the demo shows the animated clear on Wayland and on X11 (screenshots described, not committed).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the frame-loop contract (what may be touched between BeginFrame/EndFrame, who owns what) is documented; the tutorial skeleton `docs/tutorials/hello-rhi.md` is created (steps up to the clear color, with tagged samples where they can compile).
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a window animates a clear color on both display backends with validation clean and no leaked Vulkan objects at shutdown.

## Result

`DeviceCreateInfo::frames_in_flight` (1 to 3, default 2) paces `BeginFrame`: it waits for the frame that many back, resets that frame's command pools and releases retired resources, so `FrameNumber() - CompletedFrame() <= frames_in_flight` after it returns. `VulkanCommandList::BeginRendering/EndRendering` use dynamic rendering (layout `COLOR_ATTACHMENT_OPTIMAL`, load/store ops, clear colors, default viewport and scissor from the attachment extent); barriers, copies, `GenerateMips` and `End` assert they are not inside a pass. `VulkanDevice::GetLeakCounts/ResetLeakCounts` count user objects (buffers, textures, samplers, a forgotten swapchain) and the backend's raw buffers/images alive at device destruction. `game/rhi_demo` animates a clear on a real window. Tests (`tests/rhi/vulkan_frame_test.cpp`): an offscreen clear read back as pixels (red clear gives RGBA 255,0,0,255 everywhere), and a 120-frame loop on Wayland and X11 with frames in flight 1, 2 and 3, sync validation, zero validation errors/warnings, frames in flight bounded, all frames completed, zero leaks. The null-device retire queue with `simulated_latency_frames` was already tested on Day 50 (`null_device_test.cpp`). Docs 1597/1597; guide "The frame loop" and tutorial `hello-rhi.md` (its tagged null-device sample compiles and runs).

Manual check (an agent cannot see the screen): `rhi_demo` with `RHI_DEMO_FRAMES=240`, fatal validation, exited 0 on `ENGINE_WINDOW_BACKEND=wayland` and `x11` with no validation output. Wayland logged 20 frames/s (50 ms/frame) against X11's 59-65 frames/s at 60 Hz: the compositor throttles frame callbacks for a window it considers not visible (the dev session's window was not focused or shown); the animation itself was verified through the pixel readback, not by eye. Someone should look at the window once.

Deviations: `EndFrame` does not submit or present; the application calls `Submit` and `Swapchain::Present` explicitly (Day 54/56 design), and the staging ring is shared rather than sliced per frame (uploads fence-track their own slices). The leak counters cover user objects plus raw buffers/images, not every Vulkan type; the validation layer's object-leak report covers the rest.

## Post-implementation code review

`/code-review high` returned 8 findings.

Fixed:
1. The stencil attachment copy had a dead ternary: simplified, and the stencil-follows-depth behavior is documented on `DepthAttachmentDesc`.
3. The frame test did not assert zero validation messages or equal frame counts across settings, and had an unused parameter: it now asserts zero errors and warnings and equal frames for 1, 2 and 3 frames in flight.
5. The demo spun on `Minimized` and could never reach `RHI_DEMO_FRAMES`: it naps 10 ms and counts the frame.
6. Float-only clear colors and mip 0 / layer 0 rendering were undocumented: documented on `ColorAttachmentDesc`.
8. STATUS.md, this file and the docs were pending: done in this commit.

Declined:
2. "Internal leak counter covers only two types": the day's wording asked for per-type counts, but the validation layer already reports every undestroyed object at `vkDestroyDevice`/`vkDestroyInstance` and fatal validation turns that into a failure; the guide states what the counters cover.
4. "Leak counts are process-global and reset once": tests in this binary run sequentially, the only device left undestroyed on purpose is the null one, and the full suite passes in any order; making it per-device would change the destructor-time reporting for no gain.
7. "The null backend ignores `frames_in_flight` and the retire test is absent": the null device models latency with `simulated_latency_frames` and the retire-queue test is `null_device_test.cpp:896`.
