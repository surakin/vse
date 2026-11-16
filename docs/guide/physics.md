# Physics: rigid bodies that agree to the bit

Status: in progress (Phase 25). Built: the determinism foundation (Day 374). Next: shapes, mass and the components (Day 375).

**Audience:** engine programmers who write or review physics code, and hosts that compare simulations across machines. Read [Math](foundation-math.md) and
[Spatial partitioning](spatial.md) first. Design and the experiments behind it: `docs/superpowers/specs/2026-11-16-physics-design.md`.

The physics module (`engine/physics`, namespace `physics`) simulates rigid bodies with a fixed step. Its requirement beyond being correct is that the
same scene gives the **same bits** on Linux, Windows and macOS (x86-64), whatever the compiler, the optimisation level and the number of threads, so a
replay, a rollback and a networked peer can trust it. This page states the rules that make that true; every later day of the phase obeys them.

## The rules of the step

1. **The step uses `+ - * / Sqrt` on `f32` and nothing else that rounds.** IEEE 754 defines exactly these (and comparisons, min, max, abs, negation,
   conversions within range): every conforming machine gives the same bits. `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Log`, `Pow` are
   not exact and differ between glibc, the Microsoft C library and Apple's; the hardware estimates `rsqrt` and `rcp` differ between CPUs. None is used in
   the step. The quaternion update is first order with a normalising `Sqrt`; the soft-constraint coefficients are closed forms in `+ - * /`.
2. **Where a transcendental cannot be avoided it is at setup, not in the step**, it goes through `lang/scalar.h` (libm is used as is; nothing is
   reimplemented), and it is listed here: converting an authored angle to its sine and cosine (a hinge limit, a motor target, a slope limit), building
   a quaternion from an axis and an angle, reporting an angle to the host. The reference scenes use literal constants for these; a scene that does not
   is hashed separately and a disagreement between machines is recorded in `KNOWN-ISSUES.md`, not worked around.
3. **No contraction, no fast-math.** `-ffp-contract=off -fno-fast-math` on GCC and Clang, `/fp:precise` on cl, `/fp:precise` and
   `/clang:-ffp-contract=off` on clang-cl (`ENGINE_FP_COMPILE_OPTIONS`, `cmake/EngineCompilerOptions.cmake`), applied to every target of ours. A fused
   multiply-add rounds once where a multiply and an add round twice, so a build for a machine with FMA (`-march=native`, and arm64 by default) would give
   other bits, and GCC and Clang would give others still (Day 373 spike 2). The configure step stops when a fast-math flag, or contraction, is set
   anywhere (`cmake/EngineFpCheck.cmake`; a fast-math flag at link time also sets flush-to-zero for the whole process).
4. **Denormals are kept.** The engine never sets flush-to-zero or denormals-are-zero; `physics::DenormalsPreserved()` says whether the process has them
   off, and the step will check it (Day 378). A host that sets the mode for its own sake must restore it around the step.
5. **Fixed order everywhere.** A fixed time step; pairs in the order the pair tracker gives (sorted by key); islands by their lowest body id; a parallel
   job writes only its own output and the outputs are merged in chunk order; no float reduction in thread order, no atomic float add, no iteration order of
   a hash map, no sort by a float without an id tie-break. Day 373 spike 2: an unordered merge gave nine different hashes in nine runs; the ordered merge
   gave one hash for 1, 2, 4, 16 and 32 threads.

## Checking the build

`physics::ProbeHash()` evaluates a fixed set of expressions that a different rounding, contraction or flush mode would change and hashes the results;
`fp_determinism_probe` prints it with the mode the build has:

```
fp_determinism_probe: hash 50e1be9251ea408f contraction=off denormals=kept
```

The hash is the same under GCC and Clang, `-O0` to `-O3`, unity and non-unity builds, and with `-march=native -ffp-contract=off`; with contraction on it is
`60498f6f6e05389d` under both compilers. The ctest `physics_fp_determinism_probe` and the test `Determinism: the probe's hash is the committed constant`
compare it with the committed value; the Windows PC and the Mac compare it on Days 398 and 399 (arm64 can only be compiled, not run). A control probe
compiled with contraction allowed shows that a fused multiply-add would be seen (`Determinism: the control probe does fuse`; skipped where the machine has no
FMA instruction).

## The state hash

`physics::StateHasher` is FNV-1a 64: bytes in the order fed, words least significant byte first, floats by bit pattern with **-0.0 as +0.0**, and
`Valid()` false once a NaN or an infinity was fed (a simulation that produced one has failed). Nothing but the fed words is hashed: no padding, no
pointers. The reference scenes hash every body in id order: position, orientation, linear and angular velocity, then a sleep byte.

## Shapes, mass and the components

A `physics::Shape` is a plain tagged value: `Shape::MakeSphere(radius)`, `MakeBox(half_extents)` or `MakeCapsule(half_height, radius)` (a capsule lies
along its own Y axis). Host data is never aborted on: `Validate()` returns a `Validation` (`NotFinite`, `NotPositive`, ...), and every component has the
same method. `Bounds(position, rotation)` returns the smallest axis-aligned box around the rotated shape.

`MassFromDensity(shape, density)` gives the volume, mass and the inertia as three moments about the principal axes through the centre of mass, in closed
form (the capsule's hemispheres are moved to the centre by the parallel axis theorem). `InverseInertiaWorld(orientation)` is the two products
`Q R diag(1/I) R^T Q^T` the solver needs, and an axis without inertia gets a zero entry. A capsule with no cylinder part has the sphere's numbers.

The components of an entity are `RigidBody` (type static, kinematic or dynamic; flags `kBodyLockRotation`, `kBodyNoSleep`, `kBodyContinuous`; a mass
override, damping, gravity scale), `Velocity` and `Collider` (shape, offset and rotation in the body, `PhysicsMaterial`, layer 0-31, trigger flag). The
material holds friction, restitution, density and how two values combine (`CombineRule`); for a pair the rule with the larger number is used, so the
order of the two colliders never matters. The material is inline in the collider for now: a minted `PMAT` asset is deferred (see `KNOWN-ISSUES.md`).
Convex hulls, triangle meshes and compound shapes are new `ShapeKind` values on Days 381-383.

## The world: bodies, colliders and ids

`physics::PhysicsWorld` holds the bodies and colliders of a simulation. It is an ordinary value (keep it by itself or as an ECS resource with
`ecs::World::SetResource`). A `BodyId` is a slot index and a generation: a destroyed body's id is **stale** and every call refuses it (`BodyStatus::StaleId`)
instead of reaching the body that took the slot. Slots are reused last-freed-first, so two worlds built by the same calls hand out the same ids, which the
state hash relies on. Bodies and colliders live in chunks of 256 that never move: a `Body*` from `Find` stays valid until that body is destroyed, and
100,000 destroy-and-create cycles do not grow the world (`Stats().body_slots`).

`CreateBody(BodyDesc)` makes a body; `AddCollider` gives it its shape and recomputes the mass (a body takes one collider until compound bodies on Day 383).
A body's origin and its centre of mass differ when the collider is offset: `WorldCenter(id)` is the centre, and impulses and forces act about it.
`SetSettings` changes the type at any time (a body made static loses its velocity); `ApplyImpulse` changes the velocity at once, `ApplyForce` is kept until
the step takes it; `SetTransform` teleports the body and counts it, so contact caches of the body know to drop their points. Bad input (a NaN, a quaternion
that is not a unit, a refused component) is returned as `BodyStatus::Invalid`, never aborted on.

### From ECS entities: `SyncBodies`

```cpp
ecs::World world;
physics::PhysicsWorld bodies;
world.Create(physics::RigidBody(), collider, ecs::LocalTransform());
physics::SyncReport report = physics::SyncBodies(world, bodies);  // report.created == 1
```

An entity with `RigidBody`, `Collider` and `LocalTransform` and no `Parent` gets a body at its transform and `Velocity`, and a `BodyLink` component that
names it. Each call destroys the bodies of entities that died or lost a component, updates the bodies whose `RigidBody` or `Collider` changed, and creates
the missing ones **in entity order** (index, then generation), not chunk order, so two worlds holding the same entities in different archetypes get the same
body ids. An entity with invalid components is counted in `refused` and skipped. The call changes the ECS world's structure, so it runs between iterations.
Writing the simulated transforms back to `LocalTransform` is Day 378.

## The broad phase

`physics::BroadPhase` finds which pairs of colliders may touch, with Phase 18's structures and nothing new: the colliders go into a `spatial::SpatialSet`
(static bodies in its `StaticBvh`, everything else, kinematic bodies included, in the `DynamicBvh` with fat boxes, margin 0.25) and a `spatial::PairTracker`
(margin 0.1) keeps the pairs. `Update(world)` first brings the set in line with the physics world, **in collider slot order**: it adds new colliders,
removes destroyed ones, re-inserts a moved collider (the fat box makes a small move free, and a collider that did not move costs one box computation and comparison), handles a
body whose type changed and moves a teleported static body through the dynamic side and back; then it calls `RebuildStatic()` when the static tree is
stale, because items waiting outside the tree are scanned by every query (31 ms against 4 ms a frame on 2000 statics and 5000 moving boxes); then it
updates the pairs.

A changed layer re-adds the collider (a layer is fixed when an item is added) and keeps the pairs that still meet. `Pairs()` is the narrow phase's work list: sorted by key (a collider's key is its slot, then its generation: `ColliderId::ToKey()`), the smaller key first,
and only pairs that can matter: different bodies, at least one dynamic body (a kinematic body against a static one is dropped), layers that meet
(`CollisionFilter::SetMeets`, 32 layers), and trigger pairs flagged `trigger` (a trigger never reaches the solver; a kinematic body in a static trigger is
listed). `Begun()` and `Ended()` are the pairs that changed in the last update. A body flagged `kBodyContinuous` has a box that also covers its motion over
one step, so a fast body meets a thin wall before it is through it. The pair margin gives a pair that stays at the edge of contact a hysteresis: boxes
0.02 m apart half the time begin one pair and end none in 120 steps (`BroadPhase: a pair at the edge of contact...`).

With a margin of 0 the pairs are exactly the overlaps of the boxes: the test compares them with a brute-force search on 20 random scenes of 1000 bodies
(static and moving, three shape kinds, a layer that does not meet another), four rounds each, after bodies were moved, teleported and changed type.
`Stats()` reports what the last update did (added, removed, moved, begun, ended, pairs, static rebuilds, pending statics).
