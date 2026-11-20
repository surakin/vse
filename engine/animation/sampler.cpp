#include "animation/sampler.h"

#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::animation
{
namespace
{

constexpr u32 kMaxWraps = 1u << 20;

void SamplerApply(const asset::ClipSample& s, u32 joint, LocalPose* out)
{
    if (s.has_translation)
    {
        out->translation[joint] = s.translation;
    }
    if (s.has_rotation)
    {
        out->rotation[joint] = s.rotation;
    }
    if (s.has_scale)
    {
        out->scale[joint] = s.scale;
    }
}

f32 SamplerWrap(const asset::ClipView& clip, f32 time, bool looping)
{
    if (!(time >= 0.0f))
    {
        return 0.0f;
    }
    const f32 duration = clip.Duration();
    if (looping && time >= duration)
    {
        const f64 wraps = Floor(static_cast<f64>(time) / duration);
        const f64 t = static_cast<f64>(time) - wraps * duration;
        return static_cast<f32>(t < 0.0 ? 0.0 : t);
    }
    return time > duration ? duration : time;
}

// The root joint (0) at `time`: from its track, or the bind pose / identity.
void SamplerRoot(const asset::ClipView& clip, const asset::SkeletonView& skeleton, f32 time, Array<f32, 3>* t,
                 Array<f32, 4>* q)
{
    const bool additive = (clip.Flags() & asset::kClipAdditive) != 0;
    *t = additive ? Array<f32, 3>{{0.0f, 0.0f, 0.0f}} : skeleton.Translation(0);
    *q = additive ? Array<f32, 4>{{0.0f, 0.0f, 0.0f, 1.0f}} : skeleton.Rotation(0);
    if (clip.TrackCount() != 0 && clip.TrackJoint(0) == 0)
    {
        const asset::ClipSample s = clip.Sample(0, time);
        if (s.has_translation)
        {
            *t = s.translation;
        }
        if (s.has_rotation)
        {
            *q = s.rotation;
        }
    }
}

Array<f32, 4> SamplerMultiply(const Array<f32, 4>& a, const Array<f32, 4>& b)
{
    Array<f32, 4> r;
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    return r;
}

Array<f32, 4> SamplerConjugate(const Array<f32, 4>& q)
{
    return {{-q[0], -q[1], -q[2], q[3]}};
}

}  // namespace

TimeAdvance AdvanceTime(const asset::ClipView& clip, f32 time, f32 delta, bool looping)
{
    TimeAdvance out;
    const f32 duration = clip.Duration();
    f64 t = static_cast<f64>(time >= 0.0f ? time : 0.0f) + static_cast<f64>(delta > 0.0f ? delta : 0.0f);
    const f64 limit = static_cast<f64>(duration) * kMaxWraps;  // an infinite or huge step stays finite
    t = t > limit ? limit : t;
    if (looping)
    {
        if (t >= duration)
        {
            const f64 wraps = Floor(t / duration);
            out.wraps = wraps > kMaxWraps ? kMaxWraps : static_cast<u32>(wraps);
            const f64 rest = t - wraps * duration;
            out.time = static_cast<f32>(rest < 0.0 ? 0.0 : (rest > duration ? duration : rest));
        }
        else
        {
            out.time = static_cast<f32>(t);
        }
    }
    else
    {
        out.finished = t >= duration;
        out.time = out.finished ? duration : static_cast<f32>(t);
    }
    return out;
}

void SampleClip(const asset::ClipView& clip, const asset::SkeletonView& skeleton, f32 time, bool looping,
                SamplerState* state, LocalPose* out)
{
    ENGINE_ASSERT(clip.JointCount() == skeleton.JointCount(), "SampleClip: the clip is for another skeleton");
    if ((clip.Flags() & asset::kClipAdditive) != 0)
    {
        SetIdentityPose(skeleton.JointCount(), out);
    }
    else
    {
        SetBindPose(skeleton, out);
    }
    const f32 t = SamplerWrap(clip, time, looping);
    if (state != nullptr && state->cursor.Size() != static_cast<usize>(clip.TrackCount()) * 3)
    {
        state->cursor.Resize(static_cast<usize>(clip.TrackCount()) * 3);
        for (usize k = 0; k < state->cursor.Size(); ++k)
        {
            state->cursor[k] = 0;
        }
    }
    for (u32 i = 0; i < clip.TrackCount(); ++i)
    {
        SamplerApply(clip.Sample(i, t, state != nullptr ? state->cursor.Data() + 3 * static_cast<usize>(i) : nullptr),
                     clip.TrackJoint(i), out);
    }
    if ((clip.Flags() & asset::kClipRootMotion) != 0 && clip.TrackCount() != 0 && clip.TrackJoint(0) == 0)
    {
        SamplerApply(clip.Sample(0, 0.0f), 0, out);
    }
}

RootMotionDelta RootMotionOver(const asset::ClipView& clip, const asset::SkeletonView& skeleton, f32 from, f32 delta,
                               bool looping)
{
    ENGINE_ASSERT(clip.JointCount() == skeleton.JointCount(), "RootMotionOver: the clip is for another skeleton");
    const TimeAdvance a = AdvanceTime(clip, from, delta, looping);
    const f32 duration = clip.Duration();
    RootMotionDelta out;
    Array<f32, 3> t0, t1;
    Array<f32, 4> q0, q1;
    if (a.wraps == 0)
    {
        SamplerRoot(clip, skeleton, from, &t0, &q0);
        SamplerRoot(clip, skeleton, a.time, &t1, &q1);
        for (u32 c = 0; c < 3; ++c)
        {
            out.translation[c] = t1[c] - t0[c];
        }
        out.rotation = SamplerMultiply(q1, SamplerConjugate(q0));
        return out;
    }
    // Over the end and from the start again: (end - from) then (to - start), whole passes in between.
    Array<f32, 3> ts, te;
    Array<f32, 4> qs, qe;
    SamplerRoot(clip, skeleton, from, &t0, &q0);
    SamplerRoot(clip, skeleton, duration, &te, &qe);
    SamplerRoot(clip, skeleton, 0.0f, &ts, &qs);
    SamplerRoot(clip, skeleton, a.time, &t1, &q1);
    const Array<f32, 4> first = SamplerMultiply(qe, SamplerConjugate(q0));
    const Array<f32, 4> pass = SamplerMultiply(qe, SamplerConjugate(qs));
    const Array<f32, 4> last = SamplerMultiply(q1, SamplerConjugate(qs));
    for (u32 c = 0; c < 3; ++c)
    {
        out.translation[c] = (te[c] - t0[c]) + static_cast<f32>(a.wraps - 1) * (te[c] - ts[c]) + (t1[c] - ts[c]);
    }
    Array<f32, 4> r = first;
    for (u32 k = 1; k < a.wraps && k < 64; ++k)
    {
        r = SamplerMultiply(pass, r);
    }
    out.rotation = SamplerMultiply(last, r);
    return out;
}

u32 CollectEvents(const asset::ClipView& clip, f32 from, const TimeAdvance& advance, bool include_start,
                  Span<asset::ClipEvent> out, bool* overflow)
{
    if (overflow != nullptr)
    {
        *overflow = false;
    }
    u32 written = 0;
    bool full = false;
    const f32 duration = clip.Duration();
    auto emit = [&](const asset::ClipEvent& e)
    {
        if (written < out.Size())
        {
            out[written++] = e;
        }
        else
        {
            full = true;
        }
    };
    // `lo_open`: the lower bound excludes events at `lo`.
    auto range = [&](f32 lo, bool lo_open, f32 hi)
    {
        for (u32 i = 0; i < clip.EventCount(); ++i)
        {
            const asset::ClipEvent e = clip.Event(i);
            if ((lo_open ? e.time > lo : e.time >= lo) && e.time <= hi)
            {
                emit(e);
            }
        }
    };
    if (advance.wraps == 0)
    {
        range(from, !include_start, advance.time);
    }
    else
    {
        range(from, !include_start, duration);
        for (u32 k = 1; k < advance.wraps && !full; ++k)
        {
            range(0.0f, false, duration);
        }
        range(0.0f, false, advance.time);
    }
    if (full && overflow != nullptr)
    {
        *overflow = true;
    }
    return written;
}

}  // namespace vse::animation
