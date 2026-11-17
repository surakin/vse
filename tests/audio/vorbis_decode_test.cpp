// Decoded Vorbis (Phase 26 Day 433): the fixtures decode to the right length, level and order; the decode hash is a
// same-build baseline and the per-block RMS table is the cross-build reference (within a tolerance); the decoder
// survives a thousand open-decode-destroy rounds.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/vorbis.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "platform_hash.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;
using Catch::Approx;

namespace
{

Vector<byte> Load(const char* name)
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

Vector<f32> Decode(const Vector<byte>& bytes, u32* channels = nullptr)
{
    UniquePtr<VorbisDecoder> decoder;
    REQUIRE(VorbisDecoder::Open(ByteSpan(bytes.Data(), bytes.Size()), VorbisLimits(), &decoder) == VorbisStatus::Ok);
    const u32 ch = decoder->Info().channels;
    if (channels != nullptr)
    {
        *channels = ch;
    }
    Vector<f32> all;
    Vector<f32> chunk;
    chunk.Resize(static_cast<usize>(4096) * ch);
    while (true)
    {
        u32 frames = 0;
        if (decoder->Decode(chunk.Data(), 4096, &frames) != VorbisStatus::Ok)
        {
            break;
        }
        for (usize i = 0; i < static_cast<usize>(frames) * ch; ++i)
        {
            all.Push(chunk[i]);
        }
    }
    return all;
}

// RMS of one channel over the block [index * 4800, (index + 1) * 4800) frames.
f32 BlockRms(const Vector<f32>& samples, u32 channels, u32 channel, u32 index)
{
    const usize from = static_cast<usize>(index) * 4800;
    f64 sum = 0.0;
    u32 count = 0;
    for (usize n = from; n < from + 4800 && (n + 1) * channels <= samples.Size(); ++n)
    {
        const f64 v = samples[n * channels + channel];
        sum += v * v;
        count += 1;
    }
    return count == 0 ? 0.0f : static_cast<f32>(Sqrt(sum / count));
}

}  // namespace

TEST_CASE("vorbis decode: the sine's level is its amplitude over root two, block after block",
          "[audio][vorbis][decode]")
{
    u32 channels = 0;
    const Vector<f32> sound = Decode(Load("sine_mono.ogg"), &channels);
    REQUIRE(channels == 1);
    // The first and last blocks are inside the stream's own length, with no discarded packet showing as silence.
    for (u32 block = 0; block < 9; ++block)
    {
        CHECK(BlockRms(sound, 1, 0, block) == Approx(0.5f * 0.70710678f).margin(0.03f));
    }
}

TEST_CASE("vorbis decode: the reference table of per-block levels", "[audio][vorbis][decode]")
{
    // Taken from a Clang build on Linux; another compiler or machine must match within 2e-3 (libvorbis uses libm,
    // so the last bits may differ and are accepted).
    struct Row
    {
        const char* name;
        u32 channel;
        u32 block;
        f32 rms;
    };
    const Row table[] = {
        {"sine_mono.ogg", 0, 0, 0.355948f},  {"sine_mono.ogg", 0, 4, 0.355907f},  {"noise_mono.ogg", 0, 1, 0.213018f},
        {"stereo_mix.ogg", 0, 3, 0.256521f}, {"stereo_mix.ogg", 1, 3, 0.256396f}, {"surround51.ogg", 3, 1, 0.256063f},
    };
    for (const Row& row : table)
    {
        u32 channels = 0;
        const Vector<f32> sound = Decode(Load(row.name), &channels);
        const f32 rms = BlockRms(sound, channels, row.channel, row.block);
        INFO(row.name << " channel " << row.channel << " block " << row.block << " rms " << rms);
        CHECK(rms == Approx(row.rms).margin(2e-3f));
    }
}

TEST_CASE("vorbis decode: the decode hash of each fixture is the same-build baseline", "[audio][vorbis][decode][hash]")
{
    // A regression check for one build (Clang, Linux): it changes with the compiler, the libvorbis version or the
    // fixtures, and is then re-taken (the cross-build check is the RMS table above).
    const char* names[] = {"sine_mono.ogg", "noise_mono.ogg", "stereo_mix.ogg", "transient_mix.ogg", "surround51.ogg"};
    // Windows has its own (cl and clang-cl agree; Phase 24 Day 381, the Windows PC), through platform_hash.h.
    const u64 expected[5] = {testing::PlatformHash(14233851687474461274ull, 6715489784166491711ull),
                             testing::PlatformHash(11776376970900082663ull, 17197782754136045587ull),
                             testing::PlatformHash(8245827628348519891ull, 6767513533495306370ull),
                             testing::PlatformHash(14823774197410747470ull, 1892277194690204243ull),
                             testing::PlatformHash(13820256537831640291ull, 1741561902367827691ull)};
    for (u32 i = 0; i < 5; ++i)
    {
        const Vector<f32> sound = Decode(Load(names[i]));
        RenderHash hash;
        hash.AddBlock(Span<const f32>(sound.Data(), sound.Size()));
        REQUIRE(hash.Valid());
        INFO(names[i] << " hash " << hash.Value());
        CHECK(hash.Value() == expected[i]);
    }
}

TEST_CASE("vorbis decode: a thousand open-decode-destroy rounds", "[audio][vorbis][decode][soak]")
{
    const Vector<byte> bytes = Load("stereo_mix.ogg");
    Vector<f32> chunk;
    chunk.Resize(2048 * 2);
    for (u32 round = 0; round < 1000; ++round)
    {
        UniquePtr<VorbisDecoder> decoder;
        REQUIRE(VorbisDecoder::Open(ByteSpan(bytes.Data(), bytes.Size()), VorbisLimits(), &decoder) ==
                VorbisStatus::Ok);
        u32 frames = 0;
        REQUIRE(decoder->Decode(chunk.Data(), 2048, &frames) == VorbisStatus::Ok);
        REQUIRE(frames == 2048);
    }
}

TEST_CASE("vorbis decode: decoding is faster than real time by a wide margin", "[audio][vorbis][decode][cost]")
{
    const Vector<byte> bytes = Load("stereo_mix.ogg");
    const u64 start = os::MonotonicTimeNanos();
    u32 seconds = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        const Vector<f32> sound = Decode(bytes);
        seconds += static_cast<u32>(sound.Size() / 2 / 48000);
    }
    const f64 elapsed = static_cast<f64>(os::MonotonicTimeNanos() - start) * 1e-9;
    WARN("decoded " << seconds << " s of 48 kHz stereo in " << elapsed << " s, " << elapsed / seconds * 1000.0
                    << " ms per second of audio");
    CHECK(seconds == 20);
    CHECK(elapsed < static_cast<f64>(seconds) / 20.0);
}
