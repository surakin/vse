#include "audio/panning.h"

#include "lang/scalar.h"

namespace engine_placeholder::audio
{

namespace
{

struct Pan
{
    u32 channel;
    f32 x;
    f32 z;
};

// The full-range speakers of a layout in rising azimuth from the back left, so neighbours are consecutive.
constexpr Pan kQuad[] = {{2, -0.70710678f, -0.70710678f},
                         {0, -0.70710678f, 0.70710678f},
                         {1, 0.70710678f, 0.70710678f},
                         {3, 0.70710678f, -0.70710678f}};
constexpr Pan k51[] = {{4, -0.93969262f, -0.34202014f},
                       {0, -0.5f, 0.8660254f},
                       {2, 0.0f, 1.0f},
                       {1, 0.5f, 0.8660254f},
                       {5, 0.93969262f, -0.34202014f}};
constexpr Pan k71[] = {{4, -0.5f, -0.8660254f}, {6, -1.0f, 0.0f}, {0, -0.5f, 0.8660254f}, {2, 0.0f, 1.0f},
                       {1, 0.5f, 0.8660254f},   {7, 1.0f, 0.0f},  {5, 0.5f, -0.8660254f}};

void Table(ChannelLayout layout, const Pan** speakers, u32* count)
{
    switch (layout)
    {
    case ChannelLayout::Quad:
        *speakers = kQuad;
        *count = 4;
        return;
    case ChannelLayout::Surround51:
        *speakers = k51;
        *count = 5;
        return;
    default:
        *speakers = k71;
        *count = 7;
        return;
    }
}

}  // namespace

void SpeakerGains(ChannelLayout layout, f32 x, f32 y, f32 z, f32 spread, f32* gains)
{
    for (u32 i = 0; i < kMaxPanChannels; ++i)
    {
        gains[i] = 0.0f;
    }
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z) || !IsFinite(spread))
    {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
        spread = 0.0f;
    }
    spread = spread < 0.0f ? 0.0f : (spread > 1.0f ? 1.0f : spread);
    const f32 horizontal = Sqrt(x * x + z * z);
    const f32 length = Sqrt(horizontal * horizontal + y * y);
    // Elevation fills the speakers in proportion to how far the direction leaves the horizon; no direction at all
    // (the listener's own position) is fully spread.
    f32 fill = spread;
    f32 px = 0.0f;
    f32 pz = 1.0f;
    if (!(length > 1e-6f) || !(horizontal > 1e-6f * length + 1e-9f))
    {
        fill = 1.0f;
    }
    else
    {
        px = x / horizontal;
        pz = z / horizontal;
        const f32 elevation = (y < 0.0f ? -y : y) / length;
        fill = elevation > fill ? elevation : fill;
    }
    if (layout == ChannelLayout::Mono)
    {
        gains[0] = 1.0f;
        return;
    }
    if (layout == ChannelLayout::Stereo)
    {
        // The lateral position is the pan; a source behind pans the same as one in front (no head shadow here).
        const f32 even = 0.70710678f;
        const f32 left = Sqrt((1.0f - px) * 0.5f);
        const f32 right = Sqrt((1.0f + px) * 0.5f);
        gains[0] = Sqrt((1.0f - fill) * left * left + fill * even * even);
        gains[1] = Sqrt((1.0f - fill) * right * right + fill * even * even);
        return;
    }
    const Pan* speakers = nullptr;
    u32 count = 0;
    Table(layout, &speakers, &count);
    f32 point[kMaxPanChannels] = {};
    for (u32 i = 0; i < count; ++i)
    {
        const Pan& a = speakers[i];
        const Pan& b = speakers[i + 1 == count ? 0 : i + 1];
        const f32 det = a.x * b.z - b.x * a.z;
        const f32 ga = (px * b.z - pz * b.x) / det;
        const f32 gb = (a.x * pz - a.z * px) / det;
        if (ga >= -1e-6f && gb >= -1e-6f)
        {
            const f32 na = ga < 0.0f ? 0.0f : ga;
            const f32 nb = gb < 0.0f ? 0.0f : gb;
            const f32 norm = Sqrt(na * na + nb * nb);
            point[a.channel] = na / norm;
            point[b.channel] = nb / norm;
            break;
        }
    }
    const f32 even_power = 1.0f / static_cast<f32>(count);
    for (u32 i = 0; i < count; ++i)
    {
        const u32 c = speakers[i].channel;
        gains[c] = Sqrt((1.0f - fill) * point[c] * point[c] + fill * even_power);
    }
}

}  // namespace engine_placeholder::audio
