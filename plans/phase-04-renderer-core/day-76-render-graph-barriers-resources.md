# Phase 4 / Day 76 — Render Graph II: Automatic Barriers & Transient Resources

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 75.

The graph owns every state transition the RHI leaves to the caller, and allocates the frame's transient textures.

## Tasks

- [ ] State tracking: from each resource's compiled usage sequence derive the `ResourceState` before and after every pass; emit `Barrier(resource, before, after)` only when the state changes, merge consecutive reads where legal, and start virtual resources at `Undefined`.
- [ ] Imported resources carry entry and exit states (the swapchain image enters `Undefined` and exits `Present`; a persistent target enters and exits in a declared state); tested.
- [ ] Barrier placement: a pass's barriers are recorded just before its first command, outside any rendering pass (an RHI rule); color and depth attachments of one pass are transitioned together.
- [ ] Transient pool: textures/buffers with equal descriptors are reused across frames and across non-overlapping lifetimes within a frame (pooling by descriptor is required, aliasing optional); a resource unused for N frames is destroyed; counters exposed.
- [ ] Null-device oracle tests (the null device validates the state machine and counts errors): zero errors for a deferred-shaped graph, and expected barrier lists for read-then-write, write-then-sample, depth attachment to depth read, and swapchain present.
- [ ] Real-device test with validation + synchronization validation: a 4-pass graph (write, sample, compute write, sample) renders correctly with zero messages.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Barrier rules, imported-resource states and the pool are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** No hand-written barrier remains in graph-driven code, null and real devices agree, and sync validation is silent.
