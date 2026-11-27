# physics_demo

A flat floor in a low tray, a few random boxes, spheres and capsules lying on it, and more of them spawning in the air
every 0.15 s and falling onto it. When `--shapes` are alive (default 160) each new shape replaces the oldest.

Everything is an ECS entity: `scene::MeshRenderer`, `physics::RigidBody`, `physics::Collider` and `ecs::LocalTransform`
(its scale sizes the mesh). One `ecs::Schedule` runs `physics::MakePhysicsStepSystem` (fixed 60 Hz step, interpolated
poses written back to the transforms), the transform propagation, the camera system and `scene::MakeRenderSyncSystem`,
which mirrors the entities into the deferred renderer's scene. `physics::SyncBodies` makes the bodies of the shapes
spawned since the last frame. The camera and the sun are entities too.

```bash
build/linux/game/physics_demo/physics_demo                       # a window; Space pauses, Left/Right turn the camera
build/linux/game/physics_demo/physics_demo --frames 600 --screenshot shot.ppm   # offscreen, no window
```

Options: `--shapes N`, `--seed N`, `--paused`, `--frames N`, `--screenshot P`, `--backend B`, and the settings system's
`--set` / `--settings` flags (`--help`).
