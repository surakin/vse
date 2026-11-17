// Builds WAV files in memory for the tests (Phase 26 Day 417): the format coverage is in the source, not in committed
// binaries.
#pragma once

#include "audio/wav.h"
#include "containers/vector.h"
#include "lang/scalar.h"

namespace audio_test
{

using namespace engine_placeholder;

inline void PutU16(Vector<byte>& v, u32 x)
{
    v.Push(static_cast<byte>(x & 0xFF));
    v.Push(static_cast<byte>((x >> 8) & 0xFF));
}

inline void PutU32(Vector<byte>& v, u32 x)
{
    PutU16(v, x & 0xFFFF);
    PutU16(v, x >> 16);
}

inline void PutTag(Vector<byte>& v, const char* tag)
{
    for (u32 i = 0; i < 4; ++i)
    {
        v.Push(static_cast<byte>(tag[i]));
    }
}

/// What a built file holds.
struct WavSpec
{
    audio::WavEncoding encoding = audio::WavEncoding::S16;
    u32 channels = 1;
    u32 rate = 48000;
    u32 frames = 100;
    bool extensible = false;
    u32 channel_mask = 0;
    bool with_loop = false;
    u32 loop_start = 10;
    u32 loop_end_inclusive = 49;
    u32 cue_count = 0;
    u32 extra_chunk_bytes = 0;  // an unknown chunk of this size before `data`; odd sizes get a pad byte
};

inline u32 BytesOf(audio::WavEncoding e)
{
    switch (e)
    {
    case audio::WavEncoding::U8: return 1;
    case audio::WavEncoding::S16: return 2;
    case audio::WavEncoding::S24: return 3;
    case audio::WavEncoding::S32:
    case audio::WavEncoding::F32: return 4;
    case audio::WavEncoding::F64: return 8;
    }
    return 0;
}

/// The value the ramp has at sample `i` (a slow triangle in [-0.75, 0.75], exact in every format after quantisation).
inline f32 RampValue(u32 i)
{
    const u32 phase = i % 24;
    const i32 steps = phase < 12 ? static_cast<i32>(phase) : static_cast<i32>(24 - phase);  // 0..12..0
    return (static_cast<f32>(steps) - 6.0f) * 0.125f;                                       // -0.75 .. 0.75
}

inline void PutSample(Vector<byte>& v, audio::WavEncoding e, f32 x)
{
    switch (e)
    {
    case audio::WavEncoding::U8: v.Push(static_cast<byte>(static_cast<i32>(x * 128.0f) + 128)); break;
    case audio::WavEncoding::S16: PutU16(v, static_cast<u32>(static_cast<i32>(x * 32768.0f)) & 0xFFFF); break;
    case audio::WavEncoding::S24:
    {
        const u32 raw = static_cast<u32>(static_cast<i32>(x * 8388608.0f)) & 0xFFFFFF;
        v.Push(static_cast<byte>(raw & 0xFF));
        v.Push(static_cast<byte>((raw >> 8) & 0xFF));
        v.Push(static_cast<byte>((raw >> 16) & 0xFF));
        break;
    }
    case audio::WavEncoding::S32: PutU32(v, static_cast<u32>(static_cast<i32>(x * 2147483648.0f))); break;
    case audio::WavEncoding::F32:
    {
        u32 bits = 0;
        MemCopy(&bits, &x, 4);
        PutU32(v, bits);
        break;
    }
    case audio::WavEncoding::F64:
    {
        const f64 d = x;
        u64 bits = 0;
        MemCopy(&bits, &d, 8);
        PutU32(v, static_cast<u32>(bits));
        PutU32(v, static_cast<u32>(bits >> 32));
        break;
    }
    }
}

inline Vector<byte> BuildWav(const WavSpec& spec)
{
    const u32 sample_bytes = BytesOf(spec.encoding);
    const bool is_float = spec.encoding == audio::WavEncoding::F32 || spec.encoding == audio::WavEncoding::F64;
    Vector<byte> fmt;
    PutU16(fmt, spec.extensible ? 0xFFFE : (is_float ? 3 : 1));
    PutU16(fmt, spec.channels);
    PutU32(fmt, spec.rate);
    PutU32(fmt, spec.rate * spec.channels * sample_bytes);
    PutU16(fmt, spec.channels * sample_bytes);
    PutU16(fmt, sample_bytes * 8);
    if (spec.extensible)
    {
        PutU16(fmt, 22);
        PutU16(fmt, sample_bytes * 8);
        PutU32(fmt, spec.channel_mask);
        PutU16(fmt, is_float ? 3 : 1);
        const u8 tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        for (u8 b : tail)
        {
            fmt.Push(static_cast<byte>(b));
        }
    }

    Vector<byte> body;
    PutTag(body, "WAVE");
    PutTag(body, "fmt ");
    PutU32(body, static_cast<u32>(fmt.Size()));
    for (usize i = 0; i < fmt.Size(); ++i)
    {
        body.Push(fmt[i]);
    }
    if (spec.extra_chunk_bytes != 0)
    {
        PutTag(body, "LIST");
        PutU32(body, spec.extra_chunk_bytes);
        for (u32 i = 0; i < spec.extra_chunk_bytes; ++i)
        {
            body.Push(byte(0x55));
        }
        if ((spec.extra_chunk_bytes & 1) != 0)
        {
            body.Push(byte(0));
        }
    }
    PutTag(body, "data");
    const u32 data_bytes = spec.frames * spec.channels * sample_bytes;
    PutU32(body, data_bytes);
    for (u32 f = 0; f < spec.frames; ++f)
    {
        for (u32 c = 0; c < spec.channels; ++c)
        {
            PutSample(body, spec.encoding, RampValue(f * spec.channels + c));
        }
    }
    if ((data_bytes & 1) != 0)
    {
        body.Push(byte(0));
    }
    if (spec.with_loop)
    {
        PutTag(body, "smpl");
        PutU32(body, 36 + 24);
        for (u32 i = 0; i < 7; ++i)
        {
            PutU32(body, 0);  // manufacturer, product, period, unity note, pitch fraction, smpte format, smpte offset
        }
        PutU32(body, 1);  // the loop count, at offset 28
        PutU32(body, 0);  // sampler data size
        PutU32(body, 0);  // loop id
        PutU32(body, 0);  // type
        PutU32(body, spec.loop_start);
        PutU32(body, spec.loop_end_inclusive);
        PutU32(body, 0);
        PutU32(body, 0);
    }
    if (spec.cue_count != 0)
    {
        PutTag(body, "cue ");
        PutU32(body, 4 + 24 * spec.cue_count);
        PutU32(body, spec.cue_count);
        for (u32 i = 0; i < spec.cue_count; ++i)
        {
            PutU32(body, i + 1);
            PutU32(body, 0);
            PutTag(body, "data");
            PutU32(body, 0);
            PutU32(body, 0);
            PutU32(body, i * 7);
        }
    }
    Vector<byte> file;
    PutTag(file, "RIFF");
    PutU32(file, static_cast<u32>(body.Size()));
    for (usize i = 0; i < body.Size(); ++i)
    {
        file.Push(body[i]);
    }
    return file;
}

inline ByteSpan Bytes(const Vector<byte>& v)
{
    return ByteSpan(v.Data(), v.Size());
}

}  // namespace audio_test
