#include "asset_import/gltf_clip.h"

#include "algorithm.h"
#include "asset/clip.h"
#include "asset_import/gltf_accessor.h"
#include "asset_import/gltf_names.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "lang/scalar.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u8 kPathTranslation = 0;
constexpr u8 kPathRotation = 1;
constexpr u8 kPathScale = 2;
constexpr f64 kPi = 3.14159265358979323846;

Status RefuseClip(GltfError* error, StringView where, StringView what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, what);
    return Status::Make(code, "a glTF animation is refused");
}

// One channel of an animation that animates a joint.
struct ChannelDesc
{
    JsonValue sampler_value;  // the `samplers` entry the channel names
    u32 sampler = 0;
    u32 node = 0;
    u8 path = kPathTranslation;
    u32 index = 0;  // its position in the animation's `channels`
};

// One clip to make: an animation and a skin whose joints it animates.
struct ClipDesc
{
    u32 animation = 0;
    u32 skin = 0;
    Vector<ChannelDesc> channels;
};

// The errors cubic resampling made.
struct ResampleError
{
    f64 translation = 0;
    f64 rotation_degrees = 0;
    u32 channels = 0;
};

// The keys of one channel.
struct ChannelKeys
{
    Vector<ClipKey3> keys3;
    Vector<ClipKey4> keys4;
    f64 last_time = 0;
    ResampleError resample;
};

Array<f64, 4> Normalized(const f64* q, bool* ok)
{
    const f64 length = Sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    *ok = length > 1e-9;
    if (!*ok)
    {
        return {{0, 0, 0, 1}};
    }
    return {{q[0] / length, q[1] / length, q[2] / length, q[3] / length}};
}

// The angle in degrees between two unit quaternions.
f64 RotationDegrees(const f64* a, const f64* b)
{
    f64 dot = Abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    dot = dot > 1.0 ? 1.0 : dot;
    return 2.0 * Acos(dot) * 180.0 / kPi;
}

// The value of a cubic-spline channel at `time`, in `components` floats: Hermite between key `k` and `k + 1`.
void Hermite(const Vector<f32>& output, const Vector<f32>& times, usize k, f64 time, usize components, f64* out)
{
    const f64 t0 = static_cast<f64>(times[k]);
    const f64 t1 = static_cast<f64>(times[k + 1]);
    const f64 dt = t1 - t0;
    const f64 s = dt > 0 ? (time - t0) / dt : 0.0;
    const f64 s2 = s * s;
    const f64 s3 = s2 * s;
    const usize stride = components * 3;  // in tangent, value, out tangent
    for (usize c = 0; c < components; ++c)
    {
        const f64 p0 = static_cast<f64>(output[k * stride + components + c]);
        const f64 m0 = static_cast<f64>(output[k * stride + 2 * components + c]);
        const f64 m1 = static_cast<f64>(output[(k + 1) * stride + c]);
        const f64 p1 = static_cast<f64>(output[(k + 1) * stride + components + c]);
        out[c] = (2 * s3 - 3 * s2 + 1) * p0 + dt * (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * p1 +
                 dt * (s3 - s2) * m1;
    }
}

// Reads one channel's sampler into keys: `LINEAR` as it is, `STEP` as pairs, `CUBICSPLINE` resampled.
Status ReadChannelKeys(const GltfDocument& document, const ImportOptions& options, const ChannelDesc& desc,
                       ChannelKeys* out, GltfError* error)
{
    const JsonValue sampler = desc.sampler_value;
    u32 input_index = 0;
    u32 output_index = 0;
    ENGINE_TRY(sampler.RequiredIndex("input", &input_index, document.accessors.Size(), error));
    ENGINE_TRY(sampler.RequiredIndex("output", &output_index, document.accessors.Size(), error));
    StringView mode("LINEAR");
    bool has_mode = false;
    ENGINE_TRY(sampler.OptionalString("interpolation", &mode, &has_mode, error));
    const bool step = mode == StringView("STEP");
    const bool cubic = mode == StringView("CUBICSPLINE");
    if (!step && !cubic && mode != StringView("LINEAR"))
    {
        return RefuseClip(error, sampler.Get("interpolation").Path(), StringView("is not LINEAR, STEP or CUBICSPLINE"));
    }
    const GltfAccessor& input = document.accessors[input_index];
    const GltfAccessor& output = document.accessors[output_index];
    if (input.type != GltfAccessorType::Scalar || input.component_type != 5126)
    {
        return RefuseClip(error, sampler.Get("input").Path(), StringView("is not a SCALAR accessor of floats"));
    }
    const usize components = desc.path == kPathRotation ? 4 : 3;
    const GltfAccessorType want = desc.path == kPathRotation ? GltfAccessorType::Vec4 : GltfAccessorType::Vec3;
    const bool float_output = output.component_type == 5126;
    const bool integer_output = desc.path == kPathRotation && output.normalized && output.component_type != 5125;
    if (output.type != want || (!float_output && !integer_output))
    {
        return RefuseClip(error, sampler.Get("output").Path(),
                          StringView("is not an accessor of the type the path needs"));
    }
    const u64 n = input.count;
    if (output.count != (cubic ? 3 * n : n))
    {
        error->SetNumber(sampler.Path(), StringView("has different numbers of key times and values: values "),
                         output.count);
        return Status::Make(ErrorCode::Invalid, "a glTF animation is refused");
    }
    if (cubic && n < 2)
    {
        return RefuseClip(error, sampler.Path(), StringView("is CUBICSPLINE with fewer than two keys"));
    }
    if (n > kMaxClipKeys && !cubic)
    {
        error->SetNumber(sampler.Path(), StringView("has more keys than a clip channel holds (65535): "), n);
        return Status::Make(ErrorCode::TooLarge, "too many keys");
    }
    if (step && 2 * n - 1 > kMaxClipKeys)
    {
        error->SetNumber(sampler.Path(),
                         StringView("has a STEP that makes more keys than a clip channel holds (65535): "), 2 * n - 1);
        return Status::Make(ErrorCode::TooLarge, "too many keys");
    }
    Vector<f32> times;
    Vector<f32> values;
    ENGINE_TRY(ReadAccessorFloats(document, input_index, &times, error));
    ENGINE_TRY(ReadAccessorFloats(document, output_index, &values, error));
    for (usize i = 0; i < times.Size(); ++i)
    {
        if (times[i] < 0.0f || (i > 0 && !(times[i] > times[i - 1])))
        {
            error->SetNumber(sampler.Get("input").Path(),
                             StringView("has key times that are negative or not strictly increasing, at key "), i);
            return Status::Make(ErrorCode::Invalid, "a glTF animation is refused");
        }
    }
    // The keys as time and `components` values.
    struct Raw
    {
        f64 time;
        f64 value[4];
    };
    Vector<Raw> raw;
    const f64 tick = 1.0 / static_cast<f64>(options.clip_rate);
    if (!cubic)
    {
        for (usize i = 0; i < times.Size(); ++i)
        {
            Raw key = {};
            key.time = static_cast<f64>(times[i]);
            for (usize c = 0; c < components; ++c)
            {
                key.value[c] = static_cast<f64>(values[i * components + c]);
            }
            if (step && i > 0)
            {
                // The previous value holds until this key's time, and the new one is there a tick later (half the gap
                // to the next key when that is shorter): the later key one tick after the step.
                Raw hold = key;
                for (usize c = 0; c < components; ++c)
                {
                    hold.value[c] = static_cast<f64>(values[(i - 1) * components + c]);
                }
                raw.Push(hold);
                const f64 gap = i + 1 < times.Size() ? static_cast<f64>(times[i + 1]) - key.time : tick * 2.0;
                key.time += tick < gap / 2 ? tick : gap / 2;
            }
            raw.Push(key);
        }
    }
    else
    {
        const f64 start = static_cast<f64>(times[0]);
        const f64 end = static_cast<f64>(times[times.Size() - 1]);
        const f64 rate = static_cast<f64>(options.clip_rate);
        const f64 samples = Floor((end - start) * rate + 1e-9) + 1.0;
        const bool extra = start + (samples - 1.0) / rate < end - 1e-9;
        const f64 total = samples + (extra ? 1.0 : 0.0);
        if (total > static_cast<f64>(kMaxClipKeys))
        {
            error->SetNumber(sampler.Path(), StringView("resamples to more keys than a clip channel holds (65535): "),
                             static_cast<u64>(total < 1e18 ? total : 1e18));
            return Status::Make(ErrorCode::TooLarge, "too many keys");
        }
        const usize count = static_cast<usize>(total);
        usize k = 0;
        Raw previous = {};
        for (usize i = 0; i < count; ++i)
        {
            f64 t = i + 1 == count && extra ? end : start + static_cast<f64>(i) / rate;
            t = t > end ? end : t;
            while (k + 2 < times.Size() && t > static_cast<f64>(times[k + 1]))
            {
                ++k;
            }
            Raw key = {};
            key.time = t;
            Hermite(values, times, k, t, components, key.value);
            if (components == 4)
            {
                bool ok = false;
                const Array<f64, 4> q = Normalized(key.value, &ok);
                if (!ok)
                {
                    return RefuseClip(error, sampler.Path(), StringView("has a rotation of length zero"));
                }
                for (usize c = 0; c < 4; ++c)
                {
                    key.value[c] = q[c];
                }
                // The shortest arc from the key before.
                if (i > 0 && previous.value[0] * key.value[0] + previous.value[1] * key.value[1] +
                                     previous.value[2] * key.value[2] + previous.value[3] * key.value[3] <
                                 0)
                {
                    for (usize c = 0; c < 4; ++c)
                    {
                        key.value[c] = -key.value[c];
                    }
                }
            }
            if (i > 0)
            {
                // The linear key between the two resampled ones against the spline itself.
                const f64 mid = (previous.time + t) * 0.5;
                usize m = k;
                while (m > 0 && mid < static_cast<f64>(times[m]))
                {
                    --m;
                }
                while (m + 2 < times.Size() && mid > static_cast<f64>(times[m + 1]))
                {
                    ++m;
                }
                f64 exact[4];
                Hermite(values, times, m, mid, components, exact);
                f64 approx[4];
                for (usize c = 0; c < components; ++c)
                {
                    approx[c] = (previous.value[c] + key.value[c]) * 0.5;
                }
                if (components == 4)
                {
                    bool ok = false;
                    const Array<f64, 4> e = Normalized(exact, &ok);
                    const Array<f64, 4> a = Normalized(approx, &ok);
                    if (ok)
                    {
                        const f64 degrees = RotationDegrees(e.Data(), a.Data());
                        out->resample.rotation_degrees = degrees > out->resample.rotation_degrees
                                                             ? degrees
                                                             : out->resample.rotation_degrees;
                    }
                }
                else
                {
                    const f64 dx = exact[0] - approx[0];
                    const f64 dy = exact[1] - approx[1];
                    const f64 dz = exact[2] - approx[2];
                    const f64 distance = Sqrt(dx * dx + dy * dy + dz * dz);
                    out->resample.translation = distance > out->resample.translation ? distance
                                                                                     : out->resample.translation;
                }
            }
            raw.Push(key);
            previous = key;
        }
        out->resample.channels += 1;
    }
    out->last_time = raw[raw.Size() - 1].time;  // a STEP's last key lies a tick after the last key time
    // Out as keys; quaternions are unit, in one hemisphere from key to key.
    for (usize i = 0; i < raw.Size(); ++i)
    {
        if (components == 3)
        {
            ClipKey3 key;
            key.time = static_cast<f32>(raw[i].time);
            key.value = {{static_cast<f32>(raw[i].value[0]), static_cast<f32>(raw[i].value[1]),
                          static_cast<f32>(raw[i].value[2])}};
            if (out->keys3.Size() != 0 && !(key.time > out->keys3[out->keys3.Size() - 1].time))
            {
                out->keys3[out->keys3.Size() - 1].value = key.value;  // two times one float: the later key wins
                continue;
            }
            out->keys3.Push(key);
        }
        else
        {
            bool ok = false;
            Array<f64, 4> q = Normalized(raw[i].value, &ok);
            if (!ok)
            {
                return RefuseClip(error, sampler.Get("output").Path(), StringView("has a rotation of length zero"));
            }
            if (out->keys4.Size() != 0)
            {
                const ClipKey4& before = out->keys4[out->keys4.Size() - 1];
                const f64 dot = q[0] * static_cast<f64>(before.value[0]) + q[1] * static_cast<f64>(before.value[1]) +
                                q[2] * static_cast<f64>(before.value[2]) + q[3] * static_cast<f64>(before.value[3]);
                if (dot < 0)
                {
                    q = {{-q[0], -q[1], -q[2], -q[3]}};
                }
            }
            ClipKey4 key;
            key.time = static_cast<f32>(raw[i].time);
            key.value = {
                {static_cast<f32>(q[0]), static_cast<f32>(q[1]), static_cast<f32>(q[2]), static_cast<f32>(q[3])}};
            if (out->keys4.Size() != 0 && !(key.time > out->keys4[out->keys4.Size() - 1].time))
            {
                out->keys4[out->keys4.Size() - 1].value = key.value;  // two times one float: the later key wins
                continue;
            }
            out->keys4.Push(key);
        }
    }
    return Status::Success();
}

// Rotates `v` by the unit quaternion `q` (x, y, z, w).
Array<f64, 3> Rotate(const Array<f64, 4>& q, const Array<f64, 3>& v)
{
    const f64 tx = 2.0 * (q[1] * v[2] - q[2] * v[1]);
    const f64 ty = 2.0 * (q[2] * v[0] - q[0] * v[2]);
    const f64 tz = 2.0 * (q[0] * v[1] - q[1] * v[0]);
    return {{v[0] + q[3] * tx + (q[1] * tz - q[2] * ty), v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
             v[2] + q[3] * tz + (q[0] * ty - q[1] * tx)}};
}

// The channel of a joint that has non-joint nodes folded into its bind pose: the keys are of the node's own transform,
// the joint's is `ancestor * node`. For an ancestor that is a similarity (rotation, translation and one scale) that is
// exact key by key: translation `T_a + R_a * (s * T)`, rotation `R_a * R`, scale `s * S`. Any other ancestor (a shear,
// a scale that is not the same on every axis, a reflection) cannot follow a rotation or a scale that moves, and the
// channel is dropped.
// Returns false when the channel must be dropped.
bool FollowAncestor(const GltfMatrix& ancestor, u8 path, ChannelKeys* keys)
{
    bool identity = true;
    for (usize i = 0; i < 16; ++i)
    {
        const f64 want = i % 5 == 0 ? 1.0 : 0.0;
        identity = identity && Abs(ancestor.m[i] - want) < 1e-12;
    }
    if (identity)
    {
        return true;
    }
    GltfTrs trs;
    if (!DecomposeMatrix(ancestor, &trs))
    {
        return false;
    }
    const f64 s = trs.scale[0];
    if (!(s > 0.0) || Abs(trs.scale[1] - s) > 1e-6 * s || Abs(trs.scale[2] - s) > 1e-6 * s)
    {
        return false;
    }
    const Array<f64, 4>& a = trs.rotation;
    if (path == kPathTranslation)
    {
        for (usize k = 0; k < keys->keys3.Size(); ++k)
        {
            ClipKey3& key = keys->keys3[k];
            const Array<f64, 3> v = Rotate(a, {{s * static_cast<f64>(key.value[0]), s * static_cast<f64>(key.value[1]),
                                                s * static_cast<f64>(key.value[2])}});
            key.value = {{static_cast<f32>(trs.translation[0] + v[0]), static_cast<f32>(trs.translation[1] + v[1]),
                          static_cast<f32>(trs.translation[2] + v[2])}};
        }
    }
    else if (path == kPathScale)
    {
        for (usize k = 0; k < keys->keys3.Size(); ++k)
        {
            for (usize c = 0; c < 3; ++c)
            {
                keys->keys3[k].value[c] = static_cast<f32>(s * static_cast<f64>(keys->keys3[k].value[c]));
            }
        }
    }
    else
    {
        for (usize k = 0; k < keys->keys4.Size(); ++k)
        {
            const Array<f32, 4>& q = keys->keys4[k].value;
            const f64 x = static_cast<f64>(q[0]);
            const f64 y = static_cast<f64>(q[1]);
            const f64 z = static_cast<f64>(q[2]);
            const f64 w = static_cast<f64>(q[3]);
            keys->keys4[k].value = {{static_cast<f32>(a[3] * x + a[0] * w + a[1] * z - a[2] * y),
                                     static_cast<f32>(a[3] * y - a[0] * z + a[1] * w + a[2] * x),
                                     static_cast<f32>(a[3] * z + a[0] * y - a[1] * x + a[2] * w),
                                     static_cast<f32>(a[3] * w - a[0] * x - a[1] * y - a[2] * z)}};
        }
    }
    return true;
}

f64 Worse(f64 a, f64 b)
{
    return a > b ? a : b;
}

bool NameMatches(Span<const StringView> names, StringView name)
{
    for (usize i = 0; i < names.Size(); ++i)
    {
        if (names[i] == name)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

Status ImportClips(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, Span<const ImportedSkin> skins, ImportResult* result, GltfError* error)
{
    const JsonValue animations = document.root.Get("animations");
    const usize animation_count = animations.Size();
    if (animation_count == 0)
    {
        return Status::Success();
    }
    if (!(options.clip_rate > 0.0f) || options.clip_rate > 1000.0f)
    {
        error->Set(StringView(), StringView("the clip rate must be above 0 and at most 1000"));
        return Status::Make(ErrorCode::Invalid, "a bad clip rate");
    }
    if (animation_count > 65535 || skins.Size() > 65535)
    {
        error->Set(StringView("animations"), StringView("or skins are too many to number the clips"));
        return Status::Make(ErrorCode::TooLarge, "too many animations");
    }
    // Pass 1: which clips there are: an animation and each skin it animates.
    Vector<ClipDesc> descs;
    Vector<StringView> animation_names;
    for (usize a = 0; a < animation_count; ++a)
    {
        const JsonValue animation = animations.At(a);
        StringView name;
        bool has_name = false;
        ENGINE_TRY(animation.OptionalString("name", &name, &has_name, error));
        animation_names.Push(has_name ? name : StringView());
        const JsonValue channels = animation.Get("channels");
        const JsonValue samplers = animation.Get("samplers");
        if (!channels.IsArray() || channels.Size() == 0 || !samplers.IsArray())
        {
            return RefuseClip(error, animation.Path(), StringView("has no `channels` or no `samplers`"));
        }
        Vector<u64> seen;       // node * 4 + path of every channel kept
        Vector<ClipDesc> mine;  // one per skin, in skin order
        for (usize c = 0; c < channels.Size(); ++c)
        {
            const JsonValue channel = channels.At(c);
            u32 sampler_index = 0;
            ENGINE_TRY(channel.RequiredIndex("sampler", &sampler_index, samplers.Size(), error));
            const JsonValue target = channel.Get("target");
            if (!target.IsObject())
            {
                return RefuseClip(error, channel.Path(), StringView("has no `target`"));
            }
            StringView path;
            ENGINE_TRY(target.RequiredString("path", &path, error));
            if (!target.Get("node").Exists())
            {
                AddImportWarning(&result->report, channel.Path(), StringView("channel without a target node dropped"));
                continue;
            }
            u32 node = 0;
            ENGINE_TRY(target.RequiredIndex("node", &node, graph.nodes.Size(), error));
            u8 kind = kPathTranslation;
            if (path == StringView("translation"))
            {
                kind = kPathTranslation;
            }
            else if (path == StringView("rotation"))
            {
                kind = kPathRotation;
            }
            else if (path == StringView("scale"))
            {
                kind = kPathScale;
            }
            else
            {
                char what[96];
                FormatTo(what, sizeof(what), "channel on node %u dropped: `%s` is not animated by clips",
                         static_cast<unsigned>(node), path == StringView("weights") ? "weights" : "this path");
                AddImportWarning(&result->report, channel.Path(), StringView(what, CStrLength(what)));
                continue;
            }
            bool used = false;
            for (usize s = 0; s < skins.Size(); ++s)
            {
                if (skins[s].JointOfNode(node) == kNoGltfIndex)
                {
                    continue;
                }
                used = true;
                usize at = 0;
                while (at < mine.Size() && mine[at].skin != s)
                {
                    ++at;
                }
                if (at == mine.Size())
                {
                    ClipDesc desc;
                    desc.animation = static_cast<u32>(a);
                    desc.skin = static_cast<u32>(s);
                    mine.Push(static_cast<ClipDesc&&>(desc));
                }
                ChannelDesc channel_desc;
                channel_desc.sampler_value = samplers.At(sampler_index);
                channel_desc.sampler = sampler_index;
                channel_desc.node = node;
                channel_desc.index = static_cast<u32>(c);
                channel_desc.path = kind;
                mine[at].channels.Push(channel_desc);
            }
            if (!used)
            {
                char what[96];
                FormatTo(what, sizeof(what), "channel on node %u dropped: the node is no joint of a skin",
                         static_cast<unsigned>(node));
                AddImportWarning(&result->report, channel.Path(), StringView(what, CStrLength(what)));
                continue;
            }
            seen.Push(static_cast<u64>(node) * 4 + kind);
        }
        Sort(Span<u64>(seen.Data(), seen.Size()), [](const u64& x, const u64& y) { return x < y; });
        for (usize i = 1; i < seen.Size(); ++i)
        {
            if (seen[i] == seen[i - 1])
            {
                char what[80];
                FormatTo(what, sizeof(what), "has two channels for node %u and one path",
                         static_cast<unsigned>(seen[i] / 4));
                return RefuseClip(error, channels.Path(), StringView(what, CStrLength(what)));
            }
        }
        // The animation's clips in skin order.
        for (usize s = 0; s < skins.Size(); ++s)
        {
            for (usize m = 0; m < mine.Size(); ++m)
            {
                if (mine[m].skin == s)
                {
                    descs.Push(static_cast<ClipDesc&&>(mine[m]));
                }
            }
        }
    }
    if (descs.Size() == 0)
    {
        return Status::Success();
    }
    // Names, ids and paths.
    Vector<String<HeapAllocator>> raw_names;
    Vector<ManifestObject> objects;
    for (usize d = 0; d < descs.Size(); ++d)
    {
        String<HeapAllocator> raw(HeapAllocator::Default(), animation_names[descs[d].animation]);
        if (raw.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "animation%u", static_cast<unsigned>(descs[d].animation));
            raw = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        raw.Append('_');
        raw.Append(skins[descs[d].skin].name.View());
        raw_names.Push(static_cast<String<HeapAllocator>&&>(raw));
    }
    for (usize d = 0; d < descs.Size(); ++d)
    {
        objects.Push(ManifestObject{descs[d].animation * 65536u + descs[d].skin, raw_names[d].View()});
    }
    Vector<ResourceId> ids;
    AssignIds(options, ImportKind::Clip, Span<const ManifestObject>(objects.Data(), objects.Size()), &ids);
    Vector<String<HeapAllocator>> segments;
    for (usize d = 0; d < descs.Size(); ++d)
    {
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(raw_names[d].View(), &segment);
        segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&segments);
    const String<HeapAllocator> prefix = ImportPathPrefix(options);
    Vector<ChannelKeys> cache;  // the channels of the animation being made, read once however many skins use them
    Vector<bool> cached;
    u32 cache_animation = 0xFFFFFFFFu;
    for (usize d = 0; d < descs.Size(); ++d)
    {
        const ClipDesc& desc = descs[d];
        const ImportedSkin& skin = skins[desc.skin];
        if (desc.animation != cache_animation)
        {
            cache.Clear();
            cached.Clear();
            cache_animation = desc.animation;
            const usize channel_count = animations.At(desc.animation).Get("channels").Size();
            cache.Resize(channel_count);
            cached.Resize(channel_count);
        }
        Vector<ClipTrackSource> tracks;
        ResampleError resample;
        f64 duration = 0.0;
        for (usize c = 0; c < desc.channels.Size(); ++c)
        {
            const ChannelDesc& channel = desc.channels[c];
            if (!cached[channel.index])
            {
                ENGINE_TRY(ReadChannelKeys(document, options, channel, &cache[channel.index], error));
                cached[channel.index] = true;
            }
            // This clip's copy of the keys: the cache is shared by the skins that contain the node.
            ChannelKeys keys;
            keys.last_time = cache[channel.index].last_time;
            for (usize k = 0; k < cache[channel.index].keys3.Size(); ++k)
            {
                keys.keys3.Push(cache[channel.index].keys3[k]);
            }
            for (usize k = 0; k < cache[channel.index].keys4.Size(); ++k)
            {
                keys.keys4.Push(cache[channel.index].keys4[k]);
            }
            const u32 joint = skin.JointOfNode(channel.node);
            if (!FollowAncestor(skin.ancestors[joint], channel.path, &keys))
            {
                char what[112];
                FormatTo(what, sizeof(what),
                         "channel on node %u dropped: a node above the joint has a shear or an uneven scale",
                         static_cast<unsigned>(channel.node));
                AddImportWarning(&result->report, animations.At(desc.animation).Path(),
                                 StringView(what, CStrLength(what)));
                continue;
            }
            if (channel.path == kPathTranslation && options.scale != 1.0f)
            {
                for (usize k = 0; k < keys.keys3.Size(); ++k)
                {
                    for (usize e = 0; e < 3; ++e)
                    {
                        keys.keys3[k].value[e] *= options.scale;
                    }
                }
            }
            duration = keys.last_time > duration ? keys.last_time : duration;
            resample.channels += cache[channel.index].resample.channels;
            resample.translation = Worse(resample.translation, cache[channel.index].resample.translation);
            resample.rotation_degrees = Worse(resample.rotation_degrees,
                                              cache[channel.index].resample.rotation_degrees);
            usize at = 0;
            while (at < tracks.Size() && tracks[at].joint != joint)
            {
                ++at;
            }
            if (at == tracks.Size())
            {
                ClipTrackSource track;
                track.joint = joint;
                tracks.Push(static_cast<ClipTrackSource&&>(track));
            }
            if (channel.path == kPathTranslation)
            {
                tracks[at].translation = static_cast<Vector<ClipKey3>&&>(keys.keys3);
            }
            else if (channel.path == kPathScale)
            {
                tracks[at].scale = static_cast<Vector<ClipKey3>&&>(keys.keys3);
            }
            else
            {
                tracks[at].rotation = static_cast<Vector<ClipKey4>&&>(keys.keys4);
            }
        }
        if (tracks.Size() == 0)
        {
            continue;  // every channel of this clip was dropped (and said so)
        }
        if (duration <= 0.0)
        {
            duration = 1.0 / static_cast<f64>(options.clip_rate);  // a still pose: one tick long
        }
        ClipBuildInput input;
        input.duration = static_cast<f32>(duration);
        input.sample_rate = options.clip_rate;
        input.flags = NameMatches(options.looping_clips, raw_names[d].View()) ||
                              NameMatches(options.looping_clips, animation_names[desc.animation])
                          ? static_cast<u32>(kClipLooping)
                          : 0u;
        input.skeleton = skin.id;
        input.skeleton_hash = skin.hash;
        input.joint_count = skin.joint_count;
        input.bounds_min = skin.bounds_min;
        input.bounds_max = skin.bounds_max;
        input.tracks = Span<const ClipTrackSource>(tracks.Data(), tracks.Size());
        ImportedResource resource;
        resource.id = ids[d];
        resource.type = types::kAnimation;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/clips/"));
        resource.path.Append(segments[d].View());
        f32 rotation_error = 0;
        f32 translation_error = 0;
        const Status built = BuildClipBlob(types, resource.id, input, &resource.blob, 0, &rotation_error,
                                           &translation_error);
        if (built.Failed())
        {
            error->Set(animations.At(desc.animation).Path(), StringView(built.Message()));
            return built;
        }
        if (resample.channels != 0)
        {
            // The note is part of the result a host may store: its numbers do not follow the host's LC_NUMERIC.
            char rate[32];
            char translation[32];
            char rotation[32];
            FormatGeneral(rate, sizeof(rate), static_cast<f64>(options.clip_rate), 6);
            FormatGeneral(translation, sizeof(translation), static_cast<f64>(resample.translation), 6);
            FormatGeneral(rotation, sizeof(rotation), static_cast<f64>(resample.rotation_degrees), 6);
            char what[160];
            FormatTo(
                what, sizeof(what),
                "%u cubic-spline channels resampled at %s Hz: largest error %s (translation or scale) and %s degrees",
                static_cast<unsigned>(resample.channels), rate, translation, rotation);
            AddImportNote(&result->report, animations.At(desc.animation).Path(), StringView(what, CStrLength(what)));
            result->report.resampling_translation_error = result->report.resampling_translation_error >
                                                                  static_cast<f32>(resample.translation)
                                                              ? result->report.resampling_translation_error
                                                              : static_cast<f32>(resample.translation);
            result->report.resampling_rotation_degrees = result->report.resampling_rotation_degrees >
                                                                 static_cast<f32>(resample.rotation_degrees)
                                                             ? result->report.resampling_rotation_degrees
                                                             : static_cast<f32>(resample.rotation_degrees);
        }
        result->report.output_bytes += resource.blob.Size();
        if (result->report.output_bytes > options.limits.max_output_bytes)
        {
            return RefuseClip(error, animations.At(desc.animation).Path(),
                              StringView("takes the import past the limit on the bytes it may make"),
                              ErrorCode::TooLarge);
        }
        result->manifest.Add(ImportKind::Clip, desc.animation * 65536u + desc.skin, raw_names[d].View(), resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
