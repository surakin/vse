# Scripting design (Phase 27, Day 444, 2026-11-18)

Multithreaded gameplay scripting in Lua through LuaJIT, behind a language-backend seam. The headline requirement (the user, 2026-11-17): **scripted gameplay runs on
many threads; users are not limited to single-threaded script execution as in Unreal.** This spec fixes the open questions of `plans/phase-27-scripting.md` with
numbers from seven spikes (sources in `2026-11-18-scripting-spikes/`, a single throwaway program per group; machine: Linux x86-64, 32 hardware threads,
GCC 15, LuaJIT built from the pin below). Where a spike moved a plan default or showed the plan wrong, it says so (section 9).

## 1. Scope and layout

- **One module, `engine/script`**, above `jobs`, `ecs`, `settings`, `asset` and the foundation. Physics, audio and input bindings live in their own small targets
  (`script_physics`, `script_audio`, `script_input`) so `script` does not depend on them. The LuaJIT backend is `engine/script/luajit/` and is the only code that
  includes LuaJIT's headers (gate `luajit_boundary_check`, Day 445). A second tiny backend (`script_expr`, Day 447) proves the seam.
- **Not built:** other real language backends, script-visible threads, locks or atomics (never), a network debugger adapter, a visual editor.
- **Frame and units:** the engine's (metres, seconds, +Y up, right-handed).

## 2. The threading model (confirmed by spikes 1 to 3)

- A **script world** has `P` partitions (setting `script.partitions`, default **64**, 1 to 1024, independent of the thread count). An entity's scripts live in
  partition `Hash(stable entity id) % P`, fixed for the entity's life. A partition owns **one Lua state**, a command buffer, a timer set and an event queue.
- A tick runs the partitions as **jobs on the Phase 2 scheduler**. A partition's job is the only user of its state while it runs (an ownership token, atomic
  exchange, checked; a second concurrent entry aborts in debug and returns an error in release). **No Lua object crosses a state: only `Value` copies do.**
- **The world is read as of the tick start and changed only at sync points.** The parallel phase does not write the ECS at all; every write is a command in the
  partition's buffer. Own-entity reads after an own-entity write go through a small per-partition overlay, so `set_pos(e, p); get_pos(e)` returns `p`.
- **Merge order is `(partition, entity id, sequence)`.** Each partition runs its entities in id order and numbers its commands, so a partition's buffer is already
  sorted; the merge concatenates buffers in partition order and applies them. Events are delivered at the sync point in the same order and handled in the next
  phase. There is deliberately **no synchronous cross-entity call and no shared mutable Lua state**.
- **The result is identical for any thread count** for the same `P`; it may differ between `P` values (an entity's partition decides which random stream and
  which command order it sees). Spike 3 confirmed it (table 3).
- Parallel work started from scripts (`task.*`, Day 461) runs pure functions on a pool of task states over copied arguments; results arrive by latent wait in
  handle order.

## 3. World access: A or B (spike 2)

- **A. Deferred writes over the live ECS.** The ECS is immutable during the script phase because every write is a command, so scripts read the live components
  directly (the tick-start values) with no copy. Cost: the overlay for own-entity reads, and the system declares **read access** to the script-visible
  components and **write access** only at the merge (the Phase 17 schedule then keeps other writers of those components from running beside it).
- **B. A double-buffered published copy** of the components marked script-visible. Cost: a copy of every visible component every tick (the spike's tick
  included a snapshot copy of 3 floats per entity, about 0.3 ms for 100,000 entities; a real component set is larger), and a second place for the data to be
  stale. Benefit: other systems may write the live components while scripts run.
- **Recommendation: A.** The deferred-write rule already makes the live data stable, and the ECS schedule already orders conflicting systems. B is only worth it
  if a later phase needs a system that writes script-visible components concurrently with scripts; the merge point can then publish. Decision for the user.

## 4. The backend seam and the value model

- The backend interface (Day 446) is language-neutral: `Backend` creates `Context`s (one per partition) with a memory cap and budgets; a context loads a
  compiled `Chunk` (from a source or an optional bytecode cache), calls a named function with `Value` arguments, and reports errors as values. `Value` is a
  tagged union (nil, bool, i64, f64, string view, small vector 2/3/4, quaternion, entity id, list, map with a depth and size cap). Anything bigger is copied.
- **Binding style for hot calls: multiple numbers, not tables.** A Lua to C call costs **9 ns with the JIT and 11 ns in the interpreter** (spike 2), and
  `x, y, z = ent.pos(e)` allocates nothing. A `vec3` table is offered for convenience (the `math` binding, Day 453) and measured there; hot paths do not use it.
  C to Lua call overhead is **23 ns** (`lua_pcall` of a two-argument function).
- Every binding is marked `pure`, `world-read`, `world-write` or `main-thread-only` in the registry (Day 450); the marks decide which contexts may call it and
  are checked in debug.

## 5. Sandbox and budgets (spike 4)

- **Whitelist environment:** `base` minus `loadfile`, `dofile`, `load`, `loadstring`, `getfenv`, `setfenv`, `collectgarbage` and `require`; `string`, `table`,
  `math` (with `random` seeded per partition by the engine), `bit`, `coroutine`. No `io`, `os`, `package`, `debug`, `ffi`, and `jit` is hidden from scripts
  (the host keeps the C-side `luaJIT_setmode`). The spike confirmed `load` and `os`/`io` are absent from the state.
- **Memory:** a cap in the allocator works: string doubling and a table bomb fail with `not enough memory` in 5 to 20 ms and the state stays usable for close.
  **A state costs 12.7 KB empty and 28.3 KB with the whitelist** (35.9 KB with every library), 17 to 25 us to create (spike 1). 64 states are 1.8 MB, 256 are 7 MB.
  LuaJIT's compiled machine code is allocated outside the Lua allocator (`mmap`), so the memory cap does not bound it; the per-state mcode size is a
  `jit.opt` parameter (`sizemcode`), set by the backend (Day 449).
- **The instruction budget works only with the JIT off** (the finding that amends the plan, section 9). `lua_sethook` with a count hook, set before the run,
  does **not** interrupt a trace-compiled loop: with the JIT on, `while true do end` never returned (the run was killed after 60 s), a hook set from a watchdog
  thread did not stop it either, and a 2e8-iteration loop ran at the same speed with and without the hook (it was compiled). With the JIT off the hook fires:
  an infinite loop was stopped in 33 to 73 ms.
  **Measured cost (2e8-iteration arithmetic loop): JIT 481 ms, interpreter 698 ms (1.45 times), interpreter with a count hook every 1000 instructions 1,220 ms
  (2.5 times the JIT).** A budgeted world therefore runs its states with the JIT off; a trusted world (the game's own scripts) may run with the JIT and no
  instruction budget. This is a configuration choice (`script.jit`, `script.budget`), put to the user at the end of Day 444.
- **A budget error must not be catchable.** `pcall` and `coroutine.resume` swallowed the hook's error and the script looped on (`while true do pcall(...)`
  ran until killed). The sandbox replaces `pcall`, `xpcall` and `coroutine.resume`/`wrap` with C versions that re-raise once the budget has tripped; with them
  the same script stopped in 89 ms. (Day 449.)
- **Pattern matching is not interruptible.** `string.find` on a backtracking pattern ran **52 seconds** inside one C call and no hook fired. The sandbox must
  replace `string.find`, `match`, `gmatch` and `gsub` with engine versions that count matcher steps (or restrict patterns), not patch LuaJIT. (Day 449; filed
  in `KNOWN-ISSUES.md` until then.)
- **Hostile corpus results** (JIT off, 16 MB cap, 50 million instruction budget, 0.5 s wall budget): infinite loop, string doubling, deep recursion
  (`stack overflow`), table bomb, `__gc` bomb, a coroutine that never yields, `pcall` swallow, huge `string.rep`: all stopped in under 140 ms except the pattern
  case above. `__gc` finalizers run inside the budget.
- A wall-clock budget is checked in the same hook (and per partition job, so one slow script cannot stall a tick forever: the job returns an error and the
  entity's script is disabled until reload).

## 6. Latent waits (spike 5)

- `wait(seconds)`, `wait_frames(n)` and `wait_event(name)` are `coroutine.yield` with a request value; the scheduler is C (a min-heap or timer wheel per
  partition keyed by the simulation clock, then by `(entity id, sequence)` for ties). Nothing blocks a thread.
- **100,000 waiting coroutines cost 490 bytes each (49 MB total) and 32 ms to create; a resume is 73 ns** (6.75 million resumes in 0.49 s, including the naive
  scan of every waiter each tick that the real scheduler replaces with a heap). Yield across `pcall` works in LuaJIT (`pcall(function() yield() end)`).
- What Blueprint needs from it (so Phase 31 can land on it): `Delay` is `wait`; `Timeline` is a `wait_frames(1)` loop that reads the clock; an async action is a
  call that returns a completion handle plus `wait_event(handle)`; `Latent` nodes in a graph become straight-line code in one coroutine. The coroutine is
  owned by the script instance; destroying the entity destroys it (checked: the reference is released at the sync point).

## 7. Assets and bytecode (spike 6)

- **Source is minted and validated; bytecode is an optional per-architecture cache, never required.** LuaJIT bytecode starts with `1b 4c 4a 02` (version) and
  a flags byte that includes the GC64 and endianness bits; a mismatch fails the load, which falls back to the source.
- **1,000 scripts of 2.3 KB: compile from source 42 ms (42 us each); load unstripped bytecode 3.4 ms (3.4 us each), 12 times faster; bytecode is 1.7 times
  larger unstripped.** A 1,000 script project compiles in 42 ms per partition state; with `P` = 64 that is 2.7 s of CPU spread over the job threads at load, or
  one compile and 64 bytecode loads (about 0.2 s). The plan's choice stands: compile once per partition from the in-memory source (or the cache), keep the
  source in the asset for debugging and hot reload.

## 8. Building and pinning LuaJIT (spike 7)

- **LuaJIT has no release tags; its `v2.1` branch is the release.** The version string is `2.1.<commit timestamp>`. **Pin: commit
  `63c8f1a9b479ba5518cf6a170ff7729178bc3cfd` (2026-11-11, version 2.1.1794386141), GitHub archive tarball
  `https://github.com/LuaJIT/LuaJIT/archive/63c8f1a9b479ba5518cf6a170ff7729178bc3cfd.tar.gz`, SHA-256
  `8cad76f444b76001b7123dc626c5d0bfc24f1f790333737443345f7e6b41bb5f`, 1,104,417 bytes.** MIT licence (`COPYRIGHT` is copied into `third_party/luajit/`).
- **`LJ_GC64` is on by default on x86-64 and arm64** in this version (and mandatory on macOS), so `lua_newstate` with a custom allocator works (the older
  non-GC64 x64 build refused a custom allocator). Verified: `lua_newstate(alloc, ud)` returns a state on the default build.
- Built here with `make BUILDMODE=static` in 3 s (`libluajit.a`, 1.0 MB with the JIT, 0.64 MB with `-DLUAJIT_DISABLE_JIT`); the interpreter-only build ran the
  whole determinism table and **its hash equals the JIT build's** (`3a03339c4680a91` at `P` = 16). The Windows build is `msvcbuild.bat static` (present in the
  tree, run by the CMake build through `cl` from the toolchain files); macOS needs `MACOSX_DEPLOYMENT_TARGET` set (the Makefile errors without it) and builds
  x86-64 and arm64 with `TARGET_FLAGS=-arch ...`; neither was run today (Days 473 and 474).
- The engine builds LuaJIT's sources itself in CMake (vendored, no `make` at build time), so the gates, the flags, the unity rules and the warnings policy
  stay ours; LuaJIT's files are excluded from the warnings-as-errors rule like the other third-party code. The `buildvm` host tool step needs a plan on Day
  445 (the cross-build for arm64 uses a host `buildvm`).

## 9. Findings that amend the plan

1. **A hook-based instruction budget does not work with the JIT on** (section 5). Day 449's "does it still allow the JIT to compile hot loops? measure" is
   answered: it does not stop them. Budgeted worlds run interpreter-only; the budget and the JIT are a choice per world. Day 470's seam gains this.
2. **`pcall`, `xpcall` and `coroutine.resume` must be replaced** so a budget error cannot be swallowed (Day 449).
3. **The pattern functions must be replaced** by engine versions with a step count (Day 449). Not in the plan before.
4. **The tick-start copy is not needed under design A** (section 3); Day 452's "world view" is the live ECS read-only plus the overlay.
5. **Mcode is outside the memory cap** (Day 449 sets `sizemcode`; Day 476 measures).
6. The determinism contract holds with the JIT on and off in this spike, but **LuaJIT does not promise it across JIT and interpreter** (it held here, on x86-64); the replay test (Day 469) runs in the same mode it was recorded in.

## 10. Measurements (appendix)

Spike 2 and 3: 100,000 entities, each `on_update(e, neighbour, dt)` reads its own and a neighbour's position and sets a velocity (two Lua to C calls, one
random number), 20 ticks, JIT on. Milliseconds per tick, the merge (applying 100,000 velocity commands and integrating) included in the figure:

| P \ threads | 1 | 2 | 4 | 7 | 8 | 16 |
|---|---|---|---|---|---|---|
| 16 | 8.66 | 5.54 | 3.24 | 2.34 | 2.06 | 1.67 |
| 64 | 8.21 | 5.36 | 3.06 | 2.11 | 1.91 | 1.54 |
| 256 | 8.28 | 5.90 | 3.43 | 2.52 | 2.31 | 1.99 |

The merge alone is 0.33 to 0.48 ms per tick. **The state hash at the end is identical for 1, 2, 4, 7, 8 and 16 threads at every `P`** (`3a03339c4680a91` at
16, `a82368432a52b1b1` at 64, `4efd56077584f808` at 256; the values differ between `P` because the random stream is per partition). **`P` = 64 is the
default: the three `P` values scale alike, 64 gives the best time at every thread count here, and 64 partitions keep a 16-thread machine busy with a skew
margin; 16 would starve 16 threads under load imbalance; 256 costs 7 MB of states and measured slower.** (The 100,000-entity tick is 8.2 ms on one thread, so
at 60 Hz one thread runs about 200,000 such scripts; sixteen threads about a million.)

Spike 1: 8 threads, each owning 8 states (with a 32 MB cap), running four different chunks (arithmetic loop, string building, recursion, table sort) and
re-creating a random state every 16 runs, for 20 s: 430,436 chunk runs, 0 failures, plain; **under AddressSanitizer: 306,987 runs, 0 reports; under
ThreadSanitizer: 275,867 runs, 0 failures and 3 reports, all inside `libgcc_s.so`'s unwinder** (the heap blocks the unwinder allocates and moves itself; LuaJIT
uses the unwinder for error throws; none in our code or LuaJIT's C). The sanitizer builds link a plain LuaJIT (its assembler core and generated code cannot be
instrumented), so they cover the engine's side of the boundary and LuaJIT's C code, not the VM's assembly; the shared-state question is answered by the run's
8-thread behaviour and by LuaJIT's design (each `lua_State` has its own `global_State`; the JIT's mcode areas and string table are per state). A
`race:libgcc_s` TSan suppression is needed for the unwinder (Day 451, `KNOWN-ISSUES.md`).
Create and destroy a whitelisted state: 16.5 us plain, 52.7 us under TSan, 96.4 us under ASan.
