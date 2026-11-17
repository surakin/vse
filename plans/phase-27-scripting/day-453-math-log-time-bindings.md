# Phase 27 / Day 453 — Math, Log and Time Bindings

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450-452.

The first bindings: everything a script needs that does not touch the world.

## Tasks

- [ ] `vec2`, `vec3`, `vec4`, `quat`, `mat4` with arithmetic, dot, cross, length, normalize, lerp, slerp, rotate, look_at, from_euler; the engine's math through `lang/scalar.h` (libm as is), documented as not bit-reproducible across machines unless the scene avoids the transcendental functions.
- [ ] `math` additions (`clamp`, `lerp`, `sign`, `smoothstep`, `approach`), a deterministic per-state random (`random.float`, `random.int`, `random.vec3_unit`, seeded from the entity's stable id and the world seed so that the stream is the same for any thread count).
- [ ] `log.info/warn/error/debug` into `LogScript` with the script's name and line; rate limiting per script (a setting) so a script cannot flood the log; log calls from partitions go through a per-partition buffer flushed at the sync point in the merge order.
- [ ] `time`: the tick's `dt`, `time.now` (the simulation clock, deterministic), `time.real` (the wall clock, marked non-deterministic and refused when a replay is recording), the frame number.
- [ ] All bindings are `pure` or buffered, marked, documented; the conformance and binding tests run in 64 partitions at once under tsan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts have math, logging, deterministic random numbers and time, with the thread rules marked and tested.
