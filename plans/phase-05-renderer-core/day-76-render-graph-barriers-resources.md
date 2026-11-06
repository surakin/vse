# Phase 5 / Day 76 — Render Graph II: Automatic Barriers & Transient Resources

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 75.

The graph owns every state transition the RHI leaves to the caller, and allocates the frame's transient textures.

## Tasks

- [x] State tracking: from each resource's compiled usage sequence derive the `ResourceState` before and after every pass; emit `Barrier(resource, before, after)` only when the state changes, merge consecutive reads where legal, and start virtual resources at `Undefined`.
- [x] Imported resources carry entry and exit states (the swapchain image enters `Undefined` and exits `Present`; a persistent target enters and exits in a declared state); tested.
- [x] Barrier placement: a pass's barriers are recorded just before its first command, outside any rendering pass (an RHI rule); color and depth attachments of one pass are transitioned together.
- [x] Transient pool: textures/buffers with equal descriptors are reused across frames and across non-overlapping lifetimes within a frame (pooling by descriptor is required, aliasing optional); a resource unused for N frames is destroyed; counters exposed.
- [x] Null-device oracle tests (the null device validates the state machine and counts errors): zero errors for a deferred-shaped graph, and expected barrier lists for read-then-write, write-then-sample, depth attachment to depth read, and swapchain present.
- [x] Real-device test with validation + synchronization validation: a 4-pass graph (write, sample, compute write, sample) renders correctly with zero messages.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Barrier rules, imported-resource states and the pool are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `engine/renderer/render_graph.{h,cpp}`: `Compile` now plans barriers (`BarrierCount`/`BarrierAt`/`DumpBarriers`/`StateAfterLastUse`, `GraphBarrier`, `StateName`) and checks that a transient resource's usage flags allow every use. `Execute(pool, list)` takes transients from the pool at their first use, records each pass's barriers before its callback, calls it with a `PassExecuteContext` that now carries the graph (`ResolvedTexture`/`ResolvedBuffer`), and gives each transient back after its last use. A runtime failure goes to `ExecuteError()`, not the declaration-error table.
- Barrier rules: a barrier when the needed state differs from the current one or when the previous use wrote the resource (same-state write ordering); consecutive reads in one state merge; a transient starts `Undefined` (the pooled state replaces it at execution, which also orders the new use after the old one); an import starts in its declared initial state and gets an exit barrier to its final state, including when no pass uses it.
- `engine/renderer/transient_pool.{h,cpp}`: `TransientPool` pools by description (every field but the debug name), remembers the state a resource was left in, destroys what stays idle more than `max_idle_frames` frames, evicts the longest-idle entry when full, and counts creations, reuses, destructions, live and idle resources. Lifetimes that do not overlap share a texture inside one frame. Memory aliasing was not done (optional in the plan); see `KNOWN-ISSUES.md`.
- `shaders/graph_sample.frag.slang` (+ minted includes): adds two sampled textures, for the real-device test.
- Tests: `tests/renderer/render_graph_barrier_test.cpp` (15 cases: expected barrier lists for write-then-sample, read-then-write, depth to depth-read, swapchain present, merged reads, same-state writes, unused imports, attachments together, usage-flag errors; a deferred-shaped 3-color+depth graph run four frames on the null device with zero errors and 15 reuses; in-frame sharing; failed `Execute` rolls the pool back; idle destruction; full-pool eviction; a transient buffer across frames) and `tests/rhi/vulkan_render_graph_test.cpp` (clear, sampling draw, compute write, combining draw, three frames, validation + sync validation, zero messages, pixels checked). Mutations confirmed the oracles bite: dropping the texture barriers fails the null tests, and dropping the shader-read ones makes validation report `VUID-vkCmdDraw-imageLayout-00344`.
- Day 75 tests and the guide sample declared transients with a default (usage-less) description; they now give usage flags, because `Compile` rejects a use the description does not allow.
- Docs: lint clean (new symbols all documented, no new baseline entries), guide "Barriers, imports and transient resources" with a compiled sample whose output is the barrier list, reference regenerated.

## Post-implementation code review

`/code-review high`: 10 findings.

- **Fixed:** after a failed `Execute`, transients already given back kept the state the discarded commands would have left. `Execute` now rolls every transient back to the state it was taken in (`TransientPool::SetTextureState`/`SetBufferState`). Regression test (fails with the rollback disabled).
- **Fixed:** `Execute` failures were added to the sticky declaration-error table, so one failed frame made later compiles fail. They go to `ExecuteError()`.
- **Fixed:** `Execute` allocated a state array from the arena on every call. The bookkeeping lives in the resource records and one array allocated per compile.
- **Fixed:** a full pool failed although idle entries could be destroyed. The longest-idle entry is evicted first. Test.
- **Fixed:** `ResolvedTexture`/`ResolvedBuffer` disagreed with their documentation for imports. An import always resolves to its handle; a transient is invalid outside its use.
- **Fixed (readability):** the depth usage check is split into separate cases with a comment.
- **Declined:** requiring `DepthAttachment` for `DepthRead`. `DepthRead` is one state used both as a read-only attachment and as a sampled depth texture, and the graph cannot tell which; either flag is accepted, and the RHI/validation catches the other.
- **Declined:** a `static_assert` on the descriptor structs' sizes. It would break on other ABIs; the comparison functions carry a comment to keep them in step with `TextureDesc`/`BufferDesc`.
- **Declined:** a real-device test of failure and in-frame sharing. The null device already checks the state machine for both; the real device test covers the path synchronization validation can judge.
- **Noted, not a defect:** transients with a usage-less description now fail `Compile`. That is intended (the plan makes the description the contract); stated in `Compile`'s `Fails:` and the guide, and the Day 75 tests were updated.

**Done:** No hand-written barrier remains in graph-driven code, null and real devices agree, and sync validation is silent.
