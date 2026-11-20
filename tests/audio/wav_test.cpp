// The WAV parser, decoder and reader (Phase 26 Day 417): every format against a known ramp, the extensible form, loops
// and cues, padding, truncation, hostile bytes.
#include <catch2/catch_test_macros.hpp>

#include "audio/wav.h"
#include "lang/limits.h"
#include "wav_builder.h"

using namespace vse;
using namespace vse::audio;
using namespace audio_test;

namespace
{

const WavEncoding kAllEncodings[] = {WavEncoding::U8,  WavEncoding::S16, WavEncoding::S24,
                                     WavEncoding::S32, WavEncoding::F32, WavEncoding::F64};

}  // namespace

TEST_CASE("wav: every encoding decodes the ramp exactly", "[audio][wav]")
{
    for (WavEncoding encoding : kAllEncodings)
    {
        for (u32 channels : {1u, 2u})
        {
            CAPTURE(static_cast<int>(encoding), channels);
            WavSpec spec;
            spec.encoding = encoding;
            spec.channels = channels;
            spec.frames = 90;
            const Vector<byte> file = BuildWav(spec);
            AudioClipPtr clip;
            WavInfo info;
            REQUIRE(DecodeWav(Bytes(file), "ramp", &clip, &info) == WavStatus::Ok);
            CHECK(info.encoding == encoding);
            CHECK(info.frame_count == 90);
            CHECK(info.format.sample_rate == 48000);
            CHECK_FALSE(info.truncated);
            const ClipView view = clip->View();
            REQUIRE(view.samples.Size() == 90u * channels);
            for (u32 i = 0; i < view.samples.Size(); ++i)
            {
                REQUIRE(view.samples[i] == RampValue(i));
            }
        }
    }
}

TEST_CASE("wav: extensible agrees with plain, and the mask and layout are read", "[audio][wav]")
{
    WavSpec plain;
    plain.encoding = WavEncoding::S24;
    plain.channels = 6;
    plain.frames = 40;
    WavSpec extensible = plain;
    extensible.extensible = true;
    extensible.channel_mask = 0x3F;
    AudioClipPtr a;
    AudioClipPtr b;
    WavInfo info;
    REQUIRE(DecodeWav(Bytes(BuildWav(plain)), "", &a, nullptr) == WavStatus::Ok);
    REQUIRE(DecodeWav(Bytes(BuildWav(extensible)), "", &b, &info) == WavStatus::Ok);
    CHECK(info.channel_mask == 0x3F);
    CHECK(info.format.layout == ChannelLayout::Surround51);
    REQUIRE(a->View().samples.Size() == b->View().samples.Size());
    for (usize i = 0; i < a->View().samples.Size(); ++i)
    {
        REQUIRE(a->View().samples[i] == b->View().samples[i]);
    }
}

TEST_CASE("wav: layouts and rates", "[audio][wav]")
{
    const u32 channels[] = {1, 2, 4, 6, 8};
    const ChannelLayout layouts[] = {ChannelLayout::Mono, ChannelLayout::Stereo, ChannelLayout::Quad,
                                     ChannelLayout::Surround51, ChannelLayout::Surround71};
    for (u32 i = 0; i < 5; ++i)
    {
        WavSpec spec;
        spec.channels = channels[i];
        spec.rate = 44100;
        WavInfo info;
        REQUIRE(ParseWav(Bytes(BuildWav(spec)), &info) == WavStatus::Ok);
        CHECK(info.format.layout == layouts[i]);
        CHECK(info.format.sample_rate == 44100);
    }
    WavSpec three;
    three.channels = 3;
    WavInfo info;
    CHECK(ParseWav(Bytes(BuildWav(three)), &info) == WavStatus::Unsupported);
    WavSpec slow;
    slow.rate = 4000;
    CHECK(ParseWav(Bytes(BuildWav(slow)), &info) == WavStatus::Unsupported);
}

TEST_CASE("wav: the loop chunk and the cues are read", "[audio][wav]")
{
    WavSpec spec;
    spec.with_loop = true;
    spec.loop_start = 10;
    spec.loop_end_inclusive = 49;
    spec.cue_count = 3;
    AudioClipPtr clip;
    WavInfo info;
    REQUIRE(DecodeWav(Bytes(BuildWav(spec)), "loop", &clip, &info) == WavStatus::Ok);
    CHECK(info.has_loop);
    CHECK(info.loop_start == 10);
    CHECK(info.loop_end == 50);  // the file's end point is inclusive
    REQUIRE(info.cues.Size() == 3);
    CHECK(info.cues[2].id == 3);
    CHECK(info.cues[2].frame == 14);
    CHECK(clip->Desc().loop_start == 10);
    CHECK(clip->Desc().loop_end == 50);

    spec.loop_end_inclusive = 500;  // past the data: ignored, not an error
    REQUIRE(DecodeWav(Bytes(BuildWav(spec)), "", &clip, &info) == WavStatus::Ok);
    CHECK_FALSE(info.has_loop);
    CHECK(info.ignored_metadata);
    CHECK(clip->Desc().loop_end == 0);
}

TEST_CASE("wav: odd chunk sizes are padded and unknown chunks skipped", "[audio][wav]")
{
    WavSpec spec;
    spec.encoding = WavEncoding::U8;  // 99 frames of one byte: an odd data chunk
    spec.frames = 99;
    spec.extra_chunk_bytes = 7;  // an odd unknown chunk before data
    spec.with_loop = true;       // a chunk after the padded data
    AudioClipPtr clip;
    WavInfo info;
    REQUIRE(DecodeWav(Bytes(BuildWav(spec)), "", &clip, &info) == WavStatus::Ok);
    CHECK(info.frame_count == 99);
    CHECK(info.has_loop);
    for (u32 i = 0; i < 99; ++i)
    {
        REQUIRE(clip->View().samples[i] == RampValue(i));
    }
}

TEST_CASE("wav: a data chunk larger than the file uses the bytes present", "[audio][wav]")
{
    WavSpec spec;
    spec.frames = 100;
    Vector<byte> file = BuildWav(spec);
    while (file.Size() > 44 + 2 * 60 + 1)  // cut to 60 whole frames and one stray byte
    {
        file.Pop();
    }
    WavInfo info;
    REQUIRE(ParseWav(Bytes(file), &info) == WavStatus::Ok);
    CHECK(info.truncated);
    CHECK(info.frame_count == 60);
    CHECK(info.data_bytes == 120);
}

TEST_CASE("wav: refusals", "[audio][wav]")
{
    WavInfo info;
    CHECK(ParseWav(ByteSpan(), &info) == WavStatus::TooShort);
    Vector<byte> file = BuildWav(WavSpec());
    Vector<byte> bad = BuildWav(WavSpec());
    bad[0] = byte('X');
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::NotRiff);
    bad = BuildWav(WavSpec());
    bad[8] = byte('X');
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::NotWave);
    bad = BuildWav(WavSpec());
    bad[0] = byte('R');
    bad[1] = byte('F');
    bad[2] = byte('6');
    bad[3] = byte('4');
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::Unsupported);

    bad = BuildWav(WavSpec());
    bad[12] = byte('x');  // the fmt tag: no fmt chunk any more
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::NoFmt);
    bad = BuildWav(WavSpec());
    bad[36] = byte('x');  // the data tag
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::NoData);
    bad = BuildWav(WavSpec());
    bad[32] = byte(7);  // block align
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::BadFmt);
    bad = BuildWav(WavSpec());
    bad[20] = byte(2);  // format tag 2 (ADPCM)
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::Unsupported);
    bad = BuildWav(WavSpec());
    bad[16] = byte(0xFF);  // an oversized fmt chunk
    bad[17] = byte(0xFF);
    bad[18] = byte(0xFF);
    bad[19] = byte(0x7F);
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::BadChunk);
    bad = BuildWav(WavSpec());
    bad[40] = byte(0);  // the data size: zero bytes of data
    bad[41] = byte(0);
    bad[42] = byte(0);
    bad[43] = byte(0);
    CHECK(ParseWav(Bytes(bad), &info) == WavStatus::BadFrameCount);
}

TEST_CASE("wav: NaN samples are refused", "[audio][wav]")
{
    WavSpec spec;
    spec.encoding = WavEncoding::F32;
    spec.frames = 10;
    Vector<byte> file = BuildWav(spec);
    const u32 nan_bits = 0x7FC00000u;
    for (u32 i = 0; i < 4; ++i)
    {
        file[44 + 4 * 3 + i] = static_cast<byte>((nan_bits >> (8 * i)) & 0xFF);
    }
    AudioClipPtr clip;
    CHECK(DecodeWav(Bytes(file), "", &clip, nullptr) == WavStatus::BadSamples);
    CHECK_FALSE(clip);
}

TEST_CASE("wav reader: frames in order, seek and the end", "[audio][wav]")
{
    WavSpec spec;
    spec.encoding = WavEncoding::S24;
    spec.channels = 2;
    spec.frames = 50;
    const Vector<byte> file = BuildWav(spec);
    WavReader reader;
    REQUIRE(reader.Open(Bytes(file)) == WavStatus::Ok);
    f32 out[40] = {};
    REQUIRE(reader.ReadFrames(Span<f32>(out, 40), 20) == 20);
    for (u32 i = 0; i < 40; ++i)
    {
        REQUIRE(out[i] == RampValue(i));
    }
    CHECK(reader.Position() == 20);
    REQUIRE(reader.Seek(45));
    CHECK(reader.ReadFrames(Span<f32>(out, 40), 20) == 5);
    CHECK(out[0] == RampValue(90));
    CHECK(reader.ReadFrames(Span<f32>(out, 40), 20) == 0);  // at the end
    CHECK_FALSE(reader.Seek(51));
    CHECK(reader.Position() == 50);
    REQUIRE(reader.Seek(0));
    CHECK(reader.ReadFrames(Span<f32>(out, 10), 20) == 0);  // an output too small for the request
}

TEST_CASE("wav: a file cut at every length never crashes and never lies", "[audio][wav][fuzz]")
{
    WavSpec spec;
    spec.encoding = WavEncoding::S16;
    spec.channels = 2;
    spec.frames = 12;
    spec.with_loop = true;
    spec.cue_count = 2;
    spec.extra_chunk_bytes = 5;
    const Vector<byte> file = BuildWav(spec);
    for (usize length = 0; length <= file.Size(); ++length)
    {
        WavInfo info;
        const WavStatus status = ParseWav(ByteSpan(file.Data(), length), &info);
        if (status == WavStatus::Ok)
        {
            REQUIRE(info.data_offset + info.data_bytes <= length);
            REQUIRE(info.frame_count * info.frame_bytes == info.data_bytes);
            REQUIRE(info.frame_count > 0);
        }
    }
}

TEST_CASE("wav: mutated files never crash and never lie", "[audio][wav][fuzz]")
{
    WavSpec spec;
    spec.encoding = WavEncoding::S24;
    spec.channels = 2;
    spec.frames = 10;
    spec.with_loop = true;
    spec.cue_count = 2;
    spec.extensible = true;
    spec.channel_mask = 3;
    const Vector<byte> good = BuildWav(spec);
    u64 state = 0x9E3779B97F4A7C15ull;  // a fixed seed
    for (u32 round = 0; round < 6000; ++round)
    {
        Vector<byte> file;
        for (usize i = 0; i < good.Size(); ++i)
        {
            file.Push(good[i]);
        }
        const u32 changes = 1 + (round % 4);
        for (u32 c = 0; c < changes; ++c)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const usize at = static_cast<usize>((state >> 33) % file.Size());
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            file[at] = static_cast<byte>(state >> 56);
        }
        WavInfo info;
        const WavStatus status = ParseWav(Bytes(file), &info);
        if (status == WavStatus::Ok)
        {
            REQUIRE(info.data_offset + info.data_bytes <= file.Size());
            REQUIRE(info.frame_count * info.frame_bytes == info.data_bytes);
            REQUIRE(info.frame_count <= kMaxWavFrames);
            if (info.has_loop)
            {
                REQUIRE(info.loop_start < info.loop_end);
                REQUIRE(info.loop_end <= info.frame_count);
            }
            AudioClipPtr clip;
            const WavStatus decoded = DecodeWav(Bytes(file), "", &clip, nullptr);
            REQUIRE((decoded == WavStatus::Ok || decoded == WavStatus::BadSamples));
        }
    }
}
