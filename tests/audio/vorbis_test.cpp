// The Vorbis facade (Phase 26 Day 432): the fixtures open and decode, every refusal path reports a status, and a seeded
// mutation fuzz of the fixtures neither crashes nor hangs.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/vorbis.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"
#include "lang/scalar.h"
#include "os/os.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;
using Catch::Approx;

namespace
{

Vector<byte> LoadFixture(const char* name)
{
    String<HeapAllocator> full(HeapAllocator::Default());
    full.Append(StringView(ENGINE_AUDIO_FIXTURES));
    full.Append(StringView("/"));
    full.Append(StringView(name));
    io::Path path;
    REQUIRE(io::Path::Parse(full.View(), io::NativePathStyle(), &path));
    Vector<byte> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    return bytes;
}

ByteSpan View(const Vector<byte>& bytes)
{
    return ByteSpan(bytes.Data(), bytes.Size());
}

// Decodes everything with calls of `pull` frames.
Vector<f32> DecodeAll(const Vector<byte>& bytes, u32 pull, VorbisInfo* info = nullptr)
{
    UniquePtr<VorbisDecoder> decoder;
    REQUIRE(VorbisDecoder::Open(View(bytes), VorbisLimits(), &decoder) == VorbisStatus::Ok);
    if (info != nullptr)
    {
        *info = decoder->Info();
    }
    Vector<f32> all;
    Vector<f32> chunk;
    chunk.Resize(static_cast<usize>(pull) * decoder->Info().channels);
    while (true)
    {
        u32 frames = 0;
        const VorbisStatus s = decoder->Decode(chunk.Data(), pull, &frames);
        if (s != VorbisStatus::Ok)
        {
            REQUIRE(s == VorbisStatus::EndOfStream);
            break;
        }
        for (usize i = 0; i < static_cast<usize>(frames) * decoder->Info().channels; ++i)
        {
            all.Push(chunk[i]);
        }
    }
    return all;
}

// The frequency of one channel by its zero crossings in the middle of the signal.
f32 ChannelHz(const Vector<f32>& samples, u32 channels, u32 channel, u32 rate)
{
    const usize frames = samples.Size() / channels;
    const usize from = frames / 4;
    const usize to = frames * 3 / 4;
    u32 crossings = 0;
    for (usize n = from + 1; n < to; ++n)
    {
        const f32 a = samples[(n - 1) * channels + channel];
        const f32 b = samples[n * channels + channel];
        if ((a < 0.0f) != (b < 0.0f))
        {
            crossings += 1;
        }
    }
    const f32 seconds = static_cast<f32>(to - from) / static_cast<f32>(rate);
    return static_cast<f32>(crossings) / (2.0f * seconds);
}

f32 ChannelPeak(const Vector<f32>& samples, u32 channels, u32 channel)
{
    f32 peak = 0.0f;
    for (usize n = 0; n + channel < samples.Size(); n += channels)
    {
        const f32 v = samples[n + channel] < 0.0f ? -samples[n + channel] : samples[n + channel];
        peak = v > peak ? v : peak;
    }
    return peak;
}

struct XorShift
{
    u64 state = 0x2545F4914F6CDD1Dull;
    u32 Next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<u32>(state >> 16);
    }
};

}  // namespace

TEST_CASE("vorbis: the fixtures open and report what they are", "[audio][vorbis]")
{
    struct Expect
    {
        const char* name;
        u32 channels;
        u32 rate;
        u64 frames;
    };
    const Expect expect[] = {{"sine_mono.ogg", 1, 44100, 44100},
                             {"noise_mono.ogg", 1, 44100, 22050},
                             {"stereo_mix.ogg", 2, 48000, 48000},
                             {"transient_mix.ogg", 1, 44100, 44100},
                             {"surround51.ogg", 6, 48000, 14400}};
    for (const Expect& e : expect)
    {
        const Vector<byte> bytes = LoadFixture(e.name);
        UniquePtr<VorbisDecoder> decoder;
        REQUIRE(VorbisDecoder::Open(View(bytes), VorbisLimits(), &decoder) == VorbisStatus::Ok);
        CHECK(decoder->Info().channels == e.channels);
        CHECK(decoder->Info().sample_rate == e.rate);
        CHECK(decoder->Info().total_frames == e.frames);
        CHECK(decoder->Info().nominal_bitrate > 0);
    }
    const Vector<byte> sine = LoadFixture("sine_mono.ogg");
    UniquePtr<VorbisDecoder> decoder;
    REQUIRE(VorbisDecoder::Open(View(sine), VorbisLimits(), &decoder) == VorbisStatus::Ok);
    const StringView tags = decoder->Tags().View();
    CHECK(tags.Contains(StringView("title=Sine")));
    CHECK(tags.Contains(StringView("artist=Fixture Generator")));
}

TEST_CASE("vorbis: a sine decodes to its frequency and level, whatever the pull size", "[audio][vorbis]")
{
    const Vector<byte> bytes = LoadFixture("sine_mono.ogg");
    VorbisInfo info;
    const Vector<f32> whole = DecodeAll(bytes, 8192, &info);
    REQUIRE(whole.Size() == 44100);
    CHECK(ChannelHz(whole, 1, 0, 44100) == Approx(440.0f).epsilon(0.02));
    CHECK(ChannelPeak(whole, 1, 0) == Approx(0.5f).margin(0.08f));
    for (u32 pull : {1u, 100u, 1000u, 4097u})
    {
        const Vector<f32> chunks = DecodeAll(bytes, pull);
        REQUIRE(chunks.Size() == whole.Size());
        bool same = true;
        for (usize i = 0; i < whole.Size(); ++i)
        {
            same = same && chunks[i] == whole[i];
        }
        CHECK(same);
    }
}

TEST_CASE("vorbis: the stereo mix keeps its channels apart", "[audio][vorbis]")
{
    const Vector<f32> mix = DecodeAll(LoadFixture("stereo_mix.ogg"), 2048);
    REQUIRE(mix.Size() == 96000);
    // 330 Hz + 110 Hz on the left (about 3.3 crossings per 330 cycle pair), 550 Hz + 110 Hz on the right: the right is
    // higher.
    CHECK(ChannelHz(mix, 2, 1, 48000) > ChannelHz(mix, 2, 0, 48000));
    CHECK(ChannelPeak(mix, 2, 0) > 0.2f);
    CHECK(ChannelPeak(mix, 2, 1) > 0.2f);
}

TEST_CASE("vorbis: 5.1 comes out in the engine's channel order", "[audio][vorbis]")
{
    const Vector<f32> sound = DecodeAll(LoadFixture("surround51.ogg"), 1024);
    REQUIRE(sound.Size() == 14400u * 6u);
    // The engine order is front left, front right, centre, LFE, back left, back right.
    const f32 expect[6] = {220.0f, 330.0f, 440.0f, 55.0f, 660.0f, 880.0f};
    for (u32 c = 0; c < 6; ++c)
    {
        CHECK(ChannelHz(sound, 6, c, 48000) == Approx(expect[c]).epsilon(0.1));
    }
}

TEST_CASE("vorbis: noise and clicks decode to finite samples of the stated length", "[audio][vorbis]")
{
    for (const char* name : {"noise_mono.ogg", "transient_mix.ogg"})
    {
        VorbisInfo info;
        const Vector<f32> samples = DecodeAll(LoadFixture(name), 777, &info);
        CHECK(samples.Size() == info.total_frames * info.channels);
        for (usize i = 0; i < samples.Size(); ++i)
        {
            REQUIRE(IsFinite(samples[i]));
        }
    }
}

TEST_CASE("vorbis: every refusal reports a status", "[audio][vorbis]")
{
    UniquePtr<VorbisDecoder> decoder;
    const Vector<byte> sine = LoadFixture("sine_mono.ogg");
    CHECK(VorbisDecoder::Open(ByteSpan(), VorbisLimits(), &decoder) == VorbisStatus::InvalidInput);
    // Not Ogg at all.
    Vector<byte> garbage;
    for (u32 i = 0; i < 5000; ++i)
    {
        garbage.Push(static_cast<byte>(i * 31u));
    }
    CHECK(VorbisDecoder::Open(View(garbage), VorbisLimits(), &decoder) == VorbisStatus::NotVorbis);
    // Ogg, but FLAC.
    CHECK(VorbisDecoder::Open(View(LoadFixture("flac_in_ogg.ogg")), VorbisLimits(), &decoder) ==
          VorbisStatus::NotVorbis);
    // A header cut short.
    CHECK(VorbisDecoder::Open(ByteSpan(sine.Data(), 120), VorbisLimits(), &decoder) == VorbisStatus::Truncated);
    // Three channels and a 4 kHz rate have no place in the engine.
    CHECK(VorbisDecoder::Open(View(LoadFixture("three_channels.ogg")), VorbisLimits(), &decoder) ==
          VorbisStatus::Unsupported);
    CHECK(VorbisDecoder::Open(View(LoadFixture("rate_4000.ogg")), VorbisLimits(), &decoder) ==
          VorbisStatus::Unsupported);
    // The limits.
    VorbisLimits small;
    small.max_input_bytes = 100;
    CHECK(VorbisDecoder::Open(View(sine), small, &decoder) == VorbisStatus::LimitExceeded);
    small = VorbisLimits();
    small.max_setup_bytes = 16;
    CHECK(VorbisDecoder::Open(View(sine), small, &decoder) == VorbisStatus::LimitExceeded);
    small = VorbisLimits();
    small.max_pages = 1;
    CHECK(VorbisDecoder::Open(View(sine), small, &decoder) == VorbisStatus::LimitExceeded);
    small = VorbisLimits();
    small.max_steps = 0;
    CHECK(VorbisDecoder::Open(View(sine), small, &decoder) == VorbisStatus::InvalidInput);
}

TEST_CASE("vorbis: the frame cap stops a decode short of the stream", "[audio][vorbis]")
{
    const Vector<byte> sine = LoadFixture("sine_mono.ogg");
    VorbisLimits limits;
    limits.max_frames = 5000;
    UniquePtr<VorbisDecoder> decoder;
    // The header claims 44,100 frames, the cap is 5,000 and the stream's own length is the lower of the two.
    REQUIRE(VorbisDecoder::Open(View(sine), limits, &decoder) == VorbisStatus::Ok);
    Vector<f32> out;
    out.Resize(8192);
    u32 total = 0;
    while (true)
    {
        u32 frames = 0;
        if (decoder->Decode(out.Data(), 8192, &frames) != VorbisStatus::Ok)
        {
            break;
        }
        total += frames;
    }
    CHECK(total == 5000);
    CHECK(decoder->Position() == 5000);
}

TEST_CASE("vorbis: the facade header includes no library header", "[audio][vorbis]")
{
    String<HeapAllocator> full(HeapAllocator::Default());
    full.Append(StringView(ENGINE_AUDIO_FIXTURES));
    full.Append(StringView("/../../../engine/audio/vorbis.h"));
    io::Path path;
    REQUIRE(io::Path::Parse(full.View(), io::NativePathStyle(), &path));
    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(io::ReadTextFile(path, &text).Ok());
    CHECK(!text.View().Contains(StringView("#include \"ogg/")));
    CHECK(!text.View().Contains(StringView("#include \"vorbis/")));
}

namespace
{

// Opens and decodes everything: only the status values matter, and it must finish.
void Exercise(const Vector<byte>& bytes)
{
    UniquePtr<VorbisDecoder> decoder;
    VorbisLimits limits;
    limits.max_frames = 1u << 20;
    limits.max_steps = 1u << 16;
    const VorbisStatus opened = VorbisDecoder::Open(View(bytes), limits, &decoder);
    if (opened != VorbisStatus::Ok)
    {
        return;
    }
    Vector<f32> out;
    out.Resize(static_cast<usize>(4096) * decoder->Info().channels);
    for (u32 call = 0; call < 2000; ++call)
    {
        u32 frames = 0;
        if (decoder->Decode(out.Data(), 4096, &frames) != VorbisStatus::Ok)
        {
            break;
        }
    }
}

}  // namespace

TEST_CASE("vorbis: truncation at every length is refused or decoded, never a crash", "[audio][vorbis][fuzz]")
{
    for (const char* name : {"sine_mono.ogg", "stereo_mix.ogg", "transient_mix.ogg"})
    {
        const Vector<byte> bytes = LoadFixture(name);
        for (usize length = 0; length < bytes.Size(); length += 11)
        {
            Vector<byte> cut;
            for (usize i = 0; i < length; ++i)
            {
                cut.Push(bytes[i]);
            }
            Exercise(cut);
        }
    }
    SUCCEED();
}

TEST_CASE("vorbis: seeded mutations of the fixtures neither crash nor hang", "[audio][vorbis][fuzz]")
{
    const char* names[] = {"sine_mono.ogg", "noise_mono.ogg", "stereo_mix.ogg", "transient_mix.ogg", "surround51.ogg"};
    XorShift rng;
    const u64 start = os::MonotonicTimeNanos();
    u32 runs = 0;
    for (u32 round = 0; round < 1200 && os::MonotonicTimeNanos() - start < 60ull * 1000000000ull; ++round)
    {
        Vector<byte> bytes = LoadFixture(names[round % 5]);
        const u32 kind = rng.Next() % 4;
        if (kind == 0)
        {
            // Bit flips, anywhere (the headers and the first pages are where the library parses the most).
            const u32 flips = 1 + rng.Next() % 8;
            for (u32 f = 0; f < flips; ++f)
            {
                const usize at = (rng.Next() % 2 == 0) ? rng.Next() % (bytes.Size() < 600 ? bytes.Size() : 600)
                                                       : rng.Next() % bytes.Size();
                bytes[at] = static_cast<byte>(bytes[at] ^ (1u << (rng.Next() % 8)));
            }
        }
        else if (kind == 1)
        {
            // A random overwrite of a short run.
            const usize at = rng.Next() % bytes.Size();
            const usize n = 1 + rng.Next() % 16;
            for (usize i = 0; i < n && at + i < bytes.Size(); ++i)
            {
                bytes[at + i] = static_cast<byte>(rng.Next());
            }
        }
        else if (kind == 2)
        {
            // Two blocks swapped (a page-level shuffle, more or less).
            const usize n = 64 + rng.Next() % 512;
            if (bytes.Size() > 2 * n)
            {
                const usize a = rng.Next() % (bytes.Size() - n);
                const usize b = rng.Next() % (bytes.Size() - n);
                for (usize i = 0; i < n; ++i)
                {
                    const byte t = bytes[a + i];
                    bytes[a + i] = bytes[b + i];
                    bytes[b + i] = t;
                }
            }
        }
        else
        {
            bytes.Resize(1 + rng.Next() % bytes.Size());
        }
        Exercise(bytes);
        runs += 1;
    }
    CHECK(runs >= 200);
}
