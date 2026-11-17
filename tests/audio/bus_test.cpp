// Buses, sends, ducking, meters and the master limiter (Phase 26 Day 422).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/limiter.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

AudioClipPtr BusConstant(f32 value, u32 frames)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(value);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "bus", &clip) ==
            ClipStatus::Ok);
    return clip;
}

MixerConfig BusConfig(bool limiter = false)
{
    MixerConfig config;
    config.layout = ChannelLayout::Mono;
    config.quality = ResamplerQuality::Linear;
    config.max_voices = 8;
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    config.limiter = limiter;
    return config;
}

Vector<f32> BusRender(Mixer& mixer, u32 blocks)
{
    Vector<f32> out;
    Vector<f32> block;
    for (u32 i = 0; i < kBlockFrames; ++i)
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

VoiceId BusPlay(Mixer& mixer, f32 value, BusId bus)
{
    VoiceDesc desc;
    desc.clip = BusConstant(value, 64);
    desc.loop = true;
    desc.pan = 0.0f;
    desc.bus = bus;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    return id;
}

// A mono voice on a mono output is not panned: the clip as it is.
constexpr f32 kCentre = 1.0f;

}  // namespace

TEST_CASE("Bus: the master gain scales the mix exactly", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusPlay(mixer, 0.5f, kMasterBus);
    REQUIRE(mixer.SetBusGain(kMasterBus, 0.5f, 1) == MixerStatus::Ok);
    const Vector<f32> out = BusRender(mixer, 2);
    CHECK(out[10] == Catch::Approx(0.5f * kCentre).margin(1e-6));
    CHECK(out[kBlockFrames + 10] == Catch::Approx(0.25f * kCentre).margin(1e-6));
}

TEST_CASE("Bus: children sum into the parent and gains multiply", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId sfx, guns;
    REQUIRE(mixer.CreateBus("sfx", kMasterBus, &sfx, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("guns", sfx, &guns, 0) == MixerStatus::Ok);
    CHECK(mixer.FindBus("guns") == guns);
    CHECK(!mixer.FindBus("nothing").IsValid());
    REQUIRE(mixer.CreateBus("sfx", kMasterBus, &sfx, 0) == MixerStatus::NoBusSlot);  // the name is taken
    BusPlay(mixer, 1.0f, guns);
    REQUIRE(mixer.SetBusGain(sfx, 0.5f, 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusGain(guns, 0.5f, 0) == MixerStatus::Ok);
    const Vector<f32> out = BusRender(mixer, 1);
    CHECK(out[5] == Catch::Approx(0.25f * kCentre).margin(1e-6));
}

TEST_CASE("Bus: mute and solo on a three-bus scene", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId a, b, c;
    REQUIRE(mixer.CreateBus("a", kMasterBus, &a, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("b", kMasterBus, &b, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("c", b, &c, 0) == MixerStatus::Ok);
    BusPlay(mixer, 0.1f, a);
    BusPlay(mixer, 0.2f, b);
    BusPlay(mixer, 0.4f, c);
    Vector<f32> out = BusRender(mixer, 1);
    CHECK(out[3] == Catch::Approx(0.7f * kCentre).margin(1e-6));
    REQUIRE(mixer.SetBusMute(a, true, 1) == MixerStatus::Ok);
    out = BusRender(mixer, 1);
    CHECK(out[3] == Catch::Approx(0.6f * kCentre).margin(1e-6));
    REQUIRE(mixer.SetBusMute(a, false, 2) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusSolo(b, true, 2) == MixerStatus::Ok);  // b and its child c stay; a goes
    out = BusRender(mixer, 1);
    CHECK(out[3] == Catch::Approx(0.6f * kCentre).margin(1e-6));
    REQUIRE(mixer.SetBusSolo(c, true, 3) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusSolo(b, false, 3) == MixerStatus::Ok);  // only c, whose ancestor b stays audible
    out = BusRender(mixer, 1);
    CHECK(out[3] == Catch::Approx(0.6f * kCentre).margin(1e-6));
    REQUIRE(mixer.SetBusSolo(c, false, 4) == MixerStatus::Ok);
    out = BusRender(mixer, 1);
    CHECK(out[3] == Catch::Approx(0.7f * kCentre).margin(1e-6));
}

TEST_CASE("Bus: sends feed an auxiliary bus, pre or post fader", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId music, aux;
    REQUIRE(mixer.CreateBus("music", kMasterBus, &music, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("aux", kMasterBus, &aux, 0) == MixerStatus::Ok);
    BusPlay(mixer, 1.0f, music);
    REQUIRE(mixer.SetBusSend(music, 0, aux, 0.5f, false, 0) == MixerStatus::Ok);
    Vector<f32> out = BusRender(mixer, 1);
    CHECK(out[2] == Catch::Approx(1.5f * kCentre).margin(1e-6));  // dry plus half again through the aux
    REQUIRE(mixer.SetBusGain(music, 0.0f, 1) == MixerStatus::Ok);
    out = BusRender(mixer, 1);
    CHECK(out[2] == 0.0f);  // post fader: the send is cut with the bus
    REQUIRE(mixer.SetBusSend(music, 0, aux, 0.5f, true, 2) == MixerStatus::Ok);
    out = BusRender(mixer, 1);
    CHECK(out[2] == Catch::Approx(0.5f * kCentre).margin(1e-6));  // pre fader: the send survives the fader
}

TEST_CASE("Bus: a send or a duck that would make a cycle is refused", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId a, b, c;
    REQUIRE(mixer.CreateBus("a", kMasterBus, &a, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("b", a, &b, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("c", kMasterBus, &c, 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetBusSend(a, 0, c, 1.0f, false, 0) == MixerStatus::Ok);
    CHECK(mixer.SetBusSend(c, 0, a, 1.0f, false, 0) == MixerStatus::Cycle);
    CHECK(mixer.SetBusSend(c, 0, b, 1.0f, false, 0) == MixerStatus::Cycle);  // b feeds a which feeds c
    CHECK(mixer.SetDuck(a, a, 0.5f, 0.1f, 100.0f, 0) == MixerStatus::Cycle);
    CHECK(mixer.SetBusSend(kMasterBus, 0, a, 1.0f, false, 0) == MixerStatus::InvalidBus);
    BusId bad;
    bad.index = 20;
    CHECK(mixer.SetBusGain(bad, 1.0f, 0) == MixerStatus::InvalidBus);
}

TEST_CASE("Bus: ducking lowers a bus while its source is loud and recovers after", "[audio][bus]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId music, voice;
    REQUIRE(mixer.CreateBus("music", kMasterBus, &music, 0) == MixerStatus::Ok);
    REQUIRE(mixer.CreateBus("voice", kMasterBus, &voice, 0) == MixerStatus::Ok);
    BusPlay(mixer, 0.5f, music);
    VoiceDesc speech;
    speech.clip = BusConstant(0.5f, 64);
    speech.loop = false;
    speech.bus = voice;
    VoiceId speech_id;
    REQUIRE(mixer.SetDuck(music, voice, 0.25f, 0.01f, 2048.0f, 0) == MixerStatus::Ok);
    Vector<f32> quiet = BusRender(mixer, 1);
    CHECK(quiet[4] == Catch::Approx(0.5f * kCentre).margin(1e-6));  // nothing on the source: not ducked
    REQUIRE(mixer.Play(speech, &speech_id, 1) == MixerStatus::Ok);
    const Vector<f32> out = BusRender(mixer, 40);
    // The speech clip is 64 frames, so it is over after the first block; the music is ducked in the block after the
    // source was loud and then recovers toward 1 without ever overshooting.
    const f32 full = 0.5f * kCentre;
    f32 lowest = full;
    f32 previous = 0.0f;
    bool recovering = false;
    for (u32 blk = 1; blk < 40; ++blk)
    {
        const f32 v = out[blk * kBlockFrames + 4];
        if (v < lowest)
        {
            lowest = v;
        }
        if (recovering)
        {
            CHECK(v >= previous - 1e-7f);
            CHECK(v <= full + 1e-6f);
        }
        if (blk > 2 && v > previous)
        {
            recovering = true;
        }
        previous = v;
    }
    CHECK(lowest < 0.5f * full);
    CHECK(out[39 * kBlockFrames + 4] > 0.9f * full);
}

TEST_CASE("Bus: a metered bus reports its level once per block", "[audio][bus][meter]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig()) == MixerStatus::Ok);
    BusId sfx;
    REQUIRE(mixer.CreateBus("sfx", kMasterBus, &sfx, 0) == MixerStatus::Ok);
    BusPlay(mixer, 0.5f, sfx);
    REQUIRE(mixer.SetMeter(sfx, true, 0) == MixerStatus::Ok);
    BusRender(mixer, 3);
    VoiceEvent events[16];
    const u32 count = mixer.PollEvents(events, 16);
    u32 levels = 0;
    for (u32 i = 0; i < count; ++i)
    {
        if (events[i].type == VoiceEventType::BusLevel && events[i].id.index == sfx.index)
        {
            levels += 1;
            CHECK(events[i].peak == Catch::Approx(0.5f * kCentre).margin(1e-6));
            CHECK(events[i].rms == Catch::Approx(0.5f * kCentre).margin(1e-5));
        }
    }
    CHECK(levels == 3);
}

TEST_CASE("Limiter: the output never exceeds the threshold", "[audio][limiter]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(BusConfig(true)) == MixerStatus::Ok);
    BusPlay(mixer, 0.9f, kMasterBus);
    BusPlay(mixer, 0.9f, kMasterBus);
    BusPlay(mixer, 0.9f, kMasterBus);
    const Vector<f32> out = BusRender(mixer, 12);
    f32 peak = 0.0f;
    for (u32 i = 0; i < out.Size(); ++i)
    {
        peak = FMax(peak, Abs(out[i]));
    }
    CHECK(peak <= 0.9f + 1e-6f);
    CHECK(mixer.Stats().limiter_min_gain < 1.0f);
}

TEST_CASE("Limiter: below the threshold it passes the signal bit for bit, delayed by its look-ahead",
          "[audio][limiter]")
{
    Mixer plain;
    Mixer limited;
    REQUIRE(plain.Init(BusConfig(false)) == MixerStatus::Ok);
    REQUIRE(limited.Init(BusConfig(true)) == MixerStatus::Ok);
    Vector<f32> source;
    for (u32 i = 0; i < 1000; ++i)
    {
        source.Push(0.4f * Sin(static_cast<f32>(i) * 0.05f));
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = source.Size();
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(source.Data(), source.Size()), "sine", &clip) ==
            ClipStatus::Ok);
    VoiceDesc voice;
    voice.clip = clip;
    voice.loop = true;
    VoiceId id;
    REQUIRE(plain.Play(voice, &id, 0) == MixerStatus::Ok);
    REQUIRE(limited.Play(voice, &id, 0) == MixerStatus::Ok);
    const Vector<f32> a = BusRender(plain, 6);
    const Vector<f32> b = BusRender(limited, 6);
    const u32 latency = LimiterConfig().lookahead_frames;
    for (u32 n = latency; n < a.Size(); ++n)
    {
        REQUIRE(b[n] == a[n - latency]);
    }
    CHECK(limited.Stats().limiter_min_gain == 1.0f);
}

TEST_CASE("Bus: the decibel helpers", "[audio][bus]")
{
    CHECK(DbToGain(0.0f) == Catch::Approx(1.0f));
    CHECK(DbToGain(-6.0206f) == Catch::Approx(0.5f).margin(1e-4));
    CHECK(DbToGain(20.0f) == Catch::Approx(10.0f).margin(1e-4));
    CHECK(GainToDb(1.0f) == Catch::Approx(0.0f).margin(1e-6));
    CHECK(GainToDb(0.1f) == Catch::Approx(-20.0f).margin(1e-4));
    CHECK(GainToDb(0.0f) == -1000.0f);
}
