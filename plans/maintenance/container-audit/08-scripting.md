# Container audit / Day 8: Scripting: coroutines, values, handlers, overlay, tasks, queries

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

**The world state must stay identical at any thread count** (`tests/script`, `tests/script_demo` and the C sample compare hashes at 1 to 16 threads).
A structure changes only how something is found, never the order in which callbacks, merges, events or resumes happen.

## Tasks

- [ ] **Suspended coroutines** (`luajit_backend.cpp:2100-2124`, `expr/expr_backend.cpp:1216-1220,1309-1314`):
  - a `HashMap<u64 id, u32 index>` beside `suspended_`, with `SwapRemove` and the moved element's index updated;
  - any walk of `suspended_` that reaches an output (abandon all, statistics) iterates in id order (sorted at the walk), not in the vector order that swap-remove scrambles.
  - Test: resume order and results equal at 1 to 16 threads.
  - Day 1 `waits` before/after.
- [ ] **`Value` maps** (`script/value.cpp:380-433`):
  - `keys_` keeps insertion order, which is the encoding order and part of the hashes;
  - a map past 8 keys builds an index `HashMap<String, u32>` (lazily, searched by `StringView`), so `Set` and `Find` are O(1);
  - `Set` does not allocate a `String` for a key that exists;
  - the depth, size limits and hostile-input rules are unchanged;
  - `luajit_backend.cpp:583-600` and `value_codec.cpp:364` insert in bulk when the keys are known to be unique (a decoder check that fails on duplicates, as now).
  - Tests: encode and decode byte-identical to the old code, and the hostile-value suite passes.
  - Day 1 `values` before/after.
- [ ] **Event handlers** (`script_runtime.cpp:1499-1506` `HasHandler`, `event_bus.cpp:59-75,105-112`):
  - a `HashMap<String, u32>` count of waiters per event name, kept in step with subscribe and unsubscribe;
  - `Unsubscribe` removes in place (no rebuild);
  - `Take` matches natives by a name index;
  - delivery order is unchanged.
  - Day 1 `events` before/after.
- [ ] **The command overlay** (`command_buffer.cpp:108-127`): a `HashMap` keyed by (entity, field), cleared per callback with `Clear()` (the table is kept). The merge order is unchanged.
  Day 1 `overlay` before/after.
- [ ] **Tasks** (`task_pool.cpp:172-186,401-403`):
  - `taken` becomes a `HashSet`;
  - the `known` keys become a `HashMap`;
  - `script_runtime.cpp`'s `WakeWaiters`/`WakeTasks` (`:760-834`), `Compact` (`:409-423`) and `Reconcile` (`:338-350`) reuse member vectors (reserved, `Clear()`) instead of building new ones each tick;
  - the double sort in `Reconcile` becomes one.
  - Day 1 `tasks` before/after.
- [ ] **Queries and queues:**
  - `component_bindings.cpp:1137-1164` `RunQuery` selects the first `limit` above `after` with `NthElement` plus a sort of those only, and `SortIds` (`:1093-1132`) uses `Sort`;
  - `script_debug.cpp:308` and `script_runtime.cpp:582` use `Deque`;
  - `expr_backend.cpp:1024-1056` (globals, functions, natives looked up by name in the interpreter loop) resolves names once at compile time to indices, or uses a `HashMap<String, u32>`.
- [ ] **Sorted vectors kept:** the string-keyed `groups` in `script_runtime.cpp:142-226` and the `FindInstance`/`LoadedAt` binary searches stay.
  They give the fixed order the merge needs. Record that they are deliberate.
- [ ] **Determinism proof:**
  - `tests/script`, `tests/script_demo` and the C scripting sample hashes at 1 to 16 threads are unchanged;
  - `script_bench_check` passes;
  - the nojit tree (`build-nojit`, `scripts/test-phase.sh 27 --no-display --build-dir build-nojit`) passes.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (script, script_physics, script_media, engine_c), plain, then asan and tsan;
  - builds: both compilers, both unity modes; `luajit_boundary_check`;
  - `docs/guide/scripting.md` "Cost" updated; `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:**
- Resuming, value maps, handler checks, the overlay and task takes are O(1) per operation.
- Paging selects instead of sorting everything.
- The per-tick rebuilds reuse their storage.
- Every script hash is unchanged at 1 to 16 threads in both JIT modes, with the Day 1 numbers before and after.
