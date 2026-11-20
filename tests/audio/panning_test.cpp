// Spatialisation (Phase 26 Day 429): speaker gains for every layout, constant power, smoothness, and the mixer's
// directional voices with the LFE send.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/mixer.h"
#include "audio/panning.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"

using namespace vse;
using namespace vse::audio;
using Catch::Approx;

namespace
{

constexpr ChannelLayout kLayouts[] = {ChannelLayout::Stereo, ChannelLayout::Quad, ChannelLayout::Surround51,
                                      ChannelLayout::Surround71};

void Gains(ChannelLayout layout, f32 azimuth_degrees, f32* g, f32 elevation = 0.0f, f32 spread = 0.0f)
{
    const f64 az = static_cast<f64>(azimuth_degrees) * 3.14159265358979 / 180.0;
    const f64 el = static_cast<f64>(elevation) * 3.14159265358979 / 180.0;
    SpeakerGains(layout, static_cast<f32>(Sin(az) * Cos(el)), static_cast<f32>(Sin(el)),
                 static_cast<f32>(Cos(az) * Cos(el)), spread, g);
}

f32 Power(const f32* g)
{
    f32 sum = 0.0f;
    for (u32 i = 0; i < kMaxPanChannels; ++i)
    {
        sum += g[i] * g[i];
    }
    return sum;
}

}  // namespace

TEST_CASE("panning: ahead, left, right and behind in every layout", "[audio][panning]")
{
    f32 g[kMaxPanChannels];
    const f32 h = 0.70710678f;
    Gains(ChannelLayout::Stereo, 0, g);
    CHECK(g[0] == Approx(h).margin(1e-4));
    CHECK(g[1] == Approx(h).margin(1e-4));
    Gains(ChannelLayout::Stereo, -90, g);
    CHECK(g[0] == Approx(1.0f).margin(1e-4));
    CHECK(g[1] == Approx(0.0f).margin(1e-3));
    Gains(ChannelLayout::Stereo, 90, g);
    CHECK(g[1] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Stereo, 180, g);
    CHECK(g[0] == Approx(h).margin(1e-3));
    CHECK(g[1] == Approx(h).margin(1e-3));

    Gains(ChannelLayout::Quad, 45, g);  // exactly at the front right speaker
    CHECK(g[1] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Quad, 0, g);
    CHECK(g[0] == Approx(h).margin(1e-4));
    CHECK(g[1] == Approx(h).margin(1e-4));
    Gains(ChannelLayout::Quad, 180, g);
    CHECK(g[2] == Approx(h).margin(1e-4));
    CHECK(g[3] == Approx(h).margin(1e-4));
    Gains(ChannelLayout::Quad, -90, g);
    CHECK(g[0] == Approx(h).margin(1e-4));
    CHECK(g[2] == Approx(h).margin(1e-4));

    Gains(ChannelLayout::Surround51, 0, g);
    CHECK(g[2] == Approx(1.0f).margin(1e-4));
    CHECK(g[0] == Approx(0.0f).margin(1e-4));
    CHECK(g[3] == 0.0f);
    Gains(ChannelLayout::Surround51, 30, g);
    CHECK(g[1] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Surround51, 180, g);
    CHECK(g[4] == Approx(h).margin(1e-4));
    CHECK(g[5] == Approx(h).margin(1e-4));

    Gains(ChannelLayout::Surround71, 0, g);
    CHECK(g[2] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Surround71, -90, g);
    CHECK(g[6] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Surround71, 90, g);
    CHECK(g[7] == Approx(1.0f).margin(1e-4));
    Gains(ChannelLayout::Surround71, 180, g);
    CHECK(g[4] == Approx(h).margin(1e-4));
    CHECK(g[5] == Approx(h).margin(1e-4));
}

TEST_CASE("panning: constant power for 360 azimuths, with elevation and spread", "[audio][panning]")
{
    for (ChannelLayout layout : kLayouts)
    {
        for (i32 az = 0; az < 360; ++az)
        {
            for (f32 spread : {0.0f, 0.3f, 1.0f})
            {
                for (f32 elevation : {0.0f, 25.0f, -60.0f})
                {
                    f32 g[kMaxPanChannels];
                    Gains(layout, static_cast<f32>(az), g, elevation, spread);
                    REQUIRE(Power(g) == Approx(1.0f).margin(1e-4));
                    for (u32 i = 0; i < kMaxPanChannels; ++i)
                    {
                        REQUIRE(g[i] >= 0.0f);
                    }
                }
            }
        }
    }
}

TEST_CASE("panning: the gains vary smoothly between adjacent degrees", "[audio][panning]")
{
    for (ChannelLayout layout : kLayouts)
    {
        f32 previous[kMaxPanChannels];
        Gains(layout, 0.0f, previous);
        for (i32 az = 1; az <= 360; ++az)
        {
            f32 g[kMaxPanChannels];
            Gains(layout, static_cast<f32>(az), g);
            for (u32 i = 0; i < kMaxPanChannels; ++i)
            {
                const f32 step = g[i] > previous[i] ? g[i] - previous[i] : previous[i] - g[i];
                REQUIRE(step < 0.06f);
                previous[i] = g[i];
            }
        }
    }
}

TEST_CASE("panning: no direction (the listener's own position) fills every speaker", "[audio][panning]")
{
    f32 g[kMaxPanChannels];
    SpeakerGains(ChannelLayout::Surround51, 0.0f, 0.0f, 0.0f, 0.0f, g);
    CHECK(g[0] == Approx(Sqrt(0.2f)));
    CHECK(g[2] == Approx(Sqrt(0.2f)));
    CHECK(g[3] == 0.0f);
    SpeakerGains(ChannelLayout::Stereo, 0.0f, 1.0f, 0.0f, 0.0f, g);  // overhead
    CHECK(g[0] == Approx(0.70710678f));
    SpeakerGains(ChannelLayout::Stereo, Limits<f32>::Infinity(), 0.0f, 0.0f, 0.0f, g);  // not finite: centred
    CHECK(g[0] == Approx(0.70710678f));
}

namespace
{

MixerConfig PanConfig(ChannelLayout layout)
{
    MixerConfig mc;
    mc.layout = layout;
    mc.quality = ResamplerQuality::Linear;
    mc.max_voices = 4;
    mc.ramp_frames = 0;
    mc.lead_blocks = 0;
    return mc;
}

AudioClipConstPtr PanClip()
{
    Vector<f32> samples;
    for (u32 i = 0; i < 48000; ++i)
    {
        samples.Push(0.5f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = 48000;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "pan", &clip) ==
            ClipStatus::Ok);
    return clip;
}

}  // namespace

TEST_CASE("panning: a directional voice lands in the right channels of a 5.1 mix and feeds the LFE", "[audio][panning]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(PanConfig(ChannelLayout::Surround51)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = PanClip();
    desc.directional = true;
    desc.direction_x = 0.0f;
    desc.direction_z = 1.0f;
    desc.lfe_send = 0.5f;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out;
    out.Resize(256 * 6);
    mixer.Render(out.Data(), 256);
    const f32* frame = out.Data() + 100 * 6;
    CHECK(frame[2] == Approx(0.5f).margin(1e-4));  // centre
    CHECK(frame[0] == Approx(0.0f).margin(1e-4));
    CHECK(frame[3] == Approx(0.25f).margin(1e-4));  // LFE at half of the voice level
    // Move it to the right: the front right speaker takes over.
    REQUIRE(mixer.SetDirection(id, 1.0f, 0.0f, 1.7320508f, 0.0f) == MixerStatus::Ok);  // 30 degrees
    mixer.Render(out.Data(), 256);
    frame = out.Data() + 200 * 6;
    CHECK(frame[1] == Approx(0.5f).margin(1e-4));
    CHECK(frame[2] == Approx(0.0f).margin(1e-4));
    CHECK(mixer.SetDirection(id, Limits<f32>::QuietNaN(), 0.0f, 1.0f, 0.0f) == MixerStatus::InvalidParam);
}

TEST_CASE("panning: a ramp moves the gains over the ramp without a step", "[audio][panning]")
{
    Mixer mixer;
    MixerConfig mc = PanConfig(ChannelLayout::Stereo);
    mc.ramp_frames = 128;
    REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = PanClip();
    desc.directional = true;
    desc.direction_x = -1.0f;
    desc.direction_z = 0.0f;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out;
    out.Resize(256 * 2);
    mixer.Render(out.Data(), 256);
    REQUIRE(mixer.SetDirection(id, 1.0f, 0.0f, 0.0f, 0.0f) == MixerStatus::Ok);
    mixer.Render(out.Data(), 256);
    f32 largest = 0.0f;
    for (u32 n = 1; n < 256; ++n)
    {
        const f32 d = out[n * 2] - out[(n - 1) * 2];
        largest = FMax(largest, d < 0.0f ? -d : d);
    }
    CHECK(largest < 0.02f);
    CHECK(out[255 * 2 + 1] == Approx(0.5f).margin(1e-3));
    CHECK(out[255 * 2] == Approx(0.0f).margin(1e-3));
}
