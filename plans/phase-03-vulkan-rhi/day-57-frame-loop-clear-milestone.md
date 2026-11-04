# Phase 3 / Day 57 — Frame Loop, Frames in Flight & the Clear-Color Window

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 56.

The first on-screen result and the frame pacing machinery everything else sits on.

## Tasks

- [ ] `FrameContext`/`FrameRing` in the RHI: N = 2 frames in flight (configurable 1-3), each owning a command-pool set, upload ring slice, acquire/render-finished semaphores and a timeline fence value; `Device::BeginFrame()` waits for the frame N-back to finish, resets its pools, runs `CollectRetired(completed_frame)`, advances the frame number; `EndFrame()` submits and presents.
- [ ] The retire queue is wired for real: resources destroyed during frame F are freed when frame F's fence completes (unit-tested on the null device with a fake fence, then `[gpu]`).
- [ ] `game/rhi_demo/` (new dogfood app, built as `rhi_demo`): opens a window (backend from the platform layer), creates a device + swapchain, and each frame clears the swapchain image to a color that animates with time using `BeginRendering` (clear load op) / `EndRendering`; closes cleanly on window close; logs frame time every second.
- [ ] Resize and minimize handled in the loop using Day 56 statuses; shutdown order (`WaitIdle` → destroy resources → swapchain → device → instance) asserted by a leak counter that reports any unreleased Vulkan object at exit (debug builds) via per-type live counts.
- [ ] Tests `[gpu][window]`: run 120 frames headless-ish (real window if available) with validation fatal, assert zero validation messages and that at most N frames were ever in flight; leak counters are zero after teardown; a test with frames-in-flight = 1 and = 3 behaves identically.
- [ ] Manual check recorded in the day file: the demo shows the animated clear on Wayland and on X11 (screenshots described, not committed).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the frame-loop contract (what may be touched between BeginFrame/EndFrame, who owns what) is documented; the tutorial skeleton `docs/tutorials/hello-rhi.md` is created (steps up to the clear color, with tagged samples where they can compile).
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a window animates a clear color on both display backends with validation clean and no leaked Vulkan objects at shutdown.
