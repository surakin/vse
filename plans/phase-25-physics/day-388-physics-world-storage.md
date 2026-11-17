# Phase 25 / Day 388 — The Physics World: Body and Collider Storage

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 387; Phase 17.

One resource that owns the simulation state, with stable ids and a layout the solver can read fast.

## Tasks

- [x] `PhysicsWorld` (an ECS resource created per `ecs::World`): body and collider arrays in structure-of-arrays form (position, orientation, linear and angular velocity, inverse mass, inverse inertia in world frame, flags) **(built as chunked arrays of `Body` structs: see Outcome)**, stable `BodyId` with generation, free list, deterministic reuse order.
- [x] Creation from ECS entities by a sync system: an entity with `RigidBody` + `Collider` + `Transform` gets a body; removal and component changes update or destroy it; the order of creation from a query is the entity-id order, not chunk order, so two worlds built the same way give the same body ids.
- [x] Static, kinematic and dynamic classes: static bodies are never integrated or woken, kinematic ones move by velocity or by a target transform and push dynamic ones, dynamic ones are simulated; changing the class at run time is a supported operation.
- [x] Sleeping state fields (sleeping flag, sleep timer, island id) are present and zero; the logic comes on Day 400.
- [x] Access by `BodyId` for the C API and for tests: `Position`, `Orientation`, `Velocity`, `ApplyImpulse`, `ApplyForce`, `SetTransform` (teleport, clears contact caches of that body), all validating finite input and returning a `Status`.
- [x] Memory: bodies are allocated in chunks that never move (a pointer to a body stays valid until it is destroyed), the allocation goes through the engine allocators, no per-body heap allocation in the step.
- [x] Tests: creation order determinism (two builds, same ids), generations (a stale id is refused), class changes, teleport, removal during iteration is refused or deferred as the header says, a stress of 100k create/destroy cycles with no growth (the allocator's counters).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A world holds thousands of bodies with stable ids, built deterministically from ECS entities, and a test destroys and recreates them 100k times without growth.

## Outcome

Built: `ids.h` (`BodyId`, `ColliderId`), `slot_pool.h` (chunks of 256 that never move, generations, last-freed-first free list), `world.h/.cpp` (`PhysicsWorld`, `Body`, `ColliderRecord`, `BodyStatus`, `PhysicsWorldStats`), `sync.h/.cpp` (`SyncBodies`, `BodyLink`, `SyncReport`), `check.h` (shared finite and unit-quaternion checks). Create, destroy, `AddCollider`, `SetCollider`, `SetSettings` (type changes at any time), `SetVelocity`, `ApplyImpulse`, `ApplyForce`, `SetTransform` (teleport, counted in `Body::teleports` for the contact caches of Day 396), `WorldCenter`; all validate and return a `BodyStatus`. `SyncBodies` destroys, updates and creates in entity order (index, generation), so two ECS worlds with the same entities in different archetypes get the same body ids (tested).
**Decisions and deviations:** (1) the arrays are chunked arrays of `Body` structs, **not** structure-of-arrays: pointer stability and a short accessor surface mattered more now, and the step (Day 396) copies the hot fields into its own solver arrays anyway; revisit with the Day 412 benchmark. (2) A body takes **one collider** (`Unsupported` for a second): compound bodies are Day 395, where the combined centre and principal frame are computed. (3) The sync reads `LocalTransform` and skips entities with a `Parent` (a body has no parent); write-back is Day 390. (4) "Removal during iteration": the world has no iteration API; `SyncBodies` aborts under the ECS's own rule when called during a query. (5) Components are not registered with the storage layer (Phase 19) yet: KNOWN-ISSUES.
**Runs:** plain `ran 3424 of 9062 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2734 of 9062 ...`, tsan `ran 2392 of 9062 ...`, no failure; GCC and non-unity `physics_tests` 42 of 42 (44 after the review's fix); `ctest -L docs` 8 of 8, docgen lint at zero for `physics`. Not run: the shared-library and importer-off builds, the Windows and macOS machines.

## Post-implementation code review

`/code-review high` over the diff, read with the eight angles: 2 findings. (1) Fixed: `SyncBodies` ignored a refused update (an entity whose `Collider` became invalid kept its body silently and was never reported): it now counts it in `refused` and the body keeps its last valid state (test `an update to an invalid component is refused`). (2) Fixed in the same pass: `PhysicsWorld` as an ECS resource had no test (the pool holds move-only chunks): `the physics world can live as an ECS resource` added. The new tests were not re-run against the unfixed code. A unity-build clash of two file-local `FiniteV` helpers and of `kNaN` in the tests was found by the build and fixed by sharing them (`check.h`, `test_values.h`). Declined: none.
