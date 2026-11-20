// Filters, delay, reverb and the bus inserts (Phase 26 Days 423-424).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/delay.h"
#include "audio/filter.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "platform_hash.h"

using namespace vse;
using namespace vse::audio;

namespace
{

constexpr u32 kRate = 48000;

// The steady-state gain of an effect on a sine of `frequency`, measured by running it.
f32 EfMeasure(Effect& effect, f32 frequency, u32 frames = 20000)
{
    effect.Reset();
    f64 energy = 0.0;
    Vector<f32> one;
    one.Push(0.0f);
    for (u32 n = 0; n < frames; ++n)
    {
        one[0] = Sin(6.283185307f * frequency * static_cast<f32>(n) / static_cast<f32>(kRate));
        effect.Process(one.Data(), 1);
        if (n >= frames - 4800)
        {
            energy += static_cast<f64>(one[0]) * static_cast<f64>(one[0]);
        }
    }
    // The RMS of a sine is its amplitude over sqrt(2), and the window holds whole cycles of none of the test
    // frequencies exactly, so the estimate carries a small error the tolerance allows for.
    return static_cast<f32>(Sqrt(2.0 * energy / 4800.0));
}

AudioClipPtr EfClip(const Vector<f32>& samples, ChannelLayout layout = ChannelLayout::Mono)
{
    ClipDesc desc;
    desc.format.sample_rate = kRate;
    desc.format.layout = layout;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = samples.Size() / (layout == ChannelLayout::Mono ? 1 : 2);
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "ef", &clip) ==
            ClipStatus::Ok);
    return clip;
}

MixerConfig EfConfig(ChannelLayout layout)
{
    MixerConfig config;
    config.layout = layout;
    config.quality = ResamplerQuality::Linear;
    config.max_voices = 4;
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    return config;
}

Vector<f32> EfRender(Mixer& mixer, u32 blocks, u32 channels)
{
    Vector<f32> out;
    Vector<f32> block;
    for (u32 i = 0; i < kBlockFrames * channels; ++i)
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

const FilterType kTypes[] = {FilterType::LowPass, FilterType::HighPass, FilterType::BandPass, FilterType::Notch,
                             FilterType::Peaking, FilterType::LowShelf, FilterType::HighShelf};

}  // namespace

TEST_CASE("Filter: the analytic gain at the cutoff and at the ends", "[audio][filter]")
{
    const f32 fc = 1000.0f;
    {
        const FilterDesign lp = DesignFilter(FilterType::LowPass, kRate, fc, 0.7071f, 0.0f);
        CHECK(FilterMagnitude(lp.params, kRate, 1.0f) == Catch::Approx(1.0f).margin(1e-3));
        CHECK(FilterMagnitude(lp.params, kRate, fc) == Catch::Approx(0.7071f).margin(1e-3));
        CHECK(FilterMagnitude(lp.params, kRate, 20000.0f) < 0.01f);
    }
    {
        const FilterDesign hp = DesignFilter(FilterType::HighPass, kRate, fc, 2.0f, 0.0f);
        CHECK(FilterMagnitude(hp.params, kRate, fc) == Catch::Approx(2.0f).margin(1e-3));
        CHECK(FilterMagnitude(hp.params, kRate, 10.0f) < 1e-3f);
        CHECK(FilterMagnitude(hp.params, kRate, 23000.0f) == Catch::Approx(1.0f).margin(0.02));
    }
    {
        const FilterDesign bp = DesignFilter(FilterType::BandPass, kRate, fc, 4.0f, 0.0f);
        CHECK(FilterMagnitude(bp.params, kRate, fc) == Catch::Approx(1.0f).margin(1e-3));
        CHECK(FilterMagnitude(bp.params, kRate, 100.0f) < 0.15f);
        CHECK(FilterMagnitude(bp.params, kRate, 10000.0f) < 0.15f);
    }
    {
        const FilterDesign notch = DesignFilter(FilterType::Notch, kRate, fc, 4.0f, 0.0f);
        CHECK(FilterMagnitude(notch.params, kRate, fc) < 1e-3f);
        CHECK(FilterMagnitude(notch.params, kRate, 100.0f) == Catch::Approx(1.0f).margin(0.02));
    }
    {
        const FilterDesign peak = DesignFilter(FilterType::Peaking, kRate, fc, 1.0f, 12.0f);
        CHECK(FilterMagnitude(peak.params, kRate, fc) == Catch::Approx(3.98107f).margin(2e-3));
        CHECK(FilterMagnitude(peak.params, kRate, 20.0f) == Catch::Approx(1.0f).margin(0.01));
        const FilterDesign low = DesignFilter(FilterType::LowShelf, kRate, fc, 0.7071f, -12.0f);
        CHECK(FilterMagnitude(low.params, kRate, 1.0f) == Catch::Approx(0.251189f).margin(2e-3));
        CHECK(FilterMagnitude(low.params, kRate, 22000.0f) == Catch::Approx(1.0f).margin(0.01));
        const FilterDesign high = DesignFilter(FilterType::HighShelf, kRate, fc, 0.7071f, 6.0f);
        CHECK(FilterMagnitude(high.params, kRate, 22000.0f) == Catch::Approx(1.99526f).margin(0.01));
        CHECK(FilterMagnitude(high.params, kRate, 10.0f) == Catch::Approx(1.0f).margin(0.01));
    }
}

TEST_CASE("Filter: the running effect matches the response at five frequencies", "[audio][filter]")
{
    const f32 frequencies[5] = {80.0f, 400.0f, 1000.0f, 3000.0f, 9000.0f};
    for (const FilterType type : kTypes)
    {
        UniquePtr<Effect> effect = MakeFilterEffect(1, 0);
        REQUIRE(effect != nullptr);
        const FilterDesign design = DesignFilter(type, kRate, 1000.0f, 1.2f, 9.0f);
        REQUIRE(!design.clamped);
        effect->SetParams(design.params);
        for (const f32 f : frequencies)
        {
            const f32 expected = FilterMagnitude(design.params, kRate, f);
            const f32 measured = EfMeasure(*effect, f);
            CHECK(measured == Catch::Approx(expected).margin(0.02 * expected + 0.004));
        }
    }
}

TEST_CASE("Filter: the identity passes the signal bit for bit", "[audio][filter]")
{
    UniquePtr<Effect> effect = MakeFilterEffect(2, 64);
    REQUIRE(effect != nullptr);
    Vector<f32> signal;
    Vector<f32> copy;
    for (u32 i = 0; i < 600; ++i)
    {
        signal.Push(0.9f * Sin(0.3f * static_cast<f32>(i)));
        copy.Push(signal[i]);
    }
    effect->Process(signal.Data(), 300);
    for (u32 i = 0; i < 600; ++i)
    {
        REQUIRE(signal[i] == copy[i]);
    }
    // A filter designed as the identity (a peaking filter at 0 dB) is also exactly transparent after its coefficients
    // are set: b0 = 1, b1 = a1, b2 = a2 only approximately, so it is checked within rounding instead.
    const FilterDesign flat = DesignFilter(FilterType::Peaking, kRate, 1000.0f, 1.0f, 0.0f);
    effect->SetParams(flat.params);
    effect->Process(signal.Data(), 300);
    for (u32 i = 0; i < 600; ++i)
    {
        REQUIRE(signal[i] == Catch::Approx(copy[i]).margin(1e-5));
    }
}

TEST_CASE("Filter: a request out of range is clamped and an unstable one becomes the identity", "[audio][filter]")
{
    const FilterDesign high = DesignFilter(FilterType::LowPass, kRate, 100000.0f, 0.7f, 0.0f);
    CHECK(high.clamped);
    CHECK(FilterMagnitude(high.params, kRate, 100.0f) == Catch::Approx(1.0f).margin(1e-2));
    const FilterDesign q = DesignFilter(FilterType::BandPass, kRate, 500.0f, 0.001f, 0.0f);
    CHECK(q.clamped);
    CHECK(DesignFilter(FilterType::LowPass, kRate, 500.0f, 0.7f, 0.0f).clamped == false);
    const FilterDesign nan = DesignFilter(FilterType::LowPass, kRate, Limits<f32>::QuietNaN(), 0.7f, 0.0f);
    CHECK(nan.clamped);
    CHECK(nan.params.v[0] == 1.0f);
    CHECK(nan.params.v[3] == 0.0f);
}

TEST_CASE("Filter: a sweep ramps instead of jumping", "[audio][filter]")
{
    Vector<f32> step;
    for (u32 i = 0; i < 400; ++i)
    {
        step.Push(1.0f);
    }
    const FilterDesign boost = DesignFilter(FilterType::LowShelf, kRate, 5000.0f, 0.7071f, 20.0f);
    f32 jump[2] = {};
    const u32 ramps[2] = {0, 128};
    for (u32 k = 0; k < 2; ++k)
    {
        UniquePtr<Effect> effect = MakeFilterEffect(1, ramps[k]);
        Vector<f32> signal;
        for (u32 i = 0; i < 400; ++i)
        {
            signal.Push(1.0f);
        }
        effect->Process(signal.Data(), 100);
        effect->SetParams(boost.params);
        effect->Process(signal.Data() + 100, 300);
        for (u32 i = 101; i < 400; ++i)
        {
            jump[k] = FMax(jump[k], Abs(signal[i] - signal[i - 1]));
        }
    }
    CHECK(jump[0] > 1.0f);            // instant: the output steps
    CHECK(jump[1] < 0.5f * jump[0]);  // ramped over 128 frames: a smaller step
}

TEST_CASE("Filter: a ring-out ends in exact zero within the stated tail", "[audio][filter]")
{
    UniquePtr<Effect> effect = MakeFilterEffect(1, 0);
    const FilterDesign design = DesignFilter(FilterType::BandPass, kRate, 120.0f, 20.0f, 0.0f);
    effect->SetParams(design.params);
    const u32 tail = effect->TailFrames(design.params);
    CHECK(tail > 100);
    Vector<f32> signal;
    signal.Push(1.0f);
    for (u32 i = 0; i < tail + 8; ++i)
    {
        signal.Push(0.0f);
    }
    effect->Process(signal.Data(), static_cast<u32>(signal.Size()));
    CHECK(signal[0] != 0.0f);
    for (u32 i = tail + 1; i < signal.Size(); ++i)
    {
        REQUIRE(signal[i] == 0.0f);
    }
    CHECK(effect->TailFrames(IdentityFilterParams()) == 0);
}

TEST_CASE("Filter: the one-pole follows its step response and opens bit-exactly", "[audio][filter]")
{
    UniquePtr<Effect> effect = MakeOnePoleEffect(1, 0);
    Vector<f32> signal;
    for (u32 i = 0; i < 2000; ++i)
    {
        signal.Push(1.0f);
    }
    effect->SetParams(OnePoleParams(kRate, 480.0f));
    effect->Process(signal.Data(), 2000);
    const f32 a = 1.0f - Exp(-2.0f * 3.14159265f * 480.0f / static_cast<f32>(kRate));
    CHECK(signal[0] == Catch::Approx(a).margin(1e-6));
    CHECK(signal[99] == Catch::Approx(1.0f - Pow(1.0f - a, 100.0f)).margin(1e-4));
    CHECK(signal[1999] == Catch::Approx(1.0f).margin(1e-4));
    UniquePtr<Effect> open = MakeOnePoleEffect(1, 0);
    Vector<f32> data;
    data.Push(0.3f);
    data.Push(-0.7f);
    open->Process(data.Data(), 2);
    CHECK(data[0] == 0.3f);
    CHECK(data[1] == -0.7f);
    CHECK(OnePoleParams(kRate, 30000.0f).v[0] == 1.0f);
}

TEST_CASE("Delay: an impulse returns at the set time with the set feedback", "[audio][delay]")
{
    UniquePtr<Effect> effect = MakeDelayEffect(1, kRate, 1.0f, 0);
    REQUIRE(effect != nullptr);
    DelayDesc desc;
    desc.delay_seconds = 100.0f / static_cast<f32>(kRate);
    desc.feedback = 0.5f;
    desc.wet = 1.0f;
    desc.dry = 0.0f;
    effect->SetParams(MakeDelayParams(kRate, desc));
    Vector<f32> signal;
    for (u32 i = 0; i < 700; ++i)
    {
        signal.Push(i == 0 ? 1.0f : 0.0f);
    }
    effect->Process(signal.Data(), 700);
    for (u32 i = 0; i < 700; ++i)
    {
        const f32 expected = i == 100 ? 1.0f : (i == 200 ? 0.5f : (i == 300 ? 0.25f : (i == 400 ? 0.125f : 0.0f)));
        if (i < 450)
        {
            REQUIRE(signal[i] == Catch::Approx(expected).margin(1e-4));
        }
    }
}

TEST_CASE("Delay: a fractional time interpolates and the dry signal mixes", "[audio][delay]")
{
    UniquePtr<Effect> effect = MakeDelayEffect(1, kRate, 1.0f, 0);
    DelayDesc desc;
    desc.delay_seconds = 10.5f / static_cast<f32>(kRate);
    desc.feedback = 0.0f;
    desc.wet = 1.0f;
    desc.dry = 0.5f;
    effect->SetParams(MakeDelayParams(kRate, desc));
    Vector<f32> signal;
    for (u32 i = 0; i < 40; ++i)
    {
        signal.Push(i == 0 ? 1.0f : 0.0f);
    }
    effect->Process(signal.Data(), 40);
    CHECK(signal[0] == Catch::Approx(0.5f).margin(1e-6));
    CHECK(signal[10] == Catch::Approx(0.5f).margin(1e-5));
    CHECK(signal[11] == Catch::Approx(0.5f).margin(1e-5));
    CHECK(signal[12] == 0.0f);
    CHECK(MakeDelayEffect(1, kRate, 100.0f, 0) == nullptr);
    CHECK(MakeDelayEffect(9, kRate, 1.0f, 0) == nullptr);
}

TEST_CASE("Reverb: the decay reaches -60 dB at the set decay time", "[audio][reverb]")
{
    ReverbDesc desc;
    desc.decay_seconds = 1.0f;
    desc.damping = 0.0f;
    desc.wet = 1.0f;
    desc.dry = 0.0f;
    UniquePtr<Effect> effect = MakeReverbEffect(1, desc);
    REQUIRE(effect != nullptr);
    const u32 frames = kRate * 3;
    Vector<f32> signal;
    for (u32 i = 0; i < frames; ++i)
    {
        signal.Push(i == 0 ? 1.0f : 0.0f);
    }
    effect->Process(signal.Data(), frames);
    // The Schroeder backward integral of the energy.
    Vector<f64> edc;
    for (u32 i = 0; i < frames; ++i)
    {
        edc.Push(0.0);
    }
    f64 sum = 0.0;
    for (u32 i = frames; i > 0; --i)
    {
        sum += static_cast<f64>(signal[i - 1]) * static_cast<f64>(signal[i - 1]);
        edc[i - 1] = sum;
    }
    u32 t5 = 0;
    u32 t35 = 0;
    for (u32 i = 0; i < frames; ++i)
    {
        const f64 db = 10.0 * Ln(edc[i] / edc[0]) / 2.302585092994046;
        if (t5 == 0 && db <= -5.0)
        {
            t5 = i;
        }
        if (t35 == 0 && db <= -35.0)
        {
            t35 = i;
        }
    }
    REQUIRE(t5 > 0);
    REQUIRE(t35 > t5);
    const f64 t60 = 2.0 * static_cast<f64>(t35 - t5) / static_cast<f64>(kRate);
    CHECK(t60 == Catch::Approx(1.0).epsilon(0.05));
}

TEST_CASE("Reverb: a mono impulse becomes a decorrelated stereo field", "[audio][reverb]")
{
    const ReverbDesc desc = ReverbPresetDesc(ReverbPreset::Hall, kRate);
    UniquePtr<Effect> effect = MakeReverbEffect(2, desc);
    REQUIRE(effect != nullptr);
    const u32 frames = kRate;
    Vector<f32> signal;
    for (u32 i = 0; i < frames * 2; ++i)
    {
        signal.Push(i == 0 || i == 1 ? 0.5f : 0.0f);
    }
    effect->Process(signal.Data(), frames);
    f64 ll = 0.0, rr = 0.0, lr = 0.0;
    for (u32 i = 4000; i < frames; ++i)
    {
        const f64 l = signal[i * 2];
        const f64 r = signal[i * 2 + 1];
        ll += l * l;
        rr += r * r;
        lr += l * r;
    }
    REQUIRE(ll > 0.0);
    REQUIRE(rr > 0.0);
    CHECK(Abs(lr) / Sqrt(ll * rr) < 0.3);
    u32 lengths[8];
    ReverbLineLengths(kRate, 1.0f, lengths);
    for (u32 i = 1; i < 8; ++i)
    {
        CHECK(lengths[i] > lengths[i - 1]);
    }
    CHECK(MakeReverbEffect(0, desc) == nullptr);
}

TEST_CASE("Inserts: a filter on a bus, params, bypass and refusals", "[audio][effect][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(EfConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    BusId sfx;
    REQUIRE(mixer.CreateBus("sfx", kMasterBus, &sfx, 0) == MixerStatus::Ok);
    Vector<f32> tone;
    for (u32 i = 0; i < 480; ++i)
    {
        tone.Push(Sin(6.283185307f * 8000.0f * static_cast<f32>(i) / static_cast<f32>(kRate)));
    }
    VoiceDesc voice;
    voice.clip = EfClip(tone);
    voice.loop = true;
    voice.bus = sfx;
    VoiceId id;
    REQUIRE(mixer.Play(voice, &id, 0) == MixerStatus::Ok);
    CHECK(mixer.SetInsert(sfx, 0, MakeFilterEffect(2, 0), 0) == MixerStatus::InvalidParam);  // a channel mismatch
    CHECK(mixer.SetInsert(sfx, 9, MakeFilterEffect(1, 0), 0) == MixerStatus::InvalidParam);
    CHECK(mixer.SetInsertParams(sfx, 0, IdentityFilterParams(), 0) == MixerStatus::InvalidParam);  // empty slot
    REQUIRE(mixer.SetInsert(sfx, 0, MakeFilterEffect(1, 0), 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetInsertParams(sfx, 0, DesignFilter(FilterType::LowPass, kRate, 500.0f, 0.7071f, 0.0f).params, 0) ==
            MixerStatus::Ok);
    Vector<f32> out = EfRender(mixer, 3, 1);
    f32 peak = 0.0f;
    for (u32 i = 2 * kBlockFrames; i < out.Size(); ++i)
    {
        peak = FMax(peak, Abs(out[i]));
    }
    CHECK(peak < 0.01f);  // 8 kHz through a 500 Hz low-pass
    REQUIRE(mixer.ClearInsert(sfx, 0, 3) == MixerStatus::Ok);
    out = EfRender(mixer, 1, 1);
    peak = 0.0f;
    for (u32 i = 0; i < out.Size(); ++i)
    {
        peak = FMax(peak, Abs(out[i]));
    }
    CHECK(peak > 0.8f);
    CHECK(mixer.SetInsertParams(sfx, 0, IdentityFilterParams(), 4) == MixerStatus::InvalidParam);
}

TEST_CASE("Inserts: a reverb tail keeps ringing after the voice has ended", "[audio][effect][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(EfConfig(ChannelLayout::Stereo)) == MixerStatus::Ok);
    BusId room;
    REQUIRE(mixer.CreateBus("room", kMasterBus, &room, 0) == MixerStatus::Ok);
    ReverbDesc desc = ReverbPresetDesc(ReverbPreset::SmallRoom, kRate);
    desc.wet = 1.0f;
    desc.dry = 0.0f;
    UniquePtr<Effect> reverb = MakeReverbEffect(2, desc);
    const u32 tail = reverb->TailFrames(MakeReverbParams(desc));
    REQUIRE(mixer.SetInsert(room, 0, Move(reverb), 0) == MixerStatus::Ok);
    Vector<f32> click;
    for (u32 i = 0; i < 128; ++i)
    {
        click.Push(i < 4 ? 0.8f : 0.0f);
        click.Push(i < 4 ? 0.8f : 0.0f);
    }
    VoiceDesc voice;
    voice.clip = EfClip(click, ChannelLayout::Stereo);
    voice.bus = room;
    VoiceId id;
    REQUIRE(mixer.Play(voice, &id, 0) == MixerStatus::Ok);
    const Vector<f32> out = EfRender(mixer, 40, 2);
    f32 late = 0.0f;
    for (u32 i = 20 * kBlockFrames * 2; i < 24 * kBlockFrames * 2; ++i)
    {
        late = FMax(late, Abs(out[i]));
    }
    CHECK(late > 1e-4f);  // 5000 frames after the voice ended, the room is still ringing
    CHECK(tail > 20 * kBlockFrames);
}

TEST_CASE("Inserts: the echo and the reverb scenes render the committed hashes", "[audio][effect][hash]")
{
    u64 hashes[2] = {};
    for (u32 scene = 0; scene < 2; ++scene)
    {
        const u32 channels = scene == 0 ? 1 : 2;
        Mixer mixer;
        REQUIRE(mixer.Init(EfConfig(scene == 0 ? ChannelLayout::Mono : ChannelLayout::Stereo)) == MixerStatus::Ok);
        BusId bus;
        REQUIRE(mixer.CreateBus("fx", kMasterBus, &bus, 0) == MixerStatus::Ok);
        if (scene == 0)
        {
            DelayDesc desc;
            desc.delay_seconds = 0.03f;
            desc.feedback = 0.55f;
            desc.damping = 0.4f;
            UniquePtr<Effect> delay = MakeDelayEffect(1, kRate, 0.5f, 64);
            REQUIRE(mixer.SetInsert(bus, 0, Move(delay), 0) == MixerStatus::Ok);
            REQUIRE(mixer.SetInsertParams(bus, 0, MakeDelayParams(kRate, desc), 0) == MixerStatus::Ok);
        }
        else
        {
            const ReverbDesc desc = ReverbPresetDesc(ReverbPreset::Hall, kRate);
            REQUIRE(mixer.SetInsert(bus, 0, MakeReverbEffect(2, desc), 0) == MixerStatus::Ok);
            REQUIRE(mixer.SetInsert(bus, 1, MakeFilterEffect(2, 0), 0) == MixerStatus::Ok);
            REQUIRE(mixer.SetInsertParams(bus, 1,
                                          DesignFilter(FilterType::HighShelf, kRate, 6000.0f, 0.7f, -6.0f).params,
                                          0) == MixerStatus::Ok);
        }
        Vector<f32> source;
        for (u32 i = 0; i < 3000; ++i)
        {
            source.Push(0.4f * Sin(0.05f * static_cast<f32>(i)) * (i < 1500 ? 1.0f : 0.0f));
            if (channels == 2)
            {
                source.Push(0.3f * Sin(0.07f * static_cast<f32>(i)) * (i < 1500 ? 1.0f : 0.0f));
            }
        }
        VoiceDesc voice;
        voice.clip = EfClip(source, channels == 2 ? ChannelLayout::Stereo : ChannelLayout::Mono);
        voice.bus = bus;
        VoiceId id;
        REQUIRE(mixer.Play(voice, &id, 0) == MixerStatus::Ok);
        RenderHash hash;
        const Vector<f32> out = EfRender(mixer, 60, channels);
        for (u32 b = 0; b < 60; ++b)
        {
            hash.AddBlock(Span<const f32>(out.Data() + static_cast<usize>(b) * kBlockFrames * channels,
                                          static_cast<usize>(kBlockFrames) * channels));
        }
        REQUIRE(hash.Valid());
        hashes[scene] = hash.Value();
    }
    CHECK(hashes[0] == testing::PlatformHash(0x6afbd0d8694df430ull, 0x2ae7595c67d1ab6dull, 0x8a305eea71f21699ull));
    CHECK(hashes[1] == testing::PlatformHash(0xc8fe19a969ce6c8full, 0x43e16b3965dc2afbull, 0xa321c9f1cedbc469ull));
}
