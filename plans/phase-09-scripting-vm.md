# Phase 9 — Scripting VM

**Depends on:** Phase 4 (ECS).
**Goal:** a custom bytecode VM for gameplay scripting — also the
compilation/interpretation target for Phase 10's Blueprint importer, so
its design should keep Blueprint's semantics (event graphs, latent nodes,
function calls, casts) in mind even though this phase itself doesn't parse
any Unreal data.
**Definition of done:** the custom bytecode VM runs a gameplay script that
spawns and moves an ECS entity.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Bytecode format definition.
- A compiler front-end for our own minimal scripting language (syntax TBD
  when this phase starts — keep it simple; this is not a general-purpose
  language design exercise).
- VM/interpreter (stack-based or register-based — decide against real
  performance data, not speculatively).
- Engine API bindings: entity spawn/destroy, component get/set, basic
  math/logging calls callable from scripts.
- Event/callback model (e.g. "on tick," "on collision") — needed both for
  native gameplay scripting and, later, as the landing target for
  Blueprint's event graphs (Phase 10).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- Phase 10 (Blueprint import) will likely surface gaps in this VM's node
  coverage (loops, latent/async waits, casts, interfaces) — expect to
  revisit this phase's scope once Phase 10 starts, rather than trying to
  anticipate all of Blueprint's semantics up front.
