# Tutorial: Physics in Your Window

**Goal:** Put bodies, a joint, a character and a trigger in a world through the C API, step them with `frame`, read what happened, and rewind the physics with a snapshot.

**You need:** the program of [A 3D Scene in Your Window](a-3d-scene-in-your-window.md) working, or just [Load the Engine and Clear a Window](loading-the-engine.md) (physics needs no window). The program of this page is `tests/engine_library/physics_sample.c`; `cmake --build build/linux --target physics_sample` builds it and `./build/linux/tests/engine_library/physics_sample` runs it: it needs no display, runs 600 frames, rewinds to frame 300 and prints a checksum that is the same on every build and machine. The guide: [Physics](../guide/physics.md) and [The Engine as a Loadable Library](../guide/engine-library.md).

## Steps

1. **Make bodies from entities.** An entity with `ENGINE_COMPONENT_TRANSFORM`, `ENGINE_COMPONENT_RIGID_BODY` (`ENGINE_BODY_STATIC`, `ENGINE_BODY_KINEMATIC` or `ENGINE_BODY_DYNAMIC`) and `ENGINE_COMPONENT_COLLIDER` (a sphere, a box or a capsule; a layer, a group and a mask; `is_trigger` for a volume that reports and does not push) is a body. The frame makes the physics body the first time it sees the three, steps it, and writes the pose back to the transform.
2. **Step with `frame`.** Each `frame` call runs the fixed physics steps its `delta_seconds` is worth (60 Hz by default; the `physics.*` settings change it). The numbers are plain: no callback is made, and a program that never sets a body component pays nothing.
3. **Tie two bodies with a joint.** An entity with `ENGINE_COMPONENT_JOINT` is the joint: `entity_a` and `entity_b` name the bodies (0 on one side ties the other to the world), `kind` is `ENGINE_JOINT_BALL`, `_DISTANCE`, `_FIXED` or `_HINGE`, and the anchors and the axis are in world space when you set it. `break_force` above zero makes it break; `joint_reaction` reads the force and torque it carried in the last step. Setting the component again with new numbers makes the joint again.
4. **Walk a character.** An entity with a transform and `ENGINE_COMPONENT_CHARACTER` gets an upright capsule. Each step, `character_move` with the wanted velocity slides it along walls, climbs steps and keeps it on the ground; `character_jump` starts a jump from the ground. The state it returns says whether it stands, on what, and what it ran into. The entity's transform follows the capsule's centre.
5. **Read the events.** `physics_events` copies the events of the last frame to an array of yours: contact begin and end (with the point, the normal and the depth), trigger enter and exit, and joint breaks, each with the two entities. Call it after `frame`; the next `frame` replaces them. Asking with a capacity of 0 returns only the count.
6. **Rewind.** `physics_snapshot` returns the physics state as a buffer you release with `engine_buffer_release`; `physics_restore` puts it back (the bodies, velocities and contacts), and the transforms follow. A restored run continues bit for bit as the first did, which is what the sample checks with its checksum. Restore into a world that holds the same entities: bodies of entities made since are made again, bodies of entities destroyed since are removed by the next frame.
7. **Ask about the world.** `physics_raycast`, `physics_overlap_sphere` and `physics_overlap_box` answer with entities; `body_apply_force`, `body_apply_impulse`, `body_set_velocity` and `body_teleport` push bodies from outside the step.

## Where to go next

- [A 3D Scene in Your Window](a-3d-scene-in-your-window.md) to draw what you simulate: the transforms the physics writes are the ones the renderer reads.
- The guide's sections on joints, the character controller, events and snapshots.
