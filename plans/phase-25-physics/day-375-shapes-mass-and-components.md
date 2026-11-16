# Phase 25 / Day 375 — Shapes, Mass Properties and the Physics Components

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 374; Phase 17 (components), Phase 22 (assets).

The data model: what a body and a collider are, with their mass and bounds.

## Tasks

- [ ] New module `engine/physics` (CMake target `physics`, layer above `spatial`; `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt`, `docs/reference/modules.txt`, `tests/phases.txt` lines in the same commit).
- [ ] Shape value types: `SphereShape`, `BoxShape` (half extents), `CapsuleShape` (half height, radius), `ConvexHullShape` (a handle to minted hull data, filled on Day 381), `MeshShape` (static, Day 382), `CompoundShape` (Day 383); `Shape` is a tagged value with `Bounds(transform)` returning a `math::Aabb`.
- [ ] Mass properties: volume, centre of mass and inertia tensor for sphere, box and capsule (closed forms) and a convex hull (the standard tetrahedron decomposition); `MassFromDensity(shape, density)`; inertia stored as the diagonal in the principal frame plus a rotation, so the world tensor is two multiplications.
- [ ] ECS components in `physics/components.h`: `RigidBody` (type static/kinematic/dynamic, mass override, linear/angular damping, gravity scale, flags: `lock_rotation`, `allow_sleep`, `continuous`), `Velocity` (linear and angular), `Collider` (shape, local offset, layer, `PhysicsMaterial` handle or inline friction/restitution/density, `is_trigger`), registered with the component registry and the storage layer's identity/serialization where the engine does that for components (Phase 19).
- [ ] `PhysicsMaterial` as a minted asset type (`PMAT`): friction, restitution, density, combine rule (average, minimum, multiply, maximum); asset loader through `asset::ResourceManager`, minted by `asset_mint` (deterministic) with a golden hash test.
- [ ] Validation: non-finite or non-positive sizes, masses and densities are refused with a `Status` (no abort on host data); each shape has a `Valid()` and the components report what was refused.
- [ ] Tests: mass and inertia of each shape against analytic values (within 1e-5 relative); a rotated box's world inertia; AABBs of shapes under rotation contain a dense sample of surface points; refused shapes; the material round trip and its minted hash.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Bodies and colliders exist as components with correct mass, inertia and bounds for sphere, box and capsule, hulls are specified (filled on Day 381), and the module is in the architecture gates.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
