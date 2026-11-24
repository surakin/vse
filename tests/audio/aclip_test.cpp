// The audio clip resource (Phase 26 Day 418): mint a WAV, view the blob, load it through the resource manager, refuse
// corruption at every byte.
#include <catch2/catch_test_macros.hpp>

#include "asset/audio_clip.h"
#include "asset/cpu_loaders.h"
#include "asset/memory_backend.h"
#include "asset/resource_manager.h"
#include "audio/asset_clip.h"
#include "audio_source.h"
#include "containers/unique_ptr.h"
#include "cstr/mem_ops.h"
#include "wav_builder.h"

using namespace vse;
using namespace vse::asset;
using namespace audio_test;

namespace
{

const char* const kSource = "id 00000000000000aa\nwav ramp.wav\n";

Vector<byte> Mint(const char* source_text, const Vector<byte>& wav, u32 target = 0)
{
    AudioSource source;
    AudioSourceError error;
    REQUIRE(ParseAudioSource(StringView(source_text), &source, &error).Ok());
    TypeRegistry types;
    Vector<byte> blob;
    const Status status = MintAudio(source, Bytes(wav), types, &blob, &error, target);
    INFO(error.message.Data());
    REQUIRE(status.Ok());
    return blob;
}

SharedBytesPtr Share(const Vector<byte>& v)
{
    return CopyShared(ByteSpan(v.Data(), v.Size()));
}

}  // namespace

TEST_CASE("aclip: mint then view, loop points survive, f32 is stored as is", "[audio][aclip]")
{
    WavSpec spec;
    spec.channels = 2;
    spec.frames = 100;
    spec.with_loop = true;
    const Vector<byte> blob = Mint(kSource, BuildWav(spec));
    TypeRegistry types;
    AudioClipView view;
    REQUIRE(view.Open(Share(blob), types).Ok());
    CHECK(view.Id() == ResourceId::FromValue(0xaa));
    CHECK(view.Header().sample_rate == 48000);
    CHECK(view.Header().channels == 2);
    CHECK(view.Header().frames == 100);
    CHECK(view.Header().loop_start == 10);
    CHECK(view.Header().loop_end == 50);
    CHECK(view.Samples().Size() == 100u * 2 * 4);

    audio::AudioClipPtr clip;
    REQUIRE(audio::MakeClipFromAsset(view, "ramp", &clip) == audio::ClipStatus::Ok);
    CHECK(clip->Bytes().Get() == view.Bytes().Get());  // shared in place
    CHECK(clip->Desc().loop_end == 50);
    for (u32 i = 0; i < 200; ++i)
    {
        REQUIRE(clip->View().samples[i] == RampValue(i));
    }
}

TEST_CASE("aclip: a source loop overrides the file's, s16 and normalisation work", "[audio][aclip]")
{
    WavSpec spec;
    spec.frames = 100;
    spec.with_loop = true;
    const Vector<byte> wav = BuildWav(spec);
    const Vector<byte> blob = Mint("id 00000000000000aa\nwav a.wav\nloop 5 90\nformat s16\nnormalize on\n", wav);
    TypeRegistry types;
    AudioClipView view;
    REQUIRE(view.Open(Share(blob), types).Ok());
    CHECK(view.Header().storage == AudioClipStorage::S16);
    CHECK(view.Header().loop_start == 5);
    CHECK(view.Header().loop_end == 90);
    CHECK(view.Samples().Size() == 200);
    audio::AudioClipPtr clip;
    REQUIRE(audio::MakeClipFromAsset(view, "", &clip) == audio::ClipStatus::Ok);
    CHECK(clip->Bytes().Get() != view.Bytes().Get());  // converted once
    f32 peak = 0.0f;
    for (usize i = 0; i < clip->View().samples.Size(); ++i)
    {
        const f32 a = clip->View().samples[i] < 0 ? -clip->View().samples[i] : clip->View().samples[i];
        peak = a > peak ? a : peak;
    }
    CHECK(peak > 0.999f);  // the ramp's 0.75 peak, scaled to 1.0 and quantised to 16 bits
    CHECK(peak <= 1.0f);
}

TEST_CASE("aclip: two mints are byte-identical and the target is recorded", "[audio][aclip]")
{
    WavSpec spec;
    spec.frames = 64;
    const Vector<byte> wav = BuildWav(spec);
    const Vector<byte> a = Mint(kSource, wav);
    const Vector<byte> b = Mint(kSource, wav);
    REQUIRE(a.Size() == b.Size());
    for (usize i = 0; i < a.Size(); ++i)
    {
        REQUIRE(a[i] == b[i]);
    }
}

TEST_CASE("aclip: source errors name the line", "[audio][aclip]")
{
    AudioSource source;
    AudioSourceError error;
    CHECK(ParseAudioSource("wav a.wav\n", &source, &error).Failed());  // the id must come first
    CHECK(error.line == 1);
    CHECK(ParseAudioSource("id 00000000000000aa\n", &source, &error).Failed());  // no wav
    CHECK(error.line == 0);
    CHECK(ParseAudioSource("id 00000000000000aa\nwav a.wav\nformat u8\n", &source, &error).Failed());
    CHECK(error.line == 3);
    CHECK(ParseAudioSource("id 00000000000000aa\nwav a.wav\nloop 9 3\n", &source, &error).Failed());
    CHECK(ParseAudioSource("id 00000000000000aa\nwav a.wav\nwav b.wav\n", &source, &error).Failed());
    CHECK(ParseAudioSource("id 00000000000000aa\nwav a.wav\nbogus 1\n", &source, &error).Failed());

    TypeRegistry types;
    Vector<byte> blob;
    REQUIRE(ParseAudioSource("id 00000000000000aa\nwav a.wav\nloop 5 500\n", &source, &error).Ok());
    WavSpec spec;
    spec.frames = 100;
    CHECK(MintAudio(source, Bytes(BuildWav(spec)), types, &blob, &error).Failed());  // the loop ends after the data
    const byte junk[4] = {byte(1), byte(2), byte(3), byte(4)};
    CHECK(MintAudio(source, ByteSpan(junk, 4), types, &blob, &error).Failed());
}

TEST_CASE("aclip: a corrupted blob is refused or harmless at every byte", "[audio][aclip][fuzz]")
{
    WavSpec spec;
    spec.channels = 2;
    spec.frames = 20;
    spec.with_loop = true;
    spec.loop_end_inclusive = 15;
    const Vector<byte> good = Mint(kSource, BuildWav(spec));
    TypeRegistry types;
    for (usize i = 0; i < good.Size(); ++i)
    {
        for (u32 flip : {0x01u, 0x80u, 0xFFu})
        {
            Vector<byte> bad;
            for (usize k = 0; k < good.Size(); ++k)
            {
                bad.Push(good[k]);
            }
            bad[i] = static_cast<byte>(static_cast<u32>(bad[i]) ^ flip);
            AudioClipView view;
            if (view.Open(Share(bad), types).Ok())
            {
                audio::AudioClipPtr clip;
                const audio::ClipStatus status = audio::MakeClipFromAsset(view, "", &clip);
                // A flipped codec field claims Ogg Vorbis over PCM bytes (Day 434): refused as a bad format.
                REQUIRE((status == audio::ClipStatus::Ok || status == audio::ClipStatus::NotFinite ||
                         status == audio::ClipStatus::BadFormat));
                if (status == audio::ClipStatus::Ok)
                {
                    REQUIRE(clip->View().samples.Size() ==
                            static_cast<usize>(view.Header().frames) * view.Header().channels);
                }
            }
        }
    }
    for (usize length = 0; length < good.Size(); ++length)  // truncation
    {
        AudioClipView view;
        Vector<byte> cut;
        for (usize k = 0; k < length; ++k)
        {
            cut.Push(good[k]);
        }
        REQUIRE(view.Open(Share(cut), types).Failed());
    }
}

TEST_CASE("aclip: every hostile header field and sample is refused with an error status", "[audio][aclip][fuzz]")
{
    WavSpec spec;
    spec.encoding = audio::WavEncoding::F32;
    spec.channels = 2;
    spec.frames = 20;
    spec.with_loop = true;
    spec.loop_start = 2;
    spec.loop_end_inclusive = 15;
    const Vector<byte> good = Mint(kSource, BuildWav(spec));
    TypeRegistry types;
    // The header section: rate, channels, storage, frames, loop start, loop end, codec, reserved (eight u32).
    const u32 expected[8] = {48000, 2, 0, 20, 2, 16, 0, 0};
    usize head = 0;
    for (usize i = 0; i + 32 <= good.Size() && head == 0; ++i)
    {
        bool match = true;
        for (u32 f = 0; f < 8 && match; ++f)
        {
            u32 v = 0;
            for (u32 k = 0; k < 4; ++k)
            {
                v |= static_cast<u32>(good[i + 4 * f + k]) << (8 * k);
            }
            match = v == expected[f];
        }
        head = match ? i : 0;
    }
    REQUIRE(head != 0);
    struct Hostile
    {
        u32 field;
        u32 value;
    };
    const Hostile hostile[] = {
        {0, 0},           {0, 7999},        {0, 384001},      {0, 0xFFFFFFFFu}, {1, 0},
        {1, 3},           {1, 9},           {1, 0xFFFFFFFFu}, {2, 1},           {2, 2},
        {2, 0xFFFFFFFFu}, {3, 0},           {3, 19},          {3, 21},          {3, (1u << 29) + 1},
        {3, 0xFFFFFFFFu}, {4, 16},          {4, 17},          {4, 0xFFFFFFFFu}, {5, 21},
        {5, 2},           {5, 0xFFFFFFFFu}, {6, 2},           {6, 0xFFFFFFFFu}, {7, 1},
        {7, 0xFFFFFFFFu},
    };
    for (const Hostile& h : hostile)
    {
        Vector<byte> bad;
        for (usize k = 0; k < good.Size(); ++k)
        {
            bad.Push(good[k]);
        }
        for (u32 k = 0; k < 4; ++k)
        {
            bad[head + 4 * h.field + k] = static_cast<byte>((h.value >> (8 * k)) & 0xFFu);
        }
        AudioClipView view;
        bool refused = view.Open(Share(bad), types).Failed();
        if (!refused)
        {
            audio::AudioClipPtr clip;
            refused = audio::MakeClipFromAsset(view, "", &clip) != audio::ClipStatus::Ok;
        }
        INFO("field " << h.field << " value " << h.value);
        CHECK(refused);
    }
    // A non-finite sample (NaN, both infinities) at the first, a middle and the last sample.
    const u32 non_finite[] = {0x7FC00000u, 0x7F800000u, 0xFF800000u};
    const usize sample_bytes = 20 * 2 * 4;
    usize samples = 0;
    {
        AudioClipView view;
        REQUIRE(view.Open(Share(good), types).Ok());
        audio::AudioClipPtr clip;
        REQUIRE(audio::MakeClipFromAsset(view, "", &clip) == audio::ClipStatus::Ok);
        // The samples are stored as is: find the clip's samples in the blob.
        for (usize i = head + 32; i + sample_bytes <= good.Size() && samples == 0; ++i)
        {
            samples = MemCompare(good.Data() + i, clip->View().samples.Data(), sample_bytes) == 0 ? i : 0;
        }
    }
    REQUIRE(samples != 0);
    for (u32 bits : non_finite)
    {
        for (usize index : {usize{0}, usize{17}, usize{39}})
        {
            Vector<byte> bad;
            for (usize k = 0; k < good.Size(); ++k)
            {
                bad.Push(good[k]);
            }
            for (u32 k = 0; k < 4; ++k)
            {
                bad[samples + index * 4 + k] = static_cast<byte>((bits >> (8 * k)) & 0xFFu);
            }
            AudioClipView view;
            REQUIRE(view.Open(Share(bad), types).Ok());  // the view does not scan samples
            audio::AudioClipPtr clip;
            CHECK(audio::MakeClipFromAsset(view, "", &clip) == audio::ClipStatus::NotFinite);
        }
    }
}

TEST_CASE("aclip: the builder refuses what the view would", "[audio][aclip]")
{
    TypeRegistry types;
    Vector<byte> out;
    AudioClipHeader header;
    header.sample_rate = 48000;
    header.channels = 2;
    header.frames = 4;
    const byte samples[32] = {};
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), header, ByteSpan(samples, 32), &out).Ok());
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), header, ByteSpan(samples, 31), &out).Failed());
    AudioClipHeader bad = header;
    bad.channels = 3;
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), bad, ByteSpan(samples, 32), &out).Failed());
    bad = header;
    bad.sample_rate = 100;
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), bad, ByteSpan(samples, 32), &out).Failed());
    bad = header;
    bad.loop_start = 1;  // a start without an end
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), bad, ByteSpan(samples, 32), &out).Failed());
    bad = header;
    bad.frames = 0;
    CHECK(BuildAudioClipBlob(types, ResourceId::FromValue(1), bad, ByteSpan(samples, 0), &out).Failed());
}

TEST_CASE("aclip: the resource manager loads and releases a clip, and names a missing one", "[audio][aclip]")
{
    WavSpec spec;
    spec.frames = 50;
    const Vector<byte> blob = Mint(kSource, BuildWav(spec));
    TypeRegistry types;
    HostMemoryBackend backend(types, u64{8} << 20);
    ManagerConfig config;
    config.buffer_pool_bytes = u64{16} << 20;
    UniquePtr<ResourceManager> manager = MakeUnique<ResourceManager>(types, config);
    RegisterCpuLoaders(manager.Get());
    REQUIRE(manager->Mount(StringView("/"), &backend, 0).Ok());
    REQUIRE(backend.Add(ResourceId::FromValue(0xaa), ByteSpan(blob.Data(), blob.Size())).Ok());

    audio::AudioClipPtr clip;
    REQUIRE(audio::LoadAudioClip(*manager, ResourceId::FromValue(0xaa), "ramp", &clip).Ok());
    CHECK(clip->Frames() == 50);
    CHECK(clip->Name() == StringView("ramp"));
    manager.Reset();  // the manager lets go; an f32 clip keeps its buffer alive
    CHECK(clip->View().samples[7] == RampValue(7));

    UniquePtr<ResourceManager> again = MakeUnique<ResourceManager>(types, config);
    RegisterCpuLoaders(again.Get());
    REQUIRE(again->Mount(StringView("/"), &backend, 0).Ok());
    audio::AudioClipPtr missing;
    CHECK(audio::LoadAudioClip(*again, ResourceId::FromValue(0xbb), "", &missing).Is(ErrorCode::NotFound));
    CHECK_FALSE(missing);
}
