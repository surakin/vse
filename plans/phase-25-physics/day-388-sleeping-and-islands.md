# Phase 25 / Day 388 — Sleeping and Islands

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 387.

Bodies at rest cost nothing, and independent groups are separated deterministically.

## Tasks

- [ ] Island building from the constraint graph (contacts with non-static bodies and joints) by union-find with the root always the lowest body id, so island membership and order do not depend on traversal order; islands are listed sorted by their lowest body id.
- [ ] Sleep: an island sleeps when every body in it has stayed below `physics.sleep_linear` and `physics.sleep_angular` for `physics.sleep_time` seconds; sleeping bodies are removed from integration and the solver but stay in the broad phase as static-like entries; the pair tracker keeps their pairs without producing events.
- [ ] Wake rules: a contact begin with an awake body, a joint attached to an awake body, an applied force or impulse, a teleport, a class change, removal of a body that was in contact, and any change of a sleeping body's collider wake the whole island.
- [ ] Kinematic and static bodies never keep an island awake by themselves; a kinematic body that moves wakes what it touches.
- [ ] `allow_sleep = false` on a body keeps its island awake.
- [ ] Tests: a stack goes to sleep after the set time and the step cost drops (the stats show zero active constraints); a thrown body that lands wakes a sleeping stack; removing the box under a sleeping stack wakes it; island membership is the same for shuffled body creation orders (same ids by creation sort, same islands); a 10k-body scene mostly asleep steps at a fraction of the awake cost (recorded in the day file).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Resting scenes fall asleep, wake exactly by the stated rules, and the island decomposition is independent of build order.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
