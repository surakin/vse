#include "animation/components.h"

#include "containers/array.h"
#include "ecs/query.h"
#include "log/log.h"
#include "sync/atomic.h"
#include "storage/field.h"

namespace vse::animation
{
namespace
{

using ecs::ChunkView;
using ecs::Optional;
using ecs::OptionalWrite;
using ecs::Read;
using ecs::Write;

constexpr u32 kEventsPerEntity = 64;

struct SysCounts
{
    Atomic<u64> updated{0};
    Atomic<u64> refused{0};
};

// The clip's weight track at `time` into the entity's weights (glTF coverage Day 9); the count becomes the track's.
void SampleMorphWeights(const asset::ClipView& clip, f32 time, MorphWeights* weights)
{
    const u32 n = clip.WeightTargetCount() < kMaxMorphWeights ? clip.WeightTargetCount() : kMaxMorphWeights;
    clip.SampleWeights(time, weights->weights.Data(), n);
    weights->count = n > weights->count ? n : weights->count;
}

// Advances a player over `dt` and records the step for the events pass; returns where the clip is now.
TimeAdvance StepPlayer(AnimationPlayer* p, const asset::ClipView& clip, f32 dt)
{
    const f32 from = p->time;
    TimeAdvance advance;
    advance.time = from;
    if (p->playing && !p->finished)
    {
        advance = AdvanceTime(clip, from, dt * p->speed, p->looping);
        p->finished = advance.finished;
    }
    p->step_from = from;
    p->step_wraps = advance.wraps;
    p->step_time = advance.time;
    p->step_include = !p->started;
    p->step_valid = true;
    p->started = true;
    p->time = advance.time;
    return advance;
}

// A clip of morph weights only (glTF coverage Day 9): no skeleton, a weight track.
bool WeightsOnly(const asset::ClipView& clip)
{
    return clip.Skeleton().IsNone() && clip.WeightTargetCount() != 0;
}

// The clip belongs to the skeleton when its recorded id, hash and joint count are the skeleton's.
bool SysBound(const asset::ClipView& clip, const asset::SkeletonView& skeleton)
{
    return clip.Bind(skeleton).Ok();
}

// Adds the host's rotations to a pose (a joint beyond the pose is skipped: the host may have set it for another
// skeleton).
void ApplyJointRotations(const JointRotations& rotations, LocalPose* pose)
{
    for (usize k = 0; k < rotations.list.Size(); ++k)
    {
        if (rotations.list[k].joint < pose->joint_count)
        {
            ComposeJointRotation(pose, rotations.list[k].joint, rotations.list[k].rotation);
        }
    }
}

// Writes the palette of the bind pose with the host's rotations.
void WriteBindPalette(const asset::SkeletonView& skeleton, const JointRotations& rotations, SkeletonInstance* inst)
{
    LocalPose pose;
    SetBindPose(skeleton, &pose);
    ApplyJointRotations(rotations, &pose);
    ModelPose model;
    ModelFromLocal(skeleton, pose, &model);
    inst->palette.Resize(pose.joint_count);
    SkinningMatrices(skeleton, model, Span<Mat3x4>(inst->palette.Data(), inst->palette.Size()));
}

}  // namespace

storage::Status RegisterAnimationComponents(storage::ComponentRegistry* registry)
{
    ENGINE_TRY(registry->Register<AnimationPlayer>(
        {.id = kAnimationPlayerId,
         .name = "animation_player",
         .fields = {storage::Field<&AnimationPlayer::clip>(
                        "clip", storage::FieldOptions::Reference(storage::ResourceTag('A', 'N', 'I', 'M'), true)),
                    storage::Field<&AnimationPlayer::time>("time"), storage::Field<&AnimationPlayer::speed>("speed"),
                    storage::Field<&AnimationPlayer::weight>("weight"),
                    storage::Field<&AnimationPlayer::playing>("playing"),
                    storage::Field<&AnimationPlayer::looping>("looping")}}));
    ENGINE_TRY(registry->Register<SkeletonInstance>(
        {.id = kSkeletonInstanceId,
         .name = "skeleton_instance",
         .fields = {storage::Field<&SkeletonInstance::skeleton>(
             "skeleton", storage::FieldOptions::Reference(storage::ResourceTag('S', 'K', 'E', 'L'), false))}}));
    ENGINE_TRY(registry->Register<RootMotion>(
        {.id = kRootMotionId, .name = "root_motion", .flags = storage::kComponentDerived}));
    ENGINE_TRY(registry->Register<JointRotations>(
        {.id = kJointRotationsId, .name = "joint_rotations", .flags = storage::kComponentDerived}));
    ENGINE_TRY(registry->Register<MorphWeights>({.id = kMorphWeightsId,
                                                 .name = "morph_weights",
                                                 .fields = {storage::Field<&MorphWeights::count>("count"),
                                                            storage::Field<&MorphWeights::weights>("weights")}}));
    return storage::Status::Success();
}

AnimationStats AnimationSystem::Update(ecs::World* world, jobs::Scheduler* scheduler, f32 dt)
{
    AnimationStats stats;
    SysCounts counts;
    const AnimationResolver* resolver = resolver_;
    auto query = world->Query<Write<AnimationPlayer>, Write<SkeletonInstance>, OptionalWrite<RootMotion>,
                              OptionalWrite<MorphWeights>, Optional<JointRotations>>();
    query.ForEachChunkParallel(
        scheduler,
        [&](ChunkView& view)
        {
            Span<AnimationPlayer> players = view.Write<AnimationPlayer>();
            Span<SkeletonInstance> instances = view.Write<SkeletonInstance>();
            Span<RootMotion> roots = view.Write<RootMotion>();
            Span<MorphWeights> morphs = view.Write<MorphWeights>();
            Span<const JointRotations> rotations = view.Read<JointRotations>();
            for (usize i = 0; i < players.Size(); ++i)
            {
                AnimationPlayer& p = players[i];
                SkeletonInstance& inst = instances[i];
                p.step_valid = false;
                const asset::SkeletonView* skeleton = resolver->FindSkeleton(
                    asset::ResourceId::FromValue(inst.skeleton));
                const asset::ClipView* clip = resolver->FindClip(asset::ResourceId::FromValue(p.clip));
                if (skeleton != nullptr && clip != nullptr && WeightsOnly(*clip) && !morphs.Empty())
                {
                    // A skinned mesh that plays a clip of its morph weights only: the weights move, the skeleton
                    // stays in its bind pose (2026-11-20: such an entity was skipped).
                    const TimeAdvance advance = StepPlayer(&p, *clip, dt);
                    LocalPose bind;
                    SetBindPose(*skeleton, &bind);
                    if (!rotations.Empty())
                    {
                        ApplyJointRotations(rotations[i], &bind);
                    }
                    ModelPose model;
                    ModelFromLocal(*skeleton, bind, &model);
                    inst.palette.Resize(bind.joint_count);
                    SkinningMatrices(*skeleton, model, Span<Mat3x4>(inst.palette.Data(), inst.palette.Size()));
                    SampleMorphWeights(*clip, advance.time, &morphs[i]);
                    counts.updated.FetchAdd(1, MemoryOrder::Relaxed);
                    continue;
                }
                if (skeleton != nullptr && (p.clip == 0 || clip == nullptr) && !rotations.Empty() &&
                    skeleton->JointCount() != 0 && skeleton->JointCount() <= kMaxPoseJoints)
                {  // no clip, or not loaded yet: the bind pose with the host's rotations
                    WriteBindPalette(*skeleton, rotations[i], &inst);
                    counts.updated.FetchAdd(1, MemoryOrder::Relaxed);
                    continue;
                }
                if (skeleton == nullptr || clip == nullptr || !SysBound(*clip, *skeleton) ||
                    (clip->Flags() & asset::kClipAdditive) != 0)
                {
                    counts.refused.FetchAdd(1, MemoryOrder::Relaxed);
                    continue;
                }
                const f32 from = p.time;
                const TimeAdvance advance = StepPlayer(&p, *clip, dt);

                LocalPose pose;
                SampleClip(*clip, *skeleton, advance.time, p.looping, &p.sampler, &pose);
                if (p.weight < 1.0f)
                {
                    LocalPose bind;
                    SetBindPose(*skeleton, &bind);
                    Blend(&bind, pose, p.weight < 0.0f ? 0.0f : (p.weight > 1.0f ? 1.0f : p.weight));
                    pose = bind;
                }
                if (p.fade_clip != 0)
                {  // a cross-fade: the clip faded out is sampled too and the two poses are blended
                    const asset::ClipView* fading = resolver->FindClip(asset::ResourceId::FromValue(p.fade_clip));
                    if (fading == nullptr || !SysBound(*fading, *skeleton) ||
                        (fading->Flags() & asset::kClipAdditive) != 0 || !(p.fade_total > 0.0f))
                    {
                        p.fade_clip = 0;  // gone or not for this skeleton: the fade ends
                    }
                    else
                    {
                        const f32 step = p.playing ? dt * p.speed : 0.0f;
                        p.fade_elapsed += dt;
                        p.fade_time = AdvanceTime(*fading, p.fade_time, step, true).time;
                        LocalPose old_pose;
                        SampleClip(*fading, *skeleton, p.fade_time, true, &p.fade_sampler, &old_pose);
                        const f32 t = p.fade_elapsed >= p.fade_total ? 1.0f : p.fade_elapsed / p.fade_total;
                        Blend(&old_pose, pose, t);  // old * (1 - t) + new * t
                        pose = old_pose;
                        if (p.fade_elapsed >= p.fade_total)
                        {
                            p.fade_clip = 0;
                        }
                    }
                }
                if (!rotations.Empty())
                {
                    ApplyJointRotations(rotations[i], &pose);
                }
                ModelPose model;
                ModelFromLocal(*skeleton, pose, &model);
                inst.palette.Resize(pose.joint_count);
                SkinningMatrices(*skeleton, model, Span<Mat3x4>(inst.palette.Data(), inst.palette.Size()));
                if (!morphs.Empty() && clip->WeightTargetCount() != 0)
                {
                    SampleMorphWeights(*clip, advance.time, &morphs[i]);  // glTF coverage Day 9
                }
                if (!roots.Empty() && (clip->Flags() & asset::kClipRootMotion) != 0)
                {
                    roots[i].delta = RootMotionOver(*clip, *skeleton, from, p.playing ? dt * p.speed : 0.0f, p.looping);
                }
                counts.updated.FetchAdd(1, MemoryOrder::Relaxed);
            }
        });
    // A skeleton that plays nothing but has host rotations: the bind pose with them.
    auto bare = world->Query<Write<SkeletonInstance>, Read<JointRotations>, ecs::Without<AnimationPlayer>>();
    bare.ForEachChunkParallel(scheduler,
                              [&](ChunkView& view)
                              {
                                  Span<SkeletonInstance> instances = view.Write<SkeletonInstance>();
                                  Span<const JointRotations> rotations = view.Read<JointRotations>();
                                  for (usize i = 0; i < instances.Size(); ++i)
                                  {
                                      const asset::SkeletonView* skeleton = resolver->FindSkeleton(
                                          asset::ResourceId::FromValue(instances[i].skeleton));
                                      if (skeleton != nullptr && skeleton->JointCount() != 0 &&
                                          skeleton->JointCount() <= kMaxPoseJoints)
                                      {
                                          WriteBindPalette(*skeleton, rotations[i], &instances[i]);
                                      }
                                  }
                              });
    // Clips of morph weights only (glTF coverage Day 9): an entity with no skeleton plays its clip into its weights.
    auto weights_only = world->Query<Write<AnimationPlayer>, Write<MorphWeights>, ecs::Without<SkeletonInstance>>();
    weights_only.ForEachChunkParallel(scheduler,
                                      [&](ChunkView& view)
                                      {
                                          Span<AnimationPlayer> players = view.Write<AnimationPlayer>();
                                          Span<MorphWeights> morphs = view.Write<MorphWeights>();
                                          for (usize i = 0; i < players.Size(); ++i)
                                          {
                                              AnimationPlayer& p = players[i];
                                              p.step_valid = false;
                                              const asset::ClipView* clip = resolver->FindClip(
                                                  asset::ResourceId::FromValue(p.clip));
                                              if (clip == nullptr || clip->WeightTargetCount() == 0)
                                              {
                                                  counts.refused.FetchAdd(1, MemoryOrder::Relaxed);
                                                  continue;
                                              }
                                              const TimeAdvance advance = StepPlayer(&p, *clip, dt);
                                              SampleMorphWeights(*clip, advance.time, &morphs[i]);
                                              counts.updated.FetchAdd(1, MemoryOrder::Relaxed);
                                          }
                                      });
    stats.players_updated = counts.updated.Load(MemoryOrder::Relaxed);
    stats.refused = counts.refused.Load(MemoryOrder::Relaxed);
    if (stats.refused != 0)
    {
        Log(LogCore, LogVerbosity::Warn,
            "animation: entities skipped: a skeleton or clip is not loaded, or the clip was made for another skeleton");
    }

    // The events, in entity order, from the steps the parallel pass recorded.
    AnimationEvents* sink = world->TryResource<AnimationEvents>();
    if (sink != nullptr)
    {
        sink->events.Clear();
        auto events = world->Query<Read<AnimationPlayer>, ecs::With<SkeletonInstance>>();
        events.ForEach(
            [&](ecs::Entity entity, const AnimationPlayer& p)
            {
                if (!p.step_valid)
                {
                    return;
                }
                const asset::ClipView* clip = resolver->FindClip(asset::ResourceId::FromValue(p.clip));
                if (clip == nullptr || clip->EventCount() == 0)
                {
                    return;
                }
                Array<asset::ClipEvent, kEventsPerEntity> found;
                TimeAdvance advance;
                advance.time = p.step_time;
                advance.wraps = p.step_wraps;
                bool overflow = false;
                const u32 n = CollectEvents(*clip, p.step_from, advance, p.step_include,
                                            Span<asset::ClipEvent>(found.Data(), found.Size()), &overflow);
                for (u32 k = 0; k < n; ++k)
                {
                    AnimationEvent e;
                    e.entity = entity;
                    e.time = found[k].time;
                    e.name_hash = found[k].name_hash;
                    e.payload = found[k].payload;
                    sink->events.Push(e);
                }
                stats.events += n;
                stats.events_dropped += overflow ? 1 : 0;
            });
    }
    return stats;
}

}  // namespace vse::animation
