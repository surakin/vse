# Phase 19 / Day 264 — Settings, Asynchronous Saves, Autosave & the Demo

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 255, 260-263; Phase 2 (jobs); Phase 11 (settings); Phase 17-18 demos.

Integrate: settings, the asynchronous forms on the job system, and a demo that proves the loaded world is the original.

## Tasks

- [ ] **Settings** `storage.path`, `storage.layout` (`blob`, `columns`), `storage.autosave_seconds`, `storage.max_record_bytes` (Day 257) and the `database.*` group (Day 255), all through `settings::Registry` with `env` and `flag` names (`RegisterStorageSettings`); `WorldStorage::OpenFromSettings`; no ad hoc `getenv` or flag (`config_access_check`).
- [ ] **Asynchronous forms:** `SaveAsync` and `LoadAsync` return a `jobs` handle, encode on workers (chunk ranges as jobs, results appended in chunk order so the stored state does not depend on the schedule), write on one writer job, report progress, honour `CancelToken`; exactly one writer per database (a second save while one runs returns `Busy`); reads run on worker jobs between batches; the world is not modified during a load until the apply step (which runs on the caller's thread, documented).
- [ ] **Autosave:** a system that runs an incremental `SaveAsync` every `storage.autosave_seconds` without stalling the frame (the world snapshot rule of the spec: what is encoded is what the frame left at the system's stage, tested for torn data under TSan).
- [ ] **Demo `game/storage_demo`:** builds the Phase 17 scene and the Phase 18 spatial scene, saves them to a SQLite file under the settings path, loads into a fresh world, and checks: every component equal; the offscreen render of the loaded scene through the scene bridge equals the original by golden image (null-backend draw-list comparison where no GPU is present); the spatial sets equal a brute-force rebuild; then runs 60 frames with autosave and loads the autosaved file again. Registered in `tests/phases.txt` as `spatial`-style ctest `storage_demo_runs`.
- [ ] **Tutorial** `docs/tutorials/saving-a-world.md` (goal, steps, a docgen-compared sample).
- [ ] **Tests** (`tests/storage/async_test.cpp`): async save equals sync save byte for byte over 100 random worlds and every worker count 1-8; cancel at every progress callback leaves the store at the previous state; a second writer gets `Busy`; TSan runs of the async paths and of readers during a save; settings round trip.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a world saved asynchronously and autosaved while running loads back identical, and every storage knob is a setting.
