// Sample conversions, interleaving and channel matrices (Phase 26 Day 416).
#include <catch2/catch_test_macros.hpp>

#include "audio/convert.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

TEST_CASE("integer to float scales", "[audio][convert]")
{
    CHECK(S16ToF32(-32768) == -1.0f);
    CHECK(S16ToF32(0) == 0.0f);
    CHECK(S16ToF32(16384) == 0.5f);
    CHECK(S16ToF32(32767) == 32767.0f / 32768.0f);
    CHECK(S24ToF32(-8388608) == -1.0f);
    CHECK(S24ToF32(4194304) == 0.5f);
    CHECK(S32ToF32(Limits<i32>::Min()) == -1.0f);
    CHECK(S32ToF32(0x40000000) == 0.5f);
    CHECK(U8ToF32(0) == -1.0f);
    CHECK(U8ToF32(128) == 0.0f);
    CHECK(U8ToF32(192) == 0.5f);
}

TEST_CASE("float to integer rounds halves away, clamps and silences NaN", "[audio][convert]")
{
    CHECK(F32ToS16(0.0f) == 0);
    CHECK(F32ToS16(1.0f) == 32767);
    CHECK(F32ToS16(-1.0f) == -32768);
    CHECK(F32ToS16(2.0f) == 32767);
    CHECK(F32ToS16(-7.0f) == -32768);
    CHECK(F32ToS16(Limits<f32>::Infinity()) == 32767);
    CHECK(F32ToS16(Limits<f32>::QuietNaN()) == 0);
    CHECK(F32ToS16(0.5f / 32768.0f) == 1);    // 0.5 rounds away from zero
    CHECK(F32ToS16(-0.5f / 32768.0f) == -1);  // and so does -0.5
    CHECK(F32ToS16(0.49f / 32768.0f) == 0);
    CHECK(F32ToS16(1.5f / 32768.0f) == 2);
    CHECK(F32ToS24(1.0f) == 8388607);
    CHECK(F32ToS24(-1.0f) == -8388608);
    CHECK(F32ToS32(1.0f) == Limits<i32>::Max());
    CHECK(F32ToS32(-1.0f) == Limits<i32>::Min());
    CHECK(F32ToS32(Limits<f32>::QuietNaN()) == 0);
    CHECK(F32ToU8(0.0f) == 128);
    CHECK(F32ToU8(1.0f) == 255);
    CHECK(F32ToU8(-1.0f) == 0);
    CHECK(F32ToU8(3.0f) == 255);
}

TEST_CASE("every 16-bit and 8-bit value round-trips exactly", "[audio][convert]")
{
    for (i32 v = -32768; v <= 32767; ++v)
    {
        REQUIRE(F32ToS16(S16ToF32(static_cast<i16>(v))) == v);
    }
    for (u32 v = 0; v < 256; ++v)
    {
        REQUIRE(F32ToU8(U8ToF32(static_cast<u8>(v))) == v);
    }
}

TEST_CASE("24-bit values round-trip exactly and 32-bit within the float's precision", "[audio][convert]")
{
    for (i32 v = -8388608; v <= 8388607; v += 977)
    {
        REQUIRE(F32ToS24(S24ToF32(v)) == v);
    }
    REQUIRE(F32ToS24(S24ToF32(8388607)) == 8388607);
    REQUIRE(F32ToS24(S24ToF32(-8388608)) == -8388608);
    const i32 samples[] = {0, 1, -1, 123456789, -987654321, 0x7FFFFF00, Limits<i32>::Min() + 5};
    for (i32 v : samples)
    {
        const i64 back = F32ToS32(S32ToF32(v));
        const i64 diff = back > v ? back - v : v - back;
        CHECK(diff <= 128);  // an f32 holds 24 bits of a 32-bit value
    }
}

TEST_CASE("bulk conversion of bytes: every format, both ways", "[audio][convert]")
{
    const f32 source[5] = {0.0f, 0.5f, -0.5f, 0.25f, -1.0f};
    const SampleFormat formats[] = {SampleFormat::U8, SampleFormat::S16, SampleFormat::S24, SampleFormat::S32,
                                    SampleFormat::F32};
    for (SampleFormat format : formats)
    {
        CAPTURE(static_cast<int>(format));
        byte bytes[5 * 4] = {};
        const usize size = 5 * BytesPerSample(format);
        REQUIRE(ConvertFromF32(format, Span<const f32>(source, 5), MutableByteSpan(bytes, size)));
        f32 back[5] = {};
        REQUIRE(ConvertToF32(format, ByteSpan(bytes, size), Span<f32>(back, 5)));
        for (u32 i = 0; i < 5; ++i)
        {
            CHECK(back[i] == source[i]);  // all five values are exact in every format
        }
    }
}

TEST_CASE("bulk conversion reads little-endian bytes and refuses a wrong size", "[audio][convert]")
{
    const byte s16[4] = {byte(0x00), byte(0x80), byte(0xFF), byte(0x7F)};  // -32768, 32767
    f32 out[2] = {};
    REQUIRE(ConvertToF32(SampleFormat::S16, ByteSpan(s16, 4), Span<f32>(out, 2)));
    CHECK(out[0] == -1.0f);
    CHECK(out[1] == 32767.0f / 32768.0f);

    const byte s24[3] = {byte(0x00), byte(0x00), byte(0x80)};  // -8388608
    f32 one[1] = {};
    REQUIRE(ConvertToF32(SampleFormat::S24, ByteSpan(s24, 3), Span<f32>(one, 1)));
    CHECK(one[0] == -1.0f);

    f32 untouched[2] = {7.0f, 7.0f};
    CHECK_FALSE(ConvertToF32(SampleFormat::S16, ByteSpan(s16, 3), Span<f32>(untouched, 2)));
    CHECK(untouched[0] == 7.0f);
    byte small[3] = {};
    CHECK_FALSE(ConvertFromF32(SampleFormat::S16, Span<const f32>(untouched, 2), MutableByteSpan(small, 3)));
}

TEST_CASE("interleave and deinterleave", "[audio][convert]")
{
    const f32 planar[6] = {1, 2, 3, 10, 20, 30};  // two channels, three frames
    f32 interleaved[6] = {};
    REQUIRE(Interleave(Span<const f32>(planar, 6), Span<f32>(interleaved, 6), 2));
    const f32 expected[6] = {1, 10, 2, 20, 3, 30};
    for (u32 i = 0; i < 6; ++i)
    {
        CHECK(interleaved[i] == expected[i]);
    }
    f32 again[6] = {};
    REQUIRE(Deinterleave(Span<const f32>(interleaved, 6), Span<f32>(again, 6), 2));
    for (u32 i = 0; i < 6; ++i)
    {
        CHECK(again[i] == planar[i]);
    }
    CHECK_FALSE(Interleave(Span<const f32>(planar, 6), Span<f32>(interleaved, 6), 4));  // 6 is not a multiple of 4
    CHECK_FALSE(Interleave(Span<const f32>(planar, 6), Span<f32>(interleaved, 6), 0));
    CHECK_FALSE(Interleave(Span<const f32>(planar, 6), Span<f32>(interleaved, 5), 2));
}

TEST_CASE("mono to stereo copies and stereo to mono averages", "[audio][convert]")
{
    const f32 mono[2] = {0.25f, -0.5f};
    f32 stereo[4] = {};
    REQUIRE(ApplyChannelMatrix(BuildChannelMatrix(ChannelLayout::Mono, ChannelLayout::Stereo), Span<const f32>(mono, 2),
                               Span<f32>(stereo, 4), 2));
    CHECK(stereo[0] == 0.25f);
    CHECK(stereo[1] == 0.25f);
    CHECK(stereo[2] == -0.5f);
    CHECK(stereo[3] == -0.5f);

    const f32 two[4] = {1.0f, 0.5f, -0.25f, 0.25f};
    f32 averaged[2] = {};
    REQUIRE(ApplyChannelMatrix(BuildChannelMatrix(ChannelLayout::Stereo, ChannelLayout::Mono), Span<const f32>(two, 4),
                               Span<f32>(averaged, 2), 2));
    CHECK(averaged[0] == 0.75f);
    CHECK(averaged[1] == 0.0f);
}

TEST_CASE("5.1 to stereo uses the ITU coefficients", "[audio][convert]")
{
    // FL, FR, FC, LFE, BL, BR
    const f32 frame[6] = {1.0f, 0.0f, 0.5f, 1.0f, 0.25f, 0.125f};
    f32 out[2] = {};
    const ChannelMatrix m = BuildChannelMatrix(ChannelLayout::Surround51, ChannelLayout::Stereo);
    REQUIRE(ApplyChannelMatrix(m, Span<const f32>(frame, 6), Span<f32>(out, 2), 1));
    const f32 a = kDownmixGain;
    CHECK(out[0] == 1.0f + a * 0.5f + a * 0.25f);  // the LFE is dropped
    CHECK(out[1] == 0.0f + a * 0.5f + a * 0.125f);
    CHECK(m.gain[0][3] == 0.0f);
    CHECK(m.gain[1][3] == 0.0f);

    f32 mono[1] = {};
    REQUIRE(ApplyChannelMatrix(BuildChannelMatrix(ChannelLayout::Surround51, ChannelLayout::Mono),
                               Span<const f32>(frame, 6), Span<f32>(mono, 1), 1));
    CHECK(Abs(mono[0] - 0.5f * (out[0] + out[1])) < 1.0e-6f);
}

TEST_CASE("other layout conversions", "[audio][convert]")
{
    // Identity.
    const ChannelMatrix identity = BuildChannelMatrix(ChannelLayout::Quad, ChannelLayout::Quad);
    for (u32 o = 0; o < 4; ++o)
    {
        for (u32 i = 0; i < 4; ++i)
        {
            CHECK(identity.gain[o][i] == (o == i ? 1.0f : 0.0f));
        }
    }
    // 7.1 to 5.1: the sides fold into the backs.
    const ChannelMatrix to51 = BuildChannelMatrix(ChannelLayout::Surround71, ChannelLayout::Surround51);
    CHECK(to51.gain[4][4] == 1.0f);
    CHECK(to51.gain[4][6] == kDownmixGain);
    CHECK(to51.gain[5][7] == kDownmixGain);
    CHECK(to51.gain[3][3] == 1.0f);
    // 5.1 to quad: the center goes to both fronts, the LFE is dropped.
    const ChannelMatrix to_quad = BuildChannelMatrix(ChannelLayout::Surround51, ChannelLayout::Quad);
    CHECK(to_quad.gain[0][2] == kDownmixGain);
    CHECK(to_quad.gain[1][2] == kDownmixGain);
    CHECK(to_quad.gain[2][4] == 1.0f);
    // Stereo to 5.1 leaves the other channels silent.
    const ChannelMatrix up = BuildChannelMatrix(ChannelLayout::Stereo, ChannelLayout::Surround51);
    CHECK(up.gain[0][0] == 1.0f);
    CHECK(up.gain[1][1] == 1.0f);
    CHECK(up.gain[2][0] == 0.0f);
    // Mono to mono.
    CHECK(BuildChannelMatrix(ChannelLayout::Mono, ChannelLayout::Mono).gain[0][0] == 1.0f);
}

TEST_CASE("a matrix refuses buffers that are too small", "[audio][convert]")
{
    const ChannelMatrix m = BuildChannelMatrix(ChannelLayout::Mono, ChannelLayout::Stereo);
    const f32 in[2] = {1.0f, 2.0f};
    f32 out[3] = {};
    CHECK_FALSE(ApplyChannelMatrix(m, Span<const f32>(in, 2), Span<f32>(out, 3), 2));
    CHECK_FALSE(ApplyChannelMatrix(m, Span<const f32>(in, 1), Span<f32>(out, 3), 2));
}
