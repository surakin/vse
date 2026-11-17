// The mixer (Phase 26 Day 420): voices, the three resamplers, the loop wrap, the pan law, voice stealing, virtual
// voices and the committed hash of the reference scene.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "platform_hash.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

AudioClipPtr MxMakeClip(const Vector<f32>& samples, ChannelLayout layout, u32 rate, u64 loop_start = 0,
                        u64 loop_end = 0)
{
    ClipDesc desc;
    desc.format.sample_rate = rate;
    desc.format.layout = layout;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = samples.Size() / ChannelCount(layout);
    desc.loop_start = loop_start;
    desc.loop_end = loop_end;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "test", &clip) ==
            ClipStatus::Ok);
    return clip;
}

// A mono ramp: frame i holds i / 1024 (exact in f32).
AudioClipPtr MxRamp(u32 frames)
{
    Vector<f32> samples;
    samples.Reserve(frames);
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(static_cast<f32>(i) / 1024.0f);
    }
    return MxMakeClip(samples, ChannelLayout::Mono, 48000);
}

MixerConfig MxConfig(ChannelLayout layout, ResamplerQuality quality, u32 voices = 16)
{
    MixerConfig config;
    config.layout = layout;
    config.quality = quality;
    config.max_voices = voices;
    config.ramp_frames = 0;  // parameter changes are instant in these tests; the ramp tests set their own
    config.lead_blocks = 0;  // commands take effect at the next block
    return config;
}

// Renders `blocks` blocks and returns the interleaved samples.
Vector<f32> MxRender(Mixer& mixer, u32 blocks)
{
    Vector<f32> out;
    Vector<f32> block;
    for (u32 i = 0; i < kBlockFrames * mixer.Channels(); ++i)
    {
        block.Push(0.0f);
    }
    for (u32 b = 0; b < blocks; ++b)
    {
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (u32 i = 0; i < block.Size(); ++i)
        {
            out.Push(block[i]);
        }
    }
    return out;
}

const ResamplerQuality kMxQualities[] = {ResamplerQuality::Linear, ResamplerQuality::Hermite, ResamplerQuality::Sinc};

}  // namespace

TEST_CASE("Mixer: a clip at pitch 1 reproduces its samples exactly, at every quality", "[audio][mixer]")
{
    for (const ResamplerQuality quality : kMxQualities)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, quality)) == MixerStatus::Ok);
        VoiceDesc desc;
        desc.clip = MxRamp(600);
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
        const Vector<f32> out = MxRender(mixer, 3);
        for (u32 i = 0; i < 600; ++i)
        {
            REQUIRE(out[i] == static_cast<f32>(i) / 1024.0f);
        }
        for (u32 i = 600; i < out.Size(); ++i)
        {
            REQUIRE(out[i] == 0.0f);
        }
        CHECK(!mixer.IsPlaying(id));
        CHECK(mixer.Stats().voices_finished == 1);
    }
}

TEST_CASE("Mixer: pitch 2 plays every second sample", "[audio][mixer]")
{
    for (const ResamplerQuality quality : kMxQualities)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, quality)) == MixerStatus::Ok);
        VoiceDesc desc;
        desc.clip = MxRamp(600);
        desc.pitch = 2.0f;
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
        const Vector<f32> out = MxRender(mixer, 2);
        for (u32 n = 0; n < 300; ++n)
        {
            REQUIRE(out[n] == static_cast<f32>(2 * n) / 1024.0f);
        }
    }
}

TEST_CASE("Mixer: an octave down keeps the sine's frequency", "[audio][mixer]")
{
    // 1 kHz at 48 kHz, played at pitch 0.5, is 500 Hz.
    Vector<f32> sine;
    for (u32 i = 0; i < 4800; ++i)
    {
        sine.Push(Sin(6.28318530718f * 1000.0f * static_cast<f32>(i) / 48000.0f));
    }
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Hermite)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxMakeClip(sine, ChannelLayout::Mono, 48000);
    desc.pitch = 0.5f;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    const Vector<f32> out = MxRender(mixer, 8);
    for (u32 n = 8; n < 2000; ++n)
    {
        const f32 expected = Sin(6.28318530718f * 500.0f * static_cast<f32>(n) / 48000.0f);
        REQUIRE(out[n] == Catch::Approx(expected).margin(2e-3));
    }
}

TEST_CASE("Mixer: a loop wraps exactly, with no gap and no mix of the two ends", "[audio][mixer][loop]")
{
    // Pitch 1: the frame after the last is the first.
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Hermite)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxRamp(100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out = MxRender(mixer, 4);
    for (u32 n = 0; n < out.Size(); ++n)
    {
        REQUIRE(out[n] == static_cast<f32>(n % 100) / 1024.0f);
    }
    CHECK(mixer.IsPlaying(id));
    // Half speed with linear interpolation: the sample between the last frame and the first is their average.
    Mixer half;
    REQUIRE(half.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Linear)) == MixerStatus::Ok);
    desc.pitch = 0.5f;
    REQUIRE(half.Play(desc, &id) == MixerStatus::Ok);
    out = MxRender(half, 2);
    CHECK(out[198] == 99.0f / 1024.0f);
    CHECK(out[199] == Catch::Approx((99.0f + 0.0f) * 0.5f / 1024.0f));
    CHECK(out[200] == 0.0f);
    CHECK(out[201] == Catch::Approx(0.5f / 1024.0f));
}

TEST_CASE("Mixer: loop points come from the clip, and the intro plays once", "[audio][mixer][loop]")
{
    Vector<f32> samples;
    for (u32 i = 0; i < 100; ++i)
    {
        samples.Push(static_cast<f32>(i) / 1024.0f);
    }
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Linear)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxMakeClip(samples, ChannelLayout::Mono, 48000, 20, 60);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    const Vector<f32> out = MxRender(mixer, 2);
    for (u32 n = 0; n < 60; ++n)
    {
        REQUIRE(out[n] == static_cast<f32>(n) / 1024.0f);
    }
    for (u32 n = 60; n < 400; ++n)
    {
        REQUIRE(out[n] == static_cast<f32>(20 + (n - 20) % 40) / 1024.0f);
    }
}

TEST_CASE("Mixer: the pan law gives the documented gains", "[audio][mixer][pan]")
{
    Vector<f32> ones;
    for (u32 i = 0; i < 512; ++i)
    {
        ones.Push(1.0f);
    }
    const f32 pans[] = {-1.0f, 0.0f, 1.0f};
    const f32 left[] = {1.0f, 0.70710678f, 0.0f};
    const f32 right[] = {0.0f, 0.70710678f, 1.0f};
    for (u32 k = 0; k < 3; ++k)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Linear)) == MixerStatus::Ok);
        VoiceDesc desc;
        desc.clip = MxMakeClip(ones, ChannelLayout::Mono, 48000);
        desc.pan = pans[k];
        desc.loop = true;
        desc.gain = 1.0f;
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
        const Vector<f32> out = MxRender(mixer, 1);
        CHECK(out[10] == Catch::Approx(left[k]).margin(1e-6));
        CHECK(out[11] == Catch::Approx(right[k]).margin(1e-6));
    }
}

TEST_CASE("Mixer: a stereo clip balances and keeps unity gain at the centre", "[audio][mixer][pan]")
{
    Vector<f32> pairs;
    for (u32 i = 0; i < 256; ++i)
    {
        pairs.Push(0.5f);
        pairs.Push(-0.25f);
    }
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Linear)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxMakeClip(pairs, ChannelLayout::Stereo, 48000);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out = MxRender(mixer, 1);
    CHECK(out[4] == 0.5f);
    CHECK(out[5] == -0.25f);
    REQUIRE(mixer.SetPan(id, 1.0f) == MixerStatus::Ok);
    out = MxRender(mixer, 1);
    CHECK(out[4] == 0.0f);
    CHECK(out[5] == -0.25f);
    REQUIRE(mixer.SetGain(id, 2.0f) == MixerStatus::Ok);
    REQUIRE(mixer.SetPan(id, 0.0f) == MixerStatus::Ok);
    out = MxRender(mixer, 1);
    CHECK(out[4] == 1.0f);
    CHECK(out[5] == -0.5f);
}

TEST_CASE("Mixer: two voices sum to the sum of two separate renders", "[audio][mixer]")
{
    Vector<f32> a;
    Vector<f32> b;
    for (u32 i = 0; i < 700; ++i)
    {
        a.Push(Sin(0.05f * static_cast<f32>(i)));
        b.Push(0.5f * Sin(0.31f * static_cast<f32>(i)));
    }
    VoiceDesc da;
    da.clip = MxMakeClip(a, ChannelLayout::Mono, 48000);
    da.pan = -0.3f;
    da.pitch = 1.3f;
    VoiceDesc db;
    db.clip = MxMakeClip(b, ChannelLayout::Mono, 48000);
    db.pan = 0.6f;
    db.pitch = 0.9f;
    Mixer both;
    Mixer only_a;
    Mixer only_b;
    REQUIRE(both.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Hermite)) == MixerStatus::Ok);
    REQUIRE(only_a.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Hermite)) == MixerStatus::Ok);
    REQUIRE(only_b.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Hermite)) == MixerStatus::Ok);
    VoiceId id;
    REQUIRE(both.Play(da, &id) == MixerStatus::Ok);
    REQUIRE(both.Play(db, &id) == MixerStatus::Ok);
    REQUIRE(only_a.Play(da, &id) == MixerStatus::Ok);
    REQUIRE(only_b.Play(db, &id) == MixerStatus::Ok);
    const Vector<f32> sum = MxRender(both, 3);
    const Vector<f32> ra = MxRender(only_a, 3);
    const Vector<f32> rb = MxRender(only_b, 3);
    for (u32 i = 0; i < sum.Size(); ++i)
    {
        REQUIRE(sum[i] == ra[i] + rb[i]);
    }
}

TEST_CASE("Mixer: a stale voice id names nothing, and slots are reused with a new generation", "[audio][mixer][voice]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Linear, 1)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxRamp(1000);
    VoiceId first;
    REQUIRE(mixer.Play(desc, &first) == MixerStatus::Ok);
    CHECK(mixer.IsPlaying(first));  // the id is usable before the audio thread has seen the command
    MxRender(mixer, 1);
    CHECK(mixer.VoiceCount() == 1);
    REQUIRE(mixer.Stop(first, 0) == MixerStatus::Ok);
    CHECK(mixer.Stop(first) == MixerStatus::InvalidVoice);
    CHECK(!mixer.IsPlaying(first));
    MxRender(mixer, 1);
    CHECK(mixer.VoiceCount() == 0);
    VoiceId second;
    REQUIRE(mixer.Play(desc, &second) == MixerStatus::Ok);
    CHECK(second.index == first.index);
    CHECK(!(second == first));
    CHECK(mixer.SetGain(first, 0.5f) == MixerStatus::InvalidVoice);
    CHECK(mixer.SetGain(second, 0.5f) == MixerStatus::Ok);
    MxRender(mixer, 1);
    f64 position = -1.0;
    CHECK(mixer.Position(second, &position) == MixerStatus::Ok);
    CHECK(position == 256.0);
    CHECK(mixer.Position(first, &position) == MixerStatus::InvalidVoice);
}

TEST_CASE("Mixer: bad parameters are refused by status", "[audio][mixer][voice]")
{
    Mixer mixer;
    MixerConfig bad;
    bad.max_voices = 0;
    CHECK(mixer.Init(bad) == MixerStatus::InvalidConfig);
    bad = MixerConfig();
    bad.sample_rate = 100;
    CHECK(mixer.Init(bad) == MixerStatus::InvalidConfig);
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Linear)) == MixerStatus::Ok);
    VoiceDesc desc;
    VoiceId id;
    CHECK(mixer.Play(desc, &id) == MixerStatus::NoClip);
    desc.clip = MxRamp(1000);
    desc.pitch = 0.0f;
    CHECK(mixer.Play(desc, &id) == MixerStatus::InvalidParam);
    desc.pitch = 100.0f;
    CHECK(mixer.Play(desc, &id) == MixerStatus::InvalidParam);
    desc.pitch = 1.0f;
    desc.gain = -1.0f;
    CHECK(mixer.Play(desc, &id) == MixerStatus::InvalidParam);
    desc.gain = 1.0f;
    desc.pan = 2.0f;
    CHECK(mixer.Play(desc, &id) == MixerStatus::InvalidParam);
    desc.pan = 0.0f;
    desc.start_frame = 1000;
    CHECK(mixer.Play(desc, &id) == MixerStatus::InvalidParam);
    desc.start_frame = 50;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    MxRender(mixer, 1);
    f64 position = 0.0;
    REQUIRE(mixer.Position(id, &position) == MixerStatus::Ok);
    CHECK(position == 50.0 + 256.0);
    const f32 not_a_number = Limits<f32>::QuietNaN();
    CHECK(mixer.SetGain(id, not_a_number) == MixerStatus::InvalidParam);
    CHECK(mixer.SetPitch(id, 0.01f) == MixerStatus::InvalidParam);
    CHECK(mixer.SetPan(id, -1.5f) == MixerStatus::InvalidParam);
    CHECK(mixer.VoiceCount() == 1);
}

TEST_CASE("Mixer: a full pool stops the lowest voice for a better one and refuses a worse one", "[audio][mixer][voice]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Linear, 2)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxRamp(1000);
    desc.loop = true;
    VoiceId quiet;
    VoiceId loud;
    desc.gain = 0.25f;
    REQUIRE(mixer.Play(desc, &quiet) == MixerStatus::Ok);
    desc.gain = 1.0f;
    REQUIRE(mixer.Play(desc, &loud) == MixerStatus::Ok);
    // Quieter than both: refused.
    VoiceId other;
    desc.gain = 0.1f;
    CHECK(mixer.Play(desc, &other) == MixerStatus::Full);
    CHECK(mixer.Stats().voices_refused == 1);
    CHECK(mixer.IsPlaying(quiet));
    // Louder than the quiet one: it is stopped.
    desc.gain = 0.5f;
    REQUIRE(mixer.Play(desc, &other) == MixerStatus::Ok);
    CHECK(!mixer.IsPlaying(quiet));
    CHECK(mixer.IsPlaying(loud));
    CHECK(mixer.Stats().voices_stolen == 1);
    // Priority beats gain: a high-priority quiet voice stops the quieter one of lower priority.
    desc.gain = 0.01f;
    desc.priority = 5;
    VoiceId priority;
    REQUIRE(mixer.Play(desc, &priority) == MixerStatus::Ok);
    CHECK(mixer.IsPlaying(priority));
    MxRender(mixer, 1);
    CHECK(mixer.VoiceCount() == 2);
}

TEST_CASE("Mixer: a virtual voice advances without mixing and can become real again", "[audio][mixer][voice]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Linear)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxRamp(2000);
    desc.is_virtual = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out = MxRender(mixer, 2);
    for (u32 i = 0; i < out.Size(); ++i)
    {
        REQUIRE(out[i] == 0.0f);
    }
    f64 position = 0.0;
    REQUIRE(mixer.Position(id, &position) == MixerStatus::Ok);
    CHECK(position == 512.0);
    CHECK(mixer.Stats().voices_virtual == 1);
    CHECK(mixer.Stats().voices_mixed == 0);
    REQUIRE(mixer.SetVirtual(id, false) == MixerStatus::Ok);
    out = MxRender(mixer, 1);
    CHECK(out[0] == 512.0f / 1024.0f);
    CHECK(out[255] == 767.0f / 1024.0f);
    // A virtual voice that runs off the end finishes.
    REQUIRE(mixer.SetVirtual(id, true) == MixerStatus::Ok);
    MxRender(mixer, 6);
    CHECK(!mixer.IsPlaying(id));
}

TEST_CASE("Mixer: pulls of any size give the same samples as whole blocks", "[audio][mixer]")
{
    const u32 sizes[] = {1, 64, 100, 256, 1000};
    Vector<f32> reference;
    for (u32 s = 0; s < 5; ++s)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MxConfig(ChannelLayout::Stereo, ResamplerQuality::Hermite)) == MixerStatus::Ok);
        VoiceDesc desc;
        desc.clip = MxRamp(3000);
        desc.pan = 0.4f;
        desc.pitch = 1.37f;
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
        Vector<f32> out;
        Vector<f32> chunk;
        for (u32 i = 0; i < sizes[s] * 2; ++i)
        {
            chunk.Push(0.0f);
        }
        u32 produced = 0;
        while (produced < 2000)
        {
            const u32 count = produced + sizes[s] > 2000 ? 2000 - produced : sizes[s];
            mixer.Render(chunk.Data(), count);
            for (u32 i = 0; i < count * 2; ++i)
            {
                out.Push(chunk[i]);
            }
            produced += count;
        }
        if (s == 0)
        {
            reference = Move(out);
            continue;
        }
        REQUIRE(out.Size() == reference.Size());
        for (u32 i = 0; i < out.Size(); ++i)
        {
            REQUIRE(out[i] == reference[i]);
        }
    }
}

TEST_CASE("Mixer: the sinc resampler keeps a constant at gain one", "[audio][mixer][sinc]")
{
    Vector<f32> ones;
    for (u32 i = 0; i < 1024; ++i)
    {
        ones.Push(1.0f);
    }
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Mono, ResamplerQuality::Sinc)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = MxMakeClip(ones, ChannelLayout::Mono, 48000);
    desc.loop = true;
    desc.pitch = 0.7f;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    const Vector<f32> out = MxRender(mixer, 8);
    for (u32 n = 0; n < out.Size(); ++n)
    {
        REQUIRE(out[n] == Catch::Approx(1.0f).margin(1e-5));
    }
}

TEST_CASE("Mixer: the settings give a mixer configuration", "[audio][mixer][settings]")
{
    AudioSettings settings;
    settings.channels = 6;
    settings.max_voices = 100000;
    settings.sample_rate = 0;
    const MixerConfig config = MixerConfigFromSettings(settings, 44100);
    CHECK(config.sample_rate == 44100);
    CHECK(config.layout == ChannelLayout::Surround51);
    CHECK(config.max_voices == kMaxVoices);
    CHECK(config.quality == ResamplerQuality::Hermite);
}

namespace
{

// The reference scene: eight looping voices of two clips at different pitches and pans, rendered for 40 blocks.
u64 MxReferenceSceneHash(ResamplerQuality quality)
{
    Vector<f32> tone;
    Vector<f32> chirp;
    for (u32 i = 0; i < 4000; ++i)
    {
        tone.Push(0.3f * Sin(0.07f * static_cast<f32>(i)) + 0.1f * Sin(0.31f * static_cast<f32>(i)));
        chirp.Push(0.25f * Sin(0.00005f * static_cast<f32>(i) * static_cast<f32>(i)));
    }
    AudioClipPtr tone_clip = MxMakeClip(tone, ChannelLayout::Mono, 48000, 100, 3900);
    AudioClipPtr chirp_clip = MxMakeClip(chirp, ChannelLayout::Mono, 44100);
    Mixer mixer;
    REQUIRE(mixer.Init(MxConfig(ChannelLayout::Stereo, quality, 8)) == MixerStatus::Ok);
    for (u32 v = 0; v < 8; ++v)
    {
        VoiceDesc desc;
        desc.clip = (v & 1) ? chirp_clip : tone_clip;
        desc.loop = true;
        desc.pitch = 0.6f + 0.17f * static_cast<f32>(v);
        desc.pan = -0.9f + 0.25f * static_cast<f32>(v);
        desc.gain = 0.3f;
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    }
    RenderHash hash;
    Vector<f32> block;
    for (u32 i = 0; i < kBlockFrames * 2; ++i)
    {
        block.Push(0.0f);
    }
    for (u32 b = 0; b < 40; ++b)
    {
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        hash.AddBlock(Span<const f32>(block.Data(), block.Size()));
    }
    REQUIRE(hash.Valid());
    return hash.Value();
}

}  // namespace

TEST_CASE("Mixer: the reference scene renders the committed hash (a same-build regression check)",
          "[audio][mixer][hash]")
{
    // The hash of the 40-block scene at each quality, committed on Linux with Clang (-ffp-contract=off; GCC could not
    // link the tests, see KNOWN-ISSUES.md). A compiler change that moves the bits re-commits these values; across
    // machines the tolerance check is used.
    CHECK(MxReferenceSceneHash(ResamplerQuality::Linear) ==
          testing::PlatformHash(0x65ea76a4b48bb7e9ull, 0x2372e9134f1be9abull, 0xd2a3eb7663627cd5ull));
    CHECK(MxReferenceSceneHash(ResamplerQuality::Hermite) ==
          testing::PlatformHash(0xd2c3a4192090c998ull, 0x95259c800dacee97ull, 0xa30c5966692fcf80ull));
    CHECK(MxReferenceSceneHash(ResamplerQuality::Sinc) ==
          testing::PlatformHash(0x7f92be7fbe3e465aull, 0x13d56671fe1a9347ull, 0x94d46fa909f4011eull));
    // Rendering twice gives the same bits.
    CHECK(MxReferenceSceneHash(ResamplerQuality::Hermite) == MxReferenceSceneHash(ResamplerQuality::Hermite));
}
