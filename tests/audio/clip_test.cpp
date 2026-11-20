// The audio clip and its validated view (Phase 26 Day 416).
#include <catch2/catch_test_macros.hpp>

#include "audio/clip.h"
#include "containers/vector.h"
#include "lang/limits.h"

using namespace vse;
using namespace vse::audio;

namespace
{

ClipDesc StereoDesc(u64 frames)
{
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Stereo;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    return desc;
}

Vector<f32> Ramp(usize count)
{
    Vector<f32> samples;
    for (usize i = 0; i < count; ++i)
    {
        samples.Push(static_cast<f32>(i) * 0.001f);
    }
    return samples;
}

}  // namespace

TEST_CASE("clip: made from samples, read through the view", "[audio][clip]")
{
    const Vector<f32> samples = Ramp(200);
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(StereoDesc(100), Span<const f32>(samples.Data(), samples.Size()), "ramp", &clip) ==
            ClipStatus::Ok);
    REQUIRE(clip);
    CHECK(clip->Frames() == 100);
    CHECK(clip->Name() == StringView("ramp"));
    CHECK(clip->ByteSize() == 800);
    CHECK(clip->Duration() == 100.0 / 48000.0);
    CHECK(clip->Format().format == SampleFormat::F32);

    const ClipView view = clip->View();
    CHECK(view.Frames() == 100);
    CHECK(view.Channels() == 2);
    CHECK(view.samples.Size() == 200);
    CHECK(view.Frame(7)[0] == samples[14]);
    CHECK(view.Frame(7)[1] == samples[15]);
    CHECK_FALSE(view.HasLoop());
    CHECK(view.Duration() == clip->Duration());
}

TEST_CASE("clip: a view over shared bytes at an offset shares the storage", "[audio][clip]")
{
    const Vector<f32> samples = Ramp(64);  // 16 bytes of header, then 30 stereo frames
    Vector<byte> bytes;
    for (u32 i = 0; i < 16; ++i)
    {
        bytes.Push(byte(0));
    }
    const byte* raw = reinterpret_cast<const byte*>(samples.Data());
    for (usize i = 0; i < 64 * sizeof(f32); ++i)
    {
        bytes.Push(raw[i]);
    }
    SharedBytesPtr shared = CopyShared(bytes);
    AudioClipPtr a;
    AudioClipPtr b;
    REQUIRE(AudioClip::Create(StereoDesc(30), shared, 16, "a", &a) == ClipStatus::Ok);
    REQUIRE(AudioClip::Create(StereoDesc(10), shared, 16 + 8 * 20, "b", &b) == ClipStatus::Ok);
    CHECK(a->Bytes().Get() == b->Bytes().Get());
    CHECK(a->Offset() == 16);
    CHECK(a->View().Frame(0)[0] == samples[0]);
    CHECK(b->View().Frame(0)[0] == samples[40]);  // 20 frames in, 2 samples each
}

TEST_CASE("clip: validation refuses what the module cannot play", "[audio][clip]")
{
    const Vector<f32> samples = Ramp(200);
    const Span<const f32> all(samples.Data(), samples.Size());
    AudioClipPtr clip;

    ClipDesc not_float = StereoDesc(100);
    not_float.format.format = SampleFormat::S16;
    CHECK(AudioClip::CreateCopy(not_float, all, "", &clip) == ClipStatus::BadFormat);
    ClipDesc bad_rate = StereoDesc(100);
    bad_rate.format.sample_rate = 100;
    CHECK(AudioClip::CreateCopy(bad_rate, all, "", &clip) == ClipStatus::BadFormat);
    CHECK(AudioClip::CreateCopy(StereoDesc(0), all, "", &clip) == ClipStatus::BadFrameCount);
    CHECK(AudioClip::CreateCopy(StereoDesc(kMaxClipFrames + 1), all, "", &clip) == ClipStatus::BadFrameCount);
    CHECK(AudioClip::CreateCopy(StereoDesc(101), all, "", &clip) == ClipStatus::TooShort);
    CHECK_FALSE(clip);

    ClipDesc loop = StereoDesc(100);
    loop.loop_start = 10;
    loop.loop_end = 90;
    REQUIRE(AudioClip::CreateCopy(loop, all, "", &clip) == ClipStatus::Ok);
    CHECK(clip->View().HasLoop());
    loop.loop_end = 101;
    CHECK(AudioClip::CreateCopy(loop, all, "", &clip) == ClipStatus::BadLoop);
    loop.loop_start = 90;
    loop.loop_end = 90;
    CHECK(AudioClip::CreateCopy(loop, all, "", &clip) == ClipStatus::BadLoop);
    loop.loop_start = 5;
    loop.loop_end = 0;  // a start without an end
    CHECK(AudioClip::CreateCopy(loop, all, "", &clip) == ClipStatus::BadLoop);
}

TEST_CASE("clip: NaN and infinity are refused, a misaligned or short span too", "[audio][clip]")
{
    Vector<f32> samples = Ramp(200);
    AudioClipPtr clip;
    samples[50] = Limits<f32>::QuietNaN();
    CHECK(AudioClip::CreateCopy(StereoDesc(100), Span<const f32>(samples.Data(), samples.Size()), "", &clip) ==
          ClipStatus::NotFinite);
    samples[50] = Limits<f32>::Infinity();
    CHECK(AudioClip::CreateCopy(StereoDesc(100), Span<const f32>(samples.Data(), samples.Size()), "", &clip) ==
          ClipStatus::NotFinite);

    const Vector<f32> good = Ramp(200);
    ClipView view;
    const byte* raw = reinterpret_cast<const byte*>(good.Data());
    CHECK(MakeClipView(StereoDesc(100), ByteSpan(raw + 1, 800), &view) == ClipStatus::Misaligned);
    CHECK(MakeClipView(StereoDesc(100), ByteSpan(raw, 799), &view) == ClipStatus::TooShort);
    CHECK(MakeClipView(StereoDesc(100), ByteSpan(raw, 800), &view) == ClipStatus::Ok);
    CHECK(view.Frames() == 100);

    SharedBytesPtr none;
    CHECK(AudioClip::Create(StereoDesc(1), none, 0, "", &clip) == ClipStatus::TooShort);
    SharedBytesPtr shared = CopyShared(ByteSpan(raw, 800));
    CHECK(AudioClip::Create(StereoDesc(100), shared, 801, "", &clip) == ClipStatus::TooShort);
    CHECK(AudioClip::Create(StereoDesc(99), shared, 2, "", &clip) == ClipStatus::Misaligned);
}

TEST_CASE("clip: a clip keeps its bytes alive after the caller lets go", "[audio][clip]")
{
    AudioClipPtr clip;
    {
        const Vector<f32> samples = Ramp(200);
        REQUIRE(AudioClip::CreateCopy(StereoDesc(100), Span<const f32>(samples.Data(), samples.Size()), "kept",
                                      &clip) == ClipStatus::Ok);
    }
    AudioClipConstPtr other = clip;
    clip = AudioClipPtr();
    CHECK(other->View().Frame(99)[1] == 199 * 0.001f);
}
