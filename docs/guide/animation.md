# Animation

Status: written. The runtime that turns a clip and a time into joint matrices: `engine/animation`. It reads the
views of [assets](assets.md) (`asset::SkeletonView`, `asset::ClipView`) and depends on `ecs`, `storage` and `jobs`; no
renderer, RHI or platform header.

## Poses

`animation::LocalPose` holds the local translation, rotation (a unit quaternion, x y z w) and scale of up to 256 joints
in structure-of-arrays form; `ModelPose` the model-space 3x4 matrices. Every operation works in place on storage the
caller owns and allocates nothing. `Blend(a, b, w)` moves `a` toward `b` (weights 0 and 1 give the inputs bit for bit;
rotations by a normalized lerp on the shorter arc), `BlendMasked` scales the weight per joint by a `JointMask`, and
`AddAdditive` applies a pose of differences from the bind pose (translation added, rotation composed, scale multiplied).
Weights are normalized by the caller. Mirroring is not supported.

## Sampling

`SampleClip(clip, skeleton, time, looping, state, &pose)` starts from the bind pose (the identity for an additive clip)
and writes every channel the clip animates. A `SamplerState` keeps one cursor per channel: steady forward playback finds
its key segment in O(1), a seek or a loop by bisection, and the result is the same either way. A clip flagged
root motion holds joint 0 at its first frame; `RootMotionOver(clip, skeleton, from, delta, looping)` gives the root's
translation and rotation difference over a step, for the game to apply. `AdvanceTime` moves a play time (wrapping or
stopping), and `CollectEvents` lists the events crossed in time order: an event at `e` is crossed once when
`from < e <= new time`, across a loop as `from < e <= duration` then `0 <= e <= new time`.

The rotation between keys is a normalized lerp, not a slerp. Over a 170 degree segment its angle differs from the
constant-speed path by at most 6.75 degrees (the test `the nlerp rotation differs from slerp by a recorded amount`
prints it); the error shrinks fast with the angle between neighboring keys, which the minter's reduction keeps small
and bounds by measuring (see assets, animation clips).

## Skinning

`ModelFromLocal(skeleton, pose, &model)` walks the parent-first joint table once. `SkinningMatrices(skeleton, model,
out)` multiplies each model matrix by its inverse bind matrix into the 48-byte, row-major 3x4 layout the GPU palette
takes (the same layout as the skeleton's inverse bind section: nothing is converted when the palette is uploaded).

## The system

`AnimationPlayer` (clip id, time, speed, weight, playing, looping) and `SkeletonInstance` (skeleton id and the palette the
system fills) are ECS components; `RootMotion` is filled when present; `RegisterAnimationComponents` registers them with
the storage registry so levels save the ids and the play state (ids 40 to 42). `AnimationSystem::Update(&world,
scheduler, dt)` advances and samples every entity with a player and an instance in parallel over the chunks, then
publishes the events into the world's `AnimationEvents` resource (when it exists) in entity order. Each entity's result
depends only on its own components, so the palettes are the same at one worker or eight.

`JointRotations` (id 44, derived, not saved) holds rotations a host adds to joints: after the clips are sampled and
blended, `ComposeJointRotation(&pose, joint, q)` makes each joint's rotation `old * q`, about the joint's own axes. An
entity with the component and no clip (no player, or a player with clip 0) gets the bind pose with the rotations.

The system resolves ids through an `AnimationResolver` (the resource manager's animation adapter, or a test). An entity whose
skeleton or clip is not loaded, or whose clip was made for another skeleton (`ClipView::Bind` refuses the id, the hash
or the joint count), is skipped and counted in `AnimationStats::refused`, with one warning line per update; nothing is
applied to the wrong rig.

```cpp compile run standalone
#include "animation/pose.h"
#include "animation/sampler.h"
#include "asset/clip.h"
#include "asset/skeleton.h"
#include "log/output.h"

using namespace vse;

int main() {
    asset::TypeRegistry types;
    asset::SkeletonJoint joint;
    MemSet(&joint, 0, sizeof(joint));
    joint.name[0] = 'a';
    joint.parent = asset::kNoParent;
    joint.rotation = {{0, 0, 0, 1}};
    joint.scale = {{1, 1, 1}};
    asset::ComputeInverseBindMatrices(Span<asset::SkeletonJoint>(&joint, 1));
    Vector<byte> skeleton_blob;
    if (asset::BuildSkeletonBlob(types, asset::ResourceId::FromValue(5), Span<const asset::SkeletonJoint>(&joint, 1),
                                 true, &skeleton_blob).Failed()) return 1;
    asset::SkeletonView skeleton;
    if (skeleton.Open(ByteSpan(skeleton_blob.Data(), skeleton_blob.Size()), types).Failed()) return 1;

    asset::ClipTrackSource track;
    track.joint = 0;
    asset::ClipKey3 k0;
    k0.time = 0;
    asset::ClipKey3 k1;
    k1.time = 1;
    k1.value = {{2, 0, 0}};
    track.translation.Push(k0);
    track.translation.Push(k1);
    asset::ClipBuildInput input;
    input.duration = 1;
    input.skeleton = skeleton.Id();
    input.skeleton_hash = skeleton.Hash();
    input.joint_count = skeleton.JointCount();
    input.tracks = Span<const asset::ClipTrackSource>(&track, 1);
    Vector<byte> clip_blob;
    if (asset::BuildClipBlob(types, asset::ResourceId::FromValue(6), input, &clip_blob).Failed()) return 1;
    asset::ClipView clip;
    if (clip.Open(ByteSpan(clip_blob.Data(), clip_blob.Size()), types).Failed()) return 1;

    animation::SamplerState state;
    animation::LocalPose pose;
    animation::SampleClip(clip, skeleton, 0.5f, false, &state, &pose);
    animation::ModelPose model;
    animation::ModelFromLocal(skeleton, pose, &model);
    animation::Mat3x4 palette[1];
    animation::SkinningMatrices(skeleton, model, Span<animation::Mat3x4>(palette, 1));
    out::Print("palette x translation: %.3f\n", palette[0][3]);
    return palette[0][3] > 0.99f && palette[0][3] < 1.01f ? 0 : 1;
}
```

## Drawing a skinned entity

The renderer takes the palette this module fills: `scene::RenderSync` (module `scene`, which therefore reads `animation::SkeletonInstance`) points the
`renderer::RenderObject` of an entity that has a `MeshRenderer` with a skinned mesh at the entity's `palette` and copies it into the frame's joint palette
when the frame is prepared. Run the `AnimationSystem` before the render stage so the palette holds this frame's pose. See "Skinned meshes" in
[Renderer](renderer.md).
