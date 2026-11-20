#include "audio/pcm_negotiate.h"

#include "audio/convert.h"

namespace vse::audio
{

namespace
{

u32 ClampU32(u32 value, u32 low, u32 high)
{
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

DeviceStatus NegotiatePcm(const PcmCaps& caps, u32 want_rate, u32 want_channels, u32 want_period, PcmGrant* out)
{
    if (caps.rate_min == 0 || caps.rate_max < caps.rate_min || caps.channels_min == 0 ||
        caps.channels_max < caps.channels_min || caps.period_min == 0 || caps.period_max < caps.period_min ||
        caps.periods_min == 0 || caps.periods_max < caps.periods_min || caps.buffer_max < caps.buffer_min)
    {
        return DeviceStatus::Unsupported;
    }
    PcmGrant grant;
    if (caps.has_f32)
    {
        grant.format = PcmFormat::F32;
    }
    else if (caps.has_s32)
    {
        grant.format = PcmFormat::S32;
    }
    else if (caps.has_s16)
    {
        grant.format = PcmFormat::S16;
    }
    else
    {
        return DeviceStatus::Unsupported;
    }
    grant.rate = ClampU32(want_rate == 0 ? 48000 : want_rate, caps.rate_min, caps.rate_max);
    grant.channels = ClampU32(want_channels, caps.channels_min, caps.channels_max);
    grant.period_frames = ClampU32(want_period, caps.period_min, caps.period_max);
    // Three periods when the card allows it; the whole buffer must fit the card's range.
    u32 periods = ClampU32(3, caps.periods_min < 2 ? 2 : caps.periods_min, caps.periods_max);
    while (periods >= 2 && periods * grant.period_frames > caps.buffer_max)
    {
        periods -= 1;
    }
    if (periods < 2 || periods < caps.periods_min)
    {
        // The period itself is too large for a buffer of two: shrink it to fit.
        periods = caps.periods_min < 2 ? 2 : caps.periods_min;
        const u32 fit = caps.buffer_max / periods;
        if (fit < caps.period_min)
        {
            return DeviceStatus::Unsupported;
        }
        grant.period_frames = fit < grant.period_frames ? fit : grant.period_frames;
    }
    while (periods * grant.period_frames < caps.buffer_min && periods < caps.periods_max)
    {
        periods += 1;
    }
    if (periods * grant.period_frames < caps.buffer_min)
    {
        return DeviceStatus::Unsupported;
    }
    grant.periods = periods;
    *out = grant;
    return DeviceStatus::Ok;
}

void ConvertForPcm(PcmFormat format, const f32* in, u32 samples, void* out)
{
    switch (format)
    {
    case PcmFormat::F32:
    {
        f32* dst = static_cast<f32*>(out);
        for (u32 i = 0; i < samples; ++i)
        {
            dst[i] = in[i];
        }
        break;
    }
    case PcmFormat::S32:
    {
        i32* dst = static_cast<i32*>(out);
        for (u32 i = 0; i < samples; ++i)
        {
            dst[i] = F32ToS32(in[i]);
        }
        break;
    }
    case PcmFormat::S16:
    {
        i16* dst = static_cast<i16*>(out);
        for (u32 i = 0; i < samples; ++i)
        {
            dst[i] = F32ToS16(in[i]);
        }
        break;
    }
    }
}

bool LayoutOf(u32 channels, ChannelLayout* out)
{
    switch (channels)
    {
    case 1: *out = ChannelLayout::Mono; return true;
    case 2: *out = ChannelLayout::Stereo; return true;
    case 4: *out = ChannelLayout::Quad; return true;
    case 6: *out = ChannelLayout::Surround51; return true;
    case 8: *out = ChannelLayout::Surround71; return true;
    default: return false;
    }
}

}  // namespace vse::audio
