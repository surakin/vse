# Phase 4 / Day 75 — Render Graph I: Declaration & Compilation

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 74.

A frame graph built only on the RHI: passes declare what they read and write, and the graph compiles that into an ordered, culled schedule.

## Tasks

- [ ] Types: virtual graph resources (textures/buffers with a descriptor, lifetime = the frame) and a `PassBuilder` (`Read`, `Write`, `CreateTexture`, `ImportTexture` for the swapchain image and persistent targets, with a usage per access: color attachment, depth attachment, depth read, sampled, storage read/write, copy src/dst); a pass is a name plus an execute callback (function pointer + context; no `std::function`).
- [ ] `RenderGraph::AddPass` records declarations only; errors (reading a resource nobody writes, writing a read-only import, a cycle) are reported at compile time with pass and resource names, never at execution.
- [ ] Compile: build dependencies from read-after-write / write-after-write / write-after-read, order passes topologically keeping declaration order stable, cull passes whose outputs nothing consumes (unless marked as having side effects or writing an import), and compute each virtual resource's first and last use.
- [ ] Debug output: a textual dump of passes, resources and edges plus a Graphviz `.dot` writer, used by tests as the expected-schedule oracle.
- [ ] Pure tests (no GPU): linear chain, diamond, unused branch culled, cycle rejected, read-before-write rejected, stable ordering, a 1000-pass random DAG compiles in bounded time and matches a reference ordering check; all graph storage comes from a per-frame arena (no heap allocation per pass).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The graph API, declaration rules, error cases and dump format are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Graphs compile to a verified schedule, bad graphs are rejected with named errors, and everything is tested without a GPU.
