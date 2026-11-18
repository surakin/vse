# Phase 27 — Scripting (Lua/LuaJIT), multithreaded

**Status: in progress. Days 444 (spec and spikes), 445 (LuaJIT vendored) and 446 (the backend interface) done 2026-11-18, `CURRENT_PHASE` is 27; the Day 444 decisions (pin, `P` = 64, world access A, budget mode per world) were accepted by the user. Phase 28 (3D text) moved from Days 444-459 to Days 478-493.**

**Depends on:** Phase 17 (ECS: components, the schedule, the structural-change rules), Phase 2 (jobs: the partitions run as jobs), Phase 22 (minted script assets, the resource manager), Phase 19 (storage: saving script state), Phase 11 (settings), Phase 9 (file layer: hot reload), Phases 23-24 (the C API), Phase 25 (physics bindings), Phase 26 (audio bindings) and Phases 13 and 21 for the Windows and macOS days.
**Goal:** embedded gameplay scripting through a real, well-known language (Lua via LuaJIT, root `PLAN.md` assumption #13) behind a pluggable language-backend seam, **with scripted gameplay that runs on all cores**: users are not limited to single-threaded script execution as in Unreal (the user, 2026-11-17). Also the compilation and execution target for Phase 32's Blueprint importer, so the binding set, the event model and the latent waits keep Blueprint's semantics in mind.
**Definition of done:** a LuaJIT gameplay script spawns and moves ECS entities through the engine's bindings; thousands of scripted entities run their callbacks on every core and the world state is **bit-identical at any thread count**; scripts wait latently, react to physics and input events, play audio, start parallel tasks, are minted and hot-reloaded, save and replay exactly; a second tiny language backend passes the same conformance suite; a C host drives all of it; the hashes agree on Linux, Windows and macOS.

## Days

444. [Design Spec, Spikes and Phase Setup](phase-27-scripting/day-444-design-spec-and-spikes.md)
445. [Vendoring LuaJIT behind a Boundary Gate](phase-27-scripting/day-445-vendor-luajit.md)
446. [The Script Module and the Language-Backend Interface](phase-27-scripting/day-446-script-module-and-backend-interface.md)
447. [A Second Backend and the Conformance Suite](phase-27-scripting/day-447-test-backend-and-conformance.md)
448. [The LuaJIT Backend I: States, Loading, Calls and Errors](phase-27-scripting/day-448-luajit-backend-i-states-and-calls.md)
449. [The LuaJIT Backend II: The Sandbox and the Budgets](phase-27-scripting/day-449-luajit-backend-ii-sandbox-and-budgets.md)
450. [Value Marshalling and the Binding Registry](phase-27-scripting/day-450-values-and-binding-registry.md)
451. [Threading I: Partitions, the State Pool and the Ownership Rules](phase-27-scripting/day-451-threading-i-partitions-and-state-pool.md)
452. [Threading II: The World View, Command Buffers and the Ordered Merge](phase-27-scripting/day-452-threading-ii-world-view-and-command-buffers.md)
453. [Math, Log and Time Bindings](phase-27-scripting/day-453-math-log-time-bindings.md)
454. [Settings Bindings](phase-27-scripting/day-454-settings-bindings.md)
455. [ECS Bindings I: Entities and Transforms](phase-27-scripting/day-455-ecs-bindings-i-entities-and-transforms.md)
456. [ECS Bindings II: Components, Queries and Render Components](phase-27-scripting/day-456-ecs-bindings-ii-components-and-queries.md)
457. [The Script Component, the Lifecycle and the Script System](phase-27-scripting/day-457-script-component-and-serial-system.md)
458. [The Parallel Script System on the Job Scheduler](phase-27-scripting/day-458-parallel-script-system.md)
459. [Events and Callbacks](phase-27-scripting/day-459-events-and-callbacks.md)
460. [Latent Waits: Coroutines in Scripts](phase-27-scripting/day-460-latent-waits.md)
461. [Script Tasks: Parallel Work Started from Lua](phase-27-scripting/day-461-script-tasks-parallel-work-from-lua.md)
462. [Minted Script Assets I: The Format and the Minter](phase-27-scripting/day-462-minted-script-assets-i.md)
463. [Minted Script Assets II: Loading, Dependencies and the Resource Manager](phase-27-scripting/day-463-minted-script-assets-ii-loading.md)
464. [Hot Reload](phase-27-scripting/day-464-hot-reload.md)
465. [Debugging: Errors, Hooks and the Console](phase-27-scripting/day-465-debugging.md)
466. [Physics Bindings I: Bodies, Forces and Queries](phase-27-scripting/day-466-physics-bindings-i.md)
467. [Physics Bindings II: Contact and Trigger Events and the Character Controller](phase-27-scripting/day-467-physics-bindings-ii-events-and-character.md)
468. [Audio and Input Bindings](phase-27-scripting/day-468-audio-and-input-bindings.md)
469. [Persistence, Snapshots and Replay](phase-27-scripting/day-469-persistence-snapshot-and-replay.md)
470. [The JIT-versus-Interpreter Platform Seam](phase-27-scripting/day-470-jit-and-interpreter-seam.md)
471. [C API I: Loading Scripts, Calling Functions and Host Functions](phase-27-scripting/day-471-c-api-i-scripts-and-calls.md)
472. [C API II: Events, Tasks and the Scripting Sample](phase-27-scripting/day-472-c-api-ii-events-and-sample.md)
473. [Windows: LuaJIT on MSVC and clang-cl, the Tests and the Hashes](phase-27-scripting/day-473-windows-luajit-and-tests.md)
474. [macOS: LuaJIT on Apple Clang, the Tests and the Hashes](phase-27-scripting/day-474-macos-luajit-and-tests.md)
475. [A Scripted Gameplay Demo: Physics, Audio and Many Threads](phase-27-scripting/day-475-scripted-physics-demo.md)
476. [Benchmarks, Stress and the Limits](phase-27-scripting/day-476-benchmarks-stress-and-limits.md)
477. [Docs, Closing Runs and the Phase Review](phase-27-scripting/day-477-docs-closing-runs-and-review.md)

Order reasoning: the spec and its seven spikes (444) settle the threading model, the sandbox and the asset format on measurements before code; LuaJIT is vendored behind a gate (445); the language-neutral backend interface and a second backend with a conformance suite (446-447) come before the real backend so the seam is not shaped by Lua; the LuaJIT backend and its sandbox (448-449); bindings need a registry with thread marks (450); **the multithreading core (451-452: partitions, ownership, world view, command buffers, ordered merge) comes before any binding that touches the world**, so no binding is ever written for a single-threaded world and retrofitted; then the bindings (453-456), the script component with a serial system first (457) and the parallel system (458) as soon as it can be tested against the serial one; events, latent waits and tasks (459-461) build on the sync points; assets and loading (462-463), hot reload (464) and debugging (465) need running scripts; physics, audio and input (466-468) need the event bus and the command buffers; persistence and replay (469) read the finished state model; the JIT seam (470) and the C API (471-472) are built on a settled core; the PC and the Mac (473-474) check the hashes; the demo (475), benchmarks (476) and the closing day (477) last. A phase cut after Day 458 would still have multithreaded scripting with ECS bindings.

## Decisions fixed by the plan (the Day 444 spec confirms or amends them)

- **One module, `engine/script`, above `jobs`, `ecs`, `settings` and `asset`;** physics, audio and input bindings live in their own small targets so `script` itself does not depend on them. LuaJIT is the first backend, vendored under `third_party/luajit/`, included only by `engine/script/luajit/` (a gate). A second tiny backend (`script_expr`) proves the seam.
- **The threading model (the user's requirement):** a script world has `P` partitions (default 64, a setting, independent of the thread count); an entity's scripts live in partition `hash(stable id) % P`; each partition owns one Lua state, used by one job at a time (an ownership token, checked); a tick runs the partitions as jobs on the Phase 2 scheduler. **No Lua object ever crosses a state; only `Value` copies do.** Scripts read the world as of the tick start, see their own entity's writes at once through an overlay, and change the world only through per-partition command buffers merged in `(partition, entity id, sequence)` order at a sync point. Events are delivered at sync points in a fixed order and handled in the next phase. **The world state is identical for any thread count** (it may differ between `P` values). There is deliberately **no synchronous cross-entity call and no shared mutable Lua state**; the guide explains why and what to do instead.
- **Parallel work from scripts:** `task.run`, `task.map` and `task.parallel_for` run pure functions on a pool of task states over copied arguments; results arrive by latent wait in handle order.
- **Latent waits** are coroutines owned by the instance and woken by a C scheduler on the simulation clock; nothing ever blocks a thread.
- **Sandbox:** a whitelist environment (no `io`, `os`, `package`, `debug`, `ffi`), instruction, time and memory budgets, a memory cap in the allocator, bounded behavior against a hostile corpus; scripts are untrusted input.
- **Assets:** scripts are minted (`asset_mint script`: validated, deterministic, id-based dependencies, an optional per-architecture bytecode cache that is never required), loaded atomically through the resource manager, compiled once per partition; hot reload swaps all partitions at one sync point, a broken edit keeps the old version.
- **Settings and persistence:** scripts reach settings only through the settings system (typed, validated, allow-listed prefixes, secrets unreadable); a script instance's marked persistent values save through `engine/storage`; sessions replay to the same hash across thread counts.
- **JIT versus interpreter** is a configuration choice (`script.jit`, `ENGINE_LUAJIT_JIT`), both modes tested (Day 470); iOS and console decisions stay with Phases 29-30.
- **C API:** scripts, calls, host functions, events and tasks through `engine_c` (Days 471-472), as for physics and audio; hosts run script worlds on their own threads.
- **Wrapped types only**, `os::CommandLine`, settings for every configurable, files through `io::`, `build_defs.h` for platform checks.

## Notes / open questions

- **Decided by the user on 2026-11-18 (after the Day 444 spikes):** LuaJIT pin `63c8f1a9b479ba5518cf6a170ff7729178bc3cfd`; default `P` = 64; world access design A (deferred writes over the live ECS, no copy); budget mode per world (budgeted = interpreter-only with the instruction hook, trusted = JIT). Amended: Days 449, 452, 470.
- Settled by the Day 444 spikes: the exact world-access design (A or B), the default `P`, the value representation of vectors in Lua, the bytecode policy, the LuaJIT pin; each is put to the user at the end of Day 444 before Day 445.
- Phase 32 (Blueprint import) will likely surface gaps in binding and event coverage (loops, latent waits, casts, interfaces); expect to revisit this phase's scope then (Day 477 writes the mapping notes).
- Not in this phase: other language backends than the test one (decided against real demand), a network debugger adapter (the debug transport interface is there; an adapter for an external debugger is later), script-visible threads or locks (never), a visual scripting editor (Phase 33 and 32).
- This is the largest phase planned so far (34 days).

- **Settings from scripts (from Phase 11 Day 139):** scripts read and set settings by dotted key through `settings::Settings` (typed `Get`/`Set` validated against the registry, so a script cannot store an out-of-range or wrongly typed value), subscribe with prefix listeners, and never open the configuration file or read the environment themselves; secret settings are not readable from scripts.
