// The music system (Phase 26 Day 436): equal-power cross-fades, a bar-aligned transition on its exact frame, layers
// that stay in step, stingers on the beat, gapless playlists, a seeded shuffle, and a replay that reproduces every
// sample.
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/stream.h"
#include "containers/vector.h"
#include "lang/scalar.h"

using namespace vse;
using namespace vse::audio;

namespace
{

AudioClipPtr MuClip(u32 frames, f32 (*value)(u32, u32), u32 salt)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(value(i, salt));
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "mu", &clip) ==
            ClipStatus::Ok);
    return clip;
}

f32 MuDc(u32, u32 salt)
{
    return static_cast<f32>(salt) / 8.0f;
}

f32 MuRamp(u32 i, u32 salt)
{
    return static_cast<f32>(salt + i + 1) / 1024.0f;
}

StreamBufferPtr MuBuffer(const AudioClipPtr& clip)
{
    UniquePtr<StreamSource> source;
    REQUIRE(MakeMemoryStream(clip, &source) == StreamStatus::Ok);
    StreamConfig config;
    config.capacity_frames = 4096;
    config.chunk_frames = 1024;
    config.low_water_frames = 2048;
    StreamBufferPtr buffer;
    REQUIRE(StreamBuffer::Create(Move(source), config, &buffer) == StreamStatus::Ok);
    buffer->Refill();
    return buffer;
}

MixerConfig MuConfig()
{
    MixerConfig config;
    config.layout = ChannelLayout::Mono;
    config.max_voices = 16;
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    return config;
}

// Runs the player and the refills for `blocks` blocks and returns the output.
Vector<f32> MuRun(Mixer& mixer, MusicPlayer& player, const Vector<StreamBufferPtr>& buffers, u32 blocks)
{
    Vector<f32> out;
    Vector<f32> block;
    block.Resize(kBlockFrames);
    for (u32 b = 0; b < blocks; ++b)
    {
        player.Update();
        for (usize i = 0; i < buffers.Size(); ++i)
        {
            buffers[i]->Refill();
        }
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            out.Push(block[i]);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("music: a cross-fade follows the cosine and the sine, so the powers add to one at every frame",
          "[audio][music]")
{
    constexpr u32 kFade = 4800;
    constexpr u64 kStart = 256 * 10;
    // The fade-out measured alone (the new track is silent), then the fade-in alone (the old track is silent).
    Vector<f32> gain_out;
    Vector<f32> gain_in;
    for (u32 pass = 0; pass < 2; ++pass)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
        MusicPlayer player(&mixer, BusId{0});
        const StreamBufferPtr a = MuBuffer(MuClip(60000, MuDc, pass == 0 ? 8 : 0));
        const StreamBufferPtr b = MuBuffer(MuClip(60000, MuDc, pass == 0 ? 0 : 8));
        Vector<StreamBufferPtr> all;
        all.Push(a);
        all.Push(b);
        REQUIRE(player.Play(a, 0, 0) == MixerStatus::Ok);
        MuRun(mixer, player, all, 0);
        // Run to the block before the fade, then ask for the cross-fade at a block-aligned frame.
        Vector<f32> head = MuRun(mixer, player, all, 9);
        REQUIRE(player.CrossFadeTo(b, kFade, kStart) == MixerStatus::Ok);
        Vector<f32> tail = MuRun(mixer, player, all, 40);
        Vector<f32>& dest = pass == 0 ? gain_out : gain_in;
        for (usize i = 0; i < head.Size(); ++i)
        {
            dest.Push(head[i]);
        }
        for (usize i = 0; i < tail.Size(); ++i)
        {
            dest.Push(tail[i]);
        }
    }
    for (u32 n = 0; n < 10 * 256; ++n)
    {
        REQUIRE(gain_out[n] == 1.0f);
        REQUIRE(gain_in[n] == 0.0f);
    }
    f32 worst = 0.0f;
    for (u32 j = 0; j < kFade; ++j)
    {
        const f32 go = gain_out[kStart + j];
        const f32 gi = gain_in[kStart + j];
        const f32 err = Abs(go * go + gi * gi - 1.0f);
        worst = err > worst ? err : worst;
    }
    CHECK(worst < 4e-3f);
    CHECK(gain_out[kStart + kFade + 600] == 0.0f);
    CHECK(gain_in[kStart + kFade + 600] == 1.0f);
}

TEST_CASE("music: a transition at the bar starts on the exact frame of the tempo", "[audio][music]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
    MusicPlayer player(&mixer, BusId{0});
    Tempo tempo;
    tempo.beats_per_minute = 130.0;
    tempo.beats_per_bar = 4;
    const StreamBufferPtr a = MuBuffer(MuClip(300000, MuDc, 8));
    const StreamBufferPtr b = MuBuffer(MuClip(300000, MuDc, 4));
    Vector<StreamBufferPtr> all;
    all.Push(a);
    all.Push(b);
    REQUIRE(player.Play(a, 0, 0, tempo) == MixerStatus::Ok);
    Vector<f32> out = MuRun(mixer, player, all, 10);
    // A bar of 130 bpm in 4/4 at 48 kHz is 4 * 60 * 48000 / 130 = 88,615.38 frames.
    const u64 bar = player.NextBarFrame();
    CHECK(bar == 88615);
    REQUIRE(player.CrossFadeAtBar(b, 0, tempo) == MixerStatus::Ok);
    const Vector<f32> more = MuRun(mixer, player, all, 400);
    for (usize i = 0; i < more.Size(); ++i)
    {
        out.Push(more[i]);
    }
    CHECK(out[bar - 1] == 0.0f);  // the old track was cut at the start of the block, the new one is not there yet
    CHECK(out[bar] == 0.5f);
    CHECK(out[bar + 1000] == 0.5f);
    CHECK(player.CurrentStartFrame() == bar);
}

TEST_CASE("music: layers stay in step through a thousand blocks and a seek", "[audio][music]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
    MusicPlayer player(&mixer, BusId{0});
    const StreamBufferPtr a = MuBuffer(MuClip(6000, MuRamp, 0));
    const StreamBufferPtr b = MuBuffer(MuClip(6000, MuRamp, 0));
    Vector<StreamBufferPtr> all;
    all.Push(a);
    all.Push(b);
    StreamBufferPtr layers[2] = {a, b};
    const f32 gains[2] = {1.0f, 0.5f};
    REQUIRE(player.PlayLayers(Span<const StreamBufferPtr>(layers, 2), Span<const f32>(gains, 2), 0) == MixerStatus::Ok);
    const Vector<f32> first = MuRun(mixer, player, all, 1000);
    for (usize n = 0; n < first.Size(); ++n)
    {
        const f32 x = static_cast<f32>(n % 6000 + 1) / 1024.0f;
        const f32 d = first[n] - 1.5f * x;
        REQUIRE((d < 1e-5f && d > -1e-5f));
    }
    player.SeekLayers(1000);
    const Vector<f32> second = MuRun(mixer, player, all, 60);
    usize at = 0;
    while (at < second.Size() && second[at] == 0.0f)
    {
        at += 1;
    }
    REQUIRE(at < second.Size());
    for (usize k = 0; at + k < second.Size(); ++k)
    {
        const f32 x = static_cast<f32>((1000 + k) % 6000 + 1) / 1024.0f;
        const f32 d = second[at + k] - 1.5f * x;
        REQUIRE((d < 1e-5f && d > -1e-5f));
    }
}

TEST_CASE("music: a stinger sounds on the exact frame of the next beat", "[audio][music]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
    MusicPlayer player(&mixer, BusId{0});
    Tempo tempo;
    tempo.beats_per_minute = 120.0;  // a beat is 24,000 frames: not a multiple of the block
    const StreamBufferPtr music = MuBuffer(MuClip(200000, MuDc, 0));
    Vector<StreamBufferPtr> all;
    all.Push(music);
    REQUIRE(player.Play(music, 0, 0, tempo) == MixerStatus::Ok);
    Vector<f32> out = MuRun(mixer, player, all, 10);
    const AudioClipPtr hit = MuClip(100, MuDc, 6);
    u64 frame = 0;
    REQUIRE(player.PlayStinger(hit, 1.0f, true, &frame) == MixerStatus::Ok);
    CHECK(frame == 24000);
    const Vector<f32> more = MuRun(mixer, player, all, 120);
    for (usize i = 0; i < more.Size(); ++i)
    {
        out.Push(more[i]);
    }
    CHECK(out[frame - 1] == 0.0f);
    CHECK(out[frame] == 0.75f);
    CHECK(out[frame + 99] == 0.75f);
    CHECK(out[frame + 100] == 0.0f);
    CHECK(player.DuckForDialogue(BusId{0}, 0.5f, 0.1f, 4800.0f) != MixerStatus::QueueFull);
}

TEST_CASE("music: a playlist of three clips is gapless on a ramp", "[audio][music]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
    MusicPlayer player(&mixer, BusId{0});
    StreamBufferPtr tracks[3] = {MuBuffer(MuClip(5000, MuRamp, 0)), MuBuffer(MuClip(5000, MuRamp, 5000)),
                                 MuBuffer(MuClip(5000, MuRamp, 10000))};
    Vector<StreamBufferPtr> all;
    for (u32 i = 0; i < 3; ++i)
    {
        all.Push(tracks[i]);
    }
    REQUIRE(player.PlayPlaylist(Span<const StreamBufferPtr>(tracks, 3), 0, false, 0) == MixerStatus::Ok);
    const Vector<f32> out = MuRun(mixer, player, all, 70);
    for (u32 n = 0; n < 15000; ++n)
    {
        REQUIRE(out[n] == static_cast<f32>(n + 1) / 1024.0f);
    }
    for (usize n = 15000; n < out.Size(); ++n)
    {
        REQUIRE(out[n] == 0.0f);
    }
    CHECK(player.PlaylistPosition() == 2);
}

TEST_CASE("music: a seeded shuffle gives the same order twice and every track once", "[audio][music]")
{
    Vector<u32> a;
    Vector<u32> b;
    Vector<u32> other;
    PlaylistOrder(12, 777, true, &a);
    PlaylistOrder(12, 777, true, &b);
    PlaylistOrder(12, 778, true, &other);
    REQUIRE(a.Size() == 12);
    bool same = true;
    bool differs = false;
    u32 seen[12] = {};
    for (u32 i = 0; i < 12; ++i)
    {
        same = same && a[i] == b[i];
        differs = differs || a[i] != other[i];
        seen[a[i]] += 1;
    }
    CHECK(same);
    CHECK(differs);
    for (u32 i = 0; i < 12; ++i)
    {
        CHECK(seen[i] == 1);
    }
    Vector<u32> natural;
    PlaylistOrder(5, 1, false, &natural);
    for (u32 i = 0; i < 5; ++i)
    {
        CHECK(natural[i] == i);
    }
}

TEST_CASE("music: a replay reproduces every sample of a transition", "[audio][music]")
{
    u64 hashes[2] = {0, 0};
    for (u32 run = 0; run < 2; ++run)
    {
        Mixer mixer;
        REQUIRE(mixer.Init(MuConfig()) == MixerStatus::Ok);
        MusicPlayer player(&mixer, BusId{0});
        Tempo tempo;
        tempo.beats_per_minute = 97.0;
        const StreamBufferPtr a = MuBuffer(MuClip(200000, MuRamp, 0));
        const StreamBufferPtr b = MuBuffer(MuClip(200000, MuRamp, 3000));
        Vector<StreamBufferPtr> all;
        all.Push(a);
        all.Push(b);
        REQUIRE(player.Play(a, 2000, 0, tempo) == MixerStatus::Ok);
        Vector<f32> out = MuRun(mixer, player, all, 30);
        REQUIRE(player.CrossFadeAtBar(b, 9000, tempo) == MixerStatus::Ok);
        const Vector<f32> more = MuRun(mixer, player, all, 600);
        for (usize i = 0; i < more.Size(); ++i)
        {
            out.Push(more[i]);
        }
        RenderHash hash;
        hash.AddBlock(Span<const f32>(out.Data(), out.Size()));
        hashes[run] = hash.Value();
    }
    CHECK(hashes[0] == hashes[1]);
}
