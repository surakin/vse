# glTF colliders / Day 1: static bodies with a parent, placed by and following the world transform

**Series:** [glTF colliders](../gltf-colliders.md) (design section 4)
**Depends on:** nothing in the series.

Today `physics::SyncBodies` (`engine/physics/sync.{h,cpp}`) makes a body only for an entity with `RigidBody`, `Collider`,
`LocalTransform` and **no** `ecs::Parent`, placed by its `LocalTransform`, and never moves an existing body from its entity. Every
spawned level entity has a parent, so a spawned level gets no bodies.

## Tasks

- [x] **Tests first** (`tests/physics/sync_test.cpp`, new cases; tag `[physics][sync]`):
  - "a static body with a parent is made at its world transform": parent at (10, 0, 0) rotated 90 degrees about Y, child at local
    (1, 0, 0); after `ecs::PropagateTransforms` (or the world's transform system) and `SyncBodies`, the body's position is (10, 0, -1)
    within 1e-5 and its rotation the parent's;
  - "a kinematic or dynamic body with a parent stays unsimulated" (no body, as today);
  - "a parented static body follows its parent": move the parent, propagate, sync: the body is re-placed; a dynamic box resting on it
    is awake after the sync; the report counts one re-placement;
  - "the scale of the world transform does not reach the body": parent scale (2, 2, 2): the body's pose is position and rotation only
    (the collider's sizes are not touched by physics: the caller scales shapes, Day 5);
  - "two worlds built the same way get the same body ids" with a mix of parented and unparented bodies (entity order);
  - the existing sync tests and the physics golden hashes pass unchanged (unparented entities behave exactly as before).
- [x] **The sync**: the query of step 2 also takes entities with `RigidBody` whose `type` is static, a `Collider`, an `ecs::WorldTransform`
  and a `Parent`; such a body is placed by the world matrix's translation and rotation (the rotation of the matrix with the scale divided
  out of each column; a column of length below 1e-12 gives no body, counted as refused). Step 1's "wanted" test accepts them. Entity
  order is kept (one gathered list, sorted as today).
- [x] **Following**: a parented static body whose entity's world position or rotation differs from the body's (exact compare of the
  derived values) is moved with `PhysicsWorld::SetPose` (or the existing teleport call; add `SetStaticPose` if a static body cannot be
  moved today) and the bodies whose broad-phase boxes touch its old or new box are woken, in body-id order. `SyncReport` gains
  `replaced` (the count). An unparented static body keeps today's behaviour (not followed: unchanged contract).
- [x] **When the sync runs**: confirm in each schedule that builds bodies (the C API world, the script demo, the physics sample) that the
  transform propagation runs before `SyncBodies`; where it does not, the parented case reads last frame's world matrix: document which,
  and make the C API world's order propagation, then sync.
- [x] **Docs**: `sync.h` (the new rule, the report field), `docs/guide/physics.md` (reference-style: parented static bodies, following,
  scale is the caller's).
- [x] **Standing rows** (series rules).

**Done:** a static body with a parent is made at, and follows, its entity's world position and rotation; unparented bodies and every
physics hash are unchanged; the new tests pass plain, asan and tsan; the review is recorded below.

## Notes (2026-11-20, Linux)

**Tests are not run per day** (the user: "run tests only at the end", 2026-11-20): the day was built (Clang unity, GCC unity-off) and
reviewed; the whole selection runs after Day 7.

- `SyncBodies` gathers a second query (static `RigidBody`, `Collider`, `WorldTransform`, `Parent`) into the same list, sorted in entity
  order as before. `PoseOfWorld` takes the translation and the rotation of the column-normalized matrix; a negative determinant flips the
  third axis so the rotation stays proper. A column shorter than 1e-12 gives no body (refused).
- Following: an exact compare of the body's pose with the derived one; `SetTransform` (which already disturbs the body and wakes its
  neighbours) plus `WakeAtNewPose`, an overlap of a box around each collider's bounds at the new pose, for the movable bodies that rest
  where it arrives. `SyncReport::replaced` counts the moves.
- **Order:** the C API's frame runs `SyncBodies` before its schedule (step, then the transform system), so a parented body reads the
  previous frame's world matrix. Day 5's resolver makes the collider only after a frame has propagated the matrix, so no body is ever
  made from the identity default; following lags one frame (recorded in the guide).
- The step writes poses back for dynamic bodies only (`physics/system.cpp`, `WriteOne`), so a parented static entity's `LocalTransform`
  is never overwritten.
- Tests (`tests/physics/sync_test.cpp`, written, run at the end): made at the world transform; kinematic and dynamic with a parent stay
  unsimulated; follows the parent and wakes a sleeping ball; scale does not reach the body; same body ids in two worlds; a mirrored parent
  gives a proper rotation.
- Docs: `sync.h`, `docs/guide/physics.md` ("From ECS entities").

## Post-implementation code review

`/code-review high` on the day's diff: 4 findings; 2 fixed, 1 deferred to Day 5 as planned, 1 kept.
1. **A mirrored parent gave an improper rotation** (fixed: the third axis is flipped; test "a mirrored parent gives a proper rotation").
2. **The follow test could not wake the ball before a step** (no contacts or query index yet): fixed, the test steps once first.
3. **The C API sync reads last frame's matrix**: Day 5's resolver makes colliders only after a propagation (its task list says so).
4. **Moving a big static collider wakes every body on it**: kept: they rest on it, so waking them is right; it costs one step of solving.
