// Stress and the perf smoke (Phase 26 Day 442): a small mixer scene with a generous bound (a gross regression fails
// here; the real numbers are `audio_bench`'s), random commands from four threads against a running audio thread, and
// clips of every length and many rates. The Release-only long run is `audio_bench --scene large`.
#include <catch2/catch_test_macros.hpp>

#include "audio/filter.h"
#include "audio/mixer.h"
#include "containers/vector.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::audio;

namespace
{

struct StRng
{
    u64 state = 0x9e3779b97f4a7c15ull;
    f32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

AudioClipPtr StTone(u32 frames, u32 rate, u32 channels = 1)
{
    Vector<f32> samples;
    samples.Reserve(static_cast<usize>(frames) * channels);
    for (u32 i = 0; i < frames; ++i)
    {
        const f32 v = 0.25f * Sin(0.05f * static_cast<f32>(i));
        for (u32 c = 0; c < channels; ++c)
        {
            samples.Push(v);
        }
    }
    ClipDesc desc;
    desc.format.sample_rate = rate;
    desc.format.layout = channels == 1 ? ChannelLayout::Mono : ChannelLayout::Stereo;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "tone", &clip) ==
            ClipStatus::Ok);
    return clip;
}

bool StFinite(const Vector<f32>& block)
{
    for (usize i = 0; i < block.Size(); ++i)
    {
        if (!(block[i] > -64.0f && block[i] < 64.0f))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Perf smoke: 200 looping voices render a block in bounded time", "[audio][perf]")
{
    MixerConfig config;
    config.max_voices = 256;
    config.lead_blocks = 0;
    config.command_capacity = 1024;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    const AudioClipPtr clip = StTone(48000, 48000);
    for (u32 i = 0; i < 200; ++i)
    {
        VoiceDesc d;
        d.clip = clip;
        d.loop = true;
        d.gain = 0.01f;
        d.pan = static_cast<f32>(i % 21) / 10.0f - 1.0f;
        VoiceId id;
        REQUIRE(mixer.Play(d, &id) == MixerStatus::Ok);
    }
    Vector<f32> out;
    out.Resize(static_cast<usize>(kBlockFrames) * 2);
    const u64 start = os::MonotonicTimeNanos();
    for (u32 b = 0; b < 100; ++b)
    {
        mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
    }
    const double per_block_ms = static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e6 / 100.0;
    INFO("per block: " << per_block_ms << " ms");
    // A Release block of this scene takes well under a millisecond; sanitizers are slower by a small factor.
    REQUIRE(per_block_ms < 50.0);
    CHECK(mixer.Stats().voices_mixed == 200);
}

TEST_CASE("Stress: random commands from four threads against a running audio thread", "[audio][stress]")
{
    MixerConfig config;
    config.max_voices = 64;
    config.lead_blocks = 1;
    config.command_capacity = 4096;
    config.event_capacity = 4096;
    config.limiter = true;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    const AudioClipPtr clip = StTone(9000, 48000);
    Mutex control;  // one control thread at a time, as the mixer's contract asks; the audio thread is separate
    Atomic<bool> stop{false};
    Atomic<bool> bad{false};
    Atomic<u64> rendered{0};

    Thread audio(
        [&]()
        {
            Vector<f32> out;
            out.Resize(static_cast<usize>(kBlockFrames) * 2);
            while (!stop.Load(MemoryOrder::Acquire))
            {
                mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
                rendered.FetchAdd(1, MemoryOrder::Relaxed);
                if (!StFinite(out))
                {
                    bad.Store(true, MemoryOrder::Release);
                }
            }
        });

    const u32 kThreads = 4;
    const u32 kCommands = 2500;  // ten thousand in all
    Vector<Thread> threads;
    for (u32 t = 0; t < kThreads; ++t)
    {
        threads.Push(Thread(
            [&, t]()
            {
                StRng rng;
                rng.state += t * 7919u;
                Vector<VoiceId> ids;
                for (u32 n = 0; n < kCommands; ++n)
                {
                    LockGuard<Mutex> lock(control);
                    const f32 pick = rng.Next();
                    if (ids.Size() < 8 || pick < 0.2f)
                    {
                        VoiceDesc d;
                        d.clip = clip;
                        d.loop = rng.Next() < 0.5f;
                        d.gain = rng.Next() * 0.2f;
                        VoiceId id;
                        if (mixer.Play(d, &id) == MixerStatus::Ok)
                        {
                            ids.Push(id);
                        }
                        continue;
                    }
                    const VoiceId id = ids[static_cast<usize>(rng.Next() * 7.99f)];
                    if (pick < 0.4f)
                    {
                        mixer.SetGain(id, rng.Next() * 0.3f);
                    }
                    else if (pick < 0.6f)
                    {
                        mixer.SetPan(id, rng.Next() * 2.0f - 1.0f);
                    }
                    else if (pick < 0.75f)
                    {
                        mixer.SetPitch(id, 0.5f + rng.Next() * 1.5f);
                    }
                    else if (pick < 0.85f)
                    {
                        mixer.Seek(id, static_cast<u64>(rng.Next() * 9000.0f));
                    }
                    else if (pick < 0.95f)
                    {
                        mixer.Stop(id);
                    }
                    else
                    {
                        mixer.Pause(id);
                        mixer.Resume(id);
                    }
                    VoiceEvent events[32];
                    mixer.PollEvents(events, 32);
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i].Join();
    }
    // Let the audio thread apply what is left, then stop it.
    const u64 mark = rendered.Load(MemoryOrder::Acquire);
    while (rendered.Load(MemoryOrder::Acquire) < mark + 8)
    {
        ThisThread::Yield();
    }
    stop.Store(true, MemoryOrder::Release);
    audio.Join();
    CHECK_FALSE(bad.Load(MemoryOrder::Acquire));
    const MixerStats stats = mixer.Stats();
    CHECK(stats.commands_applied > 0);
    CHECK(stats.voices_mixed <= 64);
}

TEST_CASE("Stress: a clip of every length and rate plays to its end", "[audio][stress]")
{
    const u32 rates[] = {8000, 11025, 22050, 44100, 48000, 96000, 192000, 384000};
    const u32 lengths[] = {1, 2, 3, 7, 16, 255, 256, 257, 1000, 4095, 4096};
    for (u32 rate : rates)
    {
        for (u32 frames : lengths)
        {
            MixerConfig config;
            config.max_voices = 4;
            config.lead_blocks = 0;
            Mixer mixer;
            REQUIRE(mixer.Init(config) == MixerStatus::Ok);
            const AudioClipPtr clip = StTone(frames, rate);
            VoiceDesc d;
            d.clip = clip;
            VoiceId id;
            REQUIRE(mixer.Play(d, &id) == MixerStatus::Ok);
            Vector<f32> out;
            out.Resize(static_cast<usize>(kBlockFrames) * 2);
            // At the lowest rate and the longest clip: 4096 * 48000 / 8000 frames, about 100 blocks; bound it.
            for (u32 b = 0; b < 400 && mixer.IsPlaying(id); ++b)
            {
                mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
                REQUIRE(StFinite(out));
            }
            CHECK_FALSE(mixer.IsPlaying(id));
        }
    }
}

TEST_CASE("Stress: an empty clip is refused", "[audio][stress]")
{
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = 0;
    AudioClipPtr clip;
    CHECK(AudioClip::CreateCopy(desc, Span<const f32>(), "empty", &clip) == ClipStatus::BadFrameCount);
}

TEST_CASE("Stress: an insert posted for another channel count is refused after a device switch", "[audio][stress]")
{
    MixerConfig config;
    config.layout = ChannelLayout::Surround71;
    config.max_voices = 4;
    config.lead_blocks = 1;
    Mixer mixer;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    // Posted for eight channels (valid then), applied after the mixer became stereo.
    REQUIRE(mixer.SetInsert(kMasterBus, 0, MakeFilterEffect(8, 0)) == MixerStatus::Ok);
    REQUIRE(mixer.Reconfigure(48000, ChannelLayout::Stereo) == MixerStatus::Ok);
    Vector<f32> out;
    out.Resize(static_cast<usize>(kBlockFrames) * 2);
    const AudioClipPtr clip = StTone(2000, 48000);
    VoiceDesc d;
    d.clip = clip;
    d.loop = true;
    VoiceId id;
    REQUIRE(mixer.Play(d, &id) == MixerStatus::Ok);
    for (u32 b = 0; b < 4; ++b)
    {
        mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));  // under asan an eight-channel insert would overrun
        CHECK(StFinite(out));
    }
}
