# Phase 0 / Day 4 — Arena Allocator

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 3 (base types & mem primitives).

## Tasks

- [ ] Define `ArenaAllocator` in
  `/engine/foundation/arena_allocator.h`/`.cpp`: a backing block
  obtained via Day 2's `os::MMap`, a bump pointer, `Alloc(size, align)`,
  `Reset()`, `FreeAll()` (unmaps the block via `os::MUnmap`).
- [ ] Implement alignment handling: round the bump pointer up to the
  requested alignment before returning it.
- [ ] Write a stress test allocating 1,000 objects of varying sizes,
  writing a unique checksum pattern into each.
- [ ] Verify no two allocations' address ranges overlap, and every
  checksum still matches after all 1,000 allocations have been made.
- [ ] Commit.

**Done:** allocate 1,000 objects, checksum-verify no overlap/corruption.
