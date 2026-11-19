# Phase 32 — Unreal Blueprint Import

**Depends on:** Phase 31 (Unreal asset & level import) for the shared
Unreal DLL embedding/package-loading setup, and Phase 27 (Scripting:
Lua/LuaJIT) as the execution target.
**Goal:** bring in gameplay *logic* authored as Unreal Blueprints, not
just static content — the other headline half of the Unreal import
objective in root `PLAN.md`.
**Definition of done:** a simple real Blueprint (a few event nodes, a
branch, a non-timeline actor move/rotate) imported from a real Unreal
project runs correctly in this engine, transpiled to Lua and executed via
Phase 27's scripting subsystem.

## Why this is the hardest sub-project in the whole plan

Flag this plainly: **pursued to full fidelity, this phase could rival the
size of the rest of the engine combined.** Blueprints compile to a graph
of nodes (`UEdGraph`) that Unreal's own editor further compiles to
internal bytecode (the Kismet/K2 VM, executed via `UObject::
ProcessEvent`), with an enormous standard node library (flow control,
casts, latent/async nodes, timelines, interfaces, macros). Default to
**breadth-first coverage of the most common node types, explicitly
punting long-tail nodes** with a clear "unsupported node" marker, rather
than chasing 100% coverage before anything ships.

## Strategy decision (defer to when this phase starts)

Two candidate approaches — pick with real sample Blueprints in hand, not
in the abstract:

- **(a) Graph-level transpilation (recommended default).** Read the
  Blueprint's node graph as live loaded `UEdGraph`/`UK2Node_*` objects
  via the same Unreal DLL embedding Phase 31 sets up — no raw byte parsing
  needed, UE's own reflection gives us the graph structure directly — and
  transpile it into Lua source (or precompiled LuaJIT bytecode), node by
  node, run through Phase 27's scripting subsystem. Higher fidelity,
  produces real inspectable/editable content in our own engine rather
  than a permanent shim.
- **(b) Reflection-driven interpretation.** Walk the graph at runtime
  against an interpreter mimicking Kismet semantics directly, skipping
  transpilation. Simpler to get started, likely slower, produces no
  native/editable output, and sidesteps Phase 27's Lua backend entirely.

## Sub-tracks (to be expanded into day-by-day tasks when started)

1. Blueprint graph export (built on Phase 31's Unreal DLL embedding — walk
   the loaded `UEdGraph`/`UK2Node_*` objects via UE's own reflection).
2. Node coverage: start with event nodes (BeginPlay, Tick), flow control
   (branch, sequence), variable get/set, function calls, casts, basic math
   — expand breadth-first from there based on real Blueprints encountered.
3. Transpiler (or interpreter, per the strategy decision) targeting
   Phase 27's Lua/LuaJIT scripting subsystem.
4. Unsupported-node reporting: a Blueprint that uses an unimplemented node
   should import with a clear, visible marker on the failure point, not
   silently produce wrong behavior.

## Explicitly out of scope initially

Latent/async nodes (delays, timelines), Blueprint interfaces, macro
libraries, and Blueprint-to-Blueprint communication — each adds real
complexity and should be scoped in once the basic event/branch/function
path is proven end to end.

## Landing notes from Phase 27 (written on Day 477): how Blueprint semantics map to the scripting model

Phase 27's scripts are multithreaded by design (partitions, tick-start reads, command buffers, one-tick events, tasks that see no world; `docs/guide/scripting.md`, `docs/guide/scripting-and-threads.md`). A Blueprint is single-threaded and
synchronous, so the transpiler must keep every node on one of the shapes below, and say "unsupported" for the rest. Nothing here needs a change to Phase 27; the recommended transpilation is the right-hand column.

| Blueprint feature | Maps to | Transpile as |
|---|---|---|
| Event graph, `BeginPlay`, `Tick`, `EndPlay` | behavior callbacks | `on_start`, `on_update(self, dt)`, `on_destroy` of one `behavior('<BlueprintName>', {...})`; one behavior per Blueprint class, one instance per actor |
| Custom event, event dispatcher (bind, call) | named events | `event.on('<Dispatcher>', fn)` in `on_create`; `event.emit` for a call to all bound; one tick of latency |
| Call a custom event on another actor | `event.emit_to(actor, name, payload)` | direct; the call returns nothing (a Blueprint event never does) |
| Function call on `self` or a pure library function | a Lua local function, or a binding (`num.*`, `vec3.*`) | direct |
| Function with outputs on **another** actor, or an interface call whose result is used | **not available** (it would call into another partition synchronously) | request event + reply event one tick later (`event.emit_to(other, 'get_health', {reply_to=self.entity})`), and the nodes after the call move into the reply handler; mark the generated code with a comment |
| Interface message to an actor that may not implement it | `emit_to` | the target's behavior ignores events it does not handle; the result of "does implement" is unsupported (emit and see) |
| Cast to a class / `Is Valid` | component and behavior checks | `component.has(entity, 'name')` and `entity.valid`; a cast fails the same way (the pin is not executed) |
| Get/Set a variable of **another** actor | a component read; a component write | `component.get/set` on a component the importer declares per Blueprint class; the read sees the tick-start value, the write lands at the sync point, so "set then immediately read it on the other actor" is unsupported |
| Variable of `self` | field of `self` | `self.<name>`; persisted state goes in a component so it saves and replays |
| `Delay`, `Retriggerable Delay` | latent waits | `wait(seconds)` in a spawned coroutine; retriggerable = `stop(handle)` then `spawn` |
| Timeline | `tween(from, to, seconds, easing, fn)` | direct for float tracks; curve-asset tracks need a sampled table |
| `Set Timer by Event` / `by Function Name` | `after` / `every` | direct, with the returned handle for clear/pause |
| Async actions (`Async Load Asset`, `MoveComponentTo`) | `wait_event` on an engine event, or a task | per node; the engine's own events are `on_asset_loaded`, physics `on_collision_*`, `on_trigger_*` |
| `For Each Loop`, `While`, `Sequence`, `Branch`, `Switch`, `Do N`, `Gate` | Lua control flow | direct; `While` runs against the instruction budget (an endless Blueprint loop ends with an error, as Unreal's infinite-loop guard does) |
| Spawn Actor, Destroy Actor | `entity.spawn`, `entity.destroy` | direct; the new entity exists after the merge (a placeholder handle works until then) |
| Overlap / Hit events | `on_trigger_enter/exit`, `on_collision_begin/end` | direct |
| Input actions | `on_input` and the input bindings | direct |
| Get All Actors Of Class | `physics.overlap_*`, or a query binding | per use; a query reads the tick-start view |
| Blueprint macros | inlined at import | expand into the caller |
| Blueprint-to-Blueprint **synchronous** state sharing, static/global variables (Game Instance, subsystems) | **not available** (scripts share no Lua state) | keep the state in a component on one owner entity, change it through events sent to the owner; the importer reports each use as a design decision |
| Multithreaded Blueprint (rare) | n/a | all Blueprints run in the model above |

What this means for fidelity: a Blueprint whose logic is "react to events, change own state, send events to others, wait" imports exactly; one that reads another actor's result in the middle of a node chain needs the reply-event rewrite
(the importer can generate it for the common shape "call, then use the result in the next nodes"). Order between actors within one tick is not observable by design, so Blueprints that depend on actor tick order are reported, not guessed.

## Notes / open questions

- Inherits Phase 31's EULA review blocker (root `PLAN.md` assumption #6) —
  this phase also links the Unreal DLL, so it can't start until that's
  resolved either.
- Full day-by-day breakdown deferred until Phase 27 is complete and real
  sample Blueprints have been inspected.
- This phase is the most likely candidate in the entire plan to need
  splitting further once started — treat the sub-tracks above as a
  starting decomposition, not a final one.
