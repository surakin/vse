#include "audio/wav.h"

#include "audio/convert.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

constexpr u32 kFormatPcm = 1;
constexpr u32 kFormatFloat = 3;
constexpr u32 kFormatExtensible = 0xFFFE;
constexpr u32 kMaxCuePointBytes = 24;

u32 Le16(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8);
}

u32 Le32(const byte* p)
{
    return Le16(p) | (Le16(p + 2) << 16);
}

bool IsTag(const byte* p, const char* tag)
{
    return p[0] == static_cast<byte>(tag[0]) && p[1] == static_cast<byte>(tag[1]) &&
           p[2] == static_cast<byte>(tag[2]) && p[3] == static_cast<byte>(tag[3]);
}

// The tail of the KSDATAFORMAT_SUBTYPE GUIDs after the two-byte format tag: 00 00 00 00 10 00 80 00 00 AA 00 38 9B 71.
bool IsStandardGuidTail(const byte* p)
{
    const u8 tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    for (u32 i = 0; i < 14; ++i)
    {
        if (p[i] != static_cast<byte>(tail[i]))
        {
            return false;
        }
    }
    return true;
}

bool LayoutForChannels(u32 channels, ChannelLayout* out)
{
    switch (channels)
    {
    case 1: *out = ChannelLayout::Mono; return true;
    case 2: *out = ChannelLayout::Stereo; return true;
    case 4: *out = ChannelLayout::Quad; return true;
    case 6: *out = ChannelLayout::Surround51; return true;
    case 8: *out = ChannelLayout::Surround71; return true;
    default: return false;
    }
}

u32 EncodingBytes(WavEncoding e)
{
    switch (e)
    {
    case WavEncoding::U8: return 1;
    case WavEncoding::S16: return 2;
    case WavEncoding::S24: return 3;
    case WavEncoding::S32:
    case WavEncoding::F32: return 4;
    case WavEncoding::F64: return 8;
    }
    return 0;
}

SampleFormat SampleFormatOf(WavEncoding e)
{
    switch (e)
    {
    case WavEncoding::U8: return SampleFormat::U8;
    case WavEncoding::S16: return SampleFormat::S16;
    case WavEncoding::S24: return SampleFormat::S24;
    case WavEncoding::S32: return SampleFormat::S32;
    case WavEncoding::F32:
    case WavEncoding::F64: return SampleFormat::F32;
    }
    return SampleFormat::F32;
}

// Converts `samples` samples of `encoding` at `p` to f32; false if the data holds something the clip refuses later
// (checked there).
void DecodeSamples(WavEncoding encoding, const byte* p, usize samples, f32* out)
{
    if (encoding == WavEncoding::F64)
    {
        for (usize i = 0; i < samples; ++i, p += 8)
        {
            const u64 bits = static_cast<u64>(Le32(p)) | (static_cast<u64>(Le32(p + 4)) << 32);
            f64 value = 0.0;
            MemCopy(&value, &bits, sizeof(value));
            out[i] = static_cast<f32>(value);
        }
        return;
    }
    ConvertToF32(SampleFormatOf(encoding), ByteSpan(p, samples * EncodingBytes(encoding)), Span<f32>(out, samples));
}

WavStatus ParseFmt(const byte* p, usize size, WavInfo* info)
{
    if (size < 16)
    {
        return WavStatus::BadFmt;
    }
    u32 tag = Le16(p);
    const u32 channels = Le16(p + 2);
    const u32 rate = Le32(p + 4);
    const u32 byte_rate = Le32(p + 8);
    const u32 block_align = Le16(p + 12);
    const u32 bits = Le16(p + 14);
    u32 mask = 0;
    if (tag == kFormatExtensible)
    {
        if (size < 40 || Le16(p + 16) < 22)
        {
            return WavStatus::BadFmt;
        }
        mask = Le32(p + 20);
        if (!IsStandardGuidTail(p + 26))
        {
            return WavStatus::Unsupported;
        }
        tag = Le16(p + 24);
    }
    if (tag != kFormatPcm && tag != kFormatFloat)
    {
        return WavStatus::Unsupported;
    }
    WavEncoding encoding;
    if (tag == kFormatPcm)
    {
        switch (bits)
        {
        case 8: encoding = WavEncoding::U8; break;
        case 16: encoding = WavEncoding::S16; break;
        case 24: encoding = WavEncoding::S24; break;
        case 32: encoding = WavEncoding::S32; break;
        default: return WavStatus::Unsupported;
        }
    }
    else
    {
        if (bits == 32)
        {
            encoding = WavEncoding::F32;
        }
        else if (bits == 64)
        {
            encoding = WavEncoding::F64;
        }
        else
        {
            return WavStatus::Unsupported;
        }
    }
    ChannelLayout layout;
    if (!LayoutForChannels(channels, &layout))
    {
        return WavStatus::Unsupported;
    }
    // channels is at most 8 here, bytes at most 8: the product cannot overflow.
    const u32 frame_bytes = channels * EncodingBytes(encoding);
    if (block_align != frame_bytes)
    {
        return WavStatus::BadFmt;
    }
    if (rate < kMinSampleRate || rate > kMaxSampleRate)
    {
        return WavStatus::Unsupported;
    }
    if (static_cast<u64>(byte_rate) != static_cast<u64>(rate) * frame_bytes)
    {
        return WavStatus::BadFmt;
    }
    info->encoding = encoding;
    info->format.sample_rate = rate;
    info->format.layout = layout;
    info->format.format = SampleFormatOf(encoding);
    info->channel_mask = mask;
    info->frame_bytes = frame_bytes;
    return WavStatus::Ok;
}

void ParseSmpl(const byte* p, usize size, WavInfo* info)
{
    // 36 bytes of header, the loop count at offset 28, then 24 bytes per loop (id, type, start, end, fraction, play
    // count).
    if (size < 36)
    {
        info->ignored_metadata = true;
        return;
    }
    const u32 loops = Le32(p + 28);
    if (loops == 0)
    {
        return;
    }
    if (static_cast<u64>(loops) * 24 > static_cast<u64>(size - 36))
    {
        info->ignored_metadata = true;
        return;
    }
    const u64 start = Le32(p + 36 + 8);
    const u64 end = static_cast<u64>(Le32(p + 36 + 12)) + 1;  // the file's end point is inclusive
    if (start >= end)
    {
        info->ignored_metadata = true;
        return;
    }
    info->loop_start = static_cast<u32>(start);
    info->loop_end = static_cast<u32>(end > 0xFFFFFFFFull ? 0xFFFFFFFFull : end);
    info->has_loop = true;
}

void ParseCue(const byte* p, usize size, WavInfo* info)
{
    if (size < 4)
    {
        info->ignored_metadata = true;
        return;
    }
    const u32 count = Le32(p);
    if (static_cast<u64>(count) * kMaxCuePointBytes > static_cast<u64>(size - 4))
    {
        info->ignored_metadata = true;
        return;
    }
    for (u32 i = 0; i < count; ++i)
    {
        if (info->cues.Size() >= kMaxWavCues)
        {
            info->ignored_metadata = true;
            break;
        }
        const byte* point = p + 4 + static_cast<usize>(i) * kMaxCuePointBytes;
        WavCue cue;
        cue.id = Le32(point);
        cue.frame = Le32(point + 20);  // the sample offset
        info->cues.Push(cue);
    }
}

}  // namespace

WavStatus ParseWav(ByteSpan bytes, WavInfo* info)
{
    if (bytes.Size() < 12)
    {
        return WavStatus::TooShort;
    }
    const byte* base = bytes.Data();
    if (IsTag(base, "RF64"))
    {
        return WavStatus::Unsupported;
    }
    if (!IsTag(base, "RIFF"))
    {
        return WavStatus::NotRiff;
    }
    if (!IsTag(base + 8, "WAVE"))
    {
        return WavStatus::NotWave;
    }
    usize end = bytes.Size();
    const u64 declared = static_cast<u64>(Le32(base + 4)) + 8;
    if (declared >= 12 && declared < end)
    {
        end = static_cast<usize>(declared);  // trailing bytes after the RIFF form are not part of it
    }

    WavInfo found;
    bool have_fmt = false;
    bool have_data = false;
    usize data_offset = 0;
    usize data_size = 0;
    bool truncated = false;
    bool saw_smpl = false;
    bool saw_cue = false;

    usize pos = 12;
    while (end - pos >= 8)  // pos never passes end: it advances by 8 plus the chunk, checked below
    {
        const byte* chunk = base + pos;
        const usize remaining = end - pos - 8;
        const u32 declared_size = Le32(chunk + 4);
        usize size = declared_size;
        if (size > remaining)
        {
            if (IsTag(chunk, "data") && !have_data)
            {
                size = remaining;  // a truncated recording: use the bytes present
                truncated = true;
            }
            else if (IsTag(chunk, "fmt ") && !have_fmt)
            {
                return WavStatus::BadChunk;
            }
            else
            {
                break;  // another chunk cut short ends the walk
            }
        }
        const byte* body = chunk + 8;
        if (IsTag(chunk, "fmt ") && !have_fmt)
        {
            const WavStatus status = ParseFmt(body, size, &found);
            if (status != WavStatus::Ok)
            {
                return status;
            }
            have_fmt = true;
        }
        else if (IsTag(chunk, "data") && !have_data)
        {
            have_data = true;
            data_offset = pos + 8;
            data_size = size;
        }
        else if (IsTag(chunk, "smpl") && !saw_smpl)
        {
            saw_smpl = true;
            ParseSmpl(body, size, &found);
        }
        else if (IsTag(chunk, "cue ") && !saw_cue)
        {
            saw_cue = true;
            ParseCue(body, size, &found);
        }
        const usize advance = 8 + size + (size & 1);
        if (advance > end - pos)
        {
            break;  // the pad byte of the last chunk is missing: fine
        }
        pos += advance;
    }
    if (!have_fmt)
    {
        return WavStatus::NoFmt;
    }
    if (!have_data)
    {
        return WavStatus::NoData;
    }
    const u64 frames = data_size / found.frame_bytes;
    if (frames == 0 || frames > kMaxWavFrames)
    {
        return WavStatus::BadFrameCount;
    }
    found.frame_count = frames;
    found.data_offset = data_offset;
    found.data_bytes = static_cast<usize>(frames * found.frame_bytes);
    found.truncated = truncated;
    if (found.has_loop && found.loop_end > frames)
    {
        found.has_loop = false;
        found.loop_start = 0;
        found.loop_end = 0;
        found.ignored_metadata = true;
    }
    usize kept = 0;
    for (usize i = 0; i < found.cues.Size(); ++i)
    {
        if (found.cues[i].frame <= frames)
        {
            found.cues[kept++] = found.cues[i];
        }
        else
        {
            found.ignored_metadata = true;
        }
    }
    while (found.cues.Size() > kept)
    {
        found.cues.Pop();
    }
    *info = Move(found);
    return WavStatus::Ok;
}

WavStatus DecodeWav(ByteSpan bytes, StringView name, AudioClipPtr* clip, WavInfo* info)
{
    WavInfo parsed;
    const WavStatus status = ParseWav(bytes, &parsed);
    if (status != WavStatus::Ok)
    {
        return status;
    }
    const usize channels = ChannelCount(parsed.format.layout);
    const usize samples = static_cast<usize>(parsed.frame_count) * channels;
    Vector<f32> decoded;
    decoded.Reserve(samples);
    for (usize i = 0; i < samples; ++i)
    {
        decoded.Push(0.0f);
    }
    DecodeSamples(parsed.encoding, bytes.Data() + parsed.data_offset, samples, decoded.Data());

    ClipDesc desc;
    desc.format = parsed.format;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = parsed.frame_count;
    if (parsed.has_loop)
    {
        desc.loop_start = parsed.loop_start;
        desc.loop_end = parsed.loop_end;
    }
    const ClipStatus clip_status = AudioClip::CreateCopy(desc, Span<const f32>(decoded.Data(), decoded.Size()), name,
                                                         clip);
    if (clip_status == ClipStatus::OutOfMemory)
    {
        return WavStatus::OutOfMemory;
    }
    if (clip_status != ClipStatus::Ok)
    {
        return WavStatus::BadSamples;
    }
    if (info != nullptr)
    {
        *info = Move(parsed);
    }
    return WavStatus::Ok;
}

WavStatus WavReader::Open(ByteSpan bytes)
{
    WavInfo parsed;
    const WavStatus status = ParseWav(bytes, &parsed);
    if (status != WavStatus::Ok)
    {
        return status;
    }
    bytes_ = bytes;
    info_ = Move(parsed);
    position_ = 0;
    return WavStatus::Ok;
}

bool WavReader::Seek(u64 frame)
{
    if (frame > info_.frame_count)
    {
        return false;
    }
    position_ = frame;
    return true;
}

usize WavReader::ReadFrames(Span<f32> out, usize max_frames)
{
    const usize channels = ChannelCount(info_.format.layout);
    if (channels == 0 || out.Size() / channels < max_frames)
    {
        return 0;
    }
    const u64 left = info_.frame_count - position_;
    const usize count = static_cast<usize>(left < max_frames ? left : max_frames);
    if (count == 0)
    {
        return 0;
    }
    const byte* p = bytes_.Data() + info_.data_offset + static_cast<usize>(position_) * info_.frame_bytes;
    DecodeSamples(info_.encoding, p, count * channels, out.Data());
    position_ += count;
    return count;
}

}  // namespace engine_placeholder::audio
