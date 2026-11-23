# Phase 27 / Day 444 — Design Spec, Spikes and Phase Setup

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Phases 17, 22, 23-26, 11.

Settle the scripting design on measurements before any code: LuaJIT in many states on many threads, the world-access model, the cost of a state, the sandbox, the coroutine model. **`CURRENT_PHASE` becomes 27 in this commit.**

## Tasks

- [x] Write `docs/superpowers/specs/2026-11-NN-scripting-design.md`: the goals (below), the threading model, the language-backend interface, the value model, the binding style, the event model, the asset format, the hot-reload rule, the debugging surface, the JIT-vs-interpreter seam, the risks.
- [x] **The headline requirement (the user, 2026-11-17): scripted gameplay runs on many threads; users are not limited to single-threaded script execution as in Unreal.** The spec states the model the plan assumes and the spikes confirm or amend it: a *script world* has `P` partitions (a setting, default 64, **independent of the thread count**); every scripted entity lives in the partition `hash(stable entity id) % P`; a partition owns exactly one Lua state, used by one job at a time; a tick runs the partitions as jobs on the Phase 2 scheduler; **results are identical for any thread count** with the same `P`.
- [x] World access under that model (the spike compares two designs and recommends one): (A) a script reads other entities' components as of the tick start and writes only through a per-partition command buffer merged in `(partition, entity id, sequence)` order at a sync point, with its own entity's writes visible to itself at once through an overlay; (B) a double-buffered published copy of the components marked script-visible. Either way no script touches another thread's data.
- [x] Spike 1, LuaJIT in many states: 64 states created and destroyed in a loop, each with a custom allocator and a memory cap, running different chunks on 8 threads for 60 s under tsan and asan; confirm no shared mutable state between states (the JIT's mcode areas, the string interning, `lj_` globals); memory per empty state, per state after the standard library whitelist, and the JIT-compiled code per state; the time to create a state.
- [x] Spike 2, a script tick at scale: 100,000 scripted entities, each an `on_update` that reads its transform and writes a velocity, across `P` = 16, 64 and 256 partitions on 1, 4, 8 and 16 threads; the time per tick and the scaling curve; the cost of the command-buffer merge; the call overhead Lua to C and C to Lua.
- [x] Spike 3, determinism: the same scene at 1, 2, 7 and 16 threads and at three `P` values (the state is allowed to differ between `P` values, never between thread counts); the merge order and the event delivery order that make it so.
- [x] Spike 4, the sandbox: the library whitelist (`base` minus `loadfile`/`dofile`/`load` with text, `string`, `table`, `math` minus `random` seeded by the engine, `bit`, `coroutine`; no `io`, `os`, `package`, `debug`, `ffi`, `jit.*` control from scripts), an instruction budget through `lua_sethook` with a count (does it still allow the JIT to compile hot loops? measure with and without), a wall-clock budget, a memory cap through the allocator; a hostile corpus (infinite loops, a string-doubling loop, deep recursion, a metatable `__gc` bomb, a coroutine that never yields).
- [x] Spike 5, coroutines as latent waits: a `wait(seconds)`, `wait_frames(n)`, `wait_event(name)` built on `coroutine.yield` with the scheduler in C; the cost of 100,000 waiting coroutines; what Blueprint's latent nodes (`Delay`, `Timeline`, async actions) need from it, so Phase 34 can land on it.
- [x] Spike 6, assets and bytecode: LuaJIT bytecode is not portable across architectures or versions; compare shipping source (hash-checked, compiled at load) and shipping per-architecture bytecode; the cost of loading 1,000 scripts both ways; recommend (the plan assumes: source is minted and validated, bytecode is an optional per-architecture cache keyed by the LuaJIT version, never required).
- [x] Spike 7, building LuaJIT: the pinned version and its build on Linux, MSVC (`msvcbuild.bat`) and macOS x86-64 and arm64, with and without `LUAJIT_DISABLE_JIT`; the `LJ_GC64` question and its effect on the custom allocator; the licence (MIT) in `third_party/`; recommend the pin and write it down with its SHA-256.
- [ ] Decisions to put to the user at the end of the day, before Day 445: the pin, the default `P`, the world-access design (A or B), and anything a spike showed to be wrong in this plan; amend the plan files in the same commit when the user decides.
- [x] `CURRENT_PHASE` set to 27; `tests/phases.txt`, `STATUS.md` (Phase 27 in progress) and `PLAN.md` (roadmap row, assumption #13's threading amendment, progress log) updated. (`tests/phases.txt` needs no line: no test file or `add_test` exists yet; Day 445 adds the first.)
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The spec and the measurements are committed, the plan reflects the user's decisions, and `CURRENT_PHASE` is 27. (Planning and spikes only: no product code, so no code review.)


## Outcome (2026-11-18)

Spec: `docs/superpowers/specs/2026-11-18-scripting-design.md`; spike sources `docs/superpowers/specs/2026-11-18-scripting-spikes/`. The threading model held:
the same scene gives the same state hash at 1, 2, 4, 7, 8 and 16 threads for `P` = 16, 64 and 256; 100,000 scripted entities tick in 8.2 ms on one thread and
1.5 ms on 16 (`P` = 64). **Three things were wrong in the plan** (spec section 9): the instruction budget does not work with the JIT on, `pcall` and
`coroutine.resume` swallow budget errors, and `string.find` on a backtracking pattern ran 52 s with no hook firing. The pin is
`63c8f1a9b479ba5518cf6a170ff7729178bc3cfd`. Not run: MSVC and macOS builds of LuaJIT (Days 473 and 474), `LUAJIT_DISABLE_JIT` was run on Linux. The decisions below
are open until the user answers: the pin, `P`, world access A or B, the budget-versus-JIT policy. No product code, so no code review.
