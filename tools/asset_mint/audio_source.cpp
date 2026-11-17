#include "audio_source.h"

#include "audio/convert.h"
#include "audio/wav.h"
#include "cstr/format.h"
#include "lang/scalar.h"
#include "source_text.h"

namespace engine_placeholder::asset
{
namespace
{

void SetError(AudioSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status Fail(AudioSourceError* error, u32 line, const char* message)
{
    SetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the audio source is invalid");
}

bool ParseFrames(StringView token, u32* out)
{
    if (token.Length() == 0 || token.Length() > 10)
    {
        return false;
    }
    u64 value = 0;
    for (usize i = 0; i < token.Length(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<u64>(token[i] - '0');
    }
    if (value > 0xFFFFFFFFull)
    {
        return false;
    }
    *out = static_cast<u32>(value);
    return true;
}

}  // namespace

Status ParseAudioSource(StringView text, AudioSource* out, AudioSourceError* error)
{
    *out = AudioSource();
    *error = AudioSourceError();
    bool have_id = false;
    bool have_wav = false;
    bool have_format = false;
    bool have_normalize = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const srctext::LineTokens t = srctext::Tokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return Fail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return Fail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return Fail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return Fail(error, line_number, "the id line must come first");
        }
        if (kind == StringView("wav"))
        {
            if (have_wav)
            {
                return Fail(error, line_number, "wav is given twice");
            }
            if (t.count != 2)
            {
                return Fail(error, line_number, "wav needs one path");
            }
            out->wav = String<HeapAllocator>(HeapAllocator::Default(), t.tokens[1]);
            have_wav = true;
        }
        else if (kind == StringView("loop"))
        {
            if (out->has_loop)
            {
                return Fail(error, line_number, "loop is given twice");
            }
            if (t.count != 3 || !ParseFrames(t.tokens[1], &out->loop_start) ||
                !ParseFrames(t.tokens[2], &out->loop_end) || out->loop_start >= out->loop_end)
            {
                return Fail(error, line_number, "loop needs two frame numbers, the start below the end");
            }
            out->has_loop = true;
        }
        else if (kind == StringView("format"))
        {
            if (have_format)
            {
                return Fail(error, line_number, "format is given twice");
            }
            if (t.count != 2 || (t.tokens[1] != StringView("f32") && t.tokens[1] != StringView("s16")))
            {
                return Fail(error, line_number, "format is f32 or s16");
            }
            out->storage = t.tokens[1] == StringView("s16") ? AudioClipStorage::S16 : AudioClipStorage::F32;
            have_format = true;
        }
        else if (kind == StringView("normalize"))
        {
            if (have_normalize)
            {
                return Fail(error, line_number, "normalize is given twice");
            }
            if (t.count != 2 || (t.tokens[1] != StringView("on") && t.tokens[1] != StringView("off")))
            {
                return Fail(error, line_number, "normalize is on or off");
            }
            out->normalize = t.tokens[1] == StringView("on");
            have_normalize = true;
        }
        else
        {
            return Fail(error, line_number, "unknown record (expected id, wav, loop, format or normalize)");
        }
    }
    if (!have_id)
    {
        return Fail(error, 0, "the source has no id line");
    }
    if (!have_wav)
    {
        return Fail(error, 0, "the source has no wav line");
    }
    return Status::Success();
}

Status MintAudio(const AudioSource& source, ByteSpan wav_bytes, const TypeRegistry& types, Vector<byte>* out,
                 AudioSourceError* error, u32 target)
{
    audio::AudioClipPtr clip;
    audio::WavInfo info;
    const audio::WavStatus wav = audio::DecodeWav(wav_bytes, StringView(), &clip, &info);
    if (wav != audio::WavStatus::Ok)
    {
        return Fail(error, 0, "the WAV file is not valid or not supported");
    }
    const audio::ClipView view = clip->View();
    Vector<f32> samples;
    samples.Reserve(view.samples.Size());
    f32 peak = 0.0f;
    for (usize i = 0; i < view.samples.Size(); ++i)
    {
        const f32 a = Abs(view.samples[i]);
        peak = a > peak ? a : peak;
    }
    const bool scale = source.normalize && peak > 0.0f;
    const f32 gain = scale ? 1.0f / peak : 1.0f;
    for (usize i = 0; i < view.samples.Size(); ++i)
    {
        samples.Push(scale ? view.samples[i] * gain : view.samples[i]);
    }

    AudioClipHeader header;
    header.sample_rate = info.format.sample_rate;
    header.channels = audio::ChannelCount(info.format.layout);
    header.storage = source.storage;
    header.frames = static_cast<u32>(info.frame_count);
    if (source.has_loop)
    {
        header.loop_start = source.loop_start;
        header.loop_end = source.loop_end;
    }
    else if (info.has_loop)
    {
        header.loop_start = info.loop_start;
        header.loop_end = info.loop_end;
    }
    if (header.loop_end > header.frames)
    {
        return Fail(error, 0, "the loop ends after the last frame");
    }

    Vector<byte> encoded;
    const usize width = source.storage == AudioClipStorage::F32 ? 4 : 2;
    encoded.Reserve(samples.Size() * width);
    for (usize i = 0; i < samples.Size() * width; ++i)
    {
        encoded.Push(byte(0));
    }
    const audio::SampleFormat format = source.storage == AudioClipStorage::F32 ? audio::SampleFormat::F32
                                                                               : audio::SampleFormat::S16;
    if (!audio::ConvertFromF32(format, Span<const f32>(samples.Data(), samples.Size()),
                               MutableByteSpan(encoded.Data(), encoded.Size())))
    {
        return Fail(error, 0, "the samples could not be encoded");
    }
    out->Clear();
    const Status built = BuildAudioClipBlob(types, source.id, header, ByteSpan(encoded.Data(), encoded.Size()), out,
                                            target);
    if (built.Failed())
    {
        SetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace engine_placeholder::asset
