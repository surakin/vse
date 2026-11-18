// audio_bench: the Phase 26 Day 442 benchmark of the mixer. One line per measurement: the time per block (256 frames
// at 48 kHz is a 5.33 ms deadline), the worst block and the share of the deadline used. The numbers are recorded in
// plans/phase-26-audio/day-442-benchmarks-stress-and-limits.md and docs/guide/audio.md ("Cost and limits"). The `large`
// scene is the Release-only stress run: it checks the output as it goes and exits 1 when a check fails (ctest
// `audio_bench_large`, registered in optimized builds only).
//
//   audio_bench [--voices N]  the voice count of the voice scenes (default: 100, 1000 and 4000)
//               [--blocks N]  timed blocks per measurement (default 400)
//               [--scene S]   voices, filters, reverb, surround, streams, large or all (default all; `all` leaves out
//               `large`)
//               [--long-blocks N] the blocks of `large` (default 10000000)
#include "audio/block_hash.h"
#include "audio/clip.h"
#include "audio/delay.h"
#include "audio/filter.h"
#include "audio/mixer.h"
#include "audio/stream.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "os/console.h"
#include "os/os.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

u32 g_blocks = 400;

struct Rng
{
    u64 state = 0x2545f4914f6cdd1dull;
    f32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

// A two-second mono clip of a few summed tones (so the resamplers have something to interpolate).
AudioClipPtr MakeTone(u32 channels)
{
    Vector<f32> samples;
    const u32 frames = 96000;
    samples.Reserve(static_cast<usize>(frames) * channels);
    for (u32 i = 0; i < frames; ++i)
    {
        const f32 t = static_cast<f32>(i) / 48000.0f;
        const f32 v = 0.2f * Sin(6.2831853f * 220.0f * t) + 0.1f * Sin(6.2831853f * 1330.0f * t);
        for (u32 c = 0; c < channels; ++c)
        {
            samples.Push(v);
        }
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = channels == 1 ? ChannelLayout::Mono : ChannelLayout::Stereo;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    if (AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "tone", &clip) != ClipStatus::Ok)
    {
        os::PrintError("audio_bench: clip creation failed\n");
    }
    return clip;
}

MixerConfig Config(ChannelLayout layout, ResamplerQuality quality, u32 voices)
{
    MixerConfig config;
    config.layout = layout;
    config.quality = quality;
    config.max_voices = voices;
    config.lead_blocks = 0;
    config.offline = false;
    config.command_capacity = 8192;  // every Play of a scene is posted before the first block
    return config;
}

struct Timing
{
    double mean_ms = 0.0;
    double worst_ms = 0.0;
};

// Renders `blocks` blocks (after a warm-up of ten) and times each one.
Timing TimeBlocks(Mixer& mixer, u32 channels, u32 blocks, StreamPump* pump = nullptr)
{
    Vector<f32> out;
    out.Resize(static_cast<usize>(kBlockFrames) * channels);
    for (u32 i = 0; i < 10; ++i)
    {
        if (pump != nullptr)
        {
            pump->Run();
        }
        mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
    }
    Timing t;
    u64 total = 0;
    u64 worst = 0;
    for (u32 i = 0; i < blocks; ++i)
    {
        if (pump != nullptr)
        {
            pump->Run();
        }
        const u64 start = os::MonotonicTimeNanos();
        mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
        const u64 ns = os::MonotonicTimeNanos() - start;
        total += ns;
        worst = ns > worst ? ns : worst;
    }
    t.mean_ms = static_cast<double>(total) / 1.0e6 / static_cast<double>(blocks);
    t.worst_ms = static_cast<double>(worst) / 1.0e6;
    return t;
}

void Report(const char* label, u32 voices, const Timing& t)
{
    const double deadline_ms = 1000.0 * static_cast<double>(kBlockFrames) / 48000.0;
    os::PrintError("%-28s %5u voices: %8.4f ms/block (%5.1f%% of the %.2f ms deadline), worst %8.4f ms\n", label,
                   voices, t.mean_ms, 100.0 * t.mean_ms / deadline_ms, deadline_ms, t.worst_ms);
}

void Start(Mixer& mixer, const AudioClipPtr& clip, u32 voices, BusId bus, BusId send, f32 send_gain, bool directional)
{
    Rng rng;
    for (u32 i = 0; i < voices; ++i)
    {
        VoiceDesc d;
        d.clip = clip;
        d.loop = true;
        d.gain = 0.02f;
        d.pan = rng.Next() * 2.0f - 1.0f;
        d.pitch = 0.75f + rng.Next() * 0.5f;
        d.start_frame = static_cast<u64>(rng.Next() * 40000.0f);
        d.bus = bus;
        d.send_bus = send;
        d.send_gain = send_gain;
        d.directional = directional;
        d.direction_x = rng.Next() * 2.0f - 1.0f;
        d.direction_z = rng.Next() * 2.0f - 1.0f;
        VoiceId id;
        mixer.Play(d, &id);
    }
}

void SceneVoices(u32 only)
{
    const AudioClipPtr clip = MakeTone(1);
    const u32 counts[] = {100, 1000, 4000};
    const ResamplerQuality qualities[] = {ResamplerQuality::Linear, ResamplerQuality::Hermite, ResamplerQuality::Sinc};
    const char* names[] = {"voices linear", "voices hermite", "voices sinc"};
    for (u32 q = 0; q < 3; ++q)
    {
        for (u32 n : counts)
        {
            if (only != 0 && n != only)
            {
                continue;
            }
            Mixer mixer;
            mixer.Init(Config(ChannelLayout::Stereo, qualities[q], n));
            Start(mixer, clip, n, BusId{0}, BusId{0xFFFFFFFFu}, 0.0f, false);
            Report(names[q], n, TimeBlocks(mixer, 2, g_blocks));
        }
    }
}

void SceneFilters(u32 voices)
{
    const AudioClipPtr clip = MakeTone(1);
    Mixer mixer;
    mixer.Init(Config(ChannelLayout::Stereo, ResamplerQuality::Hermite, voices));
    BusId sfx;
    mixer.CreateBus("sfx", kMasterBus, &sfx, 0);
    mixer.SetInsert(sfx, 0, MakeFilterEffect(2, 64), 0);
    mixer.SetInsertParams(sfx, 0, DesignFilter(FilterType::LowPass, 48000, 4000.0f, 0.7071f, 0.0f).params, 0);
    mixer.SetInsert(sfx, 1, MakeFilterEffect(2, 64), 0);
    mixer.SetInsertParams(sfx, 1, DesignFilter(FilterType::Peaking, 48000, 1000.0f, 1.0f, 6.0f).params, 0);
    Start(mixer, clip, voices, sfx, BusId{0xFFFFFFFFu}, 0.0f, false);
    Report("voices + 2 bus filters", voices, TimeBlocks(mixer, 2, g_blocks));
}

void SceneReverb(u32 voices)
{
    const AudioClipPtr clip = MakeTone(1);
    Mixer mixer;
    mixer.Init(Config(ChannelLayout::Stereo, ResamplerQuality::Hermite, voices));
    BusId aux;
    mixer.CreateBus("reverb", kMasterBus, &aux, 0);
    mixer.SetInsert(aux, 0, MakeReverbEffect(2, ReverbPresetDesc(ReverbPreset::Hall, 48000)), 0);
    Start(mixer, clip, voices, BusId{0}, aux, 0.3f, false);
    Report("voices + reverb send", voices, TimeBlocks(mixer, 2, g_blocks));
}

void SceneSurround(u32 voices)
{
    const AudioClipPtr clip = MakeTone(1);
    Mixer mixer;
    mixer.Init(Config(ChannelLayout::Surround51, ResamplerQuality::Hermite, voices));
    Start(mixer, clip, voices, BusId{0}, BusId{0xFFFFFFFFu}, 0.0f, true);
    Report("voices directional 5.1", voices, TimeBlocks(mixer, 6, g_blocks));
}

void SceneStreams(u32 count)
{
    jobs::Scheduler::Config sc;
    sc.participants = 1;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    StreamPump pump(scheduler);
    const AudioClipPtr clip = MakeTone(1);
    Mixer mixer;
    mixer.Init(Config(ChannelLayout::Stereo, ResamplerQuality::Hermite, count));
    for (u32 i = 0; i < count; ++i)
    {
        UniquePtr<StreamSource> source;
        MakeMemoryStream(clip, &source);
        StreamConfig config;
        config.id = i + 1;
        StreamBufferPtr buffer;
        StreamBuffer::Create(Move(source), config, &buffer);
        buffer->SetLoop(true);
        buffer->Refill();
        pump.Add(buffer);
        VoiceDesc d;
        d.clip = buffer->Clip();
        d.stream = buffer;
        d.gain = 0.02f;
        VoiceId id;
        mixer.Play(d, &id);
    }
    Report("streams (refill included)", count, TimeBlocks(mixer, 2, g_blocks, &pump));
}

// The stress run: a looping scene whose parameters change at random; checks that every sample is finite and bounded.
bool SceneLarge(u64 blocks)
{
    const AudioClipPtr clip = MakeTone(1);
    Mixer mixer;
    MixerConfig config = Config(ChannelLayout::Stereo, ResamplerQuality::Hermite, 64);
    config.limiter = true;
    mixer.Init(config);
    Rng rng;
    Vector<VoiceId> ids;
    for (u32 i = 0; i < 48; ++i)
    {
        VoiceDesc d;
        d.clip = clip;
        d.loop = true;
        d.gain = 0.05f;
        d.pan = rng.Next() * 2.0f - 1.0f;
        VoiceId id;
        mixer.Play(d, &id);
        ids.Push(id);
    }
    Vector<f32> out;
    out.Resize(static_cast<usize>(kBlockFrames) * 2);
    const u64 start = os::MonotonicTimeNanos();
    bool ok = true;
    for (u64 b = 0; b < blocks && ok; ++b)
    {
        const VoiceId id = ids[static_cast<usize>(rng.Next() * 47.99f)];
        switch (b % 5)
        {
        case 0: mixer.SetGain(id, rng.Next() * 0.2f); break;
        case 1: mixer.SetPan(id, rng.Next() * 2.0f - 1.0f); break;
        case 2: mixer.SetPitch(id, 0.5f + rng.Next() * 1.5f); break;
        case 3: mixer.Seek(id, static_cast<u64>(rng.Next() * 90000.0f)); break;
        default: mixer.SetLoop(id, true); break;
        }
        mixer.RenderBlock(Span<f32>(out.Data(), out.Size()));
        for (usize i = 0; i < out.Size(); ++i)
        {
            if (!(out[i] > -4.0f && out[i] < 4.0f))
            {
                os::PrintError("large: sample %zu of block %llu is %f\n", static_cast<size_t>(i),
                               static_cast<unsigned long long>(b), static_cast<double>(out[i]));
                ok = false;
                break;
            }
        }
        if (b % 1000000 == 0)
        {
            VoiceEvent events[64];
            while (mixer.PollEvents(events, 64) > 0)
            {
            }
        }
    }
    const MixerStats stats = mixer.Stats();
    if (stats.voices_mixed != 48 || stats.commands_dropped != 0)
    {
        os::PrintError("large: %u voices mixed (48 expected), %llu commands dropped\n", stats.voices_mixed,
                       static_cast<unsigned long long>(stats.commands_dropped));
        ok = false;
    }
    os::PrintError("large: %llu blocks in %.1f s, %llu commands, %llu late\n", static_cast<unsigned long long>(blocks),
                   static_cast<double>(os::MonotonicTimeNanos() - start) / 1.0e9,
                   static_cast<unsigned long long>(stats.commands_applied),
                   static_cast<unsigned long long>(stats.late_commands));
    os::PrintError("large: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    static const os::CommandLineOption kCliOptions[] = {
        {"voices", true},      {"blocks", true},           {"scene", true},
        {"long-blocks", true}, debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 6));
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("usage: %s [--voices N] [--blocks N] [--scene voices|filters|reverb|surround|streams|large|all] "
                       "[--long-blocks N]\n",
                       line.ProgramName().Data());
        return 2;
    }
    u32 voices = 0;
    const char* scene = "all";
    u64 long_blocks = 10000000;
    StringView value;
    if (line.TryGetOption("voices", value))
    {
        voices = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("blocks", value))
    {
        g_blocks = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("scene", value))
    {
        scene = value.Data();
    }
    if (line.TryGetOption("long-blocks", value))
    {
        long_blocks = static_cast<u64>(CStrToLong(value.Data()));
    }
    os::PrintError("audio_bench: %u timed blocks of %u frames at 48 kHz\n", g_blocks, static_cast<u32>(kBlockFrames));
    const bool all = CStrCompare(scene, "all") == 0;
    if (all || CStrCompare(scene, "voices") == 0)
    {
        SceneVoices(voices);
    }
    if (all || CStrCompare(scene, "filters") == 0)
    {
        SceneFilters(voices != 0 ? voices : 1000);
    }
    if (all || CStrCompare(scene, "reverb") == 0)
    {
        SceneReverb(voices != 0 ? voices : 1000);
    }
    if (all || CStrCompare(scene, "surround") == 0)
    {
        SceneSurround(voices != 0 ? voices : 1000);
    }
    if (all || CStrCompare(scene, "streams") == 0)
    {
        SceneStreams(voices != 0 ? voices : 100);
    }
    if (CStrCompare(scene, "large") == 0 && !SceneLarge(long_blocks))
    {
        return 1;
    }
    return 0;
}
