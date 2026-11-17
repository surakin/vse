# Phase 27 / Day 471 — C API I: Loading Scripts, Calling Functions and Host Functions

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Phases 23-24; Days 446-450.

A host that embeds the engine as a library can run scripts.

## Tasks

- [ ] Table functions in `include/engine/engine_c.h` (ABI additions at the end of the table, the snapshot `abi/engine_api.snapshot` updated, the `abi_check` gate green): `script_load(world, id_or_source, flags, out_script)`, `script_unload`, `script_call(world, entity_or_global, function, values_in, n, values_out, cap)`, `script_set_budget`, `script_stats`.
- [ ] `engine_value` as a sized C struct (tag, payload, byte blob, array and map through indices into a caller buffer: no engine pointer, no allocation crossing the boundary), with limits matched to `script::Value`; strings copied in and out.
- [ ] Host functions: `script_register_function(world, name, callback, user)` makes a host C function callable from scripts as a `main-thread-only` or `host-thread` binding (the host says which; a callback invoked on an engine worker thread is declared and documented, with the rule that the host's function must be thread-safe), arguments and results as `engine_value`s, errors as status codes.
- [ ] Script components on entities through the existing entity property API: `ENGINE_COMPONENT_SCRIPT` with the asset id and instance parameters; `world_frame` runs the script phases on the engine's jobs.
- [ ] Status and errors by value; every function checks handles and arguments; the C API tests (a C program under `tests/engine_library/`, allow-listed for the std gate as the other host programs) load a script, call it, register a host function, and run 1,000 frames.
- [ ] Thread rule for hosts: the C API calls that touch a world are main-thread unless documented; `script_call` from another thread is refused with a status unless the world is idle (a documented, tested rule).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host loads and calls scripts, registers its own functions for scripts to call, and runs script components in a world.
