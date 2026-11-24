# Maintenance: scripts find a joint and turn it

Not a numbered phase, no day files. Request from a host project (2026-11-24), after `host-joint-rotation.md` (thank you, it works). The host's robots tilt
their heads with their player's look angle, and the host wants that logic in a Lua behavior, not in its C++: the behavior would hear the angle as a host event and
turn the head joint. Only the C API can set a joint rotation today; a script cannot find a joint or turn it.

## Wanted

Two bindings, with the same meaning and checks as the C calls `skeleton_find_joint` and `animation_set_joint_rotation`:

1. `skeleton.find_joint(entity, name)`: the index of the joint called `name` in the skeleton of `entity` (the entity that carries the skeleton instance), or nil
   when the entity has no skeleton instance or the skeleton has no such joint. World-read, as of the tick's start. (A name the skeleton was built without is
   nil too.)
2. `animation.set_joint_rotation(entity, joint, quaternion)`: adds the rotation to the joint, after the clips, as the C call does; `quaternion` is a `quat`
   (the one `quat.from_axis_angle` makes); the identity takes it away. A world-write command applied at the merge, so a script that calls it every tick for the same
   joint overwrites itself and several calls in one tick are last-wins. The call is an error where it is made for what the C call refuses with `INVALID_ARGUMENT`
   (a joint the skeleton does not have, a rotation that is not finite or not of length 1 within 0.1%), naming the field.

`entity` may be the script's own entity (`self.entity`) or one it found.

## Tests

A C API test next to `host_script_scene.c`: a script on a skeleton entity finds `head` by name (and gets nil for a name that is not there and for an entity with no
skeleton), sets a rotation about X, and the host reads the joint's matrix with `skeleton_joint_transform` and finds the clip's matrix composed with it; the identity
brings the clip's matrix back; a bad joint index and a non-unit quaternion are script errors; an event delivered to the entity (`script_event_emit` with a `target`)
drives it.

## What the host does with it

`assets/worlds/robot_head.lua` (a behavior on each robot's skeleton entity) finds `head` once, listens to a `look` event that the host sends to that entity every
frame with the player's pitch, keeps it within 45 degrees, turns the joint by it when it moved by more than a degree, and nothing else. The host's C++ loses its
`SetJointRotation` code and only emits the event.

## Done

- [x] the two bindings (`engine_c/script_scene.cpp`, command kind 138; the validation moved to `animation::CheckHostRotation` and `SetHostJointRotation`, shared with the C call)
- [x] C API test (`host_script_scene.c`, `TestJoints`)
- [x] guide text (scripting.md)
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
