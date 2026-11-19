// The animation clip format, its source form and the minter (Phase 22 Day 320).

#include <catch2/catch_test_macros.hpp>

#include "asset/clip.h"
#include "asset/skeleton.h"
#include "containers/array.h"
#include "clip_source.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "skeleton_source.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr const char* kClipSkeletonText = "id 00000000000000bb\n"
                                          "joint root - 0 0 0  0 0 0 1  1 1 1\n"
                                          "joint arm root 0 1 0  0 0 0 1  1 1 1\n"
                                          "joint hand arm 0 1 0  0 0 0 1  1 1 1\n";

struct ClpTestSkeleton
{
    TypeRegistry types;
    Vector<byte> blob;
    SkeletonView view;
};

void ClpMakeSkeleton(ClpTestSkeleton* s)
{
    SkeletonSourceError error;
    REQUIRE(MintSkeleton(test::Vp(kClipSkeletonText), s->types, &s->blob, &error).Ok());
    REQUIRE(s->view.Open(ByteSpan(s->blob.Data(), s->blob.Size()), s->types).Ok());
}

void ClpAppend(Vector<char>* out, const char* text)
{
    for (; *text != '\0'; ++text)
    {
        out->Push(*text);
    }
}

StringView ClpView(const Vector<char>& v)
{
    return StringView(v.Data(), v.Size());
}

const char* ClpHeader()
{
    return "id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nrate 30\n";
}

Vector<byte> ClpMint(const ClpTestSkeleton& s, const Vector<char>& text, ClipLimits limits = ClipLimits())
{
    Vector<byte> blob;
    ClipSourceError error;
    const Status status = MintClip(ClpView(text), s.view, s.types, limits, &blob, &error);
    INFO(error.message.Data());
    REQUIRE(status.Ok());
    return blob;
}

Status ClpOpen(const TypeRegistry& types, const Vector<byte>& blob)
{
    ClipView view;
    return view.Open(ByteSpan(blob.Data(), blob.Size()), types);
}

// A clip with a dense rotation sweep on `arm`, a straight translation on `root` and a constant scale on `hand`.
Vector<char> ClpDenseSource()
{
    Vector<char> text;
    ClpAppend(&text, ClpHeader());
    ClpAppend(&text, "flags loop\nbounds -1 -1 -1 1 3 1\ntrack arm\n");
    for (u32 i = 0; i <= 60; ++i)
    {
        char line[160];
        const f64 time = 2.0 * i / 60.0;
        const f64 angle = 0.5 * Sin(time * 3.0);
        FormatTo(line, sizeof(line), "r %f 0 0 %f %f\n", time, Sin(angle * 0.5), Cos(angle * 0.5));
        ClpAppend(&text, line);
    }
    ClpAppend(&text, "track root\n");
    for (u32 i = 0; i <= 30; ++i)
    {
        char line[160];
        const f64 time = 2.0 * i / 30.0;
        FormatTo(line, sizeof(line), "t %f %f 0 %f\n", time, 0.5 * time, 2.0 - time);
        ClpAppend(&text, line);
    }
    ClpAppend(&text, "track hand\ns 0 1.5 1.5 1.5\ns 1 1.5 1.5 1.5\ns 2 1.5 1.5 1.5\n");
    ClpAppend(&text, "event 0.5 footstep 7\nevent 1.5 footstep\n");
    return text;
}

Vector<byte> ClpSectionCopy(const TypeRegistry& types, const Vector<byte>& blob, u32 id)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan data;
    REQUIRE(reader.FindSection(id, &data));
    Vector<byte> out;
    out.Resize(data.Size());
    MemCopy(out.Data(), data.Data(), data.Size());
    return out;
}

Vector<byte> ClpWithSection(const TypeRegistry& types, const Vector<byte>& blob, u32 id,
                            const Vector<byte>& replacement)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    BlobWriter writer(types, types::kAnimation, reader.Id(), 0);
    writer.AddDependency(reader.DependencyAt(0));
    for (u32 section = kFirstUserSection; section < kFirstUserSection + 4; ++section)
    {
        ByteSpan data;
        if (section == id)
        {
            REQUIRE(writer.AddSection(section, ByteSpan(replacement.Data(), replacement.Size())).Ok());
        }
        else if (reader.FindSection(section, &data))
        {
            REQUIRE(writer.AddSection(section, data).Ok());
        }
    }
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

struct ClpBadSource
{
    const char* body;
    u32 line;
};

}  // namespace

TEST_CASE("clip: the type is registered", "[asset][clip]")
{
    TypeRegistry types;
    TypeInfo info;
    REQUIRE(types.Find(types::kAnimation, &info));
    CHECK(info.current_version == 1);
}

TEST_CASE("clip source: each error names its line", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    const ClpBadSource cases[] = {
        {"id 00000000000000cc\nid 00000000000000cc\n", 2},
        {"id zz\n", 1},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 0\n", 3},
        {"track arm\n", 1},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack elbow\n", 4},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\ntrack arm\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nt 0 1 2 3\n", 4},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\nt 0 1 2\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\nt 3 1 2 3\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\nt 1 1 2 3\nt 1 1 2 3\n", 6},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\nr 0 1 1 1 1\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\ntrack arm\nr 0 0 0 0 nan\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nflags bounce\n", 4},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nevent 1 a\nevent 0.5 b\n", 5},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nevent 3 a\n", 4},
        {"id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\nwiggle 1\n", 4},
    };
    for (const ClpBadSource& c : cases)
    {
        INFO(c.body);
        ClipSource source;
        ClipSourceError error;
        const Status status = ParseClipSource(test::Vp(c.body), s.view, &source, &error);
        CHECK(status.Failed());
        CHECK(error.line == c.line);
    }
    SECTION("missing parts of the file as a whole")
    {
        ClipSource source;
        ClipSourceError error;
        CHECK(ParseClipSource(test::Vp("id 00000000000000cc\n"), s.view, &source, &error).Failed());
        CHECK(error.line == 0);
        CHECK(ParseClipSource(test::Vp("id 00000000000000cc\nskeleton 00000000000000bb\nduration 2\n"), s.view, &source,
                              &error)
                  .Failed());
        CHECK(error.line == 0);
    }
    SECTION("the skeleton must be the one the source names")
    {
        Vector<byte> blob;
        ClipSourceError error;
        const char* other = "id 00000000000000cc\nskeleton 00000000000000dd\nduration 2\nevent 1 a\n";
        CHECK(MintClip(test::Vp(other), s.view, s.types, ClipLimits(), &blob, &error).Failed());
    }
}

TEST_CASE("clip: dense keys reduce, stay within the bounds and reload", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    const Vector<char> text = ClpDenseSource();
    Vector<byte> blob;
    ClipSourceError error;
    ClipSource source;
    REQUIRE(ParseClipSource(ClpView(text), s.view, &source, &error).Ok());
    REQUIRE(source.tracks.Size() == 3);
    ClipBuildInput input;
    input.duration = source.duration;
    input.flags = source.flags;
    input.skeleton = source.skeleton;
    input.skeleton_hash = s.view.Hash();
    input.joint_count = s.view.JointCount();
    input.bounds_min = source.bounds_min;
    input.bounds_max = source.bounds_max;
    input.tracks = Span<const ClipTrackSource>(source.tracks.Data(), source.tracks.Size());
    input.events = Span<const ClipEvent>(source.events.Data(), source.events.Size());
    f32 rotation_error = -1;
    f32 translation_error = -1;
    REQUIRE(BuildClipBlob(s.types, source.id, input, &blob, 0, &rotation_error, &translation_error).Ok());
    CHECK(rotation_error <= input.limits.rotation_degrees);
    CHECK(translation_error <= input.limits.translation);

    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.Duration() == 2.0f);
    CHECK(view.Looping());
    CHECK(view.TrackCount() == 3);
    CHECK(view.TrackJoint(0) == 0);  // tracks are stored in joint order: root, arm, hand
    CHECK(view.TrackJoint(1) == 1);
    CHECK(view.TrackJoint(2) == 2);
    CHECK(view.RotationError() == rotation_error);
    CHECK(view.TranslationError() == translation_error);
    CHECK(view.Skeleton() == s.view.Id());
    CHECK(view.EventCount() == 2);
    CHECK(view.Event(0).time == 0.5f);
    CHECK(view.Event(0).payload == 7);
    CHECK(view.Event(0).name_hash == JointNameHash(test::Vp("footstep")));
    CHECK(view.Event(1).payload == 0);

    // The straight translation reduces to its two end keys; the constant scale holds no keys.
    CHECK(view.KeyCount(0, 0) == 2);
    CHECK(view.KeyCount(1, 1) < 61);
    CHECK(view.KeyCount(1, 1) >= 2);
    CHECK(view.IsConstant(2, 2));
    CHECK(view.KeyCount(2, 2) == 0);

    // Sampling follows the source within the recorded bounds.
    const ClipSample start = view.Sample(0, 0.0f);
    CHECK(start.has_translation);
    CHECK_FALSE(start.has_rotation);
    CHECK(start.translation[0] == 0.0f);
    const ClipSample mid = view.Sample(0, 1.0f);
    CHECK(Abs(mid.translation[0] - 0.5f) < 1e-3f);
    CHECK(Abs(mid.translation[2] - 1.0f) < 1e-3f);
    const ClipSample end = view.Sample(0, 99.0f);  // clamped
    CHECK(Abs(end.translation[2] - 0.0f) < 1e-3f);
    const ClipSample hand = view.Sample(2, 0.7f);
    CHECK(hand.has_scale);
    CHECK(hand.scale[1] == 1.5f);
    for (u32 i = 0; i <= 40; ++i)
    {
        const f64 time = 2.0 * i / 40.0;
        const ClipSample r = view.Sample(1, static_cast<f32>(time));
        const f64 angle = 0.5 * Sin(time * 3.0);
        const f64 dot = r.rotation[0] * 0.0 + r.rotation[1] * 0.0 + r.rotation[2] * Sin(angle * 0.5) +
                        r.rotation[3] * Cos(angle * 0.5);
        const f64 degrees = 2.0 * Acos(dot > 1.0 ? 1.0 : Abs(dot)) * 57.29577951308232;
        CHECK(degrees < 0.1);
    }
    CHECK(view.Bind(s.view).Ok());
}

TEST_CASE("clip: a channel of equal keys folds into the track table", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, ClpHeader());
    ClpAppend(&text, "track arm\nr 0 0 0 0 1\nr 1 0 0 0 1\nt 0 1 2 3\nt 2 1 2 3\n");
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.IsConstant(0, 0));
    CHECK(view.IsConstant(0, 1));
    CHECK(view.KeyCount(0, 0) == 0);
    const ClipSample sample = view.Sample(0, 1.3f);
    CHECK(sample.translation[1] == 2.0f);
    CHECK(sample.rotation[3] == 1.0f);
    CHECK_FALSE(sample.has_scale);
}

TEST_CASE("clip: a NaN or infinite sample time reads as the ends", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, ClpHeader());
    ClpAppend(&text, "track arm\nt 0 0 0 0\nt 2 1 0 0\n");
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.Sample(0, Limits<f32>::QuietNaN()).translation[0] == 0.0f);
    CHECK(view.Sample(0, Limits<f32>::Infinity()).translation[0] == 1.0f);
    CHECK(view.Sample(0, -Limits<f32>::Infinity()).translation[0] == 0.0f);
}

TEST_CASE("clip: a long channel of 20000 keys mints in bounded time and stays within the bounds", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, "id 00000000000000cc\nskeleton 00000000000000bb\nduration 200\ntrack arm\n");
    for (u32 i = 0; i < 20000; ++i)
    {
        char line[96];
        FormatTo(line, sizeof(line), "t %f %f %f 0\n", 0.01 * i, Sin(0.0005 * i), Cos(0.0007 * i));
        ClpAppend(&text, line);
    }
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.KeyCount(0, 0) < 20000);
    CHECK(view.TranslationError() <= ClipLimits().translation);
}

TEST_CASE("clip: keys on the sample-rate grid quantize exactly, however long the clip", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, "id 00000000000000cc\nskeleton 00000000000000bb\nduration 20000\nrate 1\ntrack arm\n");
    for (u32 i = 0; i <= 20000; i += 10)
    {
        char line[96];
        FormatTo(line, sizeof(line), "t %u %f %f 0\n", i, Sin(0.01 * i), Cos(0.013 * i));
        ClpAppend(&text, line);
    }
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.TranslationError() <= ClipLimits().translation);
    CHECK(Abs(view.Sample(0, 2000.0f).translation[0] - static_cast<f32>(Sin(0.01 * 2000))) < 1e-3f);

    SECTION("a key off the grid falls back to the finest unit, which a long clip may not afford")
    {
        Vector<char> off;
        ClpAppend(&off, "id 00000000000000cc\nskeleton 00000000000000bb\nduration 20000\nrate 1\ntrack arm\n");
        ClpAppend(&off, "t 0 0 0 0\nt 5000.5 1 0 0\nt 6000 0 0 0\nt 20000 1 0 0\n");
        Vector<byte> out;
        ClipSourceError error;
        CHECK(MintClip(ClpView(off), s.view, s.types, ClipLimits(), &out, &error).Code() == ErrorCode::LimitExceeded);
    }
}

TEST_CASE("clip: a clip with only events has no tracks", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, ClpHeader());
    ClpAppend(&text, "event 1 hit\n");
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.TrackCount() == 0);
    CHECK(view.EventCount() == 1);
}

TEST_CASE("clip: minting twice is byte-identical and the error is measured, not assumed", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    const Vector<char> text = ClpDenseSource();
    const Vector<byte> a = ClpMint(s, text);
    const Vector<byte> b = ClpMint(s, text);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);

    SECTION("limits no quantizer can meet are refused")
    {
        ClipLimits tight;
        tight.rotation_degrees = 1e-6f;
        tight.translation = 1e-9f;
        Vector<byte> out;
        ClipSourceError error;
        const Status status = MintClip(ClpView(text), s.view, s.types, tight, &out, &error);
        CHECK(status.Code() == ErrorCode::LimitExceeded);
    }
    SECTION("looser limits give a clip no larger")
    {
        ClipLimits loose;
        loose.rotation_degrees = 1.0f;
        loose.translation = 1e-2f;
        const Vector<byte> smaller = ClpMint(s, text, loose);
        CHECK(smaller.Size() <= a.Size());
    }
}

TEST_CASE("clip: binding checks the skeleton's id, hash and joint count", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    Vector<char> text;
    ClpAppend(&text, ClpHeader());
    ClpAppend(&text, "track arm\nt 0 0 0 0\nt 2 1 0 0\n");
    const Vector<byte> blob = ClpMint(s, text);
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), s.types).Ok());
    CHECK(view.Bind(s.view).Ok());

    SECTION("another skeleton id")
    {
        Vector<byte> other;
        SkeletonSourceError error;
        REQUIRE(MintSkeleton(test::Vp("id 00000000000000ee\n"
                                      "joint root - 0 0 0  0 0 0 1  1 1 1\n"
                                      "joint arm root 0 1 0  0 0 0 1  1 1 1\n"
                                      "joint hand arm 0 1 0  0 0 0 1  1 1 1\n"),
                             s.types, &other, &error)
                    .Ok());
        SkeletonView v;
        REQUIRE(v.Open(ByteSpan(other.Data(), other.Size()), s.types).Ok());
        CHECK(view.Bind(v).Failed());
    }
    SECTION("the same id with another hierarchy")
    {
        Vector<byte> other;
        SkeletonSourceError error;
        REQUIRE(MintSkeleton(test::Vp("id 00000000000000bb\n"
                                      "joint root - 0 0 0  0 0 0 1  1 1 1\n"
                                      "joint arm root 0 1 0  0 0 0 1  1 1 1\n"
                                      "joint hand root 0 1 0  0 0 0 1  1 1 1\n"),
                             s.types, &other, &error)
                    .Ok());
        SkeletonView v;
        REQUIRE(v.Open(ByteSpan(other.Data(), other.Size()), s.types).Ok());
        CHECK(view.Bind(v).Failed());
    }
}

TEST_CASE("clip: BuildClipBlob refuses what the minter would never produce", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    ClipTrackSource track;
    track.joint = 1;
    ClipKey3 k0;
    k0.time = 0;
    ClipKey3 k1;
    k1.time = 1;
    track.translation.Push(k0);
    track.translation.Push(k1);
    ClipBuildInput input;
    input.duration = 1;
    input.skeleton = s.view.Id();
    input.skeleton_hash = s.view.Hash();
    input.joint_count = 3;
    input.tracks = Span<const ClipTrackSource>(&track, 1);
    Vector<byte> out;
    REQUIRE(BuildClipBlob(s.types, test::Rid(9), input, &out).Ok());

    SECTION("a joint outside the skeleton")
    {
        track.joint = 3;
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
    SECTION("keys that do not increase")
    {
        track.translation[1].time = 0;
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
    SECTION("a key past the duration")
    {
        track.translation[1].time = 1.5f;
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
    SECTION("a non-finite key")
    {
        track.translation[1].value[0] = Limits<f32>::Infinity();
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
    SECTION("a zero duration, an unknown flag, no content")
    {
        input.duration = 0;
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
        input.duration = 1;
        input.flags = 8;
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
        input.flags = 0;
        input.tracks = Span<const ClipTrackSource>();
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
    SECTION("the same joint twice")
    {
        ClipTrackSource tracks[2];
        for (ClipTrackSource& t : tracks)
        {
            t.joint = 1;
            t.translation.Push(k0);
            t.translation.Push(k1);
        }
        input.tracks = Span<const ClipTrackSource>(tracks, 2);
        CHECK(BuildClipBlob(s.types, test::Rid(9), input, &out).Failed());
    }
}

TEST_CASE("clip: hostile blobs are refused", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    const Vector<byte> good = ClpMint(s, ClpDenseSource());
    REQUIRE(ClpOpen(s.types, good).Ok());

    SECTION("a header of the wrong size")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        header.Resize(header.Size() - 4);
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Code() == ErrorCode::Corrupt);
    }
    SECTION("a duration that is not finite or not positive")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        header[3] = 0x7f;  // the high byte of an f32: 0x7f.. with a mantissa is NaN or a huge number
        header[2] = 0xc0;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
        header[0] = header[1] = header[2] = header[3] = 0;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
    }
    SECTION("a key time scale that is zero, not finite or too fine")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        const byte original[4] = {header[72], header[73], header[74], header[75]};
        header[72] = header[73] = header[74] = header[75] = 0;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
        header[75] = 0x7f;
        header[74] = 0x80;  // +infinity
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
        header[75] = 0x47;
        header[74] = 0x00;  // 32768: a 2 s clip at that scale needs 65536 ticks of 16 bits
        header[73] = 0x00;
        header[72] = 0x00;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
        header[72] = original[0];
        header[73] = original[1];
        header[74] = original[2];
        header[75] = original[3];
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Ok());
    }
    SECTION("a flag this version does not define")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        header[8] = 0x10;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
    }
    SECTION("a reserved header field set")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        header[80] = 1;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
    }
    SECTION("counts that do not match the sections")
    {
        Vector<byte> header = ClpSectionCopy(s.types, good, kClipSectionHeader);
        header[16] = 4;  // 4 tracks for 3 joints
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
        header[16] = 3;
        header[20] = 0xff;
        header[21] = 0xff;
        header[22] = 0xff;
        header[23] = 0xff;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionHeader, header)).Failed());
    }
    SECTION("a track joint out of range or out of order")
    {
        Vector<byte> tracks = ClpSectionCopy(s.types, good, kClipSectionTracks);
        tracks[0] = 7;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
        tracks[0] = 1;  // track 0 and track 1 now both animate joint 1
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
    }
    SECTION("a channel both constant and keyed")
    {
        Vector<byte> tracks = ClpSectionCopy(s.types, good, kClipSectionTracks);
        tracks[4] |= 0x10;  // track 0 keyed translation, now also constant translation
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
    }
    SECTION("a key block outside the key section")
    {
        Vector<byte> tracks = ClpSectionCopy(s.types, good, kClipSectionTracks);
        tracks[52] = 0xfc;
        tracks[53] = 0xff;
        tracks[54] = 0xff;
        tracks[55] = 0x7f;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
    }
    SECTION("a misaligned key offset")
    {
        Vector<byte> tracks = ClpSectionCopy(s.types, good, kClipSectionTracks);
        tracks[52] = 2;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
    }
    SECTION("fewer than two keys in a keyed channel, or a huge count")
    {
        Vector<byte> tracks = ClpSectionCopy(s.types, good, kClipSectionTracks);
        tracks[48] = 1;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
        tracks[48] = 0xff;
        tracks[49] = 0xff;
        tracks[50] = 0xff;
        tracks[51] = 0xff;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionTracks, tracks)).Failed());
    }
    SECTION("key times that do not increase")
    {
        Vector<byte> keys = ClpSectionCopy(s.types, good, kClipSectionKeys);
        // The first block is track 0's translation: 24 bytes of min and extent, then the times.
        keys[24] = 0xff;
        keys[25] = 0xff;  // the first time is now 65535, past the second
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionKeys, keys)).Failed());
    }
    SECTION("a non-finite minimum")
    {
        Vector<byte> keys = ClpSectionCopy(s.types, good, kClipSectionKeys);
        keys[3] = 0x7f;
        keys[2] = 0x80;  // +infinity
        keys[1] = keys[0] = 0;
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionKeys, keys)).Failed());
    }
    SECTION("an event out of order")
    {
        Vector<byte> events = ClpSectionCopy(s.types, good, kClipSectionEvents);
        events[2] = 0x80;
        events[3] = 0x7f;  // the first event's time is +infinity
        CHECK(ClpOpen(s.types, ClpWithSection(s.types, good, kClipSectionEvents, events)).Failed());
    }
    SECTION("a dependency that is not a skeleton")
    {
        BlobReader reader;
        REQUIRE(reader.Open(ByteSpan(good.Data(), good.Size()), s.types).Ok());
        BlobWriter writer(s.types, types::kAnimation, reader.Id(), 0);
        Dependency d = reader.DependencyAt(0);
        d.role = 9;
        writer.AddDependency(d);
        for (u32 section = kFirstUserSection; section < kFirstUserSection + 4; ++section)
        {
            ByteSpan data;
            REQUIRE(reader.FindSection(section, &data));
            REQUIRE(writer.AddSection(section, data).Ok());
        }
        Vector<byte> out;
        REQUIRE(writer.Finish(&out).Ok());
        CHECK(ClpOpen(s.types, out).Failed());
    }
    SECTION("truncated blobs never open")
    {
        for (usize cut = 0; cut < good.Size(); cut += 37)
        {
            Vector<byte> part;
            part.Resize(cut);
            MemCopy(part.Data(), good.Data(), cut);
            CHECK(ClpOpen(s.types, part).Failed());
        }
    }
}

TEST_CASE("clip: a flipped byte never crashes the loader or the sampler", "[asset][clip]")
{
    ClpTestSkeleton s;
    ClpMakeSkeleton(&s);
    const Vector<byte> good = ClpMint(s, ClpDenseSource());
    test::Rng rng(0x5eed);
    for (u32 round = 0; round < 400; ++round)
    {
        Vector<byte> bad;
        bad.Resize(good.Size());
        MemCopy(bad.Data(), good.Data(), good.Size());
        bad[rng.Below(static_cast<u32>(bad.Size()))] ^= static_cast<byte>(1u << rng.Below(8));
        ClipView view;
        if (view.Open(ByteSpan(bad.Data(), bad.Size()), s.types).Ok())
        {
            for (u32 t = 0; t < view.TrackCount(); ++t)
            {
                for (u32 k = 0; k < 8; ++k)
                {
                    const ClipSample r = view.Sample(t, view.Duration() * static_cast<f32>(k) / 7.0f);
                    CHECK(r.has_translation == (view.KeyCount(t, 0) > 0 || view.IsConstant(t, 0)));
                }
            }
        }
    }
}

// glTF coverage Day 9: a clip of morph weights only has no skeleton and no dependency; its weights are sampled linearly
// between keys and clamped outside them.
TEST_CASE("clip: a weight track without a skeleton round-trips and samples linearly", "[asset][clip]")
{
    TypeRegistry types;
    const Array<f32, 3> times = {{0.0f, 1.0f, 2.0f}};
    const Array<f32, 6> values = {{0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f}};  // two weights per key
    ClipBuildInput input;
    input.duration = 2.0f;
    input.weights.target_count = 2;
    input.weights.times = Span<const f32>(times.Data(), times.Size());
    input.weights.values = Span<const f32>(values.Data(), values.Size());
    Vector<byte> blob;
    REQUIRE(BuildClipBlob(types, ResourceId::FromValue(0x71), input, &blob).Ok());
    ClipView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.JointCount() == 0);
    CHECK(view.TrackCount() == 0);
    CHECK(view.Skeleton().IsNone());
    CHECK(view.WeightTargetCount() == 2);
    CHECK(view.WeightKeyCount() == 3);
    Array<f32, 2> w = {{-1.0f, -1.0f}};
    view.SampleWeights(0.5f, w.Data(), 2);
    CHECK(w[0] == 0.5f);
    CHECK(w[1] == 0.5f);
    view.SampleWeights(5.0f, w.Data(), 2);  // past the last key: the last key
    CHECK(w[0] == 0.5f);
    CHECK(w[1] == 0.5f);
    const Array<f32, 3> backwards = {{0.0f, 2.0f, 1.0f}};
    input.weights.times = Span<const f32>(backwards.Data(), backwards.Size());
    CHECK(BuildClipBlob(types, ResourceId::FromValue(0x71), input, &blob).Failed());
}

}  // namespace engine_placeholder::asset
