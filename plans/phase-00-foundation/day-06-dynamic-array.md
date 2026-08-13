# Phase 0 / Day 6 — Dynamic Array Container

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 5 (pool & stack allocators).

## Tasks

- [ ] Implement `Array<T>` in `/engine/foundation/array.h`, templated
  over an allocator (Day 4/5 types): `Push`, `Pop`, `Insert(index)`,
  `Remove(index)`, `Reserve(capacity)`, growth-factor resize.
- [ ] Handle the resize path for non-trivially-copyable `T` correctly:
  move/copy elements into the new backing block, then free the old one.
- [ ] Write a stress test pushing 100,000 elements, verifying contents
  index-by-index immediately after every resize boundary is crossed.
- [ ] Write insert/remove correctness tests against a known small
  sequence (verify final contents and indices match expectations).
- [ ] Commit.

**Done:** stress test pushes 100k elements, contents verified.
