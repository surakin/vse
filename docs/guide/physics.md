# Physics: rigid bodies that agree to the bit

Status: written.

**Audience:** engine programmers who write or review physics code, and hosts that compare simulations across machines. Read [Math](foundation-math.md) and
[Spatial partitioning](spatial.md) first. Design and the experiments behind it: `docs/superpowers/specs/2026-11-16-physics-design.md`.

The physics module (`engine/physics`, namespace `physics`) simulates rigid bodies with a fixed step. Its requirement beyond being correct is that the
same scene gives the **same bits** on Linux, Windows and macOS (x86-64), whatever the compiler, the optimisation level and the number of threads, so a
replay, a rollback and a networked peer can trust it. This page states the rules that make that true; the module obeys them.

## The rules of the step

1. **The step uses `+ - * / Sqrt` on `f32` and nothing else that rounds.** IEEE 754 defines exactly these (and comparisons, min, max, abs, negation,
   conversions within range): every conforming machine gives the same bits. `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Log`, `Pow` are
   not exact and differ between glibc, the Microsoft C library and Apple's; the hardware estimates `rsqrt` and `rcp` differ between CPUs. None is used in
   the step. The quaternion update is first order with a normalising `Sqrt`; the soft-constraint coefficients are closed forms in `+ - * /`.
2. **Where a transcendental cannot be avoided it is at setup, not in the step**, it goes through `lang/scalar.h` (libm is used as is; nothing is
   reimplemented), and it is listed here: converting an authored angle to its sine and cosine (a hinge limit, a motor target, a slope limit), building
   a quaternion from an axis and an angle, reporting an angle to the host. The reference scenes use literal constants for these; a scene that does not
   is hashed separately and a disagreement between machines is recorded in `KNOWN-ISSUES.md`, not worked around.
3. **No contraction, no fast-math.** `-ffp-contract=off -fno-fast-math` on GCC and Clang, `/fp:precise` on cl, the Clang switches through
   `/clang:` on clang-cl (not `/fp:precise`, which is Clang's contracting `-ffp-model=precise`) (`ENGINE_FP_COMPILE_OPTIONS`,
   `cmake/EngineCompilerOptions.cmake`), applied to every target of ours. A fused
   multiply-add rounds once where a multiply and an add round twice, so a build for a machine with FMA (`-march=native`, and arm64 by default) would give
   other bits, and GCC and Clang would give others still. The configure step stops when a fast-math flag, or contraction, is set
   anywhere (`cmake/EngineFpCheck.cmake`; a fast-math flag at link time also sets flush-to-zero for the whole process).
4. **Denormals are kept.** The engine never sets flush-to-zero or denormals-are-zero; `physics::DenormalsPreserved()` says whether the process has them
   off, and the step will check it. A host that sets the mode for its own sake must restore it around the step.
5. **Fixed order everywhere.** A fixed time step; pairs in the order the pair tracker gives (sorted by key); islands by their lowest body id; a parallel
   job writes only its own output and the outputs are merged in chunk order; no float reduction in thread order, no atomic float add, no iteration order of
   a hash map, no sort by a float without an id tie-break. In a spike, an unordered merge gave nine different hashes in nine runs; the ordered merge
   gave one hash for 1, 2, 4, 16 and 32 threads.

## Checking the build

`physics::ProbeHash()` evaluates a fixed set of expressions that a different rounding, contraction or flush mode would change and hashes the results;
`fp_determinism_probe` prints it with the mode the build has:

```
fp_determinism_probe: hash 50e1be9251ea408f contraction=off denormals=kept
```

The hash is the same under GCC and Clang, `-O0` to `-O3`, unity and non-unity builds, and with `-march=native -ffp-contract=off`; with contraction on it is
`60498f6f6e05389d` under both compilers. The ctest `physics_fp_determinism_probe` and the test `Determinism: the probe's hash is the committed constant`
compare it with the committed value; the Windows PC and the Mac compare it (arm64 can only be compiled, not run). A control probe
compiled with contraction allowed shows that a fused multiply-add would be seen (`Determinism: the control probe does fuse`; skipped where the machine has no
FMA instruction).

**On Windows** the probe gives the same line under cl and clang-cl, and every committed hash of the physics tests (the six golden scenes,
the 600-step scene, the random-pair, hull, compound and mesh contacts, joints, hinges, snapshots) and the C API sample's checksum equal Linux's and the
Mac's, in unity and non-unity builds, plain and asan. The compound and mesh contact hashes first differed here as on the Mac: their samples were built
with `Sin` and `Cos`, which the C libraries round differently; the libm-free samples (`testing::TurnFromHalfCos`, `testing::Wave`) give the same
constants on Windows. The control probe is skipped under cl and clang-cl.

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
A **convex hull** is the `Hull` kind: `Shape::MakeHull(id, extent)` names a `ConvexHull` held by the world's `HullLibrary` (`PhysicsWorld::Hulls()`; ids count from 1, and a collider that names an unknown hull, or whose extent does not cover the hull, is `BodyStatus::Invalid`). A **triangle mesh** is the `Mesh` kind for level geometry: `Shape::MakeMesh(id, extent)` names a `TriangleMesh` of the world's `MeshLibrary` (`PhysicsWorld::Meshes()`). A mesh belongs to a **static** body only (a collider that names an unknown mesh, a non-covering extent or a non-static body is `BodyStatus::Invalid`, and a body that holds one cannot be made dynamic); it has no mass. A **compound** is the `Compound` kind: several shapes (spheres, boxes, capsules and hulls, up to 64) with local transforms on one body. `CompoundShape::Create` computes the whole's mass, centre of mass and principal inertia once (each child's own density, the parallel-axis theorem; `PrincipalAxes` finds the axes) and the world's `RecomputeMass` puts the body's centre of mass there, so the body turns about its true centre; the collider's own `density` is ignored for a compound. `PhysicsWorld::Compounds()` holds them; a collider whose bound does not cover the compound, or that names an unknown one, is `BodyStatus::Invalid`.

A `ConvexHull` is immutable data shared by `ConvexHullConstPtr`: vertices, polygon faces (outward normal, offset, up to 64 vertices each), the volume, the centroid and the inertia tensor about the centroid, all computed offline by the `HULL` minter (`asset_mint mint-hull`, `docs/guide/assets.md`). `ConvexHull::Create` checks what it is given and refuses anything the narrow phase could not use (`Validation`). The mass of a hull body is the hull's own: its centre of mass is the centroid and its principal axes come from a Jacobi eigen-decomposition of the stored tensor, so a body's `RecomputeMass` places the centre at `offset + rotation * centroid`.

## The world: bodies, colliders and ids

`physics::PhysicsWorld` holds the bodies and colliders of a simulation. It is an ordinary value (keep it by itself or as an ECS resource with
`ecs::World::SetResource`). A `BodyId` is a slot index and a generation: a destroyed body's id is **stale** and every call refuses it (`BodyStatus::StaleId`)
instead of reaching the body that took the slot. Slots are reused last-freed-first, so two worlds built by the same calls hand out the same ids, which the
state hash relies on. Bodies and colliders live in chunks of 256 that never move: a `Body*` from `Find` stays valid until that body is destroyed, and
100,000 destroy-and-create cycles do not grow the world (`Stats().body_slots`).

`CreateBody(BodyDesc)` makes a body; `AddCollider` gives it its shape and recomputes the mass (a body takes one collider compound bodies are described below).
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

**A static body with a parent** is the one exception to "no `Parent`": it is placed at the translation and
rotation of its `ecs::WorldTransform`, the scale divided out of each column (the caller sizes the shape; a mirror stays in the shape, the rotation
stays proper), and it follows that matrix: every call moves the body where the matrix changed (`SetTransform`, a teleport: the bodies touching it
are woken, at the old place and, by an overlap of its bounds, at the new one; counted in `SyncReport::replaced`). Dynamic and kinematic bodies with
a parent are still not simulated. The matrix is the one the propagation last wrote, so a schedule that runs the sync before the transform system
(the C API's) places such a body by the previous frame's matrix. A static body without a parent is not followed, as before. The write-back touches
dynamic bodies only, so a parented static entity's local transform is never overwritten.

## The broad phase

`physics::BroadPhase` finds which pairs of colliders may touch, with the spatial module's structures and nothing new: the colliders go into a `spatial::SpatialSet`
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
Debug and Release (`Step: the state hash of a 600-step scene...`); the Windows and macOS machines compare it.

## The narrow phase: contacts for the shapes with closed forms

`physics::Collide(a, pose_a, b, pose_b, margin, &manifold)` is the one entry point of the narrow phase. It returns `Contact` (and fills a `ContactManifold`),
`None` (the shapes are apart by more than the margin) or `Unsupported` (no test for the pair yet). The table, with the day that fills each cell:

| | sphere | capsule | box | hull |
|---|---|---|---|---|
| **sphere** | built | built | built | built |
| **capsule** | | built | built | built |
| **box** | | | built | built |
| **hull** | | | | built |
| **mesh** (static) | built | built | built | built |

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

**Box against box** is the separating axis test over the 15 axes (3 + 3 face normals and the 9 edge cross products; parallel edge pairs are skipped). A
face axis wins unless an edge axis is better by more than 1e-3, and among the faces the first one wins ties (1e-4), so a resting box does not flicker between
feature types. A **face contact** clips the incident face (the face of the other box most opposed to the reference normal) against the four side planes of the
reference face (Sutherland-Hodgman), keeps the vertices within the margin, and reduces more than four to the deepest, the farthest from it, and the two that
span the most area on either side. Each point's `feature_id` is the reference box (A or B), the reference face, the incident face and the id of the clipped
vertex (an original corner, or a hash of the two neighbours and the side plane it was cut by), so a box that slides keeps its ids. An **edge contact** is one
point between the closest points of the two support edges; its id has the top bit set. Identical boxes pick the same axis every time. Near a tie between two axes within the biases, `Collide(a, b)` and `Collide(b, a)` may choose different features (the same deepest depth, different points). **Capsule against box**
works in the box's frame: the capsule's axis is cut where it crosses the planes of the box's slabs, the distance is minimized in closed form on each piece (the
nearest point of the box moves linearly there), and the result is a sphere-like contact at the nearest point of the axis, or two points at the ends of the
stretch where the axis lies flat above a face. An axis that meets the box leaves through the face of least penetration of the whole capsule.

**Hulls** go through one path for every pair that has a hull: **GJK** on the two cores (a point, a segment, a box or a hull, each with the radius of a sphere or capsule), and when the cores overlap or touch, **EPA** on the Minkowski difference for the normal and the depth. The support of a hull is a scan of its vertices (at most 255; a hill-climbing support is deferred). Apart shapes get the distance of the cores minus the radii as a speculative depth; the gap is normalized by its own length, and cores closer than 1e-5 count as touching. When both shapes are box or hull (radius zero) and the best-aligned face of either is within 0.9 of the contact normal, the manifold is a **face contact** exactly as for two boxes (clip the incident polygon against the reference face, reduce to four); otherwise it is **one point**. A capsule against a hull is always one point for now. The `feature_id` of a face contact is the same code as for boxes with the hull's vertex index (its face indices are the hull's own); a one-point contact has `0x40000000` in its id. Because hulls go to the same code as boxes, a hull copy of a box agrees with the box tests to within the margin for shallow overlaps; a deep overlap has several ways out and the closed forms and EPA may pick different ones. 30,000 random hull pairs hash to `172dfbee2611621e` under GCC and Clang, unity and not, Debug and Release.

**Triangle meshes**. `TriangleMesh::Create(spec)` checks the numbers (counts, finite positions, indices, no triangle without area) and builds the face normals, the triangle across each edge (`Neighbor`) and a `spatial::StaticBvh` over the triangle boxes; `QueryBox` returns the triangles whose boxes overlap a box, in increasing order. The mesh is the data of the minted `MCOL` asset (`docs/guide/assets.md`). `Collide(mesh, pose, other, pose, margin, &m, hulls, meshes)` takes the other shape's box in the mesh's space, asks the hierarchy for the triangles (at most 256 are tested, 64 contacts kept), collides each as a flat two-sided convex piece through the same GJK/EPA and face clipping as hulls (the triangle has a front and a back face, so a box lying on it clips to its corners), and merges the results: the contact of the deepest triangle sets the normal, contacts within 11 degrees of it join (the same point reached through two triangles is kept once), and more than four reduce as for boxes. A flat triangle is also left by its plane: when the shortest way out is across the plane, that exit is the contact. **Internal edges:** a one-point contact on an edge shared with a nearly coplanar triangle (normals within 0.99) takes the face's normal, so a body sliding over a seam does not catch; the side is the one the other shape's centre is on. Limits: a mesh is a thin two-sided surface, so a body whose centre passes through a triangle is pushed out the other side (tunnelling, stated for the solver days); concave corners keep only the deepest normal's group for a step; a mesh against a mesh is `Unsupported`; the query allocates (a `Vector` per call).

**Compounds, groups, masks and triggers**. `Collide` takes a compound against anything: each child (or each pair of children, for two compounds) is collided on its own, children whose boxes do not meet are skipped, and the contacts merge as for a mesh; a point's `feature_id` mixes the child indices, so warm starting and events tell children apart. A collider has a `group` and a `mask` word (default group 1, mask all bits); the broad phase keeps a pair when the layer matrix lets it and `(a.group & b.mask) != 0 && (b.group & a.mask) != 0` (`GroupsCollide`); two children of two compounds are filtered the same way by their own words. There are no per-pair filter callbacks and no one-way platforms (determinism). A **trigger** collider makes pairs flagged `trigger` and begin and end events, and no contact is solved for it; two triggers make a pair only when `BroadPhaseConfig::trigger_pairs` is set (off by default). Changing a collider's shape (`SetCollider`) recomputes the body's mass and the next `BroadPhase::Update` moves its bound; contact caches arrive with the solver.

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

## Sleeping and islands

A group of dynamic bodies that touch (contacts) or are tied (joints) is an **island**. Islands are rebuilt every step by a
union-find over the bodies' slots in which the root is always the lowest slot, so the groups and their numbers do not depend on
the order bodies were made, visited or paired in: `IslandOf(id, &n)` is the lowest slot of the group plus one (zero for a static or
kinematic body). Static and kinematic bodies never join an island.

An island **falls asleep** once every body in it has stayed below `sleep_linear` (m/s) and `sleep_angular` (rad/s) for
`sleep_time` seconds (settings `physics.sleep_linear`, `physics.sleep_angular`, `physics.sleep_time`, `physics.allow_sleep`; a
body with `kBodyNoSleep` keeps its whole island awake). A sleeping body has zero velocity, is not integrated, and joins no
constraint: the solver sees no manifold and no joint for a sleeping island, so a scene at rest costs the broad phase's pair
bookkeeping and almost nothing else (`SolverStats::manifolds` is zero). It stays in the broad phase at its place, so its pairs
persist without events.

Sleeping bodies **wake**, the whole island at once, when

- a contact is made between a sleeping body and a dynamic body that is awake, a kinematic body that moves, or a body that was
  changed from outside this step;
- a joint ties it to such a body, or a joint of it is created or destroyed;
- `ApplyForce`, `ApplyImpulse`, `SetVelocity`, `SetTransform`, `SetSettings`, `AddCollider`, `SetCollider` or `Wake` is called on a
  body of the island;
- a body that is not dynamic (a platform) is moved, changed or destroyed while a sleeping body touches it: the neighbours of the
  last step's pairs wake.

A body that rests on a kinematic body that moves, however slowly, never sleeps. A contact cache entry of a sleeping contact is dropped,
so a waking stack starts without warm start impulses.

`StepStats` reports `islands`, `sleeping_bodies`, `islands_slept` and `bodies_woken`.

## The parallel step

`PhysicsWorld::SetScheduler` gives the step a `jobs::Scheduler`; with `StepConfig::threads` (setting `physics.threads`) above 1 the
narrow phase and the integration of free bodies run as jobs, and `Step` must then be called from the scheduler's main thread,
outside any job. The narrow phase works on the pair list in blocks of 32 pairs: each block only reads the bodies and writes its own
slots, and the contacts are then applied on the main thread in pair order, so the manifolds, the wakes and the island edges come out
in the same order whatever the thread count. Free bodies (those the solver did not integrate) are integrated in blocks of 256, each
body independent. No sum is made across jobs. The state hash is the same to the bit for 0 (no scheduler), 1, 2, 4, 8 and 32
participants (`tests/physics/parallel_test.cpp`). The contact solver itself still runs on one thread (`KNOWN-ISSUES.md`).

## Continuous collision

A body with `kBodyContinuous`, or one that travels more than `physics.ccd_threshold` times its collider's smallest extent in a
step (zero turns this rule off), is swept after its motion against the static and kinematic colliders its swept box meets: the
path from the step's start pose to its end pose is sampled at a quarter of the smallest extent, the first sample that
overlaps is bisected 24 times, and the body is moved back to the last pose that does not overlap, keeping its velocity, so the
next step's speculative contact removes the approach. A body already within the contact margin at the start is left to the contact
solver. The sweep uses the same `Collide` as the step, so every shape pair works, a mesh included. Only linear motion is swept (the
orientation is blended), dynamic targets are not swept, and a body is clamped once per step. `StepStats::ccd_queries` and `ccd_hits`
count the work; both stay zero when no body is continuous.

## Queries

`physics/query.h` has free functions over a `const PhysicsWorld&` that read the world as it is now (after the last step and any change made since): `RayCast` (the closest hit
with collider, body, point, normal, distance and, for a mesh, the triangle), `RayCastAll` (every collider the ray meets, nearest first,
ties by collider key), `ShapeCast` (sweeps a sphere, box, capsule or hull and returns the first collider it touches, by the same sampling
and bisection as continuous collision), `Overlap` (the colliders a shape touches, sorted by id key) and `ClosestPoint` (the nearest
surface point of any collider within a distance). A `PhysicsQueryFilter` selects colliders by layer mask, group and mask words, triggers
(`include_triggers`, off by default), the body class (`include_static`, `include_kinematic`, `include_dynamic`) and an ignore list of bodies.
A ray that starts inside a solid collider hits it at distance zero with the normal against the ray. The exact ray tests are the quadratic
for a sphere, slabs for a box, the cylinder and the two end spheres for a capsule, the face planes for a hull, the box query and
Moller-Trumbore for a mesh and the children for a compound. The queries change nothing and may run from several threads while no step or
change runs; the results are sorted, so they do not depend on the thread.

**The query index** (query and hierarchy indices series). The world keeps every collider's exact world box in a spatial set of its
own: static colliders in its static tree, the others in its dynamic tree with fat boxes. It is updated by the calls that change a
collider's box or class (`AddCollider`, `SetCollider`, `SetTransform`, `SetSettings`, `DestroyBody`), at the end of each `Step` for the
colliders of every body that is not static, and rebuilt by `Restore`; a query never changes it, so concurrent queries stay safe. A query
asks it for the colliders under the nodes its own box test accepts (`RayCast` and `RayCastAll`: the slab test along the whole ray;
`ShapeCast`: the swept box; `Overlap`: the shape's box; `ClosestPoint`, which has no box test of its own: the point's box grown by twice
the distance), sorts them by slot and runs the same per-collider code as before. Rounding is monotone, so a box test that accepts a box
accepts every box containing it: no collider the old scan would find is left out, the visit order is the old one, and the results are the
same bit for bit (`tests/physics/query_index_test.cpp` compares both paths over random changing scenes; `SetQueryIndexEnabled(false)` turns
the index off for that comparison). A pose or shape changed by writing through `Find` or `FindCollider` instead of those calls is not
followed: builds with slow checks abort the next query when the index is behind. A collider the set cannot hold (a box with a non-finite
corner) makes the queries scan every collider until it goes.

## The character controller

`CharacterController` (`physics/character.h`) is a kinematic capsule the program moves once per fixed step: `Create` makes the body and its
capsule collider, `Move(world, wish_velocity, dt)` is called before `PhysicsWorld::Step`, and `Jump` sets the vertical speed. `Move` sweeps
the capsule with the shape cast of the query module against static and kinematic bodies only, slides along what it meets (four planes in turn;
at a crease the motion follows the line of the two planes; a wall is treated as upright so the round bottom cannot ride up a corner), climbs a
ledge up to `step_height` by casting up, forward and down (it first checks that flat ground within the step height lies a radius past the edge),
snaps to the ground when walking down within the step height, treats a surface steeper than `min_ground_y` (the cosine of the slope limit) as a
wall that the character slides down, and adds the velocity of the body it stands on (a lift or a conveyor). Gravity and jumps are kept as a
vertical speed. The result is not written as a position: it is set as the body's velocity (displacement over `dt`), so the step carries the
character there and the solver pushes dynamic bodies out of its way, while nothing pushes the character. `Move` returns `CharacterEvents`
(`began_ground`, `ended_ground`, the walls hit and the last wall normal); `State()` has `on_ground`, `ground_body`, `ground_normal`. The
walk is the same at 60 and 120 steps per second to a few centimetres (`tests/physics/character_test.cpp`).

**On imported ground** (glTF colliders, 2026-11-20): a triangle mesh has no inside (a body whose centre passes through a triangle is pushed out the
other side), but the character's move is a swept capsule, so it never passes through a single-sided ground at walking or falling speeds; a fast
dynamic body can (continuous collision is for convex shapes against meshes): give such grounds a closed volume, or keep them at least as thick as a
step's fall. Static colliders with a parent (a spawned level's) follow their entity's world position and rotation, not later scale changes. A
physics snapshot restores into the world it was taken from: the mesh ids in it are that world's.

## Events, joints as components and debug drawing

**Events.** `PhysicsWorld::Events()` holds what happened in the last `Update` (or the last `Step` called by hand; `Update` clears the lists at
its start and keeps those of all its steps): `contacts` (`ContactEvent`: begin, end and, with `StepConfig::stay_events`, stay, for a pair of
colliders, with the body and entity of each, the number of points, the deepest point, the normal, the depth and the solver's total normal
impulse), `triggers` (`TriggerEvent`: enter and exit of a collider that overlaps a trigger collider, tested exactly with the narrow phase and
not by the broad phase's fat boxes) and `joint_breaks` (`JointBreakEvent`: the joint, its bodies and entities, the force and torque of its last
step). Each list is in step order and, within a step, in order of the collider keys, so two runs give the same events. A pair of bodies at rest
keeps its contact (no end event when an island falls asleep); destroying a body removes the events that name it and forgets its pairs without an
end event. `PhysicsWorld::Contacts()` is the view of every pair that touches now, with the manifold of the last step, and `AllStats()` gathers
the world's counters in one value.

**Joints as components.** An entity with a `Joint` (`physics/joint_sync.h`) names two entities and the joint's kind, anchors and limits;
`SyncJoints` (after `SyncBodies`, at a sync point) makes the physics joint when both entities have a body, adds `JointLink` and destroys the
joint when the component, an entity or a body goes. A joint that broke is noticed on the next call: its `Joint` is removed so it is not made
again.

**Transform write-back.** The step system and `WriteBackTransforms` write a dynamic body's pose into `LocalTransform` only when it differs,
and stamp the chunk as written only then (`LazyWrite`), so a sleeping body does not mark its transform changed for `Changed<LocalTransform>`.

**Debug drawing.** `DebugDraw(world, options, &lines)` appends `spatial::DebugLine` segments (a sphere is three circles of 16 segments, a box 12
edges, a capsule 68; a hull, mesh or compound is drawn as its local box) for the shapes, colored by type, sleeping state and island, the contact
points with their normals, the joints and the broad phase's boxes; the program turns them into a line pass, as with the spatial module. It reads
the world as of the last step and allocates only when the vector must grow. `StepConfig::debug_draw` (setting `physics.debug_draw`) is the
switch a program reads.

**Kinematic bodies and entities.** `DriveKinematicBodies(ecs, physics, dt)` (`physics/system.h`) sets the velocity of every kinematic body so the
next step carries it to its entity's `LocalTransform` (a platform or door moved by moving its entity); call it before `Update`, not for a body
whose velocity the program sets itself (a character's).

## Snapshot, restore and replay

`PhysicsWorld::Snapshot(&writer)` appends the whole simulation to a `bytes::ByteWriter`: the step configuration, the accumulator and the step
counter, the three slot pools (bodies, colliders and joints, with each slot's generation, whether it is alive and the free list in its order, so
the ids a program holds stay valid and the next id made is the same one), the contact solver's warm-start cache, the touching pairs and the trigger
overlaps. The layout is canonical: little-endian, floats as their bit patterns, counts as varints, a magic (`PHSN`) and a version first. The mass,
the centre of mass and the inertia are not stored: they follow from the settings and the shapes, and `Restore` makes them again. The hull, mesh and
compound libraries are content, not state, and are not in the snapshot: a snapshot names the shapes by their ids and `Restore` refuses
(`SnapshotStatus::LibraryMismatch`) one whose shapes the world's own libraries do not hold.

`Restore(bytes)` builds a world of its own from the bytes, checks everything and swaps it in only then, so a refusal leaves the world as it was.
The data is not trusted: counts are bounded by the bytes left, every float must be finite and within a bound the step can survive (positions
within `kMaxCoordinate`, sizes, offsets and anchors within `kMaxExtent`, masses between a nanogram and 1e15 kilograms), quaternions must be unit,
collider chains, owners and joint bodies must name live slots, the configuration must pass `SetStepConfig`, and trailing bytes are refused. The
answer is a `SnapshotStatus`: `Ok`, `BadMagic`, `UnknownVersion`, `Truncated`, `Corrupt`, `LibraryMismatch` or `OutOfMemory`. The same bounds are
checked by `Validate()` of `RigidBody`, `PhysicsMaterial`, `Collider`, `Shape` and `JointDesc` when a program creates the objects, so a world that
could not be built by hand cannot be restored either.

What is not in a snapshot is what is not state: the event lists of the last step, the statistics, the debug lines and the scheduler (a restored world keeps its own).
A world restored from a snapshot continues bit for bit: the state hash after every later step equals the hash of the run the snapshot was taken
from (the tests restore at a dozen points of a stack, a joint chain and a rolling scene and compare 600 steps). `SaveSnapshot(world, &store, key)` and
`LoadSnapshot(&world, store, key, &status)` (`physics/snapshot.h`) keep the bytes as one entity record in a `storage::Store`, so a save game or a
rollback buffer is the engine's storage and not a private file.

**Replay.** An `InputLog` (`physics/input_log.h`) lists the inputs a program applied to the world with the step each one was applied before:
forces, impulses, velocity and transform changes, body creation and destruction. `InputRecorder` wraps a world and records as the program calls it;
`ReplayStep(world, log, &cursor)` applies the entries of the world's current step and then steps. A run is therefore a snapshot plus a log, and
the log has its own versioned encoding (`Save`, `Load`) refused on the same terms. `FirstEntryAt(log, step)` finds where to start after restoring
a snapshot taken at that step.

## A demo you can watch

`game/physics_demo` is the whole pipeline on screen: a floor in a low tray, random boxes, spheres and capsules on it and
more of them spawning in the air and falling. Every shape is an entity (`scene::MeshRenderer`, `RigidBody`, `Collider`,
`ecs::LocalTransform`); one `ecs::Schedule` runs the physics step system, the transform propagation, the camera system
and the render sync, and `SyncBodies` makes the bodies of the shapes spawned since the last frame. Run
`build/game/physics_demo/physics_demo` (Space pauses, Left/Right turn the camera) or `--frames 600 --screenshot shot.ppm`
for an offscreen picture; the ctest `physics_demo_screenshot` runs it headless. It is a program written against the
engine's modules, not the C API (which is for hosts that embed the engine).

## Cost: the benchmark and the smoke test

`game/physics_bench` (`--bodies N --steps N --threads N --scene drop|mesh|joints|rays|characters|all`) measures the step on scenes of the day: a grid of spheres and boxes dropped on a floor (awake while falling and landing, then asleep), a world with a one-million-triangle mesh, chains of joints, ray casts and walking characters. It prints the step's time and its parts from `StepStats` (`broad_ns`, `narrow_ns`, `solve_ns`, `integrate_ns`, `after_ns`: wall-clock, not state, never in a snapshot or a hash). It is not run by ctest except for the `large` scene (below); `tests/physics/perf_smoke_test.cpp` runs 500 falling bodies with a bound far above any build's time, so only a gross regression fails.

Release build, one thread, Ryzen 9 7950X3D (Linux, clang), milliseconds per step:

| bodies | falling (no contacts) | landing (pile forming) | asleep |
|---|---|---|---|
| 1,000 | 0.97 (broad 0.82) | 0.14 | 0.14 |
| 5,000 | 7.8 (broad 7.4) | 31.9 (solve 18.9, broad 8.8, narrow 2.6) | 34.8 |
| 20,000 | 40.6 (broad 39.1) | 183.7 (solve 94.6, broad 62.3, narrow 17.9) | 212 |
| 50,000 | 108 (broad 104) | 561 (solve 273, broad 209, narrow 53) | 403 |

A mesh of 1,000,000 triangles builds in 0.43 s; 1,000 bodies on it step in 12.8 ms (narrow phase 10.7). With eight threads at 20,000 bodies the narrow phase halves (17.9 to 8.5 ms) and the solver does not scale (islands are the unit of work; a pile that is one island runs on one thread). A step of 5,000 awake bodies is therefore about 8 ms while they fall and about 32 ms while a pile forms; sleeping bodies are not free (a sleeping body still costs its broad-phase update, 7 ms per 5,000 here).

**What the benchmark found and fixed.** The pair tracker's search among the dynamic items reached as far as the largest margin of *any* item, and a single large static item (the floor, margin 10 percent of its 60 m extent) made it six metres: every moving body looked at every body within six metres, quadratic in a dense scene (650 ms for 20,000 falling bodies, with no contact at all). The tracker (`spatial::PairTracker`) now keeps the largest margin of the static items and of the dynamic items apart and searches each side with its own reach (`SpatialSet::ForEachHandleInRegion(static_region, dynamic_region, visit)`): 53 ms became 7.4 ms at 5,000 bodies, and the cost is linear. The pairs, the events and the state hashes are unchanged (the golden hashes pass). The tracker is single-threaded; at 50,000 moving bodies it is 104 ms, so parallelising its gather (disjoint output per chunk, merged in key order) remains the next step of the broad phase and is recorded in `KNOWN-ISSUES.md`.

### The Release-only stress run

`physics_bench --scene large` (ctest `physics_bench_large`, registered only for Release and RelWithDebInfo builds, about three minutes on one thread) drops 50,000 spheres and boxes between four walls and steps them 300 times with the state checked every 50 steps (finite, inside the walls, unit orientations, a valid state hash, no body or collider slot gained or lost), then steps a walled box of 100 bodies 1,000,000 times with a body kicked every 500 steps so the world never sleeps for good, checking every 50,000 steps. The pile of 50,000 bodies steps in 719 ms ( solver 353, broad 225, narrow 74, the rest 65, 135,624 contacts); the long run takes about 25 s per million steps. It exits 1 on a failed check.

## Limits

What the stress tests (`tests/physics/stress_test.cpp`) and the benchmark found, and what is deliberately not here:

- **Tall stacks.** A stack of unit boxes stands at thirty and topples at fifty (600 steps at 60 Hz, four substeps); the pile it makes is finite and bounded. More substeps or a block solver would raise the height; neither is built.
- **Large mass ratios.** At 1:10,000 (a 10-tonne box on six 1 kg boxes) the light boxes are squeezed out and thrown a few tens of metres; nothing becomes NaN and the heavy body stays above the floor. Ratios of 1:100 are inside the stability tests.
- **Queries go through the query index** (see "Queries"): 846,000 rays per second against 5,000 bodies (8,600 when they scanned), and 2,000 characters step in 10 ms (294 before). The index costs the step about 0.55 ms at 5,000 falling bodies on one thread (3%), and under 1% at rest or at 50,000 bodies, also in a world that never queries.
- **Joints and the narrow phase.** The step looks for a pair's joint among all joints that forbid collision (a linear scan per pair), which grows with joints times pairs; chains of 5,000 bodies with 5,000 joints step in 2.4 ms asleep, awake scenes of many joints are not measured.
- **One island, one thread.** The solver parallelises over islands: a single huge pile runs on one thread, and there is no graph colouring.
- **Static meshes only.** A triangle mesh collider is static; a mesh against a mesh is unsupported; no one-way platforms; no soft bodies, cloth or fluids.
- **The large stress run is Release only.** In a debug build the same code is 6 to 17 times slower (1,000 bodies landing: 1.09 ms against 0.16; 5,000 landing: 466 ms against 40; the solver alone 390 ms against 23), so the tests that run in every flavor use 400 bodies and 100,000 steps (`tests/physics/stress_test.cpp`) and the planned scale runs only where it fits: see "The Release-only stress run" above.
- **Not measured:** the number of allocations per step after warm-up and the peak memory of 50,000 bodies; 100,000 bodies and ten million steps (the large run uses 50,000 bodies and a million steps).

## The C API

A host reaches all of this through the engine's C ABI (`include/engine/engine_c.h`) with no engine pointer: `ENGINE_COMPONENT_RIGID_BODY`, `_COLLIDER`, `_JOINT` and `_CHARACTER` on an entity make the body, the collider, the joint and the character; `frame` runs the fixed steps; `physics_events`, `physics_raycast`, `physics_overlap_sphere`, `physics_overlap_box`, `body_apply_force`, `body_apply_impulse`, `body_set_velocity`, `body_teleport`, `character_move`, `character_jump`, `joint_reaction`, `physics_snapshot` and `physics_restore` are the table functions. The tutorial [Physics in Your Window](../tutorials/physics-in-your-window.md) walks through them; `tests/engine_library/physics_sample.c` is the program, `physics_fuzz.c` feeds the table thousands of random and hostile calls (no call may crash; the sanitizer flavors check memory), and the sample's checksum is the same on the three machines.
