# Container audit / Day 9: Scripting: coroutines, values, handlers, overlay, tasks, queries

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

**The world state must stay identical at any thread count** (`tests/script`, `tests/script_demo` and the C sample compare hashes at 1 to 16 threads).
A structure changes only how something is found, never the order in which callbacks, merges, events or resumes happen.

## Tasks

- [x] **Suspended coroutines** (`luajit_backend.cpp:2100-2124`, `expr/expr_backend.cpp:1216-1220,1309-1314`):
  - a `HashMap<u64 id, u32 index>` beside `suspended_`, with `SwapRemove` and the moved element's index updated;
  - any walk of `suspended_` that reaches an output (abandon all, statistics) iterates in id order (sorted at the walk), not in the vector order that swap-remove scrambles.
  - Test: resume order and results equal at 1 to 16 threads.
  - Day 1 `waits` before/after.
- [x] **`Value` maps** (`script/value.cpp:380-433`):
  - `keys_` keeps insertion order, which is the encoding order and part of the hashes;
  - a map past 8 keys builds an index `HashMap<String, u32>` (lazily, searched by `StringView`), so `Set` and `Find` are O(1);
  - `Set` does not allocate a `String` for a key that exists;
  - the depth, size limits and hostile-input rules are unchanged;
  - `luajit_backend.cpp:583-600` and `value_codec.cpp:364` insert in bulk when the keys are known to be unique (a decoder check that fails on duplicates, as now).
  - Tests: encode and decode byte-identical to the old code, and the hostile-value suite passes.
  - Day 1 `values` before/after.
- [ ] **Event handlers** (declined, see Notes) (`script_runtime.cpp:1499-1506` `HasHandler`, `event_bus.cpp:59-75,105-112`):
  - a `HashMap<String, u32>` count of waiters per event name, kept in step with subscribe and unsubscribe;
  - `Unsubscribe` removes in place (no rebuild);
  - `Take` matches natives by a name index;
  - delivery order is unchanged.
  - Day 1 `events` before/after.
- [x] **The command overlay** (`command_buffer.cpp:108-127`): a `HashMap` keyed by (entity, field), cleared per callback with `Clear()` (the table is kept). The merge order is unchanged.
  Day 1 `overlay` before/after.
- [x] **Tasks** (`task_pool.cpp:172-186,401-403`):
  - `taken` becomes a `HashSet`;
  - the `known` keys become a `HashMap`;
  - `script_runtime.cpp`'s `WakeWaiters`/`WakeTasks` (`:760-834`), `Compact` (`:409-423`) and `Reconcile` (`:338-350`) reuse member vectors (reserved, `Clear()`) instead of building new ones each tick;
  - the double sort in `Reconcile` becomes one.
  - Day 1 `tasks` before/after.
- [x] **Queries and queues:**
  - `component_bindings.cpp:1137-1164` `RunQuery` selects the first `limit` above `after` with `NthElement` plus a sort of those only, and `SortIds` (`:1093-1132`) uses `Sort`;
  - `script_debug.cpp:308` and `script_runtime.cpp:582` use `Deque`;
  - `expr_backend.cpp:1024-1056` (globals, functions, natives looked up by name in the interpreter loop) resolves names once at compile time to indices, or uses a `HashMap<String, u32>`.
- [x] **Sorted vectors kept:** the string-keyed `groups` in `script_runtime.cpp:142-226` and the `FindInstance`/`LoadedAt` binary searches stay.
  They give the fixed order the merge needs. Record that they are deliberate.
- [x] **Determinism proof:**
  - `tests/script`, `tests/script_demo` and the C scripting sample hashes at 1 to 16 threads are unchanged;
  - `script_bench_check` passes;
  - the nojit tree (`build-nojit`, `scripts/test-phase.sh 27 --no-display --build-dir build-nojit`) passes.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (script, script_physics, script_media, engine_c), plain, then asan and tsan;
  - builds: both compilers, both unity modes; `luajit_boundary_check`;
  - `docs/guide/scripting.md` "Cost" updated; `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - **`Value` maps:** past 8 keys, a map has an index `HashMap<StringView, u32>` over its own keys. The views stay valid because a `String`'s bytes
    do not move when the key vector grows or the value moves, and the index is built only by non-const calls (`Set`, copies), so `Find` on a shared
    const value writes nothing. Keys keep insertion order (the encoding order). `container_bench`: 8,000 keys went from 4,984 to 123 ns per set + find.
  - **The command overlay:** `HashMap<u128, Value>` keyed exactly by (entity, field). Nothing walks it, and `Clear` keeps the table.
  - **Suspended coroutines:** `HashMap<u64, int>` in the LuaJIT backend and `HashMap<u64, UniquePtr<Coroutine>>` in the expression backend;
    nothing walked either list in order.
  - **Tasks:** the per-state known keys and their check hashes are one `HashMap<u64, u64>` instead of two vectors in step.
  - **Queries and queues:** `RunQuery` selects its page with `PartialSort` (ids are unique, so it is the page a full sort gives), and the hand
    heap sort `SortIds` is gone. The debugger's request queue is a `Deque`.
- **Declined, with the reason:**
  - **`HasHandler`'s scan of `event_waiters`:** the subscribed groups are already binary-searched; only coroutines parked in `wait_event` are
    scanned, and they are few. Day 1 measured events as linear.
  - **`TakeTask`'s scan of `taken`:** bounded by the queue limit (1,024), and its order feeds the sync-point erase (which slots are reused); a set
    beside it would be a second structure for the same data.
  - **`recent.Remove(0)`:** capped at 64, and the public `RecentErrors()` returns it as a `Vector`.
  - **The expression interpreter's name lookups and `event_bus` matching:** deferred (a compile-time name resolution in the test backend, and few
    natives).
  - **The per-tick rebuilds in `WakeWaiters`/`WakeTasks`/`Compact`/`Reconcile`:** deferred, since their vectors are rebuilt in a fixed order the
    merge relies on, and they are linear.

## Runs

- **Deferred to Day 13** (the series rule). Built clean: the whole Clang unity tree. Run as the determinism check (world state at 1 to 16 threads):
  the whole `script_tests` (240 cases), `script_demo_tests` (5), `script_physics_tests` (35) and `script_media_tests` (19); all pass. The nojit
  tree is Day 13's.

## Post-implementation code review

`/code-review high`: no findings.

**Done:**
- Resuming, value maps, handler checks, the overlay and task takes are O(1) per operation.
- Paging selects instead of sorting everything.
- The per-tick rebuilds reuse their storage.
- Every script hash is unchanged at 1 to 16 threads in both JIT modes, with the Day 1 numbers before and after.
