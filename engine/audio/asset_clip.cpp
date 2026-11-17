#include "audio/asset_clip.h"

#include "audio/convert.h"
#include "audio/vorbis.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

ChannelLayout LayoutOf(u32 channels)
{
    switch (channels)
    {
    case 1: return ChannelLayout::Mono;
    case 2: return ChannelLayout::Stereo;
    case 4: return ChannelLayout::Quad;
    case 6: return ChannelLayout::Surround51;
    default: return ChannelLayout::Surround71;
    }
}

}  // namespace

ClipStatus MakeClipFromAsset(const asset::AudioClipView& view, StringView name, AudioClipPtr* out)
{
    const asset::AudioClipHeader& h = view.Header();
    if (!view.Bytes())
    {
        return ClipStatus::TooShort;
    }
    ClipDesc desc;
    desc.format.sample_rate = h.sample_rate;
    desc.format.layout = LayoutOf(h.channels);
    desc.format.format = SampleFormat::F32;
    desc.frame_count = h.frames;
    desc.loop_start = h.loop_start;
    desc.loop_end = h.loop_end;
    if (h.codec == asset::AudioClipCodec::Vorbis)
    {
        // Decoded once, here, to the clip's `f32` samples: the way for short effects. A music clip is streamed from the
        // same blob instead (`audio/stream.h`).
        UniquePtr<VorbisDecoder> decoder;
        const ByteSpan ogg = view.Samples();
        if (VorbisDecoder::Open(ogg, VorbisLimits(), &decoder) != VorbisStatus::Ok ||
            decoder->Info().channels != h.channels || decoder->Info().sample_rate != h.sample_rate)
        {
            return ClipStatus::BadFormat;
        }
        Vector<f32> decoded;
        const usize count = static_cast<usize>(h.frames) * h.channels;
        if (!decoded.TryResizeUninitialized(count))
        {
            return ClipStatus::TooShort;
        }
        u64 done = 0;
        while (done < h.frames)
        {
            u32 got = 0;
            const u64 want = h.frames - done < 4096 ? h.frames - done : 4096;
            if (decoder->Decode(decoded.Data() + static_cast<usize>(done) * h.channels, static_cast<u32>(want), &got) !=
                VorbisStatus::Ok)
            {
                break;
            }
            done += got;
        }
        if (done != h.frames)
        {
            return ClipStatus::TooShort;
        }
        return AudioClip::CreateCopy(desc, Span<const f32>(decoded.Data(), count), name, out);
    }
    if (h.storage == asset::AudioClipStorage::F32)
    {
        return AudioClip::Create(desc, view.Bytes(), view.SamplesOffset(), name, out);
    }
    const ByteSpan stored = view.Samples();
    const usize count = static_cast<usize>(h.frames) * h.channels;
    Vector<f32> converted;
    converted.Reserve(count);
    for (usize i = 0; i < count; ++i)
    {
        converted.Push(0.0f);
    }
    if (!ConvertToF32(SampleFormat::S16, stored, Span<f32>(converted.Data(), count)))
    {
        return ClipStatus::TooShort;
    }
    return AudioClip::CreateCopy(desc, Span<const f32>(converted.Data(), count), name, out);
}

asset::Status LoadAudioClip(asset::ResourceManager& manager, asset::ResourceId id, StringView name, AudioClipPtr* out)
{
    asset::Handle<asset::AudioClipView> handle;
    const asset::Status loaded = manager.Load<asset::AudioClipView>(id, &handle);
    if (loaded.Failed())
    {
        return loaded;
    }
    AudioClipPtr clip;
    if (MakeClipFromAsset(*handle.Get(), name, &clip) != ClipStatus::Ok)
    {
        return asset::Status::Make(asset::ErrorCode::Corrupt, "the audio clip's samples are not valid");
    }
    *out = Move(clip);
    return asset::Status::Success();
}

}  // namespace engine_placeholder::audio
