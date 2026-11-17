#include "asset/audio_clip.h"

#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kMinRate = 8000;
constexpr u32 kMaxRate = 384000;

void Store(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
    p[2] = static_cast<byte>((value >> 16) & 0xFF);
    p[3] = static_cast<byte>((value >> 24) & 0xFF);
}

u32 Load(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

bool ChannelsOk(u32 channels)
{
    return channels == 1 || channels == 2 || channels == 4 || channels == 6 || channels == 8;
}

// The first broken rule of a header, or null.
const char* CheckHeader(const AudioClipHeader& h)
{
    if (h.sample_rate < kMinRate || h.sample_rate > kMaxRate)
    {
        return "the sample rate is outside 8,000 to 384,000 Hz";
    }
    if (!ChannelsOk(h.channels))
    {
        return "the channel count is not 1, 2, 4, 6 or 8";
    }
    if (h.storage != AudioClipStorage::F32 && h.storage != AudioClipStorage::S16)
    {
        return "the stored sample format is not f32 or s16";
    }
    if (h.codec != AudioClipCodec::Pcm && h.codec != AudioClipCodec::Vorbis)
    {
        return "the codec is not PCM or Vorbis";
    }
    if (h.codec == AudioClipCodec::Vorbis && h.storage != AudioClipStorage::F32)
    {
        return "a Vorbis clip states f32 as its stored format";
    }
    if (h.frames == 0 || h.frames > kMaxAudioClipFrames)
    {
        return "the frame count is 0 or above the limit";
    }
    if (h.loop_end == 0 ? h.loop_start != 0 : !(h.loop_start < h.loop_end && h.loop_end <= h.frames))
    {
        return "the loop points are not 0 <= start < end <= frames (or both 0)";
    }
    return nullptr;
}

// The exact size of a PCM samples section (a Vorbis section has no size fixed by the header).
u64 SampleBytesOf(const AudioClipHeader& h)
{
    const u64 width = h.storage == AudioClipStorage::F32 ? 4 : 2;
    return static_cast<u64>(h.frames) * h.channels * width;
}

}  // namespace

Status BuildAudioClipBlob(const TypeRegistry& types, ResourceId id, const AudioClipHeader& header, ByteSpan samples,
                          Vector<byte>* out, u32 target, const AudioClipProvenance* provenance)
{
    if (const char* problem = CheckHeader(header))
    {
        return Status::Make(ErrorCode::Invalid, problem);
    }
    if (header.codec == AudioClipCodec::Vorbis)
    {
        if (samples.Size() == 0 || static_cast<u64>(samples.Size()) > kMaxAudioClipVorbisBytes)
        {
            return Status::Make(ErrorCode::Invalid, "the Ogg Vorbis bytes are empty or above the limit");
        }
        if (provenance != nullptr)
        {
            return Status::Make(ErrorCode::Invalid, "a Vorbis clip has no provenance: it is the source");
        }
    }
    else if (static_cast<u64>(samples.Size()) != SampleBytesOf(header))
    {
        return Status::Make(ErrorCode::Invalid, "the sample bytes do not match the frames, channels and format");
    }
    byte head[kAudioClipHeaderBytes];
    MemSet(head, 0, sizeof(head));
    Store(head, header.sample_rate);
    Store(head + 4, header.channels);
    Store(head + 8, static_cast<u32>(header.storage));
    Store(head + 12, header.frames);
    Store(head + 16, header.loop_start);
    Store(head + 20, header.loop_end);
    Store(head + 24, static_cast<u32>(header.codec));
    BlobWriter writer(types, types::kAudioClip, id, target);
    ENGINE_TRY(writer.AddSection(kAudioClipSectionHeader, ByteSpan(head, sizeof(head))));
    ENGINE_TRY(writer.AddSection(kAudioClipSectionSamples, samples));
    if (provenance != nullptr)
    {
        byte extra[kAudioClipProvenanceBytes];
        MemSet(extra, 0, sizeof(extra));
        Store(extra, provenance->decoder_version);
        Store(extra + 8, static_cast<u32>(provenance->decode_hash & 0xFFFFFFFFull));
        Store(extra + 12, static_cast<u32>(provenance->decode_hash >> 32));
        ENGINE_TRY(writer.AddSection(kAudioClipSectionProvenance, ByteSpan(extra, sizeof(extra))));
    }
    return writer.Finish(out);
}

Status AudioClipView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = AudioClipView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kAudioClip)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not an audio clip");
    }
    ByteSpan head;
    if (!blob.FindSection(kAudioClipSectionHeader, &head) || head.Size() != kAudioClipHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the audio clip header section is missing or has the wrong size");
    }
    AudioClipHeader h;
    h.sample_rate = Load(head.Data());
    h.channels = Load(head.Data() + 4);
    const u32 storage = Load(head.Data() + 8);
    h.frames = Load(head.Data() + 12);
    h.loop_start = Load(head.Data() + 16);
    h.loop_end = Load(head.Data() + 20);
    if (storage > 1)
    {
        return Status::Make(ErrorCode::Corrupt, "the stored sample format is not f32 or s16");
    }
    h.storage = static_cast<AudioClipStorage>(storage);
    const u32 codec = Load(head.Data() + 24);
    if (codec > 1)
    {
        return Status::Make(ErrorCode::Corrupt, "the codec is not PCM or Vorbis");
    }
    h.codec = static_cast<AudioClipCodec>(codec);
    if (Load(head.Data() + 28) != 0)
    {
        return Status::Make(ErrorCode::Corrupt,
                            "an audio clip header field holds a value this version does not define");
    }
    if (const char* problem = CheckHeader(h))
    {
        return Status::Make(ErrorCode::Corrupt, problem);
    }
    ByteSpan samples;
    if (!blob.FindSection(kAudioClipSectionSamples, &samples) ||
        (h.codec == AudioClipCodec::Pcm
             ? static_cast<u64>(samples.Size()) != SampleBytesOf(h)
             : (samples.Size() == 0 || static_cast<u64>(samples.Size()) > kMaxAudioClipVorbisBytes)))
    {
        return Status::Make(ErrorCode::Corrupt, "the audio clip sample section is missing or has the wrong size");
    }
    ByteSpan extra;
    if (blob.FindSection(kAudioClipSectionProvenance, &extra))
    {
        if (extra.Size() != kAudioClipProvenanceBytes || Load(extra.Data() + 4) != 0 || h.codec != AudioClipCodec::Pcm)
        {
            return Status::Make(ErrorCode::Corrupt, "the audio clip provenance section is malformed");
        }
        provenance_.decoder_version = Load(extra.Data());
        provenance_.decode_hash = static_cast<u64>(Load(extra.Data() + 8)) |
                                  (static_cast<u64>(Load(extra.Data() + 12)) << 32);
        has_provenance_ = true;
    }
    header_ = h;
    id_ = blob.Id();
    bytes_ = bytes;
    samples_ = static_cast<usize>(samples.Data() - data.Data());
    sample_bytes_ = samples.Size();
    return Status::Success();
}

ByteSpan AudioClipView::Samples() const
{
    if (!bytes_)
    {
        return ByteSpan();
    }
    return ByteSpan(bytes_->Bytes().Data() + samples_, sample_bytes_);
}

}  // namespace engine_placeholder::asset
