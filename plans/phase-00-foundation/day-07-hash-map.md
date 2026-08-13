# Phase 0 / Day 7 — Hash Map Container

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 6 (dynamic array container).

## Tasks

- [ ] Implement `HashMap<K, V>` in `/engine/foundation/hash_map.h`
  using open addressing (linear or Robin Hood probing), built over
  Day 6's `Array<T>` for backing storage.
- [ ] Implement a `Hash<K>` trait/function with specializations for
  common key types (integers, pointers) — extend to string keys once
  Day 8's string type exists, not blocking this day.
- [ ] Implement insert, lookup, delete, and load-factor-triggered
  resize/rehash.
- [ ] Write a correctness test against a known key set: insert N known
  key/value pairs, verify every lookup returns the right value, delete
  half, verify the surviving half still resolves and the deleted keys
  report absent.
- [ ] Commit.

**Done:** insert/lookup/delete correctness test against a known key
set.
