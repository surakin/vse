// Playback control (Phase 26 Day 421): the command queue, block stamps, ramps, pause, seek, events, stale ids, a full
// queue, and a producer thread whose timing must not change the render.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "lang/scalar.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

AudioClipPtr CtClip(const Vector<f32>& samples)
{
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = samples.Size();
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "ct", &clip) ==
            ClipStatus::Ok);
    return clip;
}

AudioClipPtr CtConstant(f32 value, u32 frames)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(value);
    }
    return CtClip(samples);
}

AudioClipPtr CtRamp(u32 frames)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(static_cast<f32>(i) / 1024.0f);
    }
    return CtClip(samples);
}

MixerConfig CtConfig(u32 ramp_frames = 64, u32 voices = 8)
{
    MixerConfig config;
    config.layout = ChannelLayout::Mono;
    config.quality = ResamplerQuality::Linear;
    config.max_voices = voices;
    config.ramp_frames = ramp_frames;
    config.lead_blocks = 0;
    return config;
}

Vector<f32> CtRender(Mixer& mixer, u32 blocks)
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

u32 CtEvents(Mixer& mixer, VoiceEvent* events, u32 max)
{
    return mixer.PollEvents(events, max);
}

}  // namespace

TEST_CASE("Control: a gain change ramps linearly and ends exactly on the target", "[audio][control][ramp]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetGain(id, 0.5f, 1) == MixerStatus::Ok);
    const Vector<f32> out = CtRender(mixer, 3);
    for (u32 n = 0; n < kBlockFrames; ++n)
    {
        REQUIRE(out[n] == 1.0f);
    }
    for (u32 n = 0; n < 64; ++n)
    {
        const f32 t = static_cast<f32>(n + 1) / 64.0f;
        REQUIRE(out[kBlockFrames + n] == Catch::Approx(1.0f - 0.5f * t).margin(1e-6));
    }
    CHECK(out[kBlockFrames + 63] == 0.5f);  // the target exactly, on the last frame of the ramp
    for (u32 n = kBlockFrames + 64; n < out.Size(); ++n)
    {
        REQUIRE(out[n] == 0.5f);
    }
}

TEST_CASE("Control: a stop fades out over the ramp and frees the voice", "[audio][control][ramp]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.Stop(id, Mixer::kRampFrames, 1) == MixerStatus::Ok);
    CHECK(!mixer.IsPlaying(id));  // the game sees it as gone at once
    const Vector<f32> out = CtRender(mixer, 3);
    f32 largest_step = 0.0f;
    for (u32 n = 1; n < out.Size(); ++n)
    {
        const f32 step = Abs(out[n] - out[n - 1]);
        if (step > largest_step)
        {
            largest_step = step;
        }
    }
    CHECK(largest_step <= 1.0f / 64.0f + 1e-6f);  // no jump larger than the ramp slope
    CHECK(out[kBlockFrames + 63] == 0.0f);
    CHECK(out[kBlockFrames + 64] == 0.0f);
    CHECK(mixer.VoiceCount() == 0);
    VoiceEvent events[8];
    const u32 count = CtEvents(mixer, events, 8);
    REQUIRE(count >= 1);
    CHECK(events[count - 1].type == VoiceEventType::Stopped);  // after the loop wraps of the blocks before
    CHECK(events[count - 1].id == id);
    CHECK(events[count - 1].block == 1);
}

TEST_CASE("Control: a stop with no fade silences at once, a start can fade in", "[audio][control][ramp]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    desc.fade_in = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.Stop(id, 0, 1) == MixerStatus::Ok);
    const Vector<f32> out = CtRender(mixer, 2);
    for (u32 n = 0; n < 64; ++n)
    {
        REQUIRE(out[n] == Catch::Approx(static_cast<f32>(n + 1) / 64.0f).margin(1e-6));
    }
    CHECK(out[63] == 1.0f);
    CHECK(out[255] == 1.0f);
    for (u32 n = kBlockFrames; n < out.Size(); ++n)
    {
        REQUIRE(out[n] == 0.0f);
    }
}

TEST_CASE("Control: pause holds the position and resume ramps back up", "[audio][control][pause]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtRamp(100000);
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.Pause(id, 1) == MixerStatus::Ok);
    REQUIRE(mixer.Resume(id, 3) == MixerStatus::Ok);
    const Vector<f32> out = CtRender(mixer, 4);
    // Block 1: the fade to silence over 64 frames, then nothing.
    CHECK(out[kBlockFrames + 63] == 0.0f);
    for (u32 n = kBlockFrames + 64; n < 3 * kBlockFrames; ++n)
    {
        REQUIRE(out[n] == 0.0f);
    }
    // The position stood still from the end of the ramp until the resume: 256 + 64 frames were played.
    f64 position = 0.0;
    REQUIRE(mixer.Position(id, &position) == MixerStatus::Ok);
    CHECK(position == 256.0 + 64.0 + 256.0);
    // Block 3: ramps back up from silence to the ramp clip's value at the held position.
    CHECK(out[3 * kBlockFrames] > 0.0f);
    CHECK(out[3 * kBlockFrames + 63] == Catch::Approx(static_cast<f32>(320 + 63) / 1024.0f).margin(1e-6));
}

TEST_CASE("Control: seek jumps to a frame", "[audio][control][seek]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtRamp(2000);
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.Seek(id, 1500, 1) == MixerStatus::Ok);
    REQUIRE(mixer.Seek(id, 99999, 2) == MixerStatus::Ok);
    const Vector<f32> out = CtRender(mixer, 3);
    CHECK(out[kBlockFrames] == 1500.0f / 1024.0f);
    CHECK(out[kBlockFrames + 255] == 1755.0f / 1024.0f);
    CHECK(out[2 * kBlockFrames] == 1999.0f / 1024.0f);  // clamped to the last frame
}

TEST_CASE("Control: a pitch change ramps without a jump in the output", "[audio][control][ramp]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig()) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtRamp(100000);
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetPitch(id, 2.0f, 1) == MixerStatus::Ok);
    const Vector<f32> out = CtRender(mixer, 3);
    // The speed grows from 1 to 2 frames per frame over 64 frames and then stays: steps of the ramp clip are the speed
    // over 1024.
    for (u32 n = 1; n < out.Size(); ++n)
    {
        const f32 step = out[n] - out[n - 1];
        REQUIRE(step >= 1.0f / 1024.0f - 1e-6f);
        REQUIRE(step <= 2.0f / 1024.0f + 1e-6f);
    }
    f64 position = 0.0;
    REQUIRE(mixer.Position(id, &position) == MixerStatus::Ok);
    CHECK(position > 256.0 + 64.0 * 1.5 + 2.0 * 448.0 - 3.0);
    CHECK(position < 256.0 + 64.0 * 1.5 + 2.0 * 448.0 + 3.0);
}

TEST_CASE("Control: loop, finish, steal and virtualise are reported as events, in order", "[audio][control][events]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig(0, 1)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId looping;
    REQUIRE(mixer.Play(desc, &looping, 0) == MixerStatus::Ok);
    REQUIRE(mixer.SetVirtual(looping, true, 1) == MixerStatus::Ok);
    CtRender(mixer, 2);
    VoiceEvent events[8];
    u32 count = CtEvents(mixer, events, 8);
    REQUIRE(count == 3);
    CHECK(events[0].type == VoiceEventType::Looped);
    CHECK(events[1].type == VoiceEventType::Virtualised);
    CHECK(events[2].type == VoiceEventType::Looped);
    // A new voice of equal rank takes the only slot: the old one is reported stolen.
    desc.loop = false;
    VoiceId second;
    REQUIRE(mixer.Play(desc, &second, 2) == MixerStatus::Ok);
    CHECK(second.index == looping.index);
    count = CtEvents(mixer, events, 8);
    REQUIRE(count == 1);
    CHECK(events[0].type == VoiceEventType::Stolen);
    CHECK(events[0].id == looping);
    CtRender(mixer, 2);
    count = CtEvents(mixer, events, 8);
    REQUIRE(count == 2);
    CHECK(events[0].type == VoiceEventType::Stopped);  // the stolen voice is stopped at once, ahead of the new one
    CHECK(events[0].id == looping);
    CHECK(events[1].type == VoiceEventType::Finished);
    CHECK(events[1].id == second);
    CHECK(mixer.Stats().voices_stolen == 1);
    CHECK(mixer.Stats().voices_finished == 1);
}

TEST_CASE("Control: a full command queue returns a status and counts, and recovers", "[audio][control][queue]")
{
    MixerConfig config = CtConfig();
    config.command_capacity = 4;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    u32 accepted = 0;
    MixerStatus last = MixerStatus::Ok;
    for (u32 i = 0; i < 10; ++i)
    {
        last = mixer.SetGain(id, 0.5f, 0);
        if (last == MixerStatus::Ok)
        {
            accepted += 1;
        }
    }
    CHECK(last == MixerStatus::QueueFull);
    CHECK(accepted == 3);  // the play plus three setters fill four slots
    CHECK(mixer.Stats().commands_dropped == 7);
    CtRender(mixer, 1);
    CHECK(mixer.SetGain(id, 0.25f, 0) == MixerStatus::Ok);
    // A play that does not fit leaves no voice behind.
    VoiceId other;
    for (u32 i = 0; i < 3; ++i)
    {
        REQUIRE(mixer.SetGain(id, 0.5f, 0) == MixerStatus::Ok);
    }
    CHECK(mixer.Play(desc, &other, 0) == MixerStatus::QueueFull);
    CtRender(mixer, 1);
    CHECK(mixer.VoiceCount() == 1);
    CHECK(mixer.Play(desc, &other, 0) == MixerStatus::Ok);
}

TEST_CASE("Control: a command that arrives late is applied at the current block and counted", "[audio][control][queue]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(CtConfig(0)) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id, 0) == MixerStatus::Ok);
    CtRender(mixer, 3);
    REQUIRE(mixer.SetGain(id, 0.5f, 1) == MixerStatus::Ok);  // block 1 has passed
    const Vector<f32> out = CtRender(mixer, 1);
    CHECK(out[0] == 0.5f);
    CHECK(mixer.Stats().late_commands == 1);
    CHECK(mixer.NextBlock() == 4);
}

TEST_CASE("Control: the default stamp gives the audio thread a block of notice", "[audio][control][queue]")
{
    MixerConfig config = CtConfig(0);
    config.lead_blocks = 1;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtConstant(1.0f, 100);
    desc.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);  // stamped for block 1
    const Vector<f32> out = CtRender(mixer, 2);
    CHECK(out[0] == 0.0f);
    CHECK(out[kBlockFrames] == 1.0f);
    CHECK(mixer.Stats().late_commands == 0);
}

TEST_CASE("Control: a random command stream agrees with a model of the voices", "[audio][control][model]")
{
    Mixer mixer;
    MixerConfig config = CtConfig(0, 6);
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = CtRamp(5000);
    desc.loop = true;
    struct Entry
    {
        VoiceId id;
        bool alive = false;
    };
    Entry model[6];
    u32 state = 12345;
    auto next = [&state]()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    RenderHash hash;
    Vector<f32> block;
    for (u32 i = 0; i < kBlockFrames; ++i)
    {
        block.Push(0.0f);
    }
    for (u32 step = 0; step < 1000; ++step)
    {
        const u32 op = next() % 8;
        const u32 which = next() % 6;
        Entry& entry = model[which];
        switch (op)
        {
        case 0:
        case 1:
        {
            VoiceId id;
            desc.priority = static_cast<i32>(next() % 3);
            desc.gain = 0.1f + static_cast<f32>(next() % 10) * 0.1f;
            if (mixer.Play(desc, &id, 0) == MixerStatus::Ok)
            {
                // The new voice may have taken the slot of a model entry (stolen) or a free one.
                for (Entry& e : model)
                {
                    if (e.alive && e.id.index == id.index)
                    {
                        e.alive = false;
                    }
                }
                Entry& slot = model[id.index];
                slot.id = id;
                slot.alive = true;
            }
            break;
        }
        case 2:
            if (entry.alive)
            {
                REQUIRE(mixer.Stop(entry.id, 0, 0) == MixerStatus::Ok);
                entry.alive = false;
            }
            break;
        case 3:
            if (entry.alive)
            {
                REQUIRE(mixer.SetGain(entry.id, 0.2f * static_cast<f32>(next() % 5), 0) == MixerStatus::Ok);
            }
            break;
        case 4:
            if (entry.alive)
            {
                REQUIRE(mixer.SetPitch(entry.id, 0.5f + static_cast<f32>(next() % 4) * 0.5f, 0) == MixerStatus::Ok);
            }
            break;
        case 5:
            if (entry.alive)
            {
                REQUIRE(mixer.Seek(entry.id, next() % 5000, 0) == MixerStatus::Ok);
            }
            break;
        case 6:
            if (entry.alive)
            {
                REQUIRE(mixer.Pause(entry.id, 0) == MixerStatus::Ok);
            }
            break;
        default:
            if (entry.alive)
            {
                REQUIRE(mixer.Resume(entry.id, 0) == MixerStatus::Ok);
            }
            break;
        }
        if (step % 5 == 4)
        {
            mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
            hash.AddBlock(Span<const f32>(block.Data(), block.Size()));
            u32 alive = 0;
            for (const Entry& e : model)
            {
                REQUIRE(mixer.IsPlaying(e.id) == e.alive);
                alive += e.alive ? 1 : 0;
            }
            REQUIRE(mixer.VoiceCount() == alive);
        }
    }
    CHECK(hash.Valid());
}

namespace
{

// Renders a scripted stream of commands with explicit stamps through a producer thread that posts in bursts with
// `pause_every` yields between them, and a renderer that waits for the watermark; returns the hash.
u64 CtThreadedHash(u32 yields_between_posts)
{
    MixerConfig config = CtConfig(64, 8);
    config.offline = true;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    VoiceDesc desc;
    Vector<f32> tone;
    for (u32 i = 0; i < 6000; ++i)
    {
        tone.Push(0.5f * Sin(0.05f * static_cast<f32>(i)));
    }
    desc.clip = CtClip(tone);
    desc.loop = true;
    const u32 kControlBlocks = 40;
    Thread producer(
        [&]
        {
            VoiceId ids[3];
            for (u32 block = 0; block < kControlBlocks; ++block)
            {
                if (block == 0)
                {
                    for (u32 v = 0; v < 3; ++v)
                    {
                        VoiceDesc d = desc;
                        d.pitch = 1.0f + 0.25f * static_cast<f32>(v);
                        d.pan = -0.5f + 0.5f * static_cast<f32>(v);
                        while (mixer.Play(d, &ids[v], 0) == MixerStatus::QueueFull)
                        {
                            ThisThread::Yield();
                        }
                    }
                }
                if (block % 4 == 2)
                {
                    mixer.SetGain(ids[block % 3], 0.2f + 0.05f * static_cast<f32>(block % 7), block);
                    mixer.SetPan(ids[(block + 1) % 3], -1.0f + 0.1f * static_cast<f32>(block % 20), block);
                }
                if (block == 20)
                {
                    mixer.Pause(ids[1], block);
                }
                if (block == 30)
                {
                    mixer.Resume(ids[1], block);
                    mixer.Seek(ids[2], 1234, block);
                }
                for (u32 y = 0; y < yields_between_posts; ++y)
                {
                    ThisThread::Yield();
                }
                mixer.SetWatermark(block);
            }
        });
    RenderHash hash;
    Vector<f32> out;
    for (u32 i = 0; i < kBlockFrames; ++i)
    {
        out.Push(0.0f);
    }
    for (u32 block = 0; block < kControlBlocks; ++block)
    {
        while (!mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())))
        {
            ThisThread::Yield();
        }
        hash.AddBlock(Span<const f32>(out.Data(), out.Size()));
    }
    producer.Join();
    REQUIRE(hash.Valid());
    return hash.Value();
}

}  // namespace

TEST_CASE("Control: an offline mixer waits for the watermark before it renders a block", "[audio][control][threads]")
{
    MixerConfig config = CtConfig();
    config.offline = true;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    Vector<f32> out;
    for (u32 i = 0; i < kBlockFrames; ++i)
    {
        out.Push(0.0f);
    }
    CHECK(!mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));  // nothing is posted for block 0 yet
    mixer.SetWatermark(0);
    CHECK(mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));
    CHECK(!mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));
    mixer.SetWatermark(2);
    CHECK(mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));
    CHECK(mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));
    CHECK(!mixer.TryRenderBlock(Span<f32>(out.Data(), out.Size())));
}

TEST_CASE("Control: the render does not depend on the producer thread's timing", "[audio][control][threads]")
{
    const u64 reference = CtThreadedHash(0);
    CHECK(CtThreadedHash(1) == reference);
    CHECK(CtThreadedHash(50) == reference);
    CHECK(CtThreadedHash(500) == reference);
    CHECK(CtThreadedHash(0) == reference);
}
