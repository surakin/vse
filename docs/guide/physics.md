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
A **convex hull** (Day 381) is the `Hull` kind: `Shape::MakeHull(id, extent)` names a `ConvexHull` held by the world's `HullLibrary` (`PhysicsWorld::Hulls()`; ids count from 1, and a collider that names an unknown hull, or whose extent does not cover the hull, is `BodyStatus::Invalid`). A **triangle mesh** (Day 382) is the `Mesh` kind for level geometry: `Shape::MakeMesh(id, extent)` names a `TriangleMesh` of the world's `MeshLibrary` (`PhysicsWorld::Meshes()`). A mesh belongs to a **static** body only (a collider that names an unknown mesh, a non-covering extent or a non-static body is `BodyStatus::Invalid`, and a body that holds one cannot be made dynamic); it has no mass. A **compound** (Day 383) is the `Compound` kind: several shapes (spheres, boxes, capsules and hulls, up to 64) with local transforms on one body. `CompoundShape::Create` computes the whole's mass, centre of mass and principal inertia once (each child's own density, the parallel-axis theorem; `PrincipalAxes` finds the axes) and the world's `RecomputeMass` puts the body's centre of mass there, so the body turns about its true centre; the collider's own `density` is ignored for a compound. `PhysicsWorld::Compounds()` holds them; a collider whose bound does not cover the compound, or that names an unknown one, is `BodyStatus::Invalid`.

A `ConvexHull` is immutable data shared by `ConvexHullConstPtr`: vertices, polygon faces (outward normal, offset, up to 64 vertices each), the volume, the centroid and the inertia tensor about the centroid, all computed offline by the `HULL` minter (`asset_mint mint-hull`, `docs/guide/assets.md`). `ConvexHull::Create` checks what it is given and refuses anything the narrow phase could not use (`Validation`). The mass of a hull body is the hull's own: its centre of mass is the centroid and its principal axes come from a Jacobi eigen-decomposition of the stored tensor, so a body's `RecomputeMass` places the centre at `offset + rotation * centroid`.

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
`WriteBackTransforms` and the step system (below) write the simulated transforms back.

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

## The fixed step

`PhysicsWorld::Update(real_seconds)` advances the world in fixed steps: an accumulator takes the frame's time, runs as many steps of `1 / step_hz` as it
holds, **at most `max_substeps` (default 4) per call**, and keeps the fraction for the next frame. A frame with more time than the cap can use (a stall)
drops the rest and counts it in `GetStepStats()` (`dropped_updates`, `dropped_seconds`), so the next frame is not longer for it: no spiral of death.
`Step()` runs one step now. Each step is, per body in slot order: the previous state is saved; a dynamic body gets `v += (gravity * gravity_scale + force /
mass) dt` and `w += I^-1 torque dt`, then damping `v / (1 + c dt)` (stable for any `c dt`), then semi-implicit Euler: the **centre of mass** moves by the new
velocity, the orientation turns by `q + dt/2 (w, 0) q` normalized (only `+ - * /` and `Sqrt`, so the result is the same on every machine), and the origin
follows from the two. A kinematic body moves by its velocity about its origin; a static body is skipped; forces and torques are cleared. There is no
gyroscopic term (a body with unequal moments tumbles as under explicit Euler; the solver days leave it). The step reports a process that flushes denormals
(`ENGINE_ENSURE`, `denormal_mode_steps`), because then its results are not portable.

`Interpolated(id, alpha, ...)` returns where to draw a body: the blend of its last two states by `alpha` (`Alpha()` is the accumulator's fraction), a linear
blend of the origin and a normalized blend of the orientation along the short arc; a teleport sets both states, so it is not blended from the old place.
`WriteBackTransforms(ecs, physics, alpha)` writes that into the `LocalTransform` of every dynamic body's entity, and `MakePhysicsStepSystem()` is the
schedule system that calls `Update` with the frame's `Dt` and then writes back (it declares `Writes<LocalTransform>` and `WritesResource<PhysicsWorld>`).
Run `SyncBodies` between runs of the schedule, not inside it.

The settings are `physics.step_hz` (60), `physics.max_substeps` (4), `physics.gravity_y` (-9.81) and `physics.interpolate` (true), declared by
`RegisterPhysicsSettings` with environment variables `ENGINE_PHYSICS_*` and flags `--physics-*`; `StepConfigFromSettings` turns them into a `StepConfig`.
The step rate belongs to the state: the same scene hashes differently at another rate.

`PhysicsWorld::StateHash()` is the cross-machine check: FNV-1a over every body in slot order (position, orientation, linear and angular velocity, the sleep
byte). A scene of 100 boxes and capsules with offsets, spins and damping, stepped 600 times, hashes to `238b01a717a849dd` under GCC and Clang, unity and not,
Debug and Release (`Step: the state hash of a 600-step scene...`); the Windows and macOS machines compare it on Days 398-399.

## The narrow phase: contacts for the shapes with closed forms

`physics::Collide(a, pose_a, b, pose_b, margin, &manifold)` is the one entry point of the narrow phase. It returns `Contact` (and fills a `ContactManifold`),
`None` (the shapes are apart by more than the margin) or `Unsupported` (no test for the pair yet). The table, with the day that fills each cell:

| | sphere | capsule | box | hull |
|---|---|---|---|---|
| **sphere** | built (Day 379) | built | built | built (Day 381) |
| **capsule** | | built | built (Day 380) | built (Day 381) |
| **box** | | | built (Day 380) | built (Day 381) |
| **hull** | | | | built (Day 381) |
| **mesh** (static) | built (Day 382) | built | built | built |

The call is symmetric: swapping the shapes gives the same contact with the normal negated and the two points of each contact swapped (the dispatch runs the
test in one fixed order and `ContactManifold::Flip`s the result). A manifold has a **unit normal from A toward B**, up to four points, and for each point
`position_a` and `position_b` (on the two surfaces), a `depth` and a `feature_id`. The depth is `(position_a - position_b) . normal`: positive when the
shapes overlap, **negative when they are apart by less than the margin** (a speculative contact: the solver lets them approach until the gap closes). The
margin is `physics.contact_margin` (0.02 m). The `feature_id` names the touching features (a face, an edge or a vertex of the box; the stretch end of two
parallel capsules) and is the same from step to step while they touch, so a contact cache can warm start. `CombineMaterials` fills the friction and the
restitution.

The tests: **sphere-sphere** is the distance of the centres; a sphere against a **capsule** is a sphere against the ball of the capsule's radius at the
closest point of its axis; two **capsules** take the closest points of their two axis segments (the standard clamped solution, degenerate segments
included), and **parallel** axes (sine of the angle below 1e-3) give two points at the ends of the stretch they share, so a capsule lying on another does not
rock; crossing axes through one point use the common perpendicular. A **sphere against a box** is done in the box's frame: the closest point of the box to
the centre (the region, face, edge or vertex, is the feature id); a centre inside the box leaves through the face of least penetration. Everything uses
`+ - * /` and `Sqrt` only: 100,000 random pairs of every kind hash to the same constant (`a37013a6ea09b3fe`) under GCC and Clang, unity and not, Debug and Release.

**Box against box** (Day 380) is the separating axis test over the 15 axes (3 + 3 face normals and the 9 edge cross products; parallel edge pairs are skipped). A
face axis wins unless an edge axis is better by more than 1e-3, and among the faces the first one wins ties (1e-4), so a resting box does not flicker between
feature types. A **face contact** clips the incident face (the face of the other box most opposed to the reference normal) against the four side planes of the
reference face (Sutherland-Hodgman), keeps the vertices within the margin, and reduces more than four to the deepest, the farthest from it, and the two that
span the most area on either side. Each point's `feature_id` is the reference box (A or B), the reference face, the incident face and the id of the clipped
vertex (an original corner, or a hash of the two neighbours and the side plane it was cut by), so a box that slides keeps its ids. An **edge contact** is one
point between the closest points of the two support edges; its id has the top bit set. Identical boxes pick the same axis every time. Near a tie between two axes within the biases, `Collide(a, b)` and `Collide(b, a)` may choose different features (the same deepest depth, different points). **Capsule against box**
works in the box's frame: the capsule's axis is cut where it crosses the planes of the box's slabs, the distance is minimized in closed form on each piece (the
nearest point of the box moves linearly there), and the result is a sphere-like contact at the nearest point of the axis, or two points at the ends of the
stretch where the axis lies flat above a face. An axis that meets the box leaves through the face of least penetration of the whole capsule.

**Hulls** (Day 381) go through one path for every pair that has a hull: **GJK** on the two cores (a point, a segment, a box or a hull, each with the radius of a sphere or capsule), and when the cores overlap or touch, **EPA** on the Minkowski difference for the normal and the depth. The support of a hull is a scan of its vertices (at most 255; a hill-climbing support is deferred). Apart shapes get the distance of the cores minus the radii as a speculative depth; the gap is normalized by its own length, and cores closer than 1e-5 count as touching. When both shapes are box or hull (radius zero) and the best-aligned face of either is within 0.9 of the contact normal, the manifold is a **face contact** exactly as for two boxes (clip the incident polygon against the reference face, reduce to four); otherwise it is **one point**. A capsule against a hull is always one point for now. The `feature_id` of a face contact is the same code as for boxes with the hull's vertex index (its face indices are the hull's own); a one-point contact has `0x40000000` in its id. Because hulls go to the same code as boxes, a hull copy of a box agrees with the box tests to within the margin for shallow overlaps; a deep overlap has several ways out and the closed forms and EPA may pick different ones. 30,000 random hull pairs hash to `172dfbee2611621e` under GCC and Clang, unity and not, Debug and Release.

**Triangle meshes** (Day 382). `TriangleMesh::Create(spec)` checks the numbers (counts, finite positions, indices, no triangle without area) and builds the face normals, the triangle across each edge (`Neighbor`) and a `spatial::StaticBvh` over the triangle boxes; `QueryBox` returns the triangles whose boxes overlap a box, in increasing order. The mesh is the data of the minted `MCOL` asset (`docs/guide/assets.md`). `Collide(mesh, pose, other, pose, margin, &m, hulls, meshes)` takes the other shape's box in the mesh's space, asks the hierarchy for the triangles (at most 256 are tested, 64 contacts kept), collides each as a flat two-sided convex piece through the same GJK/EPA and face clipping as hulls (the triangle has a front and a back face, so a box lying on it clips to its corners), and merges the results: the contact of the deepest triangle sets the normal, contacts within 11 degrees of it join (the same point reached through two triangles is kept once), and more than four reduce as for boxes. A flat triangle is also left by its plane: when the shortest way out is across the plane, that exit is the contact. **Internal edges:** a one-point contact on an edge shared with a nearly coplanar triangle (normals within 0.99) takes the face's normal, so a body sliding over a seam does not catch; the side is the one the other shape's centre is on. Limits: a mesh is a thin two-sided surface, so a body whose centre passes through a triangle is pushed out the other side (tunnelling, stated for the solver days); concave corners keep only the deepest normal's group for a step; a mesh against a mesh is `Unsupported`; the query allocates (a `Vector` per call).

**Compounds, groups, masks and triggers** (Day 383). `Collide` takes a compound against anything: each child (or each pair of children, for two compounds) is collided on its own, children whose boxes do not meet are skipped, and the contacts merge as for a mesh; a point's `feature_id` mixes the child indices, so warm starting and events tell children apart. A collider has a `group` and a `mask` word (default group 1, mask all bits); the broad phase keeps a pair when the layer matrix lets it and `(a.group & b.mask) != 0 && (b.group & a.mask) != 0` (`GroupsCollide`); two children of two compounds are filtered the same way by their own words. There are no per-pair filter callbacks and no one-way platforms (determinism). A **trigger** collider makes pairs flagged `trigger` and begin and end events, and no contact is solved for it; two triggers make a pair only when `BroadPhaseConfig::trigger_pairs` is set (off by default). Changing a collider's shape (`SetCollider`) recomputes the body's mass and the next `BroadPhase::Update` moves its bound; contact caches arrive with the solver on Day 384.

## The contact solver

`PhysicsWorld::Step` runs the whole pipeline: the broad phase finds the pairs, the narrow phase makes a manifold for each
non-trigger pair that has a dynamic body, and `ContactSolver` (`engine/physics/solver.h`) integrates and solves the bodies
that touch. A body that touches nothing takes one plain step (so free fall is the same recurrence as before).

- **Substeps (soft step).** The bodies that touch take `physics.substeps` (default 4) small steps. Each substep integrates
  the velocities, applies the impulses of the last substep, solves every contact (`physics.velocity_iterations` times,
  default 2), moves the bodies and solves again without position feedback (the relax pass). Small steps keep tall stacks
  still where many iterations of one big step would not.
- **Rows.** Each contact point has a normal row and two friction rows. The separation is recomputed in every substep from
  the bodies' motion and the turn of the anchors. A gap is closed in the time that remains (a speculative contact); an
  overlap beyond the slop (`physics.penetration_slop`, 1 mm) is a soft constraint, a damped spring of
  `physics.contact_hertz` (30 Hz, capped at a quarter of the substep rate; twice as stiff against static bodies) that never
  pushes faster than `contact_push_velocity` (3 m/s) and adds no energy. Friction directions follow the sliding velocity and
  are clamped to `friction * normal impulse`; friction is solved before the normal row.
- **Restitution** is applied once after the substeps, to the points that approached faster than `restitution_threshold`
  (1 m/s): they leave with `e` times their approach speed.
- **Order.** The manifolds are solved in the order of their collider keys (the lower key is body A), points in manifold
  order, forward and backward on alternate passes, so the result does not depend on the order the pairs arrive in;
  `solver_test.cpp` shuffles and flips them.
- **Warm starting.** The impulses of a point are kept by (collider A, collider B, feature id) in sorted storage and start
  the rows again next step when neither body was teleported (`SetTransform`) and the point moved less than 5 cm on body A.
- **Speed limit.** No dynamic body moves faster than `physics.max_velocity` (100 m/s) or spins faster than 100 rad/s.
- **Counters.** `GetSolverStats()`: manifolds, points, warm-started points, iterations, the deepest penetration and the
  largest impulse of the step.

A resting contact is a stiff spring, so a stack sags a little under its weight: 10 boxes of 1 m settle 3.5 cm at the top
(`stability_test.cpp` measures the sideways drift, the sag and the speed after 3600 steps).

## Joints

A joint ties two bodies (or a body and the world) together: `PhysicsWorld::CreateJoint(desc)` with a `JointDesc`
(`engine/physics/joint.h`). `MakeBallJoint`, `MakeDistanceJoint` and `MakeFixedJoint` fill the anchors and frames from the
bodies' poses at creation, so the joint starts at rest; pass a null `BodyId` for the world.

- **Ball:** the two anchors stay together, three rows, the bodies turn freely about the point.
- **Distance:** the anchors keep `length` apart (one rigid row), or pull toward it as a spring (`hertz`, `damping_ratio`), or
  stay inside `[min_length, max_length]` like a rope (one-sided limit rows).
- **Fixed:** the point rows of a ball joint plus three angle rows that keep the frames' relative orientation.
- **Hinge:** the point rows plus two rows that keep the Z axes of the two joint frames together (`MakeHingeJoint` builds
  the frames from a world point and axis, so the angle is zero where the joint was made). The angle is the twist of body B's
  frame relative to body A's about the axis; limits (`enable_limit`, `lower_angle`, `upper_angle`) are one-sided rows with
  the same softness as the contacts, the motor (`enable_motor`, `motor_speed`, `max_motor_torque`) drives B relative to A
  up to a torque cap, and `hertz` with `damping_ratio` makes an angle spring toward zero. The angle is counted across full
  turns (`JointAngle`): it is unwrapped against the last step's arc tangent, so limits beyond plus or minus pi and ten
  revolutions work. A hinge to the world takes the world as body A and the moving body as B, so a positive motor speed
  turns the body the usual way. The arc tangent (`Atan2`, libm) is the one operation of the step that is not exact; the
  hinge scene's hash is equal on clang, GCC, unity and not, Debug and Release.
- **Solving.** Joints are solved in the same substeps as the contacts, before them, in slot order, as soft constraints
  (`physics.joint_hertz` 60, damping ratio 2 in `StepConfig`) with a relax pass; their impulses warm start the next
  substep and step. A body with a joint takes its substeps in the solver.
- **Reaction:** `JointReaction` returns the force and torque the joint carried in the last step (the impulse of the last
  substep over its length).
- **Breaking:** a joint whose force or torque passes `break_force` or `break_torque` (0 is never) is removed at the end of
  the step; `StepStats::joints_broken` counts them.
- **Collision:** the two bodies of a joint do not collide with each other unless `collide_connected` is set. Destroying a
  body destroys its joints.

A pendulum on a ball joint keeps the period of the physical pendulum within 2 % and its energy within 2 % of the swing's
over 10 s (the soft constraint is slightly dissipative).
