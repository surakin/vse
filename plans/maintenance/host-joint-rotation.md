# Maintenance: the host turns a joint of a playing skeleton (a head that looks up and down)

Not a numbered phase, no day files. Request from a host project (2026-11-24). Nothing is broken; this is the one missing piece for a robot whose head follows
where its player looks.

## Why

The host's 3D call room draws every participant as a skinned robot (`robot.glb`: a skeleton with a `head` bone, rigid skinning, an idle and a walk clip that
both nudge the head a little). Each client already knows how far up or down every other participant is looking (an angle, ±80°, 20 times a second), and
wants the head to tilt with it, on top of whichever clip plays. The C API can play, speed up and stop a clip and read a joint's matrix
(`skeleton_joint_transform`), and can make an entity follow a joint, but nothing sets a joint's pose. The animation library has `BlendMasked` and `AddAdditive`
underneath, which is the shape of what is needed; it is not reachable from the C API or from scripts.

## Wanted

1. **A rotation the host adds to one joint.** For example
   `animation_set_joint_rotation(world, key, joint, const float quaternion[4])` (x, y, z, w, unit; `joint` from `skeleton_find_joint`), applied after the clips
   of that entity have been sampled and blended: the joint's local rotation becomes the clip's rotation composed with this one (the host's rotation is in the
   joint's own space, so a rotation about the joint's local X is a nod whatever the clip does). A rotation of the identity (or a call to remove it) takes it away.
   It stays until changed, so a host that sets it only when the angle changes costs nothing. How it is written is yours to choose: an additive layer on the
   entity's player, a per-joint override, or a general "pose layer" that scripts could use too. If you give it a weight, 1 is the default.
2. It works with every clip, with a clip fading in (`fade_seconds`), and with no clip playing (the bind pose plus the rotation).
3. Errors as for the other animation calls: `STALE_HANDLE`, `INVALID_ARGUMENT` (a joint that does not exist, a rotation that is not unit length within a
   tolerance or not finite), `NOT_FOUND` (no skeleton instance), `TYPE_MISMATCH`, `STRUCT_SIZE` if you use a struct.
4. Joints below the changed one follow it (the head's children), and the skinned mesh draws with it on every surface that shows the entity.
5. Document it in the C API header and in the animation section of the guide.

## Tests

A C API test (next to the animation tests): a skeleton with a parent and a child joint and a clip that moves the parent; set a rotation on the parent and
check with `skeleton_joint_transform` that the joint's matrix is the clip's matrix composed with the rotation (and the child's follows it); set the identity
and check it is the clip's matrix again; a bad index and a rotation that is not a unit quaternion fail with the codes above; the rotation survives the clip
changing (`animation_play` of another clip) and is applied while a fade runs.

## What the host does with it

`EngineScene` (`ui/shared/views/world/EngineScene.cpp`) finds the `head` joint of each robot when it spawns it (`skeleton_find_joint` on the entity that carries
the skeleton instance) and, each frame, sets the joint's rotation about its local X to the participant's look angle, limited to ±45° and changed only
when it moved by more than a degree. Nothing else on the host changes; the angle is already on the wire.

## Done

- [x] the call in `engine_c.h`
- [x] the pose layer or override behind it (`JointRotations`, applied in `AnimationSystem::Update`; ABI minor unchanged, table appended)
- [x] C API test (`host_scene.c`, `animation` unit test)
- [x] header and guide text
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
