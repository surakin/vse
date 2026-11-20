// Compressed audio clips (Phase 26 Day 434): `mint-audio` keeps or decodes an Ogg Vorbis input, the blob says which,
// and the audio module makes the same clip either way.
#include <catch2/catch_test_macros.hpp>

#include "asset/audio_clip.h"
#include "audio/asset_clip.h"
#include "audio/block_hash.h"
#include "audio/vorbis.h"
#include "audio_source.h"
#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"

using namespace vse;

namespace
{

Vector<byte> LoadOgg(const char* name)
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

asset::Status MintOggBlob(const char* text, const Vector<byte>& ogg, Vector<byte>* blob)
{
    asset::AudioSource source;
    asset::AudioSourceError error;
    REQUIRE(asset::ParseAudioSource(StringView(text), &source, &error).Ok());
    asset::TypeRegistry types;
    return asset::MintAudio(source, ByteSpan(ogg.Data(), ogg.Size()), types, blob, &error);
}

audio::AudioClipPtr ClipOf(const Vector<byte>& blob)
{
    asset::TypeRegistry types;
    asset::AudioClipView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    audio::AudioClipPtr clip;
    REQUIRE(audio::MakeClipFromAsset(view, StringView("clip"), &clip) == audio::ClipStatus::Ok);
    return clip;
}

}  // namespace

TEST_CASE("vorbis clip: a kept stream is the Ogg bytes verbatim with the stream's loop and length",
          "[audio][vorbis][clip]")
{
    const Vector<byte> ogg = LoadOgg("music_loop.ogg");
    Vector<byte> blob;
    REQUIRE(MintOggBlob("id 00000000000000b1\nogg music_loop.ogg\ncompress on\n", ogg, &blob).Ok());
    asset::TypeRegistry types;
    asset::AudioClipView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.Header().codec == asset::AudioClipCodec::Vorbis);
    CHECK(view.Header().frames == 264600);
    CHECK(view.Header().channels == 2);
    CHECK(view.Header().sample_rate == 44100);
    CHECK(view.Header().loop_start == 44100);
    CHECK(view.Header().loop_end == 220500);
    CHECK(view.Provenance() == nullptr);
    REQUIRE(view.Samples().Size() == ogg.Size());
    bool same = true;
    for (usize i = 0; i < ogg.Size(); ++i)
    {
        same = same && view.Samples().Data()[i] == ogg[i];
    }
    CHECK(same);
    Vector<byte> again;
    REQUIRE(MintOggBlob("id 00000000000000b1\nogg music_loop.ogg\ncompress on\n", ogg, &again).Ok());
    REQUIRE(again.Size() == blob.Size());
    bool stable = true;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        stable = stable && again[i] == blob[i];
    }
    CHECK(stable);
}

TEST_CASE("vorbis clip: a decoded mint records the decoder and the hash, and loads like the kept one",
          "[audio][vorbis][clip]")
{
    const Vector<byte> ogg = LoadOgg("music_loop.ogg");
    Vector<byte> kept;
    Vector<byte> decoded;
    REQUIRE(MintOggBlob("id 00000000000000b2\nogg music_loop.ogg\ncompress on\nloop 1000 5000\n", ogg, &kept).Ok());
    REQUIRE(MintOggBlob("id 00000000000000b2\nogg music_loop.ogg\ncompress off\nloop 1000 5000\n", ogg, &decoded).Ok());
    asset::TypeRegistry types;
    asset::AudioClipView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(decoded.Data(), decoded.Size())), types).Ok());
    CHECK(view.Header().codec == asset::AudioClipCodec::Pcm);
    CHECK(view.Header().loop_start == 1000);
    CHECK(view.Header().loop_end == 5000);
    REQUIRE(view.Provenance() != nullptr);
    CHECK(view.Provenance()->decoder_version == audio::kVorbisLibraryVersion);
    const audio::AudioClipPtr from_kept = ClipOf(kept);
    const audio::AudioClipPtr from_decoded = ClipOf(decoded);
    const audio::ClipView a = from_kept->View();
    const audio::ClipView b = from_decoded->View();
    REQUIRE(a.samples.Size() == 264600u * 2u);
    REQUIRE(b.samples.Size() == a.samples.Size());
    bool same = true;
    for (usize i = 0; i < a.samples.Size(); ++i)
    {
        same = same && a.samples[i] == b.samples[i];
    }
    CHECK(same);
    audio::RenderHash hash;
    hash.AddBlock(Span<const f32>(b.samples.Data(), b.samples.Size()));
    CHECK(view.Provenance()->decode_hash == hash.Value());
}

TEST_CASE("vorbis clip: auto keeps music and decodes effects", "[audio][vorbis][clip]")
{
    Vector<byte> blob;
    asset::TypeRegistry types;
    asset::AudioClipView view;
    REQUIRE(MintOggBlob("id 00000000000000b3\nogg x.ogg\n", LoadOgg("music_loop.ogg"), &blob).Ok());
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.Header().codec == asset::AudioClipCodec::Vorbis);
    REQUIRE(MintOggBlob("id 00000000000000b3\nogg x.ogg\n", LoadOgg("sine_mono.ogg"), &blob).Ok());
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.Header().codec == asset::AudioClipCodec::Pcm);
    CHECK(view.Provenance() != nullptr);
}

TEST_CASE("vorbis clip: what the source and the stream refuse", "[audio][vorbis][clip]")
{
    asset::AudioSource source;
    asset::AudioSourceError error;
    CHECK(
        asset::ParseAudioSource(StringView("id 00000000000000b4\nwav a.wav\ncompress on\n"), &source, &error).Failed());
    CHECK(asset::ParseAudioSource(StringView("id 00000000000000b4\nwav a.wav\nogg b.ogg\n"), &source, &error).Failed());
    CHECK(asset::ParseAudioSource(StringView("id 00000000000000b4\nogg a.ogg\ncompress maybe\n"), &source, &error)
              .Failed());
    Vector<byte> blob;
    CHECK(MintOggBlob("id 00000000000000b4\nogg a.ogg\ncompress on\nnormalize on\n", LoadOgg("music_loop.ogg"), &blob)
              .Failed());
    CHECK(MintOggBlob("id 00000000000000b4\nogg a.ogg\n", LoadOgg("three_channels.ogg"), &blob).Failed());
    Vector<byte> garbage;
    for (u32 i = 0; i < 3000; ++i)
    {
        garbage.Push(static_cast<byte>(i * 7u));
    }
    CHECK(MintOggBlob("id 00000000000000b4\nogg a.ogg\n", garbage, &blob).Failed());
}

TEST_CASE("vorbis clip: a damaged compressed blob is refused or harmless at every header byte",
          "[audio][vorbis][clip][fuzz]")
{
    Vector<byte> blob;
    REQUIRE(MintOggBlob("id 00000000000000b5\nogg a.ogg\ncompress on\n", LoadOgg("music_loop.ogg"), &blob).Ok());
    asset::TypeRegistry types;
    for (usize at = 0; at < 300 && at < blob.Size(); ++at)
    {
        Vector<byte> damaged;
        for (usize i = 0; i < blob.Size(); ++i)
        {
            damaged.Push(blob[i]);
        }
        damaged[at] = static_cast<byte>(damaged[at] ^ 0x55);
        asset::AudioClipView view;
        if (view.Open(CopyShared(ByteSpan(damaged.Data(), damaged.Size())), types).Ok())
        {
            audio::AudioClipPtr clip;
            audio::MakeClipFromAsset(view, StringView("x"), &clip);
        }
    }
    SUCCEED();
}
