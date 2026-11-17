// The audio snapshot (Phase 26 Day 437): a mixer saved at a block and restored into a fresh one renders the same
// samples from there on, hostile bytes are refused by value, and a failed restore leaves the mixer as it was.
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/delay.h"
#include "audio/filter.h"
#include "audio/mixer.h"
#include "audio/stream.h"
#include "build_defs.h"
#include "containers/vector.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "os/os.h"
#include "platform_hash.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

AudioClipPtr SnClip(const char* name, u32 frames, f32 (*fn)(u32), u64 loop_start = 0, u64 loop_end = 0)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(fn(i));
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    desc.loop_start = loop_start;
    desc.loop_end = loop_end;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), name, &clip) ==
            ClipStatus::Ok);
    return clip;
}

f32 SnTone(u32 i)
{
    return 0.3f * Sin(0.07f * static_cast<f32>(i)) + 0.1f * Sin(0.31f * static_cast<f32>(i));
}

f32 SnChirp(u32 i)
{
    return 0.25f * Sin(0.00005f * static_cast<f32>(i) * static_cast<f32>(i));
}

f32 SnRamp(u32 i)
{
    return static_cast<f32>(i + 1) / 4096.0f;
}

class SnResolver final : public SnapshotResolver
{
public:
    AudioClipConstPtr tone;
    AudioClipConstPtr chirp;
    AudioClipConstPtr ramp;
    StreamBufferPtr stream;

    AudioClipConstPtr FindClip(StringView name, u32 frames) override
    {
        for (const AudioClipConstPtr* c : {&tone, &chirp, &ramp})
        {
            if (*c && StringView((*c)->Name()) == name && (*c)->Frames() == frames)
            {
                return *c;
            }
        }
        return AudioClipConstPtr();
    }
    StreamBufferPtr FindStream(StringView name) override
    {
        return stream && StringView(stream->Clip()->Name()) == name ? stream : StreamBufferPtr();
    }
};

struct SnScene
{
    Mixer mixer;
    BusId music;
    BusId fx;
    Vector<VoiceId> ids;
    SnResolver clips;
};

// The topology only: two buses with a delay and a reverb, a send between them, the limiter on. No voices.
void SnTopology(SnScene& s)
{
    MixerConfig config;
    config.layout = ChannelLayout::Stereo;
    config.quality = ResamplerQuality::Hermite;
    config.max_voices = 16;
    config.ramp_frames = 256;
    config.lead_blocks = 0;
    config.limiter = true;
    REQUIRE(s.mixer.Init(config) == MixerStatus::Ok);
    REQUIRE(s.mixer.CreateBus("music", BusId{0}, &s.music) == MixerStatus::Ok);
    REQUIRE(s.mixer.CreateBus("fx", BusId{0}, &s.fx) == MixerStatus::Ok);
    DelayDesc delay;
    delay.delay_seconds = 0.12f;
    delay.feedback = 0.5f;
    delay.damping = 0.3f;
    REQUIRE(s.mixer.SetInsert(s.music, 0, MakeDelayEffect(2, 48000, 0.5f, 256)) == MixerStatus::Ok);
    REQUIRE(s.mixer.SetInsertParams(s.music, 0, MakeDelayParams(48000, delay)) == MixerStatus::Ok);
    ReverbDesc reverb;
    REQUIRE(s.mixer.SetInsert(s.fx, 0, MakeReverbEffect(2, reverb)) == MixerStatus::Ok);
    REQUIRE(s.mixer.SetInsert(s.fx, 1, MakeFilterEffect(2, 256)) == MixerStatus::Ok);
    REQUIRE(s.mixer.SetBusSend(s.music, 0, s.fx, 0.4f, false) == MixerStatus::Ok);
    s.clips.tone = SnClip("tone", 4000, SnTone, 100, 3900);
    s.clips.chirp = SnClip("chirp", 6000, SnChirp);
    s.clips.ramp = SnClip("ramp", 90000, SnRamp);
}

// The setup of block 0 and the log after it.
void SnApply(SnScene& s, u32 block)
{
    if (block == 0)
    {
        for (u32 v = 0; v < 8; ++v)
        {
            VoiceDesc desc;
            desc.clip = (v & 1) ? s.clips.chirp : s.clips.tone;
            desc.loop = true;
            desc.pitch = 0.6f + 0.17f * static_cast<f32>(v);
            desc.pan = -0.9f + 0.25f * static_cast<f32>(v);
            desc.gain = 0.3f;
            desc.bus = v < 5 ? s.music : s.fx;
            VoiceId id;
            REQUIRE(s.mixer.Play(desc, &id) == MixerStatus::Ok);
            s.ids.Push(id);
        }
    }
    if (block == 8)
    {
        REQUIRE(s.mixer.Fade(s.ids[2], 0.1f, 4000) == MixerStatus::Ok);  // a ramp in flight at the snapshot
    }
    if (block == 22)
    {
        VoiceDesc desc;
        desc.clip = s.clips.ramp;
        desc.gain = 0.2f;
        desc.bus = s.music;
        VoiceId id;
        REQUIRE(s.mixer.Play(desc, &id) == MixerStatus::Ok);
        s.ids.Push(id);
    }
    if (block == 24)
    {
        REQUIRE(s.mixer.SetBusGain(s.music, 0.5f) == MixerStatus::Ok);
    }
    if (block == 25)
    {
        REQUIRE(s.mixer.SetPitch(s.ids[0], 1.3f) == MixerStatus::Ok);
        REQUIRE(s.mixer.Stop(s.ids[1], 512) == MixerStatus::Ok);
    }
}

// Applies the log and renders blocks [from, to); a later restart applies only the entries after `from`.
Vector<f32> SnRun(SnScene& s, u32 from, u32 to)
{
    Vector<f32> out;
    Vector<f32> block;
    block.Resize(kBlockFrames * 2);
    for (u32 b = from; b < to; ++b)
    {
        SnApply(s, b);
        s.mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            out.Push(block[i]);
        }
    }
    return out;
}

bool SnSame(const Vector<f32>& a, const Vector<f32>& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        u32 x = 0;
        u32 y = 0;
        MemCopy(&x, &a[i], 4);
        MemCopy(&y, &b[i], 4);
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

Vector<byte> SnCopy(const Vector<byte>& in)
{
    Vector<byte> out;
    for (usize i = 0; i < in.Size(); ++i)
    {
        out.Push(in[i]);
    }
    return out;
}

Vector<byte> SnSave(const Mixer& mixer)
{
    bytes::ByteWriter w;
    REQUIRE(mixer.SaveState(&w) == SnapshotStatus::Ok);
    Vector<byte> out;
    for (usize i = 0; i < w.Size(); ++i)
    {
        out.Push(w.Bytes()[i]);
    }
    return out;
}

}  // namespace

TEST_CASE("Snapshot: a restored mixer renders the same samples as the original from the block it was saved at",
          "[audio][snapshot]")
{
    SnScene original;
    SnTopology(original);
    const Vector<f32> head = SnRun(original, 0, 20);
    REQUIRE(head.Size() > 0);
    const Vector<byte> snapshot = SnSave(original.mixer);
    const Vector<f32> tail = SnRun(original, 20, 60);

    SnScene restored;
    SnTopology(restored);
    // The topology's commands are applied by one block; the restore then replaces the state.
    SnRun(restored, 0, 0);
    Vector<f32> scratch;
    scratch.Resize(kBlockFrames * 2);
    restored.mixer.RenderBlock(Span<f32>(scratch.Data(), scratch.Size()));
    restored.ids.Clear();
    for (usize i = 0; i < 8; ++i)
    {
        restored.ids.Push(original.ids[i]);
    }
    REQUIRE(restored.mixer.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &restored.clips) ==
            SnapshotStatus::Ok);
    CHECK(restored.mixer.NextBlock() == 20);
    const Vector<f32> replay = SnRun(restored, 20, 60);
    CHECK(SnSame(tail, replay));
    CHECK(restored.mixer.VoiceCount() == original.mixer.VoiceCount());
}

TEST_CASE("Snapshot: refusals are values, and a failed restore leaves the mixer rendering as before",
          "[audio][snapshot]")
{
    SnScene a;
    SnTopology(a);
    SnRun(a, 0, 20);
    const Vector<byte> snapshot = SnSave(a.mixer);
    REQUIRE(snapshot.Size() > 1000);

    SnScene b;
    SnTopology(b);
    SnRun(b, 0, 20);
    const Vector<byte> before = SnSave(b.mixer);
    b.clips = a.clips;

    // Truncated at many lengths: always refused, never a crash, the mixer unchanged.
    for (usize len = 0; len < snapshot.Size(); len += (len < 200 ? 1 : 997))
    {
        const SnapshotStatus status = b.mixer.LoadState(ByteSpan(snapshot.Data(), len), &b.clips);
        REQUIRE(status != SnapshotStatus::Ok);
    }
    CHECK(SnSame(SnRun(a, 20, 24), SnRun(b, 20, 24)));
    CHECK(before.Size() == snapshot.Size());

    // A byte flip anywhere never crashes (a flip inside a float can still be a valid snapshot).
    u64 state = 0x9E3779B97F4A7C15ull;
    for (u32 i = 0; i < 300; ++i)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        Vector<byte> bad = SnCopy(snapshot);
        bad[static_cast<usize>(state % bad.Size())] ^= static_cast<byte>(1u << (state % 8));
        SnScene c;
        SnTopology(c);
        SnRun(c, 0, 1);
        c.clips = a.clips;
        c.mixer.LoadState(ByteSpan(bad.Data(), bad.Size()), &c.clips);
        SnRun(c, 1, 3);
    }

    // The wrong magic and a wrong version.
    Vector<byte> bad = SnCopy(snapshot);
    bad[0] ^= 0xFF;
    CHECK(b.mixer.LoadState(ByteSpan(bad.Data(), bad.Size()), &b.clips) == SnapshotStatus::Corrupt);
    bad = SnCopy(snapshot);
    bad[4] = 9;
    CHECK(b.mixer.LoadState(ByteSpan(bad.Data(), bad.Size()), &b.clips) == SnapshotStatus::Version);

    // An unknown clip.
    SnResolver none;
    CHECK(b.mixer.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &none) == SnapshotStatus::MissingClip);

    // A mixer set up differently.
    Mixer other;
    MixerConfig config;
    config.layout = ChannelLayout::Stereo;
    config.max_voices = 8;
    REQUIRE(other.Init(config) == MixerStatus::Ok);
    CHECK(other.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &b.clips) == SnapshotStatus::Mismatch);

    // Commands still queued: the snapshot is refused.
    VoiceId id;
    VoiceDesc desc;
    desc.clip = a.clips.tone;
    REQUIRE(b.mixer.Play(desc, &id) == MixerStatus::Ok);
    bytes::ByteWriter w;
    CHECK(b.mixer.SaveState(&w) == SnapshotStatus::Busy);
    CHECK(b.mixer.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &b.clips) == SnapshotStatus::Busy);
}

TEST_CASE("Snapshot: a stream voice resumes from its position", "[audio][snapshot][stream]")
{
    const AudioClipPtr ramp = SnClip("ramp", 90000, SnRamp);
    auto make_stream = [&]()
    {
        UniquePtr<StreamSource> source;
        REQUIRE(MakeMemoryStream(ramp, &source) == StreamStatus::Ok);
        StreamConfig config;
        config.capacity_frames = 4096;
        config.chunk_frames = 1024;
        config.low_water_frames = 2048;
        StreamBufferPtr buffer;
        REQUIRE(StreamBuffer::Create(Move(source), config, &buffer) == StreamStatus::Ok);
        buffer->Refill();
        return buffer;
    };
    auto build = [&](Mixer& mixer)
    {
        MixerConfig config;
        config.layout = ChannelLayout::Mono;
        config.max_voices = 4;
        config.ramp_frames = 0;
        config.lead_blocks = 0;
        REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    };
    Mixer one;
    build(one);
    const StreamBufferPtr first = make_stream();
    VoiceDesc desc;
    desc.clip = first->Clip();
    desc.stream = first;
    VoiceId id;
    REQUIRE(one.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> block;
    block.Resize(kBlockFrames);
    for (u32 b = 0; b < 30; ++b)
    {
        first->Refill();
        one.RenderBlock(Span<f32>(block.Data(), block.Size()));
    }
    const Vector<byte> snapshot = SnSave(one);
    Vector<f32> expected;
    for (u32 b = 0; b < 10; ++b)
    {
        first->Refill();
        one.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            expected.Push(block[i]);
        }
    }
    Mixer two;
    build(two);
    two.RenderBlock(Span<f32>(block.Data(), block.Size()));
    SnResolver resolver;
    resolver.ramp = ramp;
    resolver.stream = make_stream();
    REQUIRE(two.LoadState(ByteSpan(snapshot.Data(), snapshot.Size()), &resolver) == SnapshotStatus::Ok);
    Vector<f32> got;
    for (u32 b = 0; b < 10; ++b)
    {
        resolver.stream->Refill();
        two.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            got.Push(block[i]);
        }
    }
    CHECK(SnSame(expected, got));
}

namespace
{

f32 SnBlockRms(const Vector<f32>& samples, u32 block)
{
    f64 sum = 0.0;
    for (u32 i = 0; i < kBlockFrames * 2; ++i)
    {
        const f64 v = samples[static_cast<usize>(block) * kBlockFrames * 2 + i];
        sum += v * v;
    }
    return static_cast<f32>(Sqrt(sum / (kBlockFrames * 2)));
}

u64 SnHash(const Vector<f32>& samples)
{
    RenderHash hash;
    hash.AddBlock(Span<const f32>(samples.Data(), samples.Size()));
    REQUIRE(hash.Valid());
    return hash.Value();
}

}  // namespace

TEST_CASE("Snapshot: the buses scene renders the committed hash and its per-block levels, at any callback size",
          "[audio][snapshot][scene]")
{
    SnScene a;
    SnTopology(a);
    const Vector<f32> whole = SnRun(a, 0, 60);
    CHECK(SnHash(whole) == testing::PlatformHash(10403398739231254235ull, 10403398739231254235ull));
    // The levels every machine must match within the tolerance (rows: block, expected RMS).
    struct Row
    {
        u32 block;
        f32 rms;
    };
    const Row rows[] = {{10, 0.152793f}, {30, 0.112413f}, {50, 0.220639f}};
    for (const Row& row : rows)
    {
        const f32 got = SnBlockRms(whole, row.block);
        CHECK(Abs(got - row.rms) <= 2e-3f);
    }
    // The same scene pulled in callbacks of 64, 256 and 1024 frames gives the same samples.
    for (const u32 size : {64u, 256u, 1024u})
    {
        SnScene b;
        SnTopology(b);
        Vector<f32> out;
        Vector<f32> chunk;
        chunk.Resize(static_cast<usize>(size) * 2);
        u32 rendered = 0;
        for (u32 block = 0; rendered < 60 * kBlockFrames;)
        {
            // Commands are posted for blocks as the pulls reach them: apply the log up to the block being pulled.
            const u32 want_block = (rendered + size - 1) / kBlockFrames;
            while (block <= want_block && block < 60)
            {
                SnApply(b, block);
                block += 1;
            }
            b.mixer.Render(chunk.Data(), size);
            for (usize i = 0; i < chunk.Size(); ++i)
            {
                out.Push(chunk[i]);
            }
            rendered += size;
        }
        // Commands post as the pulls reach their blocks, so only the first 20 blocks (before the log's later entries)
        // are compared sample for sample.
        bool same = true;
        for (usize i = 0; i < 20u * kBlockFrames * 2; ++i)
        {
            same = same && out[i] == whole[i];
        }
        CHECK(same);
    }
}

TEST_CASE("Snapshot: a thousand voices render the committed hash", "[audio][snapshot][scene]")
{
    const AudioClipPtr tone = SnClip("tone", 4000, SnTone, 100, 3900);
    Mixer mixer;
    MixerConfig config;
    config.layout = ChannelLayout::Stereo;
    config.quality = ResamplerQuality::Linear;
    config.max_voices = 1000;
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    REQUIRE(mixer.Init(config) == MixerStatus::Ok);
    for (u32 v = 0; v < 1000; ++v)
    {
        VoiceDesc desc;
        desc.clip = tone;
        desc.loop = true;
        desc.pitch = 0.5f + 0.001f * static_cast<f32>(v);
        desc.pan = -1.0f + 0.002f * static_cast<f32>(v);
        desc.gain = 0.002f;
        VoiceId id;
        REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    }
    Vector<f32> out;
    Vector<f32> block;
    block.Resize(kBlockFrames * 2);
    for (u32 b = 0; b < 20; ++b)
    {
        mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        for (usize i = 0; i < block.Size(); ++i)
        {
            out.Push(block[i]);
        }
    }
    CHECK(mixer.VoiceCount() == 1000);
    CHECK(SnHash(out) == testing::PlatformHash(7677309547114575656ull, 7677309547114575656ull));
    CHECK(Abs(SnBlockRms(out, 19) - 0.002474f) <= 2e-3f);
}

TEST_CASE("Snapshot: the cost of a voice at each quality, with and without effects", "[audio][snapshot][cost]")
{
    const AudioClipPtr tone = SnClip("tone", 4000, SnTone, 100, 3900);
    const ResamplerQuality qualities[] = {ResamplerQuality::Linear, ResamplerQuality::Hermite, ResamplerQuality::Sinc};
    const char* names[] = {"linear", "hermite", "sinc"};
    for (u32 q = 0; q < 3; ++q)
    {
        for (u32 effects = 0; effects < 2; ++effects)
        {
            Mixer mixer;
            MixerConfig config;
            config.layout = ChannelLayout::Stereo;
            config.quality = qualities[q];
            config.max_voices = 256;
            config.ramp_frames = 0;
            config.lead_blocks = 0;
            REQUIRE(mixer.Init(config) == MixerStatus::Ok);
            if (effects == 1)
            {
                ReverbDesc reverb;
                REQUIRE(mixer.SetInsert(BusId{0}, 0, MakeReverbEffect(2, reverb)) == MixerStatus::Ok);
                REQUIRE(mixer.SetInsert(BusId{0}, 1, MakeDelayEffect(2, 48000, 0.5f, 256)) == MixerStatus::Ok);
            }
            for (u32 v = 0; v < 256; ++v)
            {
                VoiceDesc desc;
                desc.clip = tone;
                desc.loop = true;
                desc.pitch = 0.7f + 0.002f * static_cast<f32>(v);
                desc.pan = -1.0f + 0.0078f * static_cast<f32>(v);
                desc.gain = 0.004f;
                VoiceId id;
                REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
            }
            Vector<f32> block;
            block.Resize(kBlockFrames * 2);
            mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
            const u32 blocks = 400;
            const u64 start = os::MonotonicTimeNanos();
            for (u32 b = 0; b < blocks; ++b)
            {
                mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
            }
            const f64 elapsed = static_cast<f64>(os::MonotonicTimeNanos() - start) * 1e-9;
            const f64 real_time = static_cast<f64>(blocks) * kBlockFrames / 48000.0;
            WARN(names[q] << (effects ? " + reverb + delay" : "") << ": 256 voices use " << elapsed / real_time * 100.0
                          << " percent of one core, " << elapsed / blocks / 256.0 * 1e6 << " us per voice-block");
#if !ENGINE_SANITIZER_ADDRESS && !ENGINE_SANITIZER_THREAD
            CHECK(elapsed < real_time / 2.0);  // a loose bound: half of real time for 256 voices; sanitizers run slower
#endif
        }
    }
}
