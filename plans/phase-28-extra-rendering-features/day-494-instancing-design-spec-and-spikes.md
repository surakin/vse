# Phase 28 / Day 494 — Instancing Design Spec and Spikes

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-493.

Settle the instancing and hierarchy design before code.

## Tasks

- [x] Write `docs/superpowers/specs/2026-11-NN-instancing-design.md`: what exists (the per-object storage buffer and merged same-mesh-and-material runs), what is missing (instances that are not entities, chunked culling, hierarchy), the data structures, the draw path and the C API.
- [x] **Spike 1, the baseline:** measure today's path with 100,000 entities sharing one mesh: the CPU time of the render sync, the cull, the object data write and the draw submission, and the memory per object; the numbers justify the work and become the baseline of the day that benchmarks it.
- [x] **Spike 2, the instance layout:** structure-of-arrays (positions `Vec3`, rotation `Quat`, scale `f32` or `Vec3`: 40 bytes or less) against a `Mat4x3` per instance (48 bytes) in the GPU buffer: bandwidth, the vertex shader's cost to rebuild the matrix, the precision of large-world positions (a chunk origin plus a local offset in `f32` to keep precision far from the origin); pick.
- [x] **Spike 3, chunking:** chunk sizes of 64, 256 and 1,024 instances with a bounding sphere or box per chunk, built by spatial order (a space-filling curve) at insert time; measure the cull time for 1,000,000 instances and the cost of an update that moves one instance; pick the size and the order.
- [x] **Spike 4, the hierarchy structure:** a packed array of nodes in depth-first order with a `subtree_size` per node (a node's subtree is the next `subtree_size` entries: a cull skips a subtree by jumping, no pointers) against a parent-index array with a level-by-level update; measure the transform update of 100,000 nodes and a subtree cull; the user decided the hierarchy is its own structure, not ECS entities; the spike picks its layout.
- [x] Decisions into the spec and `PLAN.md`; adjust days 495-501 if a spike changed their scope.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (a planning and spike day: no `/code-review`; code written for a spike is deleted or kept under `tests/` or `tools/` only if it has a test).

**Done:** The instancing spec is written and committed with the four spike results and the baseline numbers.

## Notes

- Done 2026-11-23. Spec: `docs/superpowers/specs/2026-11-23-instancing-design.md`. Picks: a 40-byte record, chunks of 256 in Morton order, a depth-first hierarchy with `subtree_size` and a parent index. The GPU cost of rebuilding the matrix in the vertex shader was not measured (CPU side only); Day 497 measures it. Timings are from a Release build on one core, the null device.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

None: a planning and spike day (no `/code-review`). The spike program was deleted; the spec holds the method and the numbers.
