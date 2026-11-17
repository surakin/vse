#include "audio/clip.h"

#include "lang/scalar.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

ClipStatus MakeClipView(const ClipDesc& desc, ByteSpan bytes, ClipView* out)
{
    if (desc.format.format != SampleFormat::F32 || !IsValid(desc.format))
    {
        return ClipStatus::BadFormat;
    }
    if (desc.frame_count == 0 || desc.frame_count > kMaxClipFrames)
    {
        return ClipStatus::BadFrameCount;
    }
    const u64 needed = desc.frame_count * FrameBytes(desc.format);
    if (bytes.Size() < needed)
    {
        return ClipStatus::TooShort;
    }
    if ((reinterpret_cast<usize>(bytes.Data()) & 3u) != 0)
    {
        return ClipStatus::Misaligned;
    }
    if (desc.loop_end != 0 && !(desc.loop_start < desc.loop_end && desc.loop_end <= desc.frame_count))
    {
        return ClipStatus::BadLoop;
    }
    if (desc.loop_end == 0 && desc.loop_start != 0)
    {
        return ClipStatus::BadLoop;
    }
    const f32* samples = reinterpret_cast<const f32*>(bytes.Data());
    const usize count = static_cast<usize>(needed / sizeof(f32));
    for (usize i = 0; i < count; ++i)
    {
        if (!IsFinite(samples[i]))
        {
            return ClipStatus::NotFinite;
        }
    }
    out->desc = desc;
    out->samples = Span<const f32>(samples, count);
    return ClipStatus::Ok;
}

AudioClip::AudioClip(const ClipDesc& desc, SharedBytesPtr bytes, usize offset, StringView name)
    : desc_(desc), bytes_(Move(bytes)), offset_(offset), name_(HeapAllocator::Default(), name)
{
}

ClipView AudioClip::View() const
{
    ClipView view;
    view.desc = desc_;
    const byte* start = bytes_->Bytes().Data() + offset_;
    view.samples = Span<const f32>(reinterpret_cast<const f32*>(start), static_cast<usize>(ByteSize() / sizeof(f32)));
    return view;
}

ClipStatus AudioClip::Create(const ClipDesc& desc, SharedBytesPtr bytes, usize offset, StringView name,
                             AudioClipPtr* out)
{
    if (!bytes)
    {
        return ClipStatus::TooShort;
    }
    const ByteSpan all = bytes->Bytes();
    if (offset > all.Size())
    {
        return ClipStatus::TooShort;
    }
    ClipView view;
    const ClipStatus status = MakeClipView(desc, ByteSpan(all.Data() + offset, all.Size() - offset), &view);
    if (status != ClipStatus::Ok)
    {
        return status;
    }
    AudioClipPtr clip = MakeShared<AudioClip>(desc, Move(bytes), offset, name);
    if (!clip)
    {
        return ClipStatus::OutOfMemory;
    }
    *out = Move(clip);
    return ClipStatus::Ok;
}

ClipStatus AudioClip::CreateCopy(const ClipDesc& desc, Span<const f32> samples, StringView name, AudioClipPtr* out)
{
    if (desc.format.format != SampleFormat::F32 || !IsValid(desc.format))
    {
        return ClipStatus::BadFormat;
    }
    if (desc.frame_count == 0 || desc.frame_count > kMaxClipFrames)
    {
        return ClipStatus::BadFrameCount;
    }
    const u64 count = desc.frame_count * ChannelCount(desc.format.layout);
    if (samples.Size() < count)
    {
        return ClipStatus::TooShort;
    }
    SharedBytesPtr copy = CopyShared(
        ByteSpan(reinterpret_cast<const byte*>(samples.Data()), static_cast<usize>(count) * sizeof(f32)));
    return Create(desc, Move(copy), 0, name, out);
}

}  // namespace engine_placeholder::audio
