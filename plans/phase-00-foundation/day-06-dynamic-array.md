# Phase 0 / Day 6 — Dynamic Array Container

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 5 (pool & stack allocators).

## Tasks

- [x] Implement `Array<T, Allocator>` in `/engine/foundation/containers/array.h`
  (header-only, templated), requiring only `void* Alloc(usize size,
  usize align)` from `Allocator` — Day 4's `ArenaAllocator` satisfies
  this. `Push`, `Pop`, `Insert(index)`, `Remove(index)`,
  `Reserve(capacity)`, growth-factor resize (starts at 4, doubles).
  **Corrected from the original wording below:** old backing blocks are
  *not* individually freed on resize — none of Days 4/5's allocators
  support freeing an arbitrary block (Arena is bulk-alloc/bulk-reset
  only, Pool is fixed-slot-size, Stack is LIFO-only) — so a resize
  abandons the old block within the allocator instead. This is a
  deliberate, accepted tradeoff for arena-backed growable containers
  (bounded waste per array, reclaimed in bulk on the arena's `Reset()`),
  not an oversight; flagged and corrected here per `CLAUDE.md`'s "stop
  and edit the plan rather than silently drift" rule rather than
  implementing a `Free()` call that doesn't exist on any Day 4/5 type.
- [x] Handle the resize path for non-trivially-copyable `T` correctly:
  move-construct (falling back to copy where a type isn't movable)
  elements into the new backing block, then destroy (not free — see
  above) the old ones in place.
- [x] Write a stress test pushing 100,000 elements, verifying contents
  index-by-index immediately after every resize boundary is crossed.
- [x] Write a non-trivial-`T` test verifying the resize path actually
  move-constructs existing elements (not just relocates trivially), and
  that construction/destruction counts balance (no leaks/double-frees)
  across growth and destruction. Precisely checks the move count jumps
  by exactly (existing elements + 1) on the resize-triggering push, not
  just that `moves > 0`.
- [x] Write insert/remove correctness tests against a known small
  sequence (verify final contents and indices match expectations).
  Covers insert-at-middle, remove-at-front, insert-at-end (via
  `Insert(Size(), ...)`), remove-at-last.
- [x] Commit.

**Done:** stress test pushes 100k elements, contents verified. Verified:
`engine_main` runs all tests through Day 6, exit code 0; also clean
under a one-off `-fsanitize=address,undefined` build.
