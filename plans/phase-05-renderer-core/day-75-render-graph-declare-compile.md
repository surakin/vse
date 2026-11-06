# Phase 5 / Day 75 — Render Graph I: Declaration & Compilation

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 74.

A frame graph built only on the RHI: passes declare what they read and write, and the graph compiles that into an ordered, culled schedule.

## Tasks

- [x] Types: virtual graph resources (textures/buffers with a descriptor, lifetime = the frame) and a `PassBuilder` (`Read`, `Write`, `CreateTexture`, `ImportTexture` for the swapchain image and persistent targets, with a usage per access: color attachment, depth attachment, depth read, sampled, storage read/write, copy src/dst); a pass is a name plus an execute callback (function pointer + context; no `std::function`).
- [x] `RenderGraph::AddPass` records declarations only; errors (reading a resource nobody writes, writing a read-only import, a cycle) are reported at compile time with pass and resource names, never at execution.
- [x] Compile: build dependencies from read-after-write / write-after-write / write-after-read, order passes topologically keeping declaration order stable, cull passes whose outputs nothing consumes (unless marked as having side effects or writing an import), and compute each virtual resource's first and last use.
- [x] Debug output: a textual dump of passes, resources and edges plus a Graphviz `.dot` writer, used by tests as the expected-schedule oracle.
- [x] Pure tests (no GPU): linear chain, diamond, unused branch culled, cycle rejected, read-before-write rejected, stable ordering, a 1000-pass random DAG compiles in bounded time and matches a reference ordering check; all graph storage comes from a per-frame arena (no heap allocation per pass).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The graph API, declaration rules, error cases and dump format are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `engine/renderer/render_graph.{h,cpp}`: `RenderGraph`, `PassBuilder`, `GraphUsage`/`GraphAccess`/`PassFlags`, `StateForUsage`. Resources are created on the graph (`CreateTexture/Buffer`, `ImportTexture/Buffer`) rather than on the `PassBuilder` (the builder only declares accesses); descriptors, imported handles and initial/final states are stored and queryable for Day 76.
- Semantics are declaration order, so an ordinary graph cannot have a cycle; cycles come only from `AddDependency`, which is also the one way to order passes that share no resource. A dependency does not keep its predecessor alive.
- Compile: edges (raw/waw/war/after), stable Kahn order (smallest declaration index first, via a heap), cull (roots are `SideEffects` passes and writers of imports; liveness follows raw edges), first/last use per resource. All storage is from the caller's arena; `Compile` takes scratch space from it.
- Tests: `tests/renderer/render_graph_test.cpp`, 17 cases: chain and diamond with exact dump oracles, culling, WAW/WAR edges, ReadWrite, explicit dependencies, cycle named, every error message, full tables, error overflow, tiny arena, callbacks, dump measuring/truncation/long names, dot, a global-operator-new counter proving zero heap allocations while building and compiling 500 passes (skipped under TSan, whose runtime owns operator new), and a 1000-pass random DAG compared with an independent O(n^2) reference (schedule order and culling identical; 0.18 ms here).
- Docs: lint 459/459, guide "The render graph" with a compiled sample whose output is the dump, reference regenerated.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** `Dump`/`WriteDot` cut each line at 511 bytes and under-reported the length: now formatted straight into the caller's buffer. Test with 900-character names.
- **Fixed:** the descriptor and import states were accepted and dropped: stored, with accessors (`TextureDescOf`, `ImportedTexture`, `InitialState`, `FinalState`, ...).
- **Fixed:** declaring after a successful `Compile` left a stale schedule: any declaration clears it, a repeat `Compile` with no change returns at once. Test.
- **Fixed:** the allocation-counter canary could be elided: volatile pointer.
- **Fixed:** the wall-clock assertion is only made in non-sanitizer builds (and tightened to 100 ms); the figure is printed.
- **Fixed:** the constructor's `Aborts` was undocumented; `ReadWrite`/`Read` of an import documented and tested.
- **Declined:** an explicit dependency does not keep its predecessor alive. That is the documented meaning ("when both run"); a sync pass that must run carries `SideEffects`. Pinned by a test and stated in the header.

**Done:** Graphs compile to a verified schedule, bad graphs are rejected with named errors, and everything is tested without a GPU.
