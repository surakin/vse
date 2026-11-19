# Container audit / Day 2: Foundation I: HashMap completed, HashSet, one byte hash

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Day 1 (the `container_bench` baseline, and the Mac's `HashMap` checked).

After `eefa8c3b`, `HashMap` takes move-only entries, looks up by a borrowed key and iterates. It still lacks `Clear` and `Reserve`, divides on
every probe, and allocates in its constructor. No set exists, and FNV-1a is written out again in about 26 files. This day finishes the map and gives the engine one byte hash.

## Tasks

- [ ] **One byte hash** in the foundation's base layer: `Fnv1a64(ByteSpan)`, `Fnv1a64(StringView)` and an incremental `Fnv1a64Builder` (`Add(byte)`,
  `AddU64`, ...). They produce exactly the values of the copies they will replace on Day 4, which persist in minted outputs and state hashes.
  - Byte-for-byte tests against the published FNV-1a test vectors.
  - Tests against one value from each existing copy's formula (resource table, settings key, path, physics state hash, script runtime, pipeline caches).
  - `Hash<StringView>` uses it, then mixes the result (it does today, so the map hash is unchanged).
- [ ] **`HashMap` completed:**
  - `Clear()`, which destroys the entries and keeps the table;
  - `Reserve(n)`, which reaches capacity for `n` live entries without a rehash;
  - `Contains(key)`;
  - `FindOrInsert(key, make)` / `Emplace(key, args...)`, which returns the value and whether it was inserted;
  - capacity kept a power of two, so the probe is `& (cap - 1)`, never `%`. `initial_capacity` rounds up, and `SlotCapacity`'s test expectations change with it;
  - no allocation until the first insert (the constructor makes no table; a moved-from map already works that way);
  - the heterogeneous overloads for `Contains` and `FindOrInsert`.
  - Tests: every new member, a move-only value, and the rounding.
  - The Day 1 number for the probe cost, before and after.
- [ ] **`HashSet<K, Allocator>`** in `containers/hash_set.h`. It holds keys only: `Insert` (returns whether it was new), `Contains`, `Remove`, `Clear`, `Reserve`,
  `ForEach`, `Size`, and heterogeneous lookup.
  - Share the probing with `HashMap` (a `detail` table over a slot type). Do not copy it.
  - Tests mirror the map's: tombstone churn, move-only keys, and growth under removal.
- [ ] **`Hash` for the keys the audit found without one:**
  - `asset::ContentHash` (128 bits, beside its type);
  - a pair key helper `HashCombine(a, b)`, used by the physics pair and the theme cache key;
  - `ecs::Entity` and `storage::EntityKey` already have one, so check them only.
- [ ] **Docs:**
  - `docs/guide/foundation-types.md`: a "Choosing a container" table in reference form: map, set, vector, array, deque (Day 3), heap (Day 3), sorted vector, and what each costs;
  - `///` on every new member, with `Aborts:`/`Fails:`;
  - `docgen lint` at zero;
  - `docs/reference` regenerated.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (a foundation header: everything), plain, then asan and tsan (lifetime code);
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- Changing the map hash (for example to wyhash) is out of scope. The map's hash never reaches a persisted output, but a change would reorder `ForEach`,
  and the deterministic paths must not depend on that order anyway.

## Runs

## Post-implementation code review

**Done:**
- `HashMap` has `Clear`, `Reserve`, `Contains` and `FindOrInsert`/`Emplace`, probes by mask and allocates lazily.
- `HashSet` exists.
- `Fnv1a64` is the one byte hash, and it reproduces every existing copy's values.
- The map's probe cost is measured before and after.
- The guide's container table is written, and the docs are green.
