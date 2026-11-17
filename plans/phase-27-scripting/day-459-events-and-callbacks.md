# Phase 27 / Day 459 — Events and Callbacks

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 452, 457-458.

The event model scripts and, later, Blueprint's event graphs land on.

## Tasks

- [ ] `script::EventBus` with named events and `Value` payloads: `event.emit(name, payload)`, `event.emit_to(entity, name, payload)`, `event.on(name, fn)`, `event.off`, `event.once`; engine events (`on_tick`, `on_collision`, `on_trigger`, `on_input`, `on_setting_change`, `on_asset_loaded`) are the same mechanism.
- [ ] Threading: an emit from a partition goes into the partition's outbox; the sync point delivers all outboxes in `(sender partition, sender entity id, sequence)` order into each subscriber's partition inbox; handlers run in the **next phase** (never inside the sender's call, never concurrently with the sender's partition); a handler's own emits are delivered the phase after (a loop guard counts and caps chains per tick: setting `script.event_chain_limit`).
- [ ] Subscription lifetime: unsubscribed when the instance or entity is destroyed; a subscription to an entity's event holds a handle that is checked on delivery.
- [ ] Native subscribers: C++ systems can emit and subscribe through the same bus (the Day 466 physics events and Day 468 input events use it), with the same delivery order.
- [ ] Blueprint-shaped semantics noted for Phase 32 in the guide: event dispatchers, interface calls (`event.call(entity, interface, fn)` as a synchronous-looking call that is delivered at the sync point and answered by an event; a true synchronous cross-entity call is deliberately absent: it would break the threading model), what maps cleanly and what does not.
- [ ] Tests: ordering, delivery latency (always the next phase), chain limit, unsubscribe on destroy, equal delivery logs at 1 and 16 threads, a million events per second throughput check.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts and native systems exchange events with an order that does not depend on threads, delivered at defined points.
