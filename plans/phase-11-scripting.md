# Phase 11 — Scripting (Lua/LuaJIT)

**Depends on:** Phase 7 (ECS).
**Goal:** embedded gameplay scripting via a real, well-known language
instead of a custom bytecode VM — starting with Lua via LuaJIT (root
`PLAN.md` assumption #13). Designed with a pluggable language-backend
seam so more languages can be added later as additive backends, not a
redesign — same pattern already used for the RHI (Phase 3), resource
backends (Phase 10), and window backends (Phase 1). Also the
compilation/execution target for Phase 17's Blueprint importer, so this
phase's engine API bindings and event model should keep Blueprint's
semantics (event graphs, latent nodes, function calls, casts) in mind
even though this phase itself doesn't parse any Unreal data.
**Definition of done:** LuaJIT runs a gameplay script that spawns and
moves an ECS entity via this phase's engine API bindings.

## Key deliverables (to be expanded into day-by-day tasks when started)

- **Language-backend interface.** A narrow seam (load/compile script,
  call a function, expose an engine API surface, tick/callback dispatch)
  that the LuaJIT backend is the first implementation of. Keep it generic
  enough that a second language later plugs in without the engine-facing
  API changing.
- **LuaJIT embedding.** Build/link LuaJIT; C API integration for calling
  into Lua and for Lua calling back into engine bindings.
- **Engine API bindings** exposed to Lua: entity spawn/destroy, component
  get/set, basic math/logging calls.
- **Event/callback model** (e.g. "on tick," "on collision") — needed both
  for native gameplay scripting and, later, as the landing target for
  Blueprint's event graphs (Phase 17).
- **JIT-vs-interpreter platform seam.** LuaJIT's JIT (runtime code
  generation) is disallowed on iOS App Store builds and typically on
  console certification; LuaJIT ships an interpreter-only fallback mode
  for those. Not implemented until Phases 13/15 (macOS/iOS, consoles),
  but the embedding should make that a platform-config choice from the
  start, not an API change (ties to root `PLAN.md` assumption #11).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 7 is complete.
- Phase 17 (Blueprint import) will likely surface gaps in binding/event
  coverage (loops, latent/async waits, casts, interfaces) — expect to
  revisit this phase's scope once Phase 17 starts, rather than trying to
  anticipate all of Blueprint's semantics up front.
- Which additional "well-known languages" get added later (and when) is
  undecided — revisit against real demand, not speculatively.
- Exact JIT-vs-interpreter fallback handling for iOS/console builds is a
  day-level decision for Phases 13/15, not now.
