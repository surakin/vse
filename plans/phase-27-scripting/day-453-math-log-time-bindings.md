# Phase 27 / Day 453 — Math, Log and Time Bindings

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 450-452.

The first bindings: everything a script needs that does not touch the world.

## Tasks

- [x] `vec2`, `vec3`, `vec4`, `quat`, `mat4` with arithmetic, dot, cross, length, normalize, lerp, slerp, rotate, look_at, from_euler; the engine's math through `lang/scalar.h` (libm as is), documented as not bit-reproducible across machines unless the scene avoids the transcendental functions.
- [x] `math` additions (`clamp`, `lerp`, `sign`, `smoothstep`, `approach`), a deterministic per-state random (`random.float`, `random.int`, `random.vec3_unit`, seeded from the entity's stable id and the world seed so that the stream is the same for any thread count).
- [x] `log.info/warn/error/debug` into `LogScript` with the script's name and line; rate limiting per script (a setting) so a script cannot flood the log; log calls from partitions go through a per-partition buffer flushed at the sync point in the merge order.
- [x] `time`: the tick's `dt`, `time.now` (the simulation clock, deterministic), `time.real` (the wall clock, marked non-deterministic and refused when a replay is recording), the frame number.
- [x] All bindings are `pure` or buffered, marked, documented; the conformance and binding tests run in 64 partitions at once under tsan.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts have math, logging, deterministic random numbers and time, with the thread rules marked and tested.

## Notes

- The namespace is `num` (Lua's `math` library keeps its name); `mat4` crosses as an array of 16 numbers (column-major). `time.real` returns `nil` while a replay records (a binding cannot raise an error yet). The caller's script name and line come from a new backend hook, `script::SetCallerLocationProvider` / `CurrentCallerLocation`, published by the LuaJIT trampoline; the expr backend has none (messages name `script`). New setting `script.log_limit` (default 64 per script per tick).
- Tests: `tests/script/runtime_bindings_test.cpp` (16 cases, 4894 assertions), including 64 partitions on 8 threads drawing the same random numbers as one thread and flushing logs in partition order.

## Post-implementation code review

`/code-review high` over the day's diff: 5 findings. Fixed: (1) a Lua error inside a native (out of memory) skipped the trampoline's withdrawal of the caller-location provider, leaving a pointer to the state: `Execute` now clears it after every protected call (no test: the out-of-memory unwind inside a native cannot be forced). (2) `log.debug` was buffered and used up the script's limit even when `LogScript` drops debug: now skipped first (test added, fails without the fix). Declined: limit 0 is unbounded by design (opt-in setting); drops beyond 256 distinct script names are attributed to the 256th name (a diagnostic only); `time.real` returns nil instead of raising (bindings have no error path yet).

Runs: plain `ran 3976 of 10685 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 3271 of 10685`, tsan `ran 2926 of 10685`; only the known failures (`engine_c_surface` plain, the Job-destroyed test, the audio voice-cost test, the DXIL and Metal minted-hash tests). `ctest -L docs` 9 of 9; Clang and GCC, unity on and off: script tests pass. Not run: `-DENGINE_BUILD_SHARED=ON`; Windows and macOS (Days 473-474).
