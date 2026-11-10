# Phase 17 / Day 232 — Transforms & Hierarchy

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 229-231.

The components every spatial system shares, and parent-child propagation.

## Tasks

- [x] **Components** (`engine/ecs/transform.h`, on the engine's `math` types): `LocalTransform { Vec3 position; Quat rotation; Vec3 scale; }`, `WorldTransform { Mat4 matrix; }`, `Parent { Entity entity; }`; helpers to compose, decompose, and `LookAt`; `Transform` convenience bundle for `Create`.
- [x] **Propagation system:** world = parent world * local; entities without a `Parent` are roots (world from local); computed per hierarchy depth (depth `d+1` after `d`), each depth a parallel `ForEachChunkParallel` over `Changed<LocalTransform>` or a changed ancestor; a `HierarchyDepth` component (a hidden implementation detail) is maintained by the `Parent` add/remove commands so the depth buckets are queries, not sorts, and re-derived when a parent changes.
- [x] **Policies, documented and tested:** a dangling `Parent` (the parent was destroyed) makes the child a root and removes `Parent` (counted, logged once per frame); destroying a parent does not destroy children unless `DestroyRecursive(world, entity)` is used (a command-buffer-based helper); a cycle (A parent of B parent of A) is rejected when `Parent` is set through `SetParent(world, child, parent)` (returns a status) and, if forced by raw `Set<Parent>`, is detected by the propagation (those entities keep their previous world matrix, counted, logged once); maximum depth 64 with a counted refusal.
- [x] **Change tracking:** an entity's `WorldTransform` is marked changed only when its matrix changed (the propagation compares and writes only on difference, so the render sync of Day 233 sees exact changes); static scenes cost one `Changed` query that finds nothing.
- [x] **Tests** (`tests/ecs/transform_test.cpp`): a chain of 10 parents composes to the analytic matrix; siblings; reparenting moves the subtree; a moved root marks exactly its subtree changed and nothing else; a static world marks nothing over 100 frames; dangling parent, cycle, depth limit, recursive destroy; parallel propagation equals the sequential result on a random forest of 100k entities (hash compare); the cost of propagating 100k entities at depth 1, 4 and 16 recorded.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan (parallel propagation); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Transforms and hierarchy".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** transforms and parent-child hierarchy propagate in parallel by depth with exact change marking, and the failure cases (dangling parents, cycles, depth) have defined, tested behavior.

## Notes

- Added to the ECS core for this day: `LazyWrite<T>` query term and `ChunkView::MarkWritten<T>()` (a function stamps a chunk only after a real change), `World::Peek/Poke/Stamp`, `SystemContext::Peek/Poke/Stamp/ParallelFor/Tick/WorldId/DestroyedCount`, `WorldStats::destroyed`.
- Deviations from the plan, all deliberate: (1) no per-subtree dirty tracking: when any root, `LocalTransform`, `Parent` or `HierarchyDepth` changed (or something was destroyed) all children are recomputed, writing a matrix only when it differs, so change marking is exact but the cost of a frame with one moved root is a pass over every child; (2) a raw `Set<Parent>` is repaired when the chain is walked, which lags one frame for the descendants; (3) "logged once per frame" is replaced by the `TransformReport` counters; (4) a cycle forced by raw `Set<Parent>` is detected only for members without a consistent stored depth (a member whose stored depth looks right is computed from its parent's stored matrix).
- Cost, debug build, 100k entities, one root moved per tree (best of 5): depth 1 37 ms, depth 4 57 ms, depth 16 67 ms. Release numbers belong to Day 235.
- Runs: plain `ran 2082 of 5391 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11 window failures only); asan `ran 1565 of 5391 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`; tsan `ran 1247 of 5391 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`, both clean. GCC and Clang non-unity builds clean. `ctest -L docs` 7 of 7, lint baseline zero. Guide: `docs/guide/ecs.md` "Transforms and hierarchy".
- Windows run: open (Two machines item stays unchecked; see KNOWN-ISSUES).

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, 3 fixed, 1 declined.

- **Fixed:** `Descendants` read `*up` after `Insert` rehashed the map (heap-use-after-free; caught by the asan runs of the depth-limit and `DestroyRecursive` tests, which fail without the fix).
- **Fixed:** a destroyed parent was detected by a falling entity count, so destroying one entity and creating another in the same frame missed it. Now `WorldStats::destroyed` / `SystemContext::DestroyedCount()`; regression test "a parent destroyed while another entity is created is still noticed".
- **Fixed:** `SetParent` could leave a `Parent` without `HierarchyDepth` when the second `Set` was refused; the parent is now removed again.
- **Declined (recorded in KNOWN-ISSUES.md):** `SetParent`, `ClearParent` and `DestroyRecursive` scan every `Parent` in the world per call (O(world)). Bulk loads create entities with `Parent` and `HierarchyDepth` directly; a child index is Day 233/235 work if the benchmark shows it matters.
