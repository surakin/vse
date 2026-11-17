// Streaming sources (Phase 26 Day 435): a stream plays the same samples a clip does, a stalled refill is a counted
// underrun that recovers, a seek and a loop land on the right sample, and the refill jobs race the audio thread safely.
#include <catch2/catch_test_macros.hpp>

#include "audio/mixer.h"
#include "audio/stream.h"
#include "audio/wav.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "wav_builder.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

AudioClipPtr StRamp(u32 frames, u32 rate = 48000, u32 first = 1)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(static_cast<f32>(i + first) / 1024.0f);
    }
    ClipDesc desc;
    desc.format.sample_rate = rate;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "ramp", &clip) ==
            ClipStatus::Ok);
    return clip;
}

MixerConfig StConfig(ChannelLayout layout, u32 rate = 48000)
{
    MixerConfig config;
    config.layout = layout;
    config.sample_rate = rate;
    config.max_voices = 8;
    config.ramp_frames = 0;
    config.lead_blocks = 0;
    return config;
}

StreamBufferPtr StBuffer(UniquePtr<StreamSource> source, u32 rate = 48000, u32 capacity = 4096)
{
    StreamConfig config;
    config.sample_rate = rate;
    config.capacity_frames = capacity;
    config.chunk_frames = capacity < 1024 ? capacity : 1024;
    config.low_water_frames = capacity / 2;
    StreamBufferPtr buffer;
    REQUIRE(StreamBuffer::Create(Move(source), config, &buffer) == StreamStatus::Ok);
    return buffer;
}

StreamBufferPtr StFromClip(const AudioClipPtr& clip, u32 rate = 48000, u32 capacity = 4096)
{
    UniquePtr<StreamSource> source;
    REQUIRE(MakeMemoryStream(clip, &source) == StreamStatus::Ok);
    return StBuffer(Move(source), rate, capacity);
}

VoiceId StPlay(Mixer& mixer, const StreamBufferPtr& buffer, bool loop = false)
{
    VoiceDesc desc;
    desc.clip = buffer->Clip();
    desc.stream = buffer;
    desc.loop = loop;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    return id;
}

// Renders `blocks` blocks, refilling the buffer before each (the offline mode: nothing is late).
Vector<f32> StRender(Mixer& mixer, const StreamBufferPtr& buffer, u32 blocks, bool refill = true)
{
    Vector<f32> out;
    Vector<f32> block;
    block.Resize(static_cast<usize>(kBlockFrames) * mixer.Channels());
    for (u32 b = 0; b < blocks; ++b)
    {
        if (refill)
        {
            buffer->Refill();
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

TEST_CASE("stream: a memory stream plays the ramp exactly to its end with no underrun", "[audio][stream]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr buffer = StFromClip(StRamp(20000));
    buffer->Refill();
    const VoiceId id = StPlay(mixer, buffer);
    const Vector<f32> out = StRender(mixer, buffer, 90);
    for (u32 i = 0; i < 20000; ++i)
    {
        REQUIRE(out[i] == static_cast<f32>(i + 1) / 1024.0f);
    }
    for (usize i = 20000; i < out.Size(); ++i)
    {
        REQUIRE(out[i] == 0.0f);
    }
    CHECK(buffer->Underruns() == 0);
    CHECK(!mixer.IsPlaying(id));
}

TEST_CASE("stream: a source of another rate is resampled to the mixer's", "[audio][stream]")
{
    // A 440 Hz sine at 44.1 kHz through a 48 kHz mixer keeps its pitch and its length.
    Vector<f32> sine;
    for (u32 i = 0; i < 44100; ++i)
    {
        sine.Push(0.5f * Sin(2.0f * 3.14159265f * 440.0f * static_cast<f32>(i) / 44100.0f));
    }
    ClipDesc desc;
    desc.format.sample_rate = 44100;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = sine.Size();
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(sine.Data(), sine.Size()), "sine", &clip) == ClipStatus::Ok);
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr buffer = StFromClip(clip);
    CHECK(buffer->Length() == 48000);
    buffer->Refill();
    StPlay(mixer, buffer);
    const Vector<f32> out = StRender(mixer, buffer, 200);
    u32 crossings = 0;
    usize last = 0;
    for (usize i = 1; i < 48000; ++i)
    {
        if ((out[i - 1] < 0.0f) != (out[i] < 0.0f))
        {
            crossings += 1;
            last = i;
        }
    }
    CHECK(crossings >= 878);
    CHECK(crossings <= 882);
    CHECK(last > 47000);
    CHECK(buffer->Underruns() == 0);
}

TEST_CASE("stream: an Ogg stream plays to the end like its decode", "[audio][stream][vorbis]")
{
    String<HeapAllocator> path_text(HeapAllocator::Default());
    path_text.Append(StringView(ENGINE_AUDIO_FIXTURES));
    path_text.Append(StringView("/music_loop.ogg"));
    io::Path path;
    REQUIRE(io::Path::Parse(path_text.View(), io::NativePathStyle(), &path));
    Vector<byte> file;
    REQUIRE(io::ReadFile(path, &file).Ok());
    const SharedBytesPtr bytes = CopyShared(ByteSpan(file.Data(), file.Size()));
    UniquePtr<StreamSource> source;
    REQUIRE(MakeVorbisStream(bytes, &source) == StreamStatus::Ok);
    UniquePtr<StreamSource> reference;
    REQUIRE(MakeVorbisStream(bytes, &reference) == StreamStatus::Ok);
    const StreamBufferPtr buffer = StBuffer(Move(source), 44100, 8192);
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Stereo, 44100)) == MixerStatus::Ok);
    buffer->Refill();
    const VoiceId id = StPlay(mixer, buffer);
    const Vector<f32> out = StRender(mixer, buffer, 1100);
    Vector<f32> expect;
    expect.Resize(static_cast<usize>(2048) * 2);
    u64 frame = 0;
    bool same = true;
    while (true)
    {
        const u32 got = reference->Read(expect.Data(), 2048);
        if (got == 0)
        {
            break;
        }
        for (usize i = 0; i < static_cast<usize>(got) * 2; ++i)
        {
            const f32 a = out[static_cast<usize>(frame) * 2 + i];
            const f32 d = a - expect[i];
            same = same && (d < 1e-5f && d > -1e-5f);
        }
        frame += got;
    }
    CHECK(frame == 264600);
    CHECK(same);
    CHECK(buffer->Underruns() == 0);
    CHECK(!mixer.IsPlaying(id));
}

TEST_CASE("stream: a WAV file is read through the file layer", "[audio][stream][wav]")
{
    audio_test::WavSpec spec;
    spec.channels = 1;
    spec.frames = 30000;
    const Vector<byte> wav = audio_test::BuildWav(spec);
    String<HeapAllocator> path_text(HeapAllocator::Default());
    path_text.Append(StringView(ENGINE_AUDIO_TMP));
    path_text.Append(StringView("/stream_test.wav"));
    io::Path path;
    REQUIRE(io::Path::Parse(path_text.View(), io::NativePathStyle(), &path));
    {
        io::FilePtr out;
        REQUIRE(io::File::Open(path, io::OpenMode::Write, io::kOpenNone, &out).Ok());
        REQUIRE(out->Write(wav.Data(), wav.Size(), 0).Ok());
        REQUIRE(out->Close().Ok());
    }
    AudioClipPtr reference;
    WavInfo info;
    REQUIRE(DecodeWav(ByteSpan(wav.Data(), wav.Size()), StringView(), &reference, &info) == WavStatus::Ok);
    io::FilePtr file;
    REQUIRE(io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file).Ok());
    UniquePtr<StreamSource> source;
    REQUIRE(MakeWavFileStream(Move(file), &source) == StreamStatus::Ok);
    CHECK(source->Length() == 30000);
    const StreamBufferPtr buffer = StBuffer(Move(source));
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    buffer->Refill();
    StPlay(mixer, buffer);
    const Vector<f32> out = StRender(mixer, buffer, 130);
    const ClipView view = reference->View();
    for (u32 i = 0; i < 30000; ++i)
    {
        REQUIRE(out[i] == view.samples[i]);
    }
    CHECK(buffer->Underruns() == 0);
    // Not a WAV, and an unsupported encoding, are refused.
    io::FilePtr again;
    REQUIRE(io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &again).Ok());
    UniquePtr<StreamSource> none;
    CHECK(MakeWavFileStream(Move(again), &none) == StreamStatus::Ok);
    audio_test::WavSpec wide;
    wide.encoding = WavEncoding::S24;
    wide.frames = 100;
    const Vector<byte> wide_bytes = audio_test::BuildWav(wide);
    {
        io::FilePtr wide_file;
        REQUIRE(io::File::Open(path, io::OpenMode::Write, io::kOpenNone, &wide_file).Ok());
        REQUIRE(wide_file->Write(wide_bytes.Data(), wide_bytes.Size(), 0).Ok());
        REQUIRE(wide_file->Close().Ok());
    }
    io::FilePtr third;
    REQUIRE(io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &third).Ok());
    CHECK(MakeWavFileStream(Move(third), &none) == StreamStatus::Unsupported);
    io::Remove(path);
}

TEST_CASE("stream: a stalled refill is a counted underrun, silence, and then the stream continues", "[audio][stream]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr buffer = StFromClip(StRamp(8000), 48000, 1024);
    buffer->Refill();
    StPlay(mixer, buffer);
    Vector<f32> out = StRender(mixer, buffer, 10, false);  // the refill never runs: 1,024 frames, then silence
    CHECK(buffer->Underruns() >= 1);
    for (usize i = 0; i < 1024; ++i)
    {
        REQUIRE(out[i] == static_cast<f32>(i + 1) / 1024.0f);
    }
    for (usize i = 1024; i < out.Size(); ++i)
    {
        REQUIRE(out[i] == 0.0f);
    }
    const u64 before = buffer->Underruns();
    const Vector<f32> rest = StRender(mixer, buffer, 40, true);
    // The stalled blocks lost nothing: the stream resumes at frame 1,024.
    usize next = 1024;
    for (usize i = 0; i < rest.Size(); ++i)
    {
        if (rest[i] != 0.0f)
        {
            REQUIRE(rest[i] == static_cast<f32>(next + 1) / 1024.0f);
            next += 1;
        }
    }
    CHECK(next == 8000);
    CHECK(buffer->Underruns() >= before);
}

TEST_CASE("stream: a seek during play lands on the right sample", "[audio][stream]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr buffer = StFromClip(StRamp(40000));
    buffer->Refill();
    const VoiceId id = StPlay(mixer, buffer);
    StRender(mixer, buffer, 10);
    REQUIRE(mixer.Seek(id, 30000) == MixerStatus::Ok);
    const Vector<f32> out = StRender(mixer, buffer, 50);
    usize delivered = 0;
    for (usize i = 0; i < out.Size(); ++i)
    {
        if (out[i] != 0.0f)
        {
            // The first frame after the seek is frame 30,000 (value 30,001 / 1024) and the rest follow.
            REQUIRE(out[i] == static_cast<f32>(30000 + delivered + 1) / 1024.0f);
            delivered += 1;
        }
    }
    CHECK(delivered == 10000);
    f64 position = 0.0;
    CHECK(mixer.Position(id, &position) != MixerStatus::Ok);  // finished
    // A seek back to the start replays it.
    Mixer again;
    REQUIRE(again.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr replay = StFromClip(StRamp(3000));
    replay->Refill();
    const VoiceId rid = StPlay(again, replay);
    StRender(again, replay, 5);
    REQUIRE(again.Seek(rid, 0) == MixerStatus::Ok);
    const Vector<f32> second = StRender(again, replay, 20);
    usize first_nonzero = 0;
    while (first_nonzero < second.Size() && second[first_nonzero] == 0.0f)
    {
        first_nonzero += 1;
    }
    REQUIRE(first_nonzero < second.Size());
    CHECK(second[first_nonzero] == 1.0f / 1024.0f);
}

TEST_CASE("stream: a loop is seamless on a ramp", "[audio][stream]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const StreamBufferPtr buffer = StFromClip(StRamp(5000), 48000, 2048);
    buffer->Refill();
    StPlay(mixer, buffer, true);
    const Vector<f32> out = StRender(mixer, buffer, 200);  // 51,200 frames: ten passes
    for (usize i = 0; i < out.Size(); ++i)
    {
        REQUIRE(out[i] == static_cast<f32>(i % 5000 + 1) / 1024.0f);
    }
    CHECK(buffer->Underruns() == 0);
}

TEST_CASE("stream: a hundred streams open and close with nothing left over", "[audio][stream][soak]")
{
    Mixer mixer;
    REQUIRE(mixer.Init(StConfig(ChannelLayout::Mono)) == MixerStatus::Ok);
    const AudioClipPtr ramp = StRamp(6000);
    for (u32 i = 0; i < 100; ++i)
    {
        const StreamBufferPtr buffer = StFromClip(ramp, 48000, 2048);
        buffer->Refill();
        const VoiceId id = StPlay(mixer, buffer);
        StRender(mixer, buffer, 3);
        REQUIRE(mixer.Stop(id, 0) == MixerStatus::Ok);
        StRender(mixer, buffer, 1);
    }
    CHECK(mixer.VoiceCount() == 0);
}

TEST_CASE("stream: refill jobs on the scheduler race the consumer without losing or repeating a frame",
          "[audio][stream][jobs]")
{
    jobs::Scheduler::Config config;
    config.participants = 3;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    jobs::Scheduler& s = scheduler;
    StreamPump pump(s);
    const StreamBufferPtr buffer = StFromClip(StRamp(200000), 48000, 2048);
    pump.Add(buffer);
    pump.Run();
    Vector<f32> block;
    block.Resize(300);
    u64 expect = 0;
    u32 spins = 0;
    while (expect < 200000 && spins < 20000000)
    {
        pump.Update();
        const u32 got = buffer->Pop(block.Data(), 300);
        for (u32 i = 0; i < got; ++i)
        {
            REQUIRE(block[i] == static_cast<f32>(expect + 1) / 1024.0f);
            expect += 1;
        }
        spins += 1;
    }
    pump.Run();
    CHECK(expect == 200000);
    CHECK(buffer->Ended());
}

TEST_CASE("stream: a flush from the consumer while the job fills never mixes epochs", "[audio][stream][jobs]")
{
    jobs::Scheduler::Config config;
    config.participants = 3;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    StreamPump pump(scheduler);
    const StreamBufferPtr buffer = StFromClip(StRamp(300000), 48000, 2048);
    pump.Add(buffer);
    pump.Run();
    Vector<f32> block;
    block.Resize(256);
    u64 seed = 12345;
    u64 next = 0;  // the value the next popped frame must have once a stretch has started
    bool started = false;
    u32 flushes = 0;
    for (u32 spin = 0; spin < 200000 && flushes < 200; ++spin)
    {
        pump.Update();
        const u32 got = buffer->Pop(block.Data(), 256);
        for (u32 i = 0; i < got; ++i)
        {
            const f32 v = block[i] * 1024.0f;
            if (!started)
            {
                started = true;
                next = static_cast<u64>(v + 0.5f);
            }
            // Within a stretch the frames are consecutive; a flush starts a new one at its target.
            REQUIRE(static_cast<u64>(v + 0.5f) == next);
            next += 1;
        }
        if (spin % 97 == 0)
        {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            const u64 target = (seed >> 33) % 250000;
            buffer->Flush(target);
            flushes += 1;
            started = false;
            // After a flush no frame of the old epoch may come out: the next frames start at target + 1.
            u32 waited = 0;
            while (buffer->Buffered() == 0 && waited < 100000)
            {
                pump.Update();
                waited += 1;
            }
            const u32 first = buffer->Pop(block.Data(), 1);
            if (first == 1)
            {
                REQUIRE(static_cast<u64>(block[0] * 1024.0f + 0.5f) == target + 1);
                started = true;
                next = target + 2;
            }
        }
    }
    pump.Run();
    CHECK(flushes > 20);
}
