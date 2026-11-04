# Phase 4 / Day 77 — Render Graph III: Execution, Parallel Recording & the Swapchain

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 76.

Run the compiled graph: record passes on the job system, submit once, present.

## Tasks

- [ ] Execution: for each pass in order resolve its transient resources, record its barriers and call its callback with a `PassContext` (command list, resolved textures/buffers, the `RenderingDesc` filled from its attachments).
- [ ] Parallel recording: independent passes (and, inside a pass, slices of the draw list) record on `jobs::Scheduler` participants into separate command lists, submitted in graph order in one `Submit`; tested with a fake pass that records its thread id.
- [ ] Swapchain integration: acquire/present/resize/minimize/`Recreated` (graph resources sized relative to the swapchain are rebuilt), with the Phase 3 rules (minimized = skip the frame, lost = report).
- [ ] Frame orchestration `Renderer::RenderFrame(scene, camera)`: BeginFrame, build graph, compile, execute, EndFrame, present; graph storage is released at the frame boundary; a frame on a lost device returns an error status and leaves the renderer destroyable.
- [ ] Tests: ordering of submitted lists; parallel recording gives output identical to serial (pixel compare); 1000 frames with no growth in memory or live RHI objects; a TSan run of the null-device parallel-recording test; an ASan run of the whole renderer test set.
- [ ] Failure injection: `FaultInjection` device loss mid-graph and an allocation failure while creating a transient texture both end the frame cleanly (no crash, no leak).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `Renderer`, execution, `PassContext` and the threading contract (what may run concurrently) are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** A graph renders to the screen and offscreen, records in parallel deterministically, survives resize/minimize/loss, and leaks nothing over 1000 frames.
