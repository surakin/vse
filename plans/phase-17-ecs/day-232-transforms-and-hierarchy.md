# Phase 17 / Day 232 — Transforms & Hierarchy

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 229-231.

The components every spatial system shares, and parent-child propagation.

## Tasks

- [ ] **Components** (`engine/ecs/transform.h`, on the engine's `math` types): `LocalTransform { Vec3 position; Quat rotation; Vec3 scale; }`, `WorldTransform { Mat4 matrix; }`, `Parent { Entity entity; }`; helpers to compose, decompose, and `LookAt`; `Transform` convenience bundle for `Create`.
- [ ] **Propagation system:** world = parent world * local; entities without a `Parent` are roots (world from local); computed per hierarchy depth (depth `d+1` after `d`), each depth a parallel `ForEachChunkParallel` over `Changed<LocalTransform>` or a changed ancestor; a `HierarchyDepth` component (a hidden implementation detail) is maintained by the `Parent` add/remove commands so the depth buckets are queries, not sorts, and re-derived when a parent changes.
- [ ] **Policies, documented and tested:** a dangling `Parent` (the parent was destroyed) makes the child a root and removes `Parent` (counted, logged once per frame); destroying a parent does not destroy children unless `DestroyRecursive(world, entity)` is used (a command-buffer-based helper); a cycle (A parent of B parent of A) is rejected when `Parent` is set through `SetParent(world, child, parent)` (returns a status) and, if forced by raw `Set<Parent>`, is detected by the propagation (those entities keep their previous world matrix, counted, logged once); maximum depth 64 with a counted refusal.
- [ ] **Change tracking:** an entity's `WorldTransform` is marked changed only when its matrix changed (the propagation compares and writes only on difference, so the render sync of Day 233 sees exact changes); static scenes cost one `Changed` query that finds nothing.
- [ ] **Tests** (`tests/ecs/transform_test.cpp`): a chain of 10 parents composes to the analytic matrix; siblings; reparenting moves the subtree; a moved root marks exactly its subtree changed and nothing else; a static world marks nothing over 100 frames; dangling parent, cycle, depth limit, recursive destroy; parallel propagation equals the sequential result on a random forest of 100k entities (hash compare); the cost of propagating 100k entities at depth 1, 4 and 16 recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan (parallel propagation); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Transforms and hierarchy".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** transforms and parent-child hierarchy propagate in parallel by depth with exact change marking, and the failure cases (dangling parents, cycles, depth) have defined, tested behavior.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
