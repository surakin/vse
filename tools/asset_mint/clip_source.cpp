#include "clip_source.h"

#include "source_text.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "lang/scalar.h"

namespace engine_placeholder::asset
{
namespace
{

void CsSetError(ClipSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status CsFail(ClipSourceError* error, u32 line, const char* message)
{
    CsSetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the clip source is invalid");
}

bool CsNumbers(const srctext::LineTokens& t, usize first, usize count, f32* out)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!srctext::ParseFloat(t.tokens[first + i], &out[i]))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

Status ParseClipSource(StringView text, const SkeletonView& skeleton, ClipSource* out, ClipSourceError* error)
{
    *out = ClipSource();
    *error = ClipSourceError();
    bool have_id = false;
    bool have_skeleton = false;
    bool have_duration = false;
    bool have_flags = false;
    bool have_rate = false;
    bool have_bounds = false;
    bool in_track = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const srctext::LineTokens t = srctext::Tokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return CsFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id") || kind == StringView("skeleton"))
        {
            const bool is_id = kind == StringView("id");
            bool& seen = is_id ? have_id : have_skeleton;
            if (seen)
            {
                return CsFail(error, line_number, "the line is given twice");
            }
            ResourceId& target = is_id ? out->id : out->skeleton;
            if (t.count != 2 || ParseResourceId(t.tokens[1], &target).Failed())
            {
                return CsFail(error, line_number, "id and skeleton need one 16-digit hex resource id");
            }
            seen = true;
        }
        else if (kind == StringView("duration") || kind == StringView("rate"))
        {
            const bool is_duration = kind == StringView("duration");
            bool& seen = is_duration ? have_duration : have_rate;
            f32 v = 0;
            if (seen)
            {
                return CsFail(error, line_number, "the line is given twice");
            }
            if (t.count != 2 || !srctext::ParseFloat(t.tokens[1], &v) || !(v > 0.0f))
            {
                return CsFail(error, line_number, "duration and rate need one number above 0");
            }
            (is_duration ? out->duration : out->sample_rate) = v;
            seen = true;
        }
        else if (kind == StringView("flags"))
        {
            if (have_flags)
            {
                return CsFail(error, line_number, "flags is given twice");
            }
            for (usize i = 1; i < t.count; ++i)
            {
                if (t.tokens[i] == StringView("loop"))
                {
                    out->flags |= kClipLooping;
                }
                else if (t.tokens[i] == StringView("additive"))
                {
                    out->flags |= kClipAdditive;
                }
                else if (t.tokens[i] == StringView("rootmotion"))
                {
                    out->flags |= kClipRootMotion;
                }
                else
                {
                    return CsFail(error, line_number, "unknown flag (expected loop, additive or rootmotion)");
                }
            }
            have_flags = true;
        }
        else if (kind == StringView("bounds"))
        {
            f32 v[6];
            if (have_bounds)
            {
                return CsFail(error, line_number, "bounds is given twice");
            }
            if (t.count != 7 || !CsNumbers(t, 1, 6, v))
            {
                return CsFail(error, line_number, "bounds needs 6 numbers");
            }
            for (u32 k = 0; k < 3; ++k)
            {
                out->bounds_min[k] = v[k];
                out->bounds_max[k] = v[3 + k];
            }
            have_bounds = true;
        }
        else if (kind == StringView("track"))
        {
            if (!have_id || !have_skeleton || !have_duration)
            {
                return CsFail(error, line_number, "id, skeleton and duration must come before the first track");
            }
            if (t.count != 2)
            {
                return CsFail(error, line_number, "track needs one joint name");
            }
            u32 joint = 0;
            if (!skeleton.FindJoint(t.tokens[1], &joint))
            {
                return CsFail(error, line_number, "track names a joint the skeleton does not have");
            }
            for (usize i = 0; i < out->tracks.Size(); ++i)
            {
                if (out->tracks[i].joint == joint)
                {
                    return CsFail(error, line_number, "two tracks animate the same joint");
                }
            }
            if (out->tracks.Size() >= kMaxClipTracks)
            {
                CsSetError(error, line_number, "more than 256 tracks");
                return Status::Make(ErrorCode::LimitExceeded, "a clip holds at most 256 tracks in this version");
            }
            ClipTrackSource track;
            track.joint = joint;
            out->tracks.Push(Move(track));
            in_track = true;
        }
        else if (kind == StringView("t") || kind == StringView("s") || kind == StringView("r"))
        {
            if (!in_track)
            {
                return CsFail(error, line_number, "a key needs a track line before it");
            }
            ClipTrackSource& track = out->tracks[out->tracks.Size() - 1];
            const bool rotation = kind == StringView("r");
            f32 v[5];
            if (t.count != (rotation ? 6u : 5u) || !CsNumbers(t, 1, rotation ? 5 : 4, v))
            {
                return CsFail(error, line_number,
                              rotation ? "r needs TIME and 4 numbers" : "t and s need TIME and 3 numbers");
            }
            if (v[0] < 0.0f || v[0] > out->duration)
            {
                return CsFail(error, line_number, "a key time is outside [0, duration]");
            }
            if (rotation)
            {
                const f64 l2 = static_cast<f64>(v[1]) * v[1] + static_cast<f64>(v[2]) * v[2] +
                               static_cast<f64>(v[3]) * v[3] + static_cast<f64>(v[4]) * v[4];
                if (Abs(Sqrt(l2) - 1.0) > 1e-2)
                {
                    return CsFail(error, line_number, "the rotation is not a unit quaternion");
                }
                if (track.rotation.Size() != 0 && v[0] <= track.rotation[track.rotation.Size() - 1].time)
                {
                    return CsFail(error, line_number, "key times must increase");
                }
                if (track.rotation.Size() >= kMaxClipKeys)
                {
                    CsSetError(error, line_number, "more than 65535 keys in a channel");
                    return Status::Make(ErrorCode::LimitExceeded, "a channel holds at most 65535 keys");
                }
                ClipKey4 key;
                key.time = v[0];
                for (u32 k = 0; k < 4; ++k)
                {
                    key.value[k] = v[1 + k];
                }
                track.rotation.Push(key);
            }
            else
            {
                Vector<ClipKey3>& keys = kind == StringView("t") ? track.translation : track.scale;
                if (keys.Size() != 0 && v[0] <= keys[keys.Size() - 1].time)
                {
                    return CsFail(error, line_number, "key times must increase");
                }
                if (keys.Size() >= kMaxClipKeys)
                {
                    CsSetError(error, line_number, "more than 65535 keys in a channel");
                    return Status::Make(ErrorCode::LimitExceeded, "a channel holds at most 65535 keys");
                }
                ClipKey3 key;
                key.time = v[0];
                for (u32 k = 0; k < 3; ++k)
                {
                    key.value[k] = v[1 + k];
                }
                keys.Push(key);
            }
        }
        else if (kind == StringView("event"))
        {
            if (!have_duration)
            {
                return CsFail(error, line_number, "duration must come before the first event");
            }
            f32 time = 0;
            u32 payload = 0;
            if ((t.count != 3 && t.count != 4) || !srctext::ParseFloat(t.tokens[1], &time) ||
                (t.count == 4 && !srctext::ParseUnsigned(t.tokens[3], &payload)))
            {
                return CsFail(error, line_number, "event needs TIME NAME and an optional unsigned PAYLOAD");
            }
            if (time < 0.0f || time > out->duration ||
                (out->events.Size() != 0 && time < out->events[out->events.Size() - 1].time))
            {
                return CsFail(error, line_number, "event times must be within [0, duration] and not decrease");
            }
            if (out->events.Size() >= kMaxClipEvents)
            {
                CsSetError(error, line_number, "more than 1024 events");
                return Status::Make(ErrorCode::LimitExceeded, "a clip holds at most 1024 events in this version");
            }
            ClipEvent e;
            e.time = time;
            e.name_hash = JointNameHash(t.tokens[2]);
            e.payload = payload;
            out->events.Push(e);
        }
        else
        {
            return CsFail(error, line_number, "unknown record");
        }
    }
    if (!have_id || !have_skeleton || !have_duration)
    {
        return CsFail(error, 0, "the source needs id, skeleton and duration lines");
    }
    if (out->tracks.Size() == 0 && out->events.Size() == 0)
    {
        return CsFail(error, 0, "the source has no tracks and no events");
    }
    return Status::Success();
}

Status MintClip(StringView text, const SkeletonView& skeleton, const TypeRegistry& types, const ClipLimits& limits,
                Vector<byte>* out, ClipSourceError* error, u32 target)
{
    ClipSource source;
    ENGINE_TRY(ParseClipSource(text, skeleton, &source, error));
    if (source.skeleton != skeleton.Id())
    {
        return CsFail(error, 0, "the source names another skeleton than the one given");
    }
    ClipBuildInput input;
    input.duration = source.duration;
    input.sample_rate = source.sample_rate;
    input.flags = source.flags;
    input.skeleton = source.skeleton;
    input.skeleton_hash = skeleton.Hash();
    input.joint_count = skeleton.JointCount();
    input.bounds_min = source.bounds_min;
    input.bounds_max = source.bounds_max;
    input.tracks = Span<const ClipTrackSource>(source.tracks.Data(), source.tracks.Size());
    input.events = Span<const ClipEvent>(source.events.Data(), source.events.Size());
    input.limits = limits;
    out->Clear();
    const Status built = BuildClipBlob(types, source.id, input, out, target);
    if (built.Failed())
    {
        CsSetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace engine_placeholder::asset
