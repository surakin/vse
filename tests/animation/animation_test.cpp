// The animation runtime (Phase 22 Day 321): poses, the sampler, skinning, and the parallel system.

#include <catch2/catch_test_macros.hpp>

#include "animation/components.h"
#include "log/output.h"
#include "asset/clip.h"
#include "animation/pose.h"
#include "animation/sampler.h"
#include "clip_source.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "os/console.h"
#include "os/os.h"
#include "skeleton_source.h"
#include "storage/builtin_components.h"

namespace vse::animation
{
namespace
{

using asset::ResourceId;

void AnText(Vector<char>* out, const char* text)
{
    for (; *text != '\0'; ++text)
    {
        out->Push(*text);
    }
}

StringView AnView(const Vector<char>& v)
{
    return StringView(v.Data(), v.Size());
}

// A skeleton of `n` joints: j0 the root, jK the child of j(K-1) (or of j(K-4) every fourth), each moved and turned.
struct AnSkeleton
{
    asset::TypeRegistry types;
    Vector<byte> blob;
    asset::SkeletonView view;

    void Make(u64 id, u32 n)
    {
        Vector<char> text;
        char line[220];
        FormatTo(line, sizeof(line), "id %016llx\n", static_cast<unsigned long long>(id));
        AnText(&text, line);
        for (u32 i = 0; i < n; ++i)
        {
            const f64 half = 0.07 * (i + 1);
            char parent[16];
            FormatTo(parent, sizeof(parent), i == 0 ? "-" : "j%u", i % 4 == 0 && i >= 4 ? i - 4 : i - 1);
            FormatTo(line, sizeof(line), "joint j%u %s 0.%u %f 0.1  0 0 %f %f  1 1.%u 1\n", i, parent, i % 7,
                     1.0 + 0.05 * i, Sin(half), Cos(half), i % 3);
            AnText(&text, line);
        }
        asset::SkeletonSourceError error;
        REQUIRE(asset::MintSkeleton(AnView(text), types, &blob, &error).Ok());
        REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    }
};

struct AnClip
{
    Vector<byte> blob;
    asset::ClipView view;

    void Make(const AnSkeleton& s, const Vector<char>& text)
    {
        asset::ClipSourceError error;
        const asset::Status status = asset::MintClip(AnView(text), s.view, s.types, asset::ClipLimits(), &blob, &error);
        INFO(error.message.Data());
        REQUIRE(status.Ok());
        REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    }
};

// Rotation about z of joints 0, 5 and 10, a translation on joint 5, over `duration` seconds at 30 keys per second.
Vector<char> AnWaveClip(u64 id, u64 skeleton, f32 duration, const char* flags = "loop", const char* extra = "")
{
    Vector<char> text;
    char line[200];
    FormatTo(line, sizeof(line), "id %016llx\nskeleton %016llx\nduration %g\nrate 30\nflags %s\n",
             static_cast<unsigned long long>(id), static_cast<unsigned long long>(skeleton),
             static_cast<double>(duration), flags);
    AnText(&text, line);
    const u32 frames = static_cast<u32>(duration * 30.0f + 0.5f);
    const u32 joints[3] = {0, 5, 10};
    for (u32 k = 0; k < 3; ++k)
    {
        FormatTo(line, sizeof(line), "track j%u\n", joints[k]);
        AnText(&text, line);
        for (u32 f = 0; f <= frames; ++f)
        {
            const f64 t = f / 30.0;
            const f64 a = 0.6 * Sin(1.5 * t + k);
            FormatTo(line, sizeof(line), "r %f 0 0 %f %f\n", t, Sin(a * 0.5), Cos(a * 0.5));
            AnText(&text, line);
            if (k == 1)
            {
                FormatTo(line, sizeof(line), "t %f %f %f 0\n", t, 0.3 * Sin(t), 1.0 + 0.1 * t);
                AnText(&text, line);
            }
        }
    }
    AnText(&text, extra);
    return text;
}

class AnResolver : public AnimationResolver
{
public:
    const AnSkeleton* skeleton = nullptr;
    const AnClip* clip = nullptr;
    const AnSkeleton* other_skeleton = nullptr;
    u64 skeleton_id = 0;
    u64 clip_id = 0;
    u64 other_skeleton_id = 0;
    const asset::SkeletonView* FindSkeleton(ResourceId id) const override
    {
        if (id.Value() == skeleton_id)
        {
            return &skeleton->view;
        }
        if (other_skeleton != nullptr && id.Value() == other_skeleton_id)
        {
            return &other_skeleton->view;
        }
        return nullptr;
    }
    const asset::ClipView* FindClip(ResourceId id) const override
    {
        return id.Value() == clip_id ? &clip->view : nullptr;
    }
};

jobs::Scheduler::Config AnConfig(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

bool AnSame(const LocalPose& a, const LocalPose& b)
{
    return a.joint_count == b.joint_count &&
           MemCompare(a.translation.Data(), b.translation.Data(), sizeof(a.translation)) == 0 &&
           MemCompare(a.rotation.Data(), b.rotation.Data(), sizeof(a.rotation)) == 0 &&
           MemCompare(a.scale.Data(), b.scale.Data(), sizeof(a.scale)) == 0;
}

// The largest component difference after putting both quaternions in the same hemisphere.
f32 AnComponentDiff(const Array<f32, 4>& a, const Array<f32, 4>& b)
{
    const f32 sign = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f ? -1.0f : 1.0f;
    f32 worst = 0;
    for (u32 c = 0; c < 4; ++c)
    {
        const f32 d = Abs(a[c] - sign * b[c]);
        worst = d > worst ? d : worst;
    }
    return worst;
}

f64 AnAngleDegrees(const Array<f32, 4>& a, const Array<f32, 4>& b)
{
    f64 dot = static_cast<f64>(a[0]) * b[0] + static_cast<f64>(a[1]) * b[1] + static_cast<f64>(a[2]) * b[2] +
              static_cast<f64>(a[3]) * b[3];
    dot = Abs(dot) > 1.0 ? 1.0 : Abs(dot);
    return 2.0 * Acos(dot) * 57.29577951308232;
}

}  // namespace

TEST_CASE("animation: samples at keys and between them follow the authored curve", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    SamplerState state;
    LocalPose pose;
    for (u32 i = 0; i <= 80; ++i)
    {
        const f32 t = 2.0f * static_cast<f32>(i) / 80.0f;  // keys and half-way between keys
        SampleClip(clip.view, skeleton.view, t, false, &state, &pose);
        for (u32 k = 0; k < 3; ++k)
        {
            const u32 joint = k * 5;
            const f64 a = 0.6 * Sin(1.5 * t + k);
            const Array<f32, 4> want = {{0.0f, 0.0f, static_cast<f32>(Sin(a * 0.5)), static_cast<f32>(Cos(a * 0.5))}};
            CHECK(AnAngleDegrees(pose.rotation[joint], want) < 0.2);  // the minted bound plus the key interpolation
        }
    }
    // A joint without a track keeps the bind pose.
    SampleClip(clip.view, skeleton.view, 0.7f, false, &state, &pose);
    CHECK(pose.translation[3][1] == skeleton.view.Translation(3)[1]);
    CHECK(pose.rotation[3][2] == skeleton.view.Rotation(3)[2]);
}

TEST_CASE("animation: a cursor never changes a result, for forward, looping and seeking playback", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    SamplerState state;
    LocalPose with, without;
    f32 time = 0;
    for (u32 step = 0; step < 400; ++step)
    {
        time += 0.0137f;
        SampleClip(clip.view, skeleton.view, time, true, &state, &with);  // forward, then past the end: looping
        SampleClip(clip.view, skeleton.view, time, true, nullptr, &without);
        REQUIRE(AnSame(with, without));
    }
    const f32 seeks[] = {1.9f, 0.1f, 1.0f, 0.0f, 2.0f, 0.5f, 0.5f, 1.99f};
    for (f32 s : seeks)
    {
        SampleClip(clip.view, skeleton.view, s, false, &state, &with);
        SampleClip(clip.view, skeleton.view, s, false, nullptr, &without);
        CHECK(AnSame(with, without));
    }
    // A cursor of garbage is a valid hint.
    for (usize k = 0; k < state.cursor.Size(); ++k)
    {
        state.cursor[k] = 0xdeadbeef;
    }
    SampleClip(clip.view, skeleton.view, 1.3f, false, &state, &with);
    SampleClip(clip.view, skeleton.view, 1.3f, false, nullptr, &without);
    CHECK(AnSame(with, without));
}

TEST_CASE("animation: time advance wraps a looping clip and stops another", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 4);
    Vector<char> text;
    AnText(&text,
           "id 00000000000000c0\nskeleton 00000000000000b0\nduration 2\nrate 30\ntrack j0\nt 0 0 0 0\nt 2 1 0 0\n");
    AnClip clip;
    clip.Make(skeleton, text);
    const TimeAdvance a = AdvanceTime(clip.view, 1.5f, 1.0f, true);
    CHECK(a.wraps == 1);
    CHECK(Abs(a.time - 0.5f) < 1e-6f);
    const TimeAdvance b = AdvanceTime(clip.view, 1.5f, 5.0f, true);
    CHECK(b.wraps == 3);
    const TimeAdvance c = AdvanceTime(clip.view, 1.5f, 1.0f, false);
    CHECK(c.finished);
    CHECK(c.time == 2.0f);
    const TimeAdvance d = AdvanceTime(clip.view, 1.5f, -3.0f, true);
    CHECK(d.time == 1.5f);
}

TEST_CASE("animation: the nlerp rotation differs from slerp by a recorded amount", "[animation]")
{
    // A 170 degree segment about z: nlerp's angular speed is not constant; its largest deviation from slerp (the
    // constant-speed path) is recorded here and bounded.
    const f64 total = 170.0 * 0.017453292519943295;
    f64 worst = 0;
    for (u32 i = 1; i < 100; ++i)
    {
        const f64 u = i / 100.0;
        const f64 slerp_angle = total * u;
        // nlerp of (0,0,0,1) and (0,0,sin(t/2),cos(t/2)).
        const f64 z = u * Sin(total / 2.0);
        const f64 w = (1.0 - u) + u * Cos(total / 2.0);
        const f64 nlerp_angle = 2.0 * Atan2(z, w);
        const f64 error = Abs(nlerp_angle - slerp_angle) * 57.29577951308232;
        worst = error > worst ? error : worst;
    }
    out::Print("nlerp versus slerp over 170 degrees: worst angle error %.2f degrees\n", worst);
    CHECK(worst < 30.0);
    CHECK(worst > 1.0);  // it is not zero: that is why the figure is recorded
}

TEST_CASE("animation: blends at weight 0 and 1 reproduce the inputs, a mask leaves joints alone", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    LocalPose a, b;
    SampleClip(clip.view, skeleton.view, 0.4f, false, nullptr, &a);
    SampleClip(clip.view, skeleton.view, 1.3f, false, nullptr, &b);
    LocalPose copy = a;
    Blend(&copy, b, 0.0f);
    CHECK(AnSame(copy, a));
    Blend(&copy, b, 1.0f);
    CHECK(AnSame(copy, b));
    copy = a;
    Blend(&copy, b, 0.5f);
    CHECK_FALSE(AnSame(copy, a));
    CHECK_FALSE(AnSame(copy, b));
    for (u32 j = 0; j < 20; ++j)  // rotations stay unit
    {
        const f32 l = copy.rotation[j][0] * copy.rotation[j][0] + copy.rotation[j][1] * copy.rotation[j][1] +
                      copy.rotation[j][2] * copy.rotation[j][2] + copy.rotation[j][3] * copy.rotation[j][3];
        CHECK(Abs(l - 1.0f) < 1e-5f);
    }
    JointMask mask;
    mask.weight[5] = 255;
    mask.weight[10] = 128;
    copy = a;
    BlendMasked(&copy, b, 1.0f, mask);
    for (u32 j = 0; j < 20; ++j)
    {
        if (j == 5)
        {
            CHECK(MemCompare(copy.rotation[j].Data(), b.rotation[j].Data(), 16) == 0);
        }
        else if (j != 10)
        {
            CHECK(MemCompare(copy.rotation[j].Data(), a.rotation[j].Data(), 16) == 0);
            CHECK(MemCompare(copy.translation[j].Data(), a.translation[j].Data(), 12) == 0);
        }
    }
}

TEST_CASE("animation: an additive clip applied to the bind pose reproduces the pose it was made from", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    LocalPose bind;
    SetBindPose(skeleton.view, &bind);
    // The pose to reproduce, and the additive difference authored from it: d_t = t - bind_t, d_q = q * inverse(bind_q),
    // d_s = s / bind_s.
    LocalPose target = bind;
    for (u32 j = 0; j < 20; ++j)
    {
        const f32 half = 0.05f * static_cast<f32>(j + 1);
        target.translation[j][0] += 0.1f * static_cast<f32>(j);
        const Array<f32, 4> turn = {{0.0f, Sin(half), 0.0f, Cos(half)}};
        const Array<f32, 4>& b = bind.rotation[j];
        target.rotation[j] = {{turn[3] * b[0] + turn[0] * b[3] + turn[1] * b[2] - turn[2] * b[1],
                               turn[3] * b[1] - turn[0] * b[2] + turn[1] * b[3] + turn[2] * b[0],
                               turn[3] * b[2] + turn[0] * b[1] - turn[1] * b[0] + turn[2] * b[3],
                               turn[3] * b[3] - turn[0] * b[0] - turn[1] * b[1] - turn[2] * b[2]}};
        target.scale[j][1] *= 1.25f;
    }
    LocalPose additive;
    SetIdentityPose(20, &additive);
    for (u32 j = 0; j < 20; ++j)
    {
        const f32 half = 0.05f * static_cast<f32>(j + 1);
        additive.translation[j][0] = 0.1f * static_cast<f32>(j);
        additive.rotation[j] = {{0.0f, Sin(half), 0.0f, Cos(half)}};
        additive.scale[j][1] = 1.25f;
    }
    LocalPose result = bind;
    AddAdditive(&result, additive, 1.0f);
    for (u32 j = 0; j < 20; ++j)
    {
        CHECK(Abs(result.translation[j][0] - target.translation[j][0]) < 1e-5f);
        CHECK(AnComponentDiff(result.rotation[j], target.rotation[j]) < 1e-5f);
        CHECK(Abs(result.scale[j][1] - target.scale[j][1]) < 1e-5f);
    }
    LocalPose half = bind;
    AddAdditive(&half, additive, 0.0f);
    CHECK(AnSame(half, bind));
}

TEST_CASE("animation: an additive clip samples as differences and joints without tracks stay neutral", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f, "additive"));
    LocalPose pose;
    SampleClip(clip.view, skeleton.view, 0.5f, false, nullptr, &pose);
    CHECK(pose.translation[3][0] == 0.0f);
    CHECK(pose.rotation[3][3] == 1.0f);
    CHECK(pose.scale[3][1] == 1.0f);
}

namespace
{

// A test-local reference: double precision 4x4 matrices, a plain triple loop.
struct AnM4
{
    f64 m[4][4];
};

AnM4 AnIdentity()
{
    AnM4 r;
    for (u32 i = 0; i < 4; ++i)
    {
        for (u32 j = 0; j < 4; ++j)
        {
            r.m[i][j] = i == j ? 1.0 : 0.0;
        }
    }
    return r;
}

AnM4 AnMul(const AnM4& a, const AnM4& b)
{
    AnM4 r;
    for (u32 i = 0; i < 4; ++i)
    {
        for (u32 j = 0; j < 4; ++j)
        {
            f64 s = 0;
            for (u32 k = 0; k < 4; ++k)
            {
                s += a.m[i][k] * b.m[k][j];
            }
            r.m[i][j] = s;
        }
    }
    return r;
}

AnM4 AnTrs(const Array<f32, 3>& t, const Array<f32, 4>& q, const Array<f32, 3>& s)
{
    const f64 x = q[0], y = q[1], z = q[2], w = q[3];
    AnM4 r = AnIdentity();
    const f64 rot[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            r.m[i][j] = rot[i][j] * s[j];
        }
        r.m[i][3] = t[i];
    }
    return r;
}

}  // namespace

TEST_CASE("animation: the skinning palette matches a plain 4x4 reference and is the identity at the bind pose",
          "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    LocalPose pose;
    SampleClip(clip.view, skeleton.view, 0.8f, false, nullptr, &pose);
    ModelPose model;
    ModelFromLocal(skeleton.view, pose, &model);
    Array<Mat3x4, 20> palette;
    SkinningMatrices(skeleton.view, model, Span<Mat3x4>(palette.Data(), palette.Size()));

    AnM4 reference_model[20];
    for (u32 j = 0; j < 20; ++j)
    {
        const AnM4 local = AnTrs(pose.translation[j], pose.rotation[j], pose.scale[j]);
        const u16 parent = skeleton.view.Parent(j);
        reference_model[j] = parent == asset::kNoParent ? local : AnMul(reference_model[parent], local);
        AnM4 ibm = AnIdentity();
        const Array<f32, 12> m = skeleton.view.InverseBind(j);
        for (u32 i = 0; i < 12; ++i)
        {
            ibm.m[i / 4][i % 4] = m[i];
        }
        const AnM4 want = AnMul(reference_model[j], ibm);
        for (u32 i = 0; i < 12; ++i)
        {
            CHECK(Abs(static_cast<f64>(palette[j][i]) - want.m[i / 4][i % 4]) < 1e-5);
        }
    }

    LocalPose bind;
    SetBindPose(skeleton.view, &bind);
    ModelFromLocal(skeleton.view, bind, &model);
    SkinningMatrices(skeleton.view, model, Span<Mat3x4>(palette.Data(), palette.Size()));
    for (u32 j = 0; j < 20; ++j)
    {
        for (u32 i = 0; i < 12; ++i)
        {
            const f32 want = (i % 5 == 0) ? 1.0f : 0.0f;  // the diagonal of the 3x3 part
            CHECK(Abs(palette[j][i] - want) < 1e-4f);
        }
    }
    CHECK(sizeof(Mat3x4) == asset::kInverseBindBytes);
}

namespace
{

struct AnWorld
{
    ecs::World world;
    Vector<ecs::Entity> entities;
};

void AnPopulate(AnWorld* w, u32 count, u64 skeleton, u64 clip, f32 duration)
{
    w->world.SetResource(AnimationEvents{});
    for (u32 i = 0; i < count; ++i)
    {
        AnimationPlayer p;
        p.clip = clip;
        p.time = duration * static_cast<f32>(i % 17) / 17.0f;
        p.speed = 0.5f + 0.1f * static_cast<f32>(i % 9);
        p.looping = (i % 5) != 0;
        SkeletonInstance inst;
        inst.skeleton = skeleton;
        w->entities.Push(w->world.Create(Move(p), Move(inst), RootMotion{}));
    }
}

u64 AnHashPalettes(AnWorld* w)
{
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < w->entities.Size(); ++i)
    {
        const SkeletonInstance* inst = w->world.Get<SkeletonInstance>(w->entities[i]);
        const byte* bytes = reinterpret_cast<const byte*>(inst->palette.Data());
        for (usize k = 0; k < inst->palette.Size() * sizeof(Mat3x4); ++k)
        {
            h = (h ^ static_cast<u64>(bytes[k])) * 1099511628211ull;
        }
        const AnimationPlayer* p = w->world.Get<AnimationPlayer>(w->entities[i]);
        u32 time_bits = 0;
        MemCopy(&time_bits, &p->time, 4);
        h = (h ^ static_cast<u64>(time_bits)) * 1099511628211ull;
    }
    return h;
}

}  // namespace

TEST_CASE("animation: the system gives the same palettes at 1 and 8 workers", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    u64 hashes[2] = {0, 0};
    const usize workers[2] = {1, 8};
    for (u32 run = 0; run < 2; ++run)
    {
        AnWorld w;
        AnPopulate(&w, 700, 0xb0, 0xc0, 2.0f);
        jobs::Scheduler scheduler(AnConfig(workers[run]));
        AnimationSystem animator(&resolver);
        AnimationStats stats;
        for (u32 frame = 0; frame < 6; ++frame)
        {
            stats = animator.Update(&w.world, &scheduler, 0.07f);
        }
        CHECK(stats.players_updated == 700);
        CHECK(stats.refused == 0);
        hashes[run] = AnHashPalettes(&w);
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(hashes[0] != 0);
}

TEST_CASE("animation: events fire once per crossing, across a loop too", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f, "loop", "event 0 start 1\nevent 0.5 half 2\nevent 1.5 late 3\n"));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    AnWorld w;
    AnPopulate(&w, 1, 0xb0, 0xc0, 2.0f);
    w.world.Get<AnimationPlayer>(w.entities[0])->time = 0.0f;
    w.world.Get<AnimationPlayer>(w.entities[0])->speed = 1.0f;
    w.world.Get<AnimationPlayer>(w.entities[0])->looping = true;
    AnimationSystem animator(&resolver);

    auto payloads = [&]()
    {
        Vector<u32> out;
        const Vector<AnimationEvent>& events = w.world.Resource<AnimationEvents>().events;
        for (usize i = 0; i < events.Size(); ++i)
        {
            CHECK(events[i].entity == w.entities[0]);
            out.Push(events[i].payload);
        }
        return out;
    };
    animator.Update(&w.world, nullptr, 0.6f);  // 0 -> 0.6: the start event (first frame) and the half
    Vector<u32> got = payloads();
    REQUIRE(got.Size() == 2);
    CHECK(got[0] == 1);
    CHECK(got[1] == 2);
    animator.Update(&w.world, nullptr, 1.0f);  // 0.6 -> 1.6: the late event
    got = payloads();
    REQUIRE(got.Size() == 1);
    CHECK(got[0] == 3);
    animator.Update(&w.world, nullptr, 0.7f);  // 1.6 -> 0.3 over the loop: the start event again, once
    got = payloads();
    REQUIRE(got.Size() == 1);
    CHECK(got[0] == 1);
    animator.Update(&w.world, nullptr, 0.0f);  // nothing moved: nothing crossed
    CHECK(payloads().Size() == 0);
    animator.Update(&w.world, nullptr, 4.5f);  // 0.3 -> 4.8: two loops, ending at 0.8
    got = payloads();
    // (0.3, 2]: half and late; one whole pass in between: start, half, late; then [0, 0.8]: start and half.
    REQUIRE(got.Size() == 7);
    const u32 expected[7] = {2, 3, 1, 2, 3, 1, 2};
    for (usize i = 0; i < 7; ++i)
    {
        CHECK(got[i] == expected[i]);
    }
}

TEST_CASE("animation: a clip for another skeleton is refused, not applied", "[animation]")
{
    AnSkeleton skeleton, other;
    skeleton.Make(0xb0, 20);
    other.Make(0xb1, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.other_skeleton = &other;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.other_skeleton_id = 0xb1;
    resolver.clip_id = 0xc0;
    AnWorld w;
    AnPopulate(&w, 3, 0xb0, 0xc0, 2.0f);
    w.world.Get<SkeletonInstance>(w.entities[1])->skeleton = 0xb1;   // the clip was made for 0xb0
    w.world.Get<SkeletonInstance>(w.entities[2])->skeleton = 0x999;  // unknown
    AnimationSystem animator(&resolver);
    const AnimationStats stats = animator.Update(&w.world, nullptr, 0.1f);
    CHECK(stats.players_updated == 1);
    CHECK(stats.refused == 2);
    CHECK(w.world.Get<SkeletonInstance>(w.entities[1])->palette.Size() == 0);
    CHECK(w.world.Get<SkeletonInstance>(w.entities[0])->palette.Size() == 20);
}

// 2026-11-20 (a glTF whose only animation moves the morph weights of two skinned meshes): a clip of morph weights only
// plays on an entity that has a skeleton too. Its weights follow the clip and its skeleton stays in the bind pose; it
// was skipped ("a skeleton or clip is not loaded, or the clip was made for another skeleton").
TEST_CASE("animation: a clip of morph weights only plays on a skinned entity and on one without a skeleton",
          "[animation][morph]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 4);
    const Array<f32, 2> times = {{0.0f, 1.0f}};
    const Array<f32, 2> values = {{0.0f, 1.0f}};
    asset::ClipBuildInput input;
    input.duration = 1.0f;
    input.weights.target_count = 1;
    input.weights.times = Span<const f32>(times.Data(), times.Size());
    input.weights.values = Span<const f32>(values.Data(), values.Size());
    AnClip clip;
    REQUIRE(asset::BuildClipBlob(skeleton.types, ResourceId::FromValue(0xc0), input, &clip.blob).Ok());
    REQUIRE(clip.view.Open(ByteSpan(clip.blob.Data(), clip.blob.Size()), skeleton.types).Ok());
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;

    ecs::World world;
    const auto player = []
    {
        AnimationPlayer p;
        p.clip = 0xc0;
        return p;
    };
    SkeletonInstance inst;
    inst.skeleton = 0xb0;
    MorphWeights weights;
    weights.count = 1;
    const ecs::Entity skinned = world.Create(player(), Move(inst), MorphWeights(weights));
    const ecs::Entity plain = world.Create(player(), MorphWeights(weights));
    AnimationSystem animator(&resolver);
    const AnimationStats stats = animator.Update(&world, nullptr, 0.5f);
    CHECK(stats.refused == 0);
    CHECK(stats.players_updated == 2);
    CHECK(world.Get<MorphWeights>(skinned)->weights[0] == 0.5f);
    CHECK(world.Get<MorphWeights>(plain)->weights[0] == 0.5f);

    // The skinned entity is drawn in its bind pose.
    LocalPose bind;
    SetBindPose(skeleton.view, &bind);
    ModelPose model;
    ModelFromLocal(skeleton.view, bind, &model);
    Array<Mat3x4, 4> expected;
    SkinningMatrices(skeleton.view, model, Span<Mat3x4>(expected.Data(), 4));
    const SkeletonInstance* posed = world.Get<SkeletonInstance>(skinned);
    REQUIRE(posed->palette.Size() == 4);
    CHECK(MemCompare(posed->palette.Data(), expected.Data(), sizeof(Mat3x4) * 4) == 0);
}

TEST_CASE("animation: an additive clip, a weight above 1 and an infinite step do not break the system", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip walk, additive;
    walk.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    additive.Make(skeleton, AnWaveClip(0xc1, 0xb0, 2.0f, "additive"));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &walk;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    AnWorld w;
    AnPopulate(&w, 2, 0xb0, 0xc0, 2.0f);
    w.world.Get<AnimationPlayer>(w.entities[0])->weight = 7.0f;
    AnimationSystem animator(&resolver);
    AnimationStats stats = animator.Update(&w.world, nullptr, Limits<f32>::Infinity());
    CHECK(stats.players_updated == 2);
    for (usize i = 0; i < 2; ++i)
    {
        const f32 t = w.world.Get<AnimationPlayer>(w.entities[i])->time;
        CHECK(t >= 0.0f);
        CHECK(t <= 2.0f);  // finite, inside the clip
        const SkeletonInstance* inst = w.world.Get<SkeletonInstance>(w.entities[i]);
        CHECK(inst->palette.Size() == 20);
        CHECK(inst->palette[3][0] == inst->palette[3][0]);  // not NaN
    }
    resolver.clip = &additive;
    resolver.clip_id = 0xc1;
    w.world.Get<AnimationPlayer>(w.entities[0])->clip = 0xc1;
    w.world.Get<AnimationPlayer>(w.entities[1])->clip = 0xc1;
    stats = animator.Update(&w.world, nullptr, 0.1f);
    CHECK(stats.players_updated == 0);
    CHECK(stats.refused == 2);
}

TEST_CASE("animation: an entity that loses its skeleton instance stops publishing events", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 20);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f, "loop", "event 0.5 half 2\n"));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    AnWorld w;
    AnPopulate(&w, 1, 0xb0, 0xc0, 2.0f);
    w.world.Get<AnimationPlayer>(w.entities[0])->time = 0.0f;
    w.world.Get<AnimationPlayer>(w.entities[0])->speed = 1.0f;
    AnimationSystem animator(&resolver);
    animator.Update(&w.world, nullptr, 0.6f);
    CHECK(w.world.Resource<AnimationEvents>().events.Size() == 1);
    REQUIRE(w.world.Remove<SkeletonInstance>(w.entities[0]) == ecs::Status::Ok);
    animator.Update(&w.world, nullptr, 0.1f);
    CHECK(w.world.Resource<AnimationEvents>().events.Size() == 0);  // the last step is stale, not replayed
}

TEST_CASE("animation: root motion is the root's movement over the step, and the pose holds the root", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 4);
    Vector<char> text;
    AnText(&text, "id 00000000000000c0\nskeleton 00000000000000b0\nduration 2\nrate 30\nflags loop rootmotion\n"
                  "track j0\nt 0 0 0 0\nt 2 2 0 0\n");
    AnClip clip;
    clip.Make(skeleton, text);
    const RootMotionDelta d = RootMotionOver(clip.view, skeleton.view, 0.5f, 0.5f, true);
    CHECK(Abs(d.translation[0] - 0.5f) < 1e-3f);
    const RootMotionDelta wrapped = RootMotionOver(clip.view, skeleton.view, 1.8f, 0.5f, true);
    CHECK(Abs(wrapped.translation[0] - 0.5f) < 1e-3f);  // 0.2 to the end, 0.3 from the start
    const RootMotionDelta twice = RootMotionOver(clip.view, skeleton.view, 0.0f, 4.0f, true);
    CHECK(Abs(twice.translation[0] - 4.0f) < 1e-3f);
    LocalPose pose;
    SampleClip(clip.view, skeleton.view, 1.5f, true, nullptr, &pose);
    CHECK(pose.translation[0][0] == 0.0f);  // held at the first frame

    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    AnWorld w;
    AnPopulate(&w, 1, 0xb0, 0xc0, 2.0f);
    AnimationPlayer* p = w.world.Get<AnimationPlayer>(w.entities[0]);
    p->time = 0.25f;
    p->speed = 1.0f;
    p->looping = true;
    AnimationSystem animator(&resolver);
    animator.Update(&w.world, nullptr, 0.5f);
    CHECK(Abs(w.world.Get<RootMotion>(w.entities[0])->delta.translation[0] - 0.5f) < 1e-3f);
}

static bool AnQuatSame(const Array<f32, 4>& a, const Array<f32, 4>& b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

TEST_CASE("animation: a host rotation composes onto a joint's own rotation and the bind pose takes it", "[animation]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 6);
    LocalPose bind;
    SetBindPose(skeleton.view, &bind);
    LocalPose turned = bind;
    const Array<f32, 4> quarter = {{0.0f, 0.0f, 0.70710678f, 0.70710678f}};  // 90 degrees about Z
    ComposeJointRotation(&turned, 2, quarter);
    for (u32 j = 0; j < 6; ++j)
    {
        if (j == 2)
        {
            continue;
        }
        CHECK(AnQuatSame(turned.rotation[j], bind.rotation[j]));  // the other joints are untouched
    }
    CHECK_FALSE(AnQuatSame(turned.rotation[2], bind.rotation[2]));
    const f32 l = turned.rotation[2][0] * turned.rotation[2][0] + turned.rotation[2][1] * turned.rotation[2][1] +
                  turned.rotation[2][2] * turned.rotation[2][2] + turned.rotation[2][3] * turned.rotation[2][3];
    CHECK(Abs(l - 1.0f) < 1e-5f);
    // `old * added`: a rotation of the bind pose's own, then the added one
    LocalPose twice = turned;
    ComposeJointRotation(&twice, 2, quarter);
    LocalPose half = bind;
    const Array<f32, 4> half_turn = {{0.0f, 0.0f, 1.0f, 0.0f}};
    ComposeJointRotation(&half, 2, half_turn);
    for (u32 c = 0; c < 4; ++c)
    {
        CHECK(Abs(twice.rotation[2][c] - half.rotation[2][c]) < 1e-5f);
    }
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(RegisterAnimationComponents(&registry).Ok());
    CHECK(registry.FindByName("joint_rotations") != nullptr);
}

TEST_CASE("animation: the components register with the storage registry", "[animation]")
{
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(RegisterAnimationComponents(&registry).Ok());
    CHECK(registry.FindByName("animation_player") != nullptr);
    CHECK(registry.FindByName("skeleton_instance") != nullptr);
    CHECK(registry.Find<AnimationPlayer>() != nullptr);
    CHECK(RegisterAnimationComponents(&registry).Failed());  // twice is refused
}

TEST_CASE("animation: sampling throughput", "[.][animation-bench]")
{
    AnSkeleton skeleton;
    skeleton.Make(0xb0, 40);
    AnClip clip;
    clip.Make(skeleton, AnWaveClip(0xc0, 0xb0, 2.0f));
    AnResolver resolver;
    resolver.skeleton = &skeleton;
    resolver.clip = &clip;
    resolver.skeleton_id = 0xb0;
    resolver.clip_id = 0xc0;
    {
        SamplerState state;
        LocalPose pose;
        const u64 start = os::MonotonicTimeNanos();
        const u32 n = 100000;
        for (u32 i = 0; i < n; ++i)
        {
            SampleClip(clip.view, skeleton.view, 0.00002f * static_cast<f32>(i), true, &state, &pose);
        }
        const f64 seconds = static_cast<f64>(os::MonotonicTimeNanos() - start) * 1e-9;
        out::Print("[animation-bench] %.0f clip samples per second (40 joints, 3 tracks)\n", n / seconds);
    }
    const usize workers[2] = {1, 8};
    for (usize wk : workers)
    {
        AnWorld w;
        AnPopulate(&w, 10000, 0xb0, 0xc0, 2.0f);
        jobs::Scheduler scheduler(AnConfig(wk));
        AnimationSystem animator(&resolver);
        animator.Update(&w.world, &scheduler, 0.016f);
        const u64 start = os::MonotonicTimeNanos();
        const u32 frames = 20;
        for (u32 f = 0; f < frames; ++f)
        {
            animator.Update(&w.world, &scheduler, 0.016f);
        }
        const f64 ms = static_cast<f64>(os::MonotonicTimeNanos() - start) * 1e-6 / frames;
        out::Print("[animation-bench] 10000 characters of 20 joints: %.2f ms per update at %u workers\n", ms,
                   static_cast<unsigned>(wk));
    }
}

}  // namespace vse::animation
