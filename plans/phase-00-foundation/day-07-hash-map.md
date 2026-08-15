# Phase 0 / Day 7 — Hash Map Container

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 6 (dynamic array container).

## Tasks

- [x] Implement `HashMap<K, V, Allocator>` in
  `/engine/foundation/containers/hash_map.h` using open addressing (linear probing
  with tombstones for deletion), built over Day 6's `Array<T,
  Allocator>` for backing storage.
- [x] Implement a `Hash<K>` trait/function with specializations for
  common key types (integers, pointers) — extend to string keys once
  Day 8's string type exists, not blocking this day. Uses a Murmur3-style
  `fmix64` finalizer for avalanche.
- [x] Implement insert, lookup, delete, and load-factor-triggered
  resize/rehash (threshold 0.7, doubling). Rehash avoids needing move
  semantics on `Array` (which Day 6 doesn't provide — copy is deleted
  too) by snapshotting occupied entries into a temporary `Array`,
  `Pop()`-ing the live one back to empty, `Reserve`/`Push`-ing fresh
  empty slots at the new capacity, then reinserting from the snapshot —
  all through `Array`'s existing public API, no changes to Day 6's
  already-completed code needed.
- [x] Write a correctness test against a known key set: insert N known
  key/value pairs, verify every lookup returns the right value, delete
  half, verify the surviving half still resolves and the deleted keys
  report absent. 1000 keys against an initial capacity of 8 forces
  ~8 rehash-doubling generations, exercising the resize/rehash path
  thoroughly as a side effect of the correctness test itself.
- [x] Commit.

**Done:** insert/lookup/delete correctness test against a known key
set. Verified: `engine_main` runs all tests through Day 7, exit code 0;
also clean under a one-off `-fsanitize=address,undefined` build.
