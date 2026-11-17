// The sample formats, channel layouts and format validation (Phase 26 Day 416).
#include <catch2/catch_test_macros.hpp>

#include "audio/format.h"
#include "string/string.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

TEST_CASE("sample format sizes", "[audio][format]")
{
    CHECK(BytesPerSample(SampleFormat::U8) == 1);
    CHECK(BytesPerSample(SampleFormat::S16) == 2);
    CHECK(BytesPerSample(SampleFormat::S24) == 3);
    CHECK(BytesPerSample(SampleFormat::S32) == 4);
    CHECK(BytesPerSample(SampleFormat::F32) == 4);
}

TEST_CASE("layout channel counts and names", "[audio][format]")
{
    CHECK(ChannelCount(ChannelLayout::Mono) == 1);
    CHECK(ChannelCount(ChannelLayout::Stereo) == 2);
    CHECK(ChannelCount(ChannelLayout::Quad) == 4);
    CHECK(ChannelCount(ChannelLayout::Surround51) == 6);
    CHECK(ChannelCount(ChannelLayout::Surround71) == 8);

    const char* const stereo[] = {"FL", "FR"};
    const char* const quad[] = {"FL", "FR", "BL", "BR"};
    const char* const surround51[] = {"FL", "FR", "FC", "LFE", "BL", "BR"};
    const char* const surround71[] = {"FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR"};
    CHECK(StringView(ChannelName(ChannelLayout::Mono, 0)) == StringView("FC"));
    for (u32 i = 0; i < 2; ++i)
    {
        CHECK(StringView(ChannelName(ChannelLayout::Stereo, i)) == StringView(stereo[i]));
    }
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(StringView(ChannelName(ChannelLayout::Quad, i)) == StringView(quad[i]));
    }
    for (u32 i = 0; i < 6; ++i)
    {
        CHECK(StringView(ChannelName(ChannelLayout::Surround51, i)) == StringView(surround51[i]));
    }
    for (u32 i = 0; i < 8; ++i)
    {
        CHECK(StringView(ChannelName(ChannelLayout::Surround71, i)) == StringView(surround71[i]));
    }
    CHECK(StringView(LayoutName(ChannelLayout::Surround51)) == StringView("5.1"));
    CHECK(StringView(LayoutName(ChannelLayout::Mono)) == StringView("mono"));
}

TEST_CASE("no layout has more than kMaxChannels", "[audio][format]")
{
    const ChannelLayout layouts[] = {ChannelLayout::Mono, ChannelLayout::Stereo, ChannelLayout::Quad,
                                     ChannelLayout::Surround51, ChannelLayout::Surround71};
    for (ChannelLayout layout : layouts)
    {
        CHECK(ChannelCount(layout) <= kMaxChannels);
    }
}

TEST_CASE("format validation limits", "[audio][format]")
{
    AudioFormat format;
    CHECK(IsValid(format));

    format.sample_rate = kMinSampleRate;
    CHECK(IsValid(format));
    format.sample_rate = kMinSampleRate - 1;
    CHECK_FALSE(IsValid(format));
    format.sample_rate = kMaxSampleRate;
    CHECK(IsValid(format));
    format.sample_rate = kMaxSampleRate + 1;
    CHECK_FALSE(IsValid(format));
    format.sample_rate = 0;
    CHECK_FALSE(IsValid(format));

    AudioFormat bad_layout;
    bad_layout.layout = static_cast<ChannelLayout>(99);
    CHECK_FALSE(IsValid(bad_layout));
    AudioFormat bad_sample;
    bad_sample.format = static_cast<SampleFormat>(99);
    CHECK_FALSE(IsValid(bad_sample));
}

TEST_CASE("frame bytes", "[audio][format]")
{
    AudioFormat format;
    format.layout = ChannelLayout::Surround51;
    format.format = SampleFormat::S24;
    CHECK(FrameBytes(format) == 18);
    format.layout = ChannelLayout::Stereo;
    format.format = SampleFormat::F32;
    CHECK(FrameBytes(format) == 8);
}
