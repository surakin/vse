# glTF colliders / Day 1: static bodies with a parent, placed by and following the world transform

**Series:** [glTF colliders](../gltf-colliders.md) (design section 4)
**Depends on:** nothing in the series.

Today `physics::SyncBodies` (`engine/physics/sync.{h,cpp}`) makes a body only for an entity with `RigidBody`, `Collider`,
`LocalTransform` and **no** `ecs::Parent`, placed by its `LocalTransform`, and never moves an existing body from its entity. Every
spawned level entity has a parent, so a spawned level gets no bodies.

## Tasks

- [ ] **Tests first** (`tests/physics/sync_test.cpp`, new cases; tag `[physics][sync]`):
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
- [ ] **The sync**: the query of step 2 also takes entities with `RigidBody` whose `type` is static, a `Collider`, an `ecs::WorldTransform`
  and a `Parent`; such a body is placed by the world matrix's translation and rotation (the rotation of the matrix with the scale divided
  out of each column; a column of length below 1e-12 gives no body, counted as refused). Step 1's "wanted" test accepts them. Entity
  order is kept (one gathered list, sorted as today).
- [ ] **Following**: a parented static body whose entity's world position or rotation differs from the body's (exact compare of the
  derived values) is moved with `PhysicsWorld::SetPose` (or the existing teleport call; add `SetStaticPose` if a static body cannot be
  moved today) and the bodies whose broad-phase boxes touch its old or new box are woken, in body-id order. `SyncReport` gains
  `replaced` (the count). An unparented static body keeps today's behaviour (not followed: unchanged contract).
- [ ] **When the sync runs**: confirm in each schedule that builds bodies (the C API world, the script demo, the physics sample) that the
  transform propagation runs before `SyncBodies`; where it does not, the parented case reads last frame's world matrix: document which,
  and make the C API world's order propagation, then sync.
- [ ] **Docs**: `sync.h` (the new rule, the report field), `docs/guide/physics.md` (reference-style: parented static bodies, following,
  scale is the caller's).
- [ ] **Standing rows** (series rules).

**Done:** a static body with a parent is made at, and follows, its entity's world position and rotation; unparented bodies and every
physics hash are unchanged; the new tests pass plain, asan and tsan; the review is recorded below.
