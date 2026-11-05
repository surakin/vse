# Phase 4 / Day 77 — Render Graph III: Execution, Parallel Recording & the Swapchain

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 76.

Run the compiled graph: record passes on the job system, submit once, present.

## Tasks

- [x] Execution: for each pass in order resolve its transient resources, record its barriers and call its callback with a `PassContext` (command list, resolved textures/buffers, the `RenderingDesc` filled from its attachments).
- [x] Parallel recording (the draw-list slicing inside a pass is deferred, see Result): independent passes (and, inside a pass, slices of the draw list) record on `jobs::Scheduler` participants into separate command lists, submitted in graph order in one `Submit`; tested with a fake pass that records its thread id.
- [x] Swapchain integration: acquire/present/resize/minimize/`Recreated` (graph resources sized relative to the swapchain are rebuilt), with the Phase 3 rules (minimized = skip the frame, lost = report).
- [x] Frame orchestration `Renderer::RenderFrame(scene, camera)`: BeginFrame, build graph, compile, execute, EndFrame, present; graph storage is released at the frame boundary; a frame on a lost device returns an error status and leaves the renderer destroyable.
- [x] Tests: ordering of submitted lists; parallel recording gives output identical to serial (pixel compare); 1000 frames with no growth in memory or live RHI objects; a TSan run of the null-device parallel-recording test; an ASan run of the whole renderer test set.
- [x] Failure injection: `FaultInjection` device loss mid-graph and an allocation failure while creating a transient texture both end the frame cleanly (no crash, no leak).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `Renderer`, execution, `PassContext` and the threading contract (what may run concurrently) are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `engine/renderer/render_graph.{h,cpp}`: transient resolution moved ahead of recording (`ResolveTransients`: take at first use, give back after last use, all serial), so passes can record in any order. `Execute` (one list) is built on it; new `ExecuteParallel(pool, device, scheduler, fence)` records one command list per pass (each list created on the thread that records it, with `jobs::ParallelForBlocking`, one pass per chunk; serially without a scheduler), adds a last list for the exit barriers, and submits all lists once in schedule order. Passes that have no callback and no barriers record no list. Failure (no list, lost device, refused submit) rolls the pool back and reports through `ExecuteError()`. `PassExecuteContext` carries the graph, so callbacks resolve their textures/buffers (`ResolvedTexture`/`ResolvedBuffer`) instead of being handed a prefilled `RenderingDesc` (a pass fills it from the handles it resolves; recorded as a deliberate simplification).
- `engine/renderer/renderer.{h,cpp}`: `Renderer` (device, optional swapchain, optional scheduler, arena, pool, frame callback `FrameBuildFn`) and `RenderFrame(scene, camera, offscreen)` with `FrameStatus` {Ok, Skipped, DeviceLost, Failed}. BeginFrame, acquire (Lost, Minimized, Recreated handled; the Phase 3 rules), import the target (swapchain image Undefined to Present, or a caller texture), arena reset, callback, compile, `ExecuteParallel`, EndFrame, present. A frame that fails after acquiring still presents the image (moved to Present) so the swapchain is never left with an outstanding image; the device-lost paths leave the renderer destroyable. Stats: rendered/skipped/failed/lost (sum to the frames begun), recreations, last fence.
- Tests (`tests/renderer/renderer_test.cpp`, 13 cases + the GPU ones): swapchain frames, minimize/restore/resize with targets following and old pool entries aging out, offscreen target, callback refusal and an uncreatable transient failing the frame with recovery, device loss at frame 4 and mid-`ExecuteParallel`, 1000 frames with no change in live textures/buffers/bind groups and one pool texture, 48 independent passes recorded on at least two threads with a 40-link barrier chain that only validates if lists are submitted in order (a reversed submit makes the null device report errors), serial and marker-pass cases, counters adding up; GPU: serial and parallel recording give identical pixels (checked analytically too) and a sweep of 24 allocation-failure points ends every run cleanly. The null parallel tests also run under TSan; the whole renderer set under ASan.
- Docs: lint clean, guide "Frames, parallel recording and the swapchain" with a compiled and run null-device sample, reference regenerated.
- Deferred (KNOWN-ISSUES): slicing one pass's draw list over several lists needs RHI support for suspending and resuming a rendering pass.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** the `Renderer` constructor built the pool from the device before the null check; the check now runs first.
- **Fixed:** a pass with no callback and no barriers still got a list; it now records none (the lists are compacted before the single `Submit`). Test with the null device's list counter.
- **Fixed:** the `jobs::Scheduler` forward declaration was spliced into the middle of the header's namespace; moved to the top.
- **Fixed:** the failure contract now says pass callbacks may already have run (on several threads) when a parallel execution fails, so side effects outside the command list stay.
- **Fixed:** the frame counters did not add up (a lost frame was counted nowhere, a `Recreated` present on an abandoned frame was missed); `frames_lost` added, test that the four counters sum to the frames begun.
- **Fixed (while there):** the recording function-pointer indirection became a private static member function.
- **Declined:** asserting an open frame and participant 0 inside `ExecuteParallel`. The device's `CreateCommandList` and `ParallelForBlocking` already abort on exactly those violations, with their own messages.
- **Process:** `STATUS.md` and this file, done now.

**Done:** A graph renders to the screen and offscreen, records in parallel deterministically, survives resize/minimize/loss, and leaks nothing over 1000 frames.
