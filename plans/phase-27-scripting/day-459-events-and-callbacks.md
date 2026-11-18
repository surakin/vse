# Phase 27 / Day 459 — Events and Callbacks

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 452, 457-458.

The event model scripts and, later, Blueprint's event graphs land on.

## Tasks

- [x] `script::EventBus` with named events and `Value` payloads: `event.emit(name, payload)`, `event.emit_to(entity, name, payload)`, `event.on(name, fn)`, `event.off`, `event.once`; engine events (`on_tick`, `on_collision`, `on_trigger`, `on_input`, `on_setting_change`, `on_asset_loaded`) are the same mechanism.
- [x] Threading: an emit from a partition goes into the partition's outbox; the sync point delivers all outboxes in `(sender partition, sender entity id, sequence)` order into each subscriber's partition inbox; handlers run in the **next phase** (never inside the sender's call, never concurrently with the sender's partition); a handler's own emits are delivered the phase after (a loop guard counts and caps chains per tick: setting `script.event_chain_limit`).
- [x] Subscription lifetime: unsubscribed when the instance or entity is destroyed; a subscription to an entity's event holds a handle that is checked on delivery.
- [x] Native subscribers: C++ systems can emit and subscribe through the same bus (the Day 466 physics events and Day 468 input events use it), with the same delivery order.
- [x] Blueprint-shaped semantics noted for Phase 32 in the guide: event dispatchers, interface calls (`event.call(entity, interface, fn)` as a synchronous-looking call that is delivered at the sync point and answered by an event; a true synchronous cross-entity call is deliberately absent: it would break the threading model), what maps cleanly and what does not.
- [x] Tests: ordering, delivery latency (always the next phase), chain limit, unsubscribe on destroy, equal delivery logs at 1 and 16 threads, a million events per second throughput check.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts and native systems exchange events with an order that does not depend on threads, delivered at defined points.

## Notes

The bindings' `event` table is read-only, so the host publishes its own `event` table that falls back to it. `event.call` and the engine events (on_tick, on_collision, on_trigger, on_input, on_asset_loaded) are deferred to the days that own them (KNOWN-ISSUES.md). The Phase 32 notes are in the guide.

## Post-implementation code review

Three findings. Fixed: a subscription made by a closing instance's on_destroy was applied after its subscriptions were removed (no observer for a regression test: the groups are private; the fix filters the pending changes). Declined: emit_to a placeholder entity is silently lost (harmless, the entity has no script yet); no test of the per-partition emit cap or of emits from on_destroy.

Runs: `scripts/test-phase.sh 27 --no-display`: plain `ran 155 of 10838 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded)`, asan `ran 148 of 10838`, tsan `ran 148 of 10838`; gates 33/33; `ctest -L docs` 9/9; Clang and GCC, unity and non-unity, built clean (before the last small fix; the flavor runs are after it).
