# Phase 27 / Day 456 — ECS Bindings II: Components, Queries and Render Components

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 455.

Any registered component, by name, safely; and queries that run in parallel.

## Tasks

- [ ] The component reflection registry (the one `engine/storage` already has for saving, reused): `component.get(e, "Name")` returns a table copy of the fields, `component.set(e, "Name", fields)` validates every field's type and range, `component.add/remove/has`; unknown names and wrong field types are script errors naming the component and field.
- [ ] Script-defined components: `component.define("Health", {hp = "f32", max = "f32"})` registers a plain-data component with the engine's registry (named, versioned, saveable by Phase 19), visible to every partition; definitions are collected at load and must be identical in every state (checked), refused after the first tick.
- [ ] Queries: `query.each({"Transform", "Health"}, fn)` iterates the entities *of the calling partition* (the common case, no cross-thread data), `query.all(...)` iterates every matching entity read-only from the tick-start view; results in stable entity-id order.
- [ ] Render and light components: `MeshRenderer` (mesh and material by asset id), `LightSource`, `Camera`, so scripts can build visible things; asset ids resolve through the resource manager (Day 463), a missing one is an error value.
- [ ] Cost: a per-partition query over 100,000 entities and `query.all` over the same, recorded; `query.all` is flagged as the expensive call in the docs.
- [ ] Tests: reflection round trips, validation errors, script-defined components saved and loaded through storage, query order independent of thread count.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts read, write, define and query components by name with validation, and parallel queries give ordered results.
