# Phase 0 / Day 5 — Pool & Stack Allocators

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 4 (arena allocator).

## Tasks

- [ ] Implement `PoolAllocator` (fixed-size free-list) in
  `/engine/foundation/pool_allocator.h`/`.cpp`: backing block via
  `os::MMap`, a free-list threaded through unused slots, `Alloc()`,
  `Free(ptr)`.
- [ ] Implement `StackAllocator` in
  `/engine/foundation/stack_allocator.h`/`.cpp`: bump pointer over
  arena-style backing memory with `Push()`/`PopToMarker(marker)`
  semantics.
- [ ] Write an alloc/free churn test for the pool allocator: repeated
  random alloc/free cycles, verifying no two live allocations ever
  alias the same slot.
- [ ] Write a push/pop-marker test for the stack allocator: push N
  objects, take a marker, push M more, pop to the marker, verify only
  the first N remain valid.
- [ ] Run both under the churn/stress pattern and confirm no leaks or
  corruption in either.
- [ ] Commit.

**Done:** alloc/free churn test shows no leaks or corruption for both
allocators.
