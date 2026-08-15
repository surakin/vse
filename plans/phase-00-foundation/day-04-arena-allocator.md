# Phase 0 / Day 4 — Arena Allocator

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 3 (base types & mem primitives).

**Note (added later, alongside Day 9):** `os::MMap`/`os::MUnmap`
mentioned below were renamed to `os::ReserveMemory`/`os::ReleaseMemory`
for portability — see Day 2's note for why. `ArenaAllocator`'s own
behavior is unaffected, just the OS call it makes internally.

## Tasks

- [x] Define `ArenaAllocator` in
  `/engine/foundation/mem/arena_allocator.h`/`.cpp`: a backing block
  obtained via Day 2's `os::MMap`, a bump pointer, `Alloc(size, align)`,
  `Reset()`, `FreeAll()` (unmaps the block via `os::MUnmap`).
- [x] Implement alignment handling: round the bump pointer up to the
  requested alignment before returning it.
- [x] Write a stress test allocating 1,000 objects of varying sizes,
  writing a unique checksum pattern into each. Sizes vary 1-64 bytes
  (`1 + (i % 64)`); a 1 MiB arena comfortably avoids exercising the
  out-of-capacity path.
- [x] Verify no two allocations' address ranges overlap, and every
  checksum still matches after all 1,000 allocations have been made.
  Overlap check is O(n²) pairwise range comparison — cheap at n=1000.
- [x] Commit.

**Done:** allocate 1,000 objects, checksum-verify no overlap/corruption.
Verified: `engine_main` runs all three arena checks (allocs succeed, no
overlap, checksums intact) plus Days 1-3's tests, exit code 0.
