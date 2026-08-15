# Phase 0 / Day 5 — Pool & Stack Allocators

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 4 (arena allocator).

**Note (added later, alongside Day 9):** `os::MMap` mentioned below was
renamed to `os::ReserveMemory` for portability — see Day 2's note for
why. Both allocators' own behavior is unaffected.

## Tasks

- [x] Implement `PoolAllocator` (fixed-size free-list) in
  `/engine/foundation/mem/pool_allocator.h`/`.cpp`: backing block via
  `os::MMap`, a free-list threaded through unused slots, `Alloc()`,
  `Free(ptr)`. Slot size is rounded up to at least `sizeof(void*)` so
  every free slot can store the intrusive next-pointer in place.
- [x] Implement `StackAllocator` in
  `/engine/foundation/mem/stack_allocator.h`/`.cpp`: bump pointer over
  arena-style backing memory with `Push()`/`PopToMarker(marker)`
  semantics. Same alignment math as Day 4's `ArenaAllocator::Alloc`;
  `Marker` is just the saved offset.
- [x] Write an alloc/free churn test for the pool allocator: 10,000
  iterations of a deterministic xorshift32 PRNG picking one of 64
  logical slots to alloc-if-free / free-if-live, checking on every
  alloc that the returned pointer doesn't alias any other currently-live
  pointer.
- [x] Write a push/pop-marker test for the stack allocator: push 10
  objects, take a marker, push 5 more, pop to the marker, verify the
  first 10's contents are still intact and that the next `Push()`
  lands exactly where the popped region started (proves the offset
  actually rewound, not just that old data wasn't clobbered).
- [x] Run both under the churn/stress pattern and confirm no leaks or
  corruption in either. Also compiled and ran a one-off
  `-fsanitize=address,undefined` build outside the normal CMake flow
  for extra confidence on the pool allocator's pointer-cast-heavy
  intrusive free list — clean, no errors.
- [x] Commit.

**Done:** alloc/free churn test shows no leaks or corruption for both
allocators. Verified: `engine_main` runs all tests through Day 5,
exit code 0; ASan/UBSan build also clean.
