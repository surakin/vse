# Phase 22 — Unreal Blueprint Import

**Depends on:** Phase 21 (Unreal asset & level import) for the shared
Unreal DLL embedding/package-loading setup, and Phase 18 (Scripting:
Lua/LuaJIT) as the execution target.
**Goal:** bring in gameplay *logic* authored as Unreal Blueprints, not
just static content — the other headline half of the Unreal import
objective in root `PLAN.md`.
**Definition of done:** a simple real Blueprint (a few event nodes, a
branch, a non-timeline actor move/rotate) imported from a real Unreal
project runs correctly in this engine, transpiled to Lua and executed via
Phase 18's scripting subsystem.

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
  via the same Unreal DLL embedding Phase 21 sets up — no raw byte parsing
  needed, UE's own reflection gives us the graph structure directly — and
  transpile it into Lua source (or precompiled LuaJIT bytecode), node by
  node, run through Phase 18's scripting subsystem. Higher fidelity,
  produces real inspectable/editable content in our own engine rather
  than a permanent shim.
- **(b) Reflection-driven interpretation.** Walk the graph at runtime
  against an interpreter mimicking Kismet semantics directly, skipping
  transpilation. Simpler to get started, likely slower, produces no
  native/editable output, and sidesteps Phase 18's Lua backend entirely.

## Sub-tracks (to be expanded into day-by-day tasks when started)

1. Blueprint graph export (built on Phase 21's Unreal DLL embedding — walk
   the loaded `UEdGraph`/`UK2Node_*` objects via UE's own reflection).
2. Node coverage: start with event nodes (BeginPlay, Tick), flow control
   (branch, sequence), variable get/set, function calls, casts, basic math
   — expand breadth-first from there based on real Blueprints encountered.
3. Transpiler (or interpreter, per the strategy decision) targeting
   Phase 18's Lua/LuaJIT scripting subsystem.
4. Unsupported-node reporting: a Blueprint that uses an unimplemented node
   should import with a clear, visible marker on the failure point, not
   silently produce wrong behavior.

## Explicitly out of scope initially

Latent/async nodes (delays, timelines), Blueprint interfaces, macro
libraries, and Blueprint-to-Blueprint communication — each adds real
complexity and should be scoped in once the basic event/branch/function
path is proven end to end.

## Notes / open questions

- Inherits Phase 21's EULA review blocker (root `PLAN.md` assumption #6) —
  this phase also links the Unreal DLL, so it can't start until that's
  resolved either.
- Full day-by-day breakdown deferred until Phase 18 is complete and real
  sample Blueprints have been inspected.
- This phase is the most likely candidate in the entire plan to need
  splitting further once started — treat the sub-tracks above as a
  starting decomposition, not a final one.
