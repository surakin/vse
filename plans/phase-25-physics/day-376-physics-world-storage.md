# Phase 25 / Day 376 — The Physics World: Body and Collider Storage

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 375; Phase 17.

One resource that owns the simulation state, with stable ids and a layout the solver can read fast.

## Tasks

- [ ] `PhysicsWorld` (an ECS resource created per `ecs::World`): body and collider arrays in structure-of-arrays form (position, orientation, linear and angular velocity, inverse mass, inverse inertia in world frame, flags), stable `BodyId` with generation, free list, deterministic reuse order.
- [ ] Creation from ECS entities by a sync system: an entity with `RigidBody` + `Collider` + `Transform` gets a body; removal and component changes update or destroy it; the order of creation from a query is the entity-id order, not chunk order, so two worlds built the same way give the same body ids.
- [ ] Static, kinematic and dynamic classes: static bodies are never integrated or woken, kinematic ones move by velocity or by a target transform and push dynamic ones, dynamic ones are simulated; changing the class at run time is a supported operation.
- [ ] Sleeping state fields (sleeping flag, sleep timer, island id) are present and zero; the logic comes on Day 388.
- [ ] Access by `BodyId` for the C API and for tests: `Position`, `Orientation`, `Velocity`, `ApplyImpulse`, `ApplyForce`, `SetTransform` (teleport, clears contact caches of that body), all validating finite input and returning a `Status`.
- [ ] Memory: bodies are allocated in chunks that never move (a pointer to a body stays valid until it is destroyed), the allocation goes through the engine allocators, no per-body heap allocation in the step.
- [ ] Tests: creation order determinism (two builds, same ids), generations (a stale id is refused), class changes, teleport, removal during iteration is refused or deferred as the header says, a stress of 100k create/destroy cycles with no growth (the allocator's counters).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A world holds thousands of bodies with stable ids, built deterministically from ECS entities, and a test destroys and recreates them 100k times without growth.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
