# Container audit / Day 2: Foundation I: HashMap completed, HashSet, one byte hash

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Day 1 (the `container_bench` baseline, and the Mac's `HashMap` checked).

After `eefa8c3b`, `HashMap` takes move-only entries, looks up by a borrowed key and iterates. It still lacks `Clear` and `Reserve`, divides on
every probe, and allocates in its constructor. No set exists, and FNV-1a is written out again in about 26 files. This day finishes the map and gives the engine one byte hash.

## Tasks

- [x] **One byte hash** in the foundation's base layer: `Fnv1a64(ByteSpan)`, `Fnv1a64(StringView)` and an incremental `Fnv1a64Builder` (`Add(byte)`,
  `AddU64`, ...). They produce exactly the values of the copies they will replace on Day 4, which persist in minted outputs and state hashes.
  - Byte-for-byte tests against the published FNV-1a test vectors.
  - Tests against one value from each existing copy's formula (resource table, settings key, path, physics state hash, script runtime, pipeline caches).
  - `Hash<StringView>` uses it, then mixes the result (it does today, so the map hash is unchanged).
- [x] **`HashMap` completed:**
  - `Clear()`, which destroys the entries and keeps the table;
  - `Reserve(n)`, which reaches capacity for `n` live entries without a rehash;
  - `Contains(key)`;
  - `FindOrInsert(key, make)` / `Emplace(key, args...)`, which returns the value and whether it was inserted;
  - capacity kept a power of two, so the probe is `& (cap - 1)`, never `%`. `initial_capacity` rounds up, and `SlotCapacity`'s test expectations change with it;
  - no allocation until the first insert (the constructor makes no table; a moved-from map already works that way);
  - the heterogeneous overloads for `Contains` and `FindOrInsert`.
  - Tests: every new member, a move-only value, and the rounding.
  - The Day 1 number for the probe cost, before and after.
- [x] **`HashSet<K, Allocator>`** in `containers/hash_set.h`. It holds keys only: `Insert` (returns whether it was new), `Contains`, `Remove`, `Clear`, `Reserve`,
  `ForEach`, `Size`, and heterogeneous lookup.
  - Share the probing with `HashMap` (a `detail` table over a slot type). Do not copy it.
  - Tests mirror the map's: tombstone churn, move-only keys, and growth under removal.
- [x] **Range-for over `HashMap` and `HashSet`** (the user, 2026-11-19: every engine container supports `for (auto& x : c)`):
  - public lowercase `begin()`/`end()`, const and non-const, each with an `Internal:` label so docgen leaves them out of the reference;
  - the iterator is a `detail` type that skips empty and tombstone slots. A map yields an entry with `.key` (const) and `.value`, which works with
    structured bindings (`for (auto& [k, v] : map)`); a set yields `const K&`;
  - the order is unspecified, as with `ForEach`, which stays. Inserting or removing during the loop is not allowed (an `ENGINE_CHECK_SLOW` modification counter in debug builds);
  - the guide states the order rule: no deterministic output may depend on it.
  - Tests: a range-for visits every entry exactly once, after churn with tombstones, on an empty map and on a moved-from map, and a structured-binding loop compiles.
- [x] **`Hash` for the keys the audit found without one:**
  - `asset::ContentHash` (128 bits, beside its type);
  - a pair key helper `HashCombine(a, b)`, used by the physics pair and the theme cache key;
  - `ecs::Entity` and `storage::EntityKey` already have one, so check them only.
- [x] **Docs:**
  - `docs/guide/foundation-types.md`: a "Choosing a container" table in reference form: map, set, vector, array, deque (Day 3), heap (Day 3), sorted vector, and what each costs;
  - `///` on every new member, with `Aborts:`/`Fails:`;
  - `docgen lint` at zero;
  - `docs/reference` regenerated.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (a foundation header: everything), plain, then asan and tsan (lifetime code);
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - `containers/hash.h`: `Fnv1a64`, `Fnv1a64Builder` (`AddByte`, `AddBytes`, `AddText`, `AddCString`, `AddWord`), `HashCombine`, and the `Hash<K>`
    specializations, moved there from `hash_map.h`.
  - `HashMap`:
    - lazy first table, power-of-two capacity with a mask probe;
    - `Clear`, `Reserve`, `Contains`, `Emplace` (returns `HashMapEmplaced`), `Empty`;
    - range-for over `HashMapEntry` (`for (auto& [key, value] : map)`) with an `ENGINE_CHECK_SLOW` change counter.
  - `HashSet` over the same table.
  - `Hash<asset::ContentHash>`.
  - `explicit String(StringView)` for allocators with `Default()`, so a `String`-keyed map can make its key from a view or a literal.
  - `string.h` includes `containers/hash.h`, not the whole map.
- **A bug fixed beyond the plan:**
  - `Insert` (in `eefa8c3b`) hashed its argument with the argument's own type's hash, so an `int` key in an `i64` map, or a C string in a `String` map,
    landed in the wrong slot.
  - Now an argument that is not `K` or an agreed heterogeneous key (`kSearchesAs`) is converted to `K` before it is hashed, in `Insert` and `Emplace`.
- **Test expectations changed:** the tombstone-churn test reads the capacity after a first insert, since the constructor no longer makes a table.
- `Fnv1a64(StringView)` is not `constexpr`: `StringView` from a C string measures it at run time.
- Pages touched: `docs/guide/foundation-containers.md` (the map's members, iteration order, `HashSet`, "Hashing bytes", the invalidation table) and `docs/reference`.

- Changing the map hash (for example to wyhash) is out of scope. The map's hash never reaches a persisted output, but a change would reorder `ForEach`,
  and the deterministic paths must not depend on that order anyway.

## Runs

- **Deferred to Day 12** (the user, during this day: "skip per-day tests. test everything at the end"). That covers the scoped plain, asan and tsan runs,
  `ctest -L docs` and the before/after probe number.
- Built clean: the whole Clang unity `build` tree. The new and changed container tests were run while writing them: 17 test cases of
  `[hash_map],[hash_set],[hash]` pass. A negative-key check was shown to fail on the old map: a small program against `eefa8c3b`'s `hash_map.h` lost 5 of 50
  keys. The nine `HashMap` users that do not include `hash_map.h` themselves compile on their own, since `string.h` no longer brings in the map.
- `docs/reference` regenerated, and `docgen` reports no error.

## Post-implementation code review

`/code-review high`: 3 findings, all fixed:
1. An `Insert` replacing an existing key at the load threshold rehashed and doubled the table. It now looks the key up first when growth is due; a
   regression test fills 16 slots to the threshold and replaces every value without growth. The old code doubled to 32, by reading the code path.
2. `Emplace` probed twice for a new key. It now uses one insert probe when no growth is due.
3. The guide overstated the change check during a walk: it only fires on the next step.

**Done:**
- `HashMap` has `Clear`, `Reserve`, `Contains` and `FindOrInsert`/`Emplace`, probes by mask and allocates lazily.
- `HashSet` exists.
- `Fnv1a64` is the one byte hash, and it reproduces every existing copy's values.
- The map's probe cost is measured before and after.
- The guide's container table is written, and the docs are green.
