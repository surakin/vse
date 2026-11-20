#include "audio/format.h"

#include "log/assert.h"

namespace vse::audio
{

namespace
{

constexpr Speaker kMono[] = {Speaker::FrontCenter};
constexpr Speaker kStereo[] = {Speaker::FrontLeft, Speaker::FrontRight};
constexpr Speaker kQuad[] = {Speaker::FrontLeft, Speaker::FrontRight, Speaker::BackLeft, Speaker::BackRight};
constexpr Speaker k51[] = {Speaker::FrontLeft, Speaker::FrontRight, Speaker::FrontCenter,
                           Speaker::Lfe,       Speaker::BackLeft,   Speaker::BackRight};
constexpr Speaker k71[] = {Speaker::FrontLeft, Speaker::FrontRight, Speaker::FrontCenter, Speaker::Lfe,
                           Speaker::BackLeft,  Speaker::BackRight,  Speaker::SideLeft,    Speaker::SideRight};

}  // namespace

u32 BytesPerSample(SampleFormat format)
{
    switch (format)
    {
    case SampleFormat::U8: return 1;
    case SampleFormat::S16: return 2;
    case SampleFormat::S24: return 3;
    case SampleFormat::S32:
    case SampleFormat::F32: return 4;
    }
    return 0;
}

u32 ChannelCount(ChannelLayout layout)
{
    switch (layout)
    {
    case ChannelLayout::Mono: return 1;
    case ChannelLayout::Stereo: return 2;
    case ChannelLayout::Quad: return 4;
    case ChannelLayout::Surround51: return 6;
    case ChannelLayout::Surround71: return 8;
    }
    return 0;
}

Speaker SpeakerOf(ChannelLayout layout, u32 channel)
{
    ENGINE_ASSERT(channel < ChannelCount(layout), "SpeakerOf: channel out of range");
    switch (layout)
    {
    case ChannelLayout::Mono: return kMono[channel];
    case ChannelLayout::Stereo: return kStereo[channel];
    case ChannelLayout::Quad: return kQuad[channel];
    case ChannelLayout::Surround51: return k51[channel];
    case ChannelLayout::Surround71: return k71[channel];
    }
    return Speaker::FrontCenter;
}

const char* SpeakerName(Speaker speaker)
{
    switch (speaker)
    {
    case Speaker::FrontLeft: return "FL";
    case Speaker::FrontRight: return "FR";
    case Speaker::FrontCenter: return "FC";
    case Speaker::Lfe: return "LFE";
    case Speaker::BackLeft: return "BL";
    case Speaker::BackRight: return "BR";
    case Speaker::SideLeft: return "SL";
    case Speaker::SideRight: return "SR";
    }
    return "?";
}

const char* ChannelName(ChannelLayout layout, u32 channel)
{
    return SpeakerName(SpeakerOf(layout, channel));
}

const char* LayoutName(ChannelLayout layout)
{
    switch (layout)
    {
    case ChannelLayout::Mono: return "mono";
    case ChannelLayout::Stereo: return "stereo";
    case ChannelLayout::Quad: return "quad";
    case ChannelLayout::Surround51: return "5.1";
    case ChannelLayout::Surround71: return "7.1";
    }
    return "?";
}

u32 FrameBytes(const AudioFormat& format)
{
    return BytesPerSample(format.format) * ChannelCount(format.layout);
}

bool IsValid(const AudioFormat& format)
{
    return format.sample_rate >= kMinSampleRate && format.sample_rate <= kMaxSampleRate &&
           ChannelCount(format.layout) != 0 && BytesPerSample(format.format) != 0;
}

}  // namespace vse::audio
