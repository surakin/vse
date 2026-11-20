// Vorbis seeking and looping (Phase 26 Day 434): a seek lands on the same samples decoding from the start gives, and a
// looped decode wraps at the loop points without changing a sample.
#include <catch2/catch_test_macros.hpp>

#include "audio/vorbis.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"

using namespace vse;
using namespace vse::audio;

namespace
{

Vector<byte> LoadSeekFixture(const char* name)
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

UniquePtr<VorbisDecoder> OpenSeekDecoder(const Vector<byte>& bytes)
{
    UniquePtr<VorbisDecoder> decoder;
    REQUIRE(VorbisDecoder::Open(ByteSpan(bytes.Data(), bytes.Size()), VorbisLimits(), &decoder) == VorbisStatus::Ok);
    return decoder;
}

// Reads `frames` frames (fewer at the end) in pulls of `pull`.
Vector<f32> ReadFrames(VorbisDecoder& decoder, u64 frames, u32 pull)
{
    const u32 channels = decoder.Info().channels;
    Vector<f32> all;
    Vector<f32> chunk;
    chunk.Resize(static_cast<usize>(pull) * channels);
    u64 left = frames;
    while (left > 0)
    {
        u32 got = 0;
        const u32 want = left < pull ? static_cast<u32>(left) : pull;
        if (decoder.Decode(chunk.Data(), want, &got) != VorbisStatus::Ok)
        {
            break;
        }
        for (usize i = 0; i < static_cast<usize>(got) * channels; ++i)
        {
            all.Push(chunk[i]);
        }
        left -= got;
    }
    return all;
}

bool SameSamples(const Vector<f32>& a, usize a_from, const Vector<f32>& b, usize b_from, usize count)
{
    if (a_from + count > a.Size() || b_from + count > b.Size())
    {
        return false;
    }
    for (usize i = 0; i < count; ++i)
    {
        if (a[a_from + i] != b[b_from + i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("vorbis seek: the loop tags are read, and only valid ones", "[audio][vorbis][seek]")
{
    const Vector<byte> music = LoadSeekFixture("music_loop.ogg");
    const UniquePtr<VorbisDecoder> decoder = OpenSeekDecoder(music);
    CHECK(decoder->Info().total_frames == 264600);
    CHECK(decoder->Info().loop_start == 44100);
    CHECK(decoder->Info().loop_end == 220500);
    CHECK(OpenSeekDecoder(LoadSeekFixture("sine_mono.ogg"))->Info().loop_end == 0);
}

TEST_CASE("vorbis seek: a seek gives the samples decoding from the start gives", "[audio][vorbis][seek]")
{
    const Vector<byte> music = LoadSeekFixture("music_loop.ogg");
    UniquePtr<VorbisDecoder> whole = OpenSeekDecoder(music);
    const Vector<f32> reference = ReadFrames(*whole, 264600, 4096);
    REQUIRE(reference.Size() == 264600u * 2u);
    UniquePtr<VorbisDecoder> decoder = OpenSeekDecoder(music);
    // Forwards, backwards, to the start, to the very end, and to frames just either side of a page's first frame.
    const u64 targets[] = {100000, 3, 220500, 0, 264599, 44100, 130001, 4095, 4096, 4097, 8192, 177777, 12345};
    for (u64 target : targets)
    {
        INFO("target " << target);
        REQUIRE(decoder->SeekFrame(target) == VorbisStatus::Ok);
        CHECK(decoder->Position() == target);
        const Vector<f32> got = ReadFrames(*decoder, 3000, 777);
        const usize count = got.Size() < (264600 - target) * 2 ? got.Size() : (264600 - target) * 2;
        CHECK(count > 0);
        CHECK(SameSamples(got, 0, reference, target * 2, count));
    }
}

TEST_CASE("vorbis seek: the end of the stream", "[audio][vorbis][seek]")
{
    const Vector<byte> music = LoadSeekFixture("music_loop.ogg");
    UniquePtr<VorbisDecoder> decoder = OpenSeekDecoder(music);
    CHECK(decoder->SeekFrame(264600) == VorbisStatus::EndOfStream);
    CHECK(decoder->SeekFrame(1ull << 40) == VorbisStatus::EndOfStream);
    Vector<f32> out;
    out.Resize(2048);
    u32 got = 0;
    CHECK(decoder->Decode(out.Data(), 1024, &got) == VorbisStatus::EndOfStream);
    // And the decoder is usable again after a seek back.
    REQUIRE(decoder->SeekFrame(1000) == VorbisStatus::Ok);
    CHECK(decoder->Decode(out.Data(), 1024, &got) == VorbisStatus::Ok);
    CHECK(got == 1024);
}

TEST_CASE("vorbis seek: a looped decode wraps at the loop points without changing a sample", "[audio][vorbis][seek]")
{
    const Vector<byte> music = LoadSeekFixture("music_loop.ogg");
    UniquePtr<VorbisDecoder> whole = OpenSeekDecoder(music);
    const Vector<f32> reference = ReadFrames(*whole, 264600, 4096);
    UniquePtr<VorbisDecoder> decoder = OpenSeekDecoder(music);
    decoder->SetLooping(true);
    // Two and a half passes of the loop body, with a pull size that does not divide it.
    const u64 span = 220500 - 44100;
    const u64 total = 220500 + span + span / 2;
    const Vector<f32> looped = ReadFrames(*decoder, total, 1000);
    REQUIRE(looped.Size() == total * 2);
    CHECK(SameSamples(looped, 0, reference, 0, 220500 * 2));
    CHECK(SameSamples(looped, 220500 * 2, reference, 44100 * 2, span * 2));
    CHECK(SameSamples(looped, (220500 + span) * 2, reference, 44100 * 2, (span / 2) * 2));
}

TEST_CASE("vorbis seek: seeking and looping in damaged streams never crashes or hangs", "[audio][vorbis][seek][fuzz]")
{
    u64 state = 0x9E3779B97F4A7C15ull;
    auto next = [&state]() -> u32
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<u32>(state >> 16);
    };
    Vector<f32> out;
    out.Resize(4096 * 2);
    for (u32 round = 0; round < 150; ++round)
    {
        Vector<byte> bytes = LoadSeekFixture("music_loop.ogg");
        const u32 flips = 1 + next() % 12;
        for (u32 f = 0; f < flips; ++f)
        {
            const usize at = next() % bytes.Size();
            bytes[at] = static_cast<byte>(bytes[at] ^ (1u << (next() % 8)));
        }
        if (round % 5 == 0)
        {
            bytes.Resize(1 + next() % bytes.Size());
        }
        UniquePtr<VorbisDecoder> decoder;
        VorbisLimits limits;
        limits.max_steps = 1u << 14;
        if (VorbisDecoder::Open(ByteSpan(bytes.Data(), bytes.Size()), limits, &decoder) != VorbisStatus::Ok)
        {
            continue;
        }
        decoder->SetLooping(true);
        for (u32 i = 0; i < 6; ++i)
        {
            decoder->SeekFrame(next() % 270000);
            u32 got = 0;
            decoder->Decode(out.Data(), 4096, &got);
        }
    }
    SUCCEED();
}
