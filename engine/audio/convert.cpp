#include "audio/convert.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::audio
{

namespace
{

// Rounds `scaled` (already multiplied by the scale) to the nearest integer, halves away from zero, clamped to [low,
// high]; a NaN is silence (0).
f64 RoundClamp(f64 scaled, f64 low, f64 high)
{
    if (IsNaN(scaled))
    {
        return 0.0;
    }
    f64 rounded = Round(scaled);
    if (rounded < low)
    {
        rounded = low;
    }
    if (rounded > high)
    {
        rounded = high;
    }
    return rounded;
}

i32 ReadS24(const byte* p)
{
    const u32 raw = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16);
    // Sign-extend from bit 23 without a shift of a negative value.
    return static_cast<i32>(raw ^ 0x800000u) - 0x800000;
}

u32 ReadU32(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

void WriteU32(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFFu);
    p[1] = static_cast<byte>((value >> 8) & 0xFFu);
    p[2] = static_cast<byte>((value >> 16) & 0xFFu);
    p[3] = static_cast<byte>((value >> 24) & 0xFFu);
}

int ChannelOf(ChannelLayout layout, Speaker speaker)
{
    const u32 count = ChannelCount(layout);
    for (u32 c = 0; c < count; ++c)
    {
        if (SpeakerOf(layout, c) == speaker)
        {
            return static_cast<int>(c);
        }
    }
    return -1;
}

// Adds `gain` from input channel `in` to the output channel of `speaker`, when the destination has one.
void AddTo(ChannelMatrix& m, ChannelLayout to, Speaker speaker, u32 in, f32 gain)
{
    const int out = ChannelOf(to, speaker);
    if (out >= 0)
    {
        m.gain[out][in] += gain;
    }
}

ChannelMatrix Build(ChannelLayout from, ChannelLayout to)
{
    ChannelMatrix m;
    m.inputs = ChannelCount(from);
    m.outputs = ChannelCount(to);
    if (from == ChannelLayout::Mono)
    {
        if (to == ChannelLayout::Mono)
        {
            m.gain[0][0] = 1.0f;
        }
        else
        {
            AddTo(m, to, Speaker::FrontLeft, 0, 1.0f);
            AddTo(m, to, Speaker::FrontRight, 0, 1.0f);
        }
        return m;
    }
    for (u32 in = 0; in < m.inputs; ++in)
    {
        const Speaker s = SpeakerOf(from, in);
        if (ChannelOf(to, s) >= 0)
        {
            AddTo(m, to, s, in, 1.0f);
            continue;
        }
        switch (s)
        {
        case Speaker::FrontCenter:
            AddTo(m, to, Speaker::FrontLeft, in, kDownmixGain);
            AddTo(m, to, Speaker::FrontRight, in, kDownmixGain);
            break;
        case Speaker::BackLeft: AddTo(m, to, Speaker::FrontLeft, in, kDownmixGain); break;
        case Speaker::BackRight: AddTo(m, to, Speaker::FrontRight, in, kDownmixGain); break;
        case Speaker::SideLeft:
            AddTo(m, to, ChannelOf(to, Speaker::BackLeft) >= 0 ? Speaker::BackLeft : Speaker::FrontLeft, in,
                  kDownmixGain);
            break;
        case Speaker::SideRight:
            AddTo(m, to, ChannelOf(to, Speaker::BackRight) >= 0 ? Speaker::BackRight : Speaker::FrontRight, in,
                  kDownmixGain);
            break;
        case Speaker::FrontLeft:
        case Speaker::FrontRight:
        case Speaker::Lfe: break;  // every layout but mono has the fronts; the LFE is dropped
        }
    }
    return m;
}

}  // namespace

f32 U8ToF32(u8 value)
{
    return static_cast<f32>(static_cast<int>(value) - 128) * (1.0f / 128.0f);
}

f32 S16ToF32(i16 value)
{
    return static_cast<f32>(value) * (1.0f / 32768.0f);
}

f32 S24ToF32(i32 value)
{
    return static_cast<f32>(value) * (1.0f / 8388608.0f);
}

f32 S32ToF32(i32 value)
{
    return static_cast<f32>(static_cast<f64>(value) * (1.0 / 2147483648.0));
}

u8 F32ToU8(f32 value)
{
    return static_cast<u8>(static_cast<int>(RoundClamp(static_cast<f64>(value) * 128.0, -128.0, 127.0)) + 128);
}

i16 F32ToS16(f32 value)
{
    return static_cast<i16>(RoundClamp(static_cast<f64>(value) * 32768.0, -32768.0, 32767.0));
}

i32 F32ToS24(f32 value)
{
    return static_cast<i32>(RoundClamp(static_cast<f64>(value) * 8388608.0, -8388608.0, 8388607.0));
}

i32 F32ToS32(f32 value)
{
    return static_cast<i32>(RoundClamp(static_cast<f64>(value) * 2147483648.0, -2147483648.0, 2147483647.0));
}

bool ConvertToF32(SampleFormat format, ByteSpan in, Span<f32> out)
{
    const usize width = BytesPerSample(format);
    if (width == 0 || in.Size() != width * out.Size())
    {
        return false;
    }
    const byte* p = in.Data();
    for (usize i = 0; i < out.Size(); ++i, p += width)
    {
        switch (format)
        {
        case SampleFormat::U8: out[i] = U8ToF32(static_cast<u8>(p[0])); break;
        case SampleFormat::S16:
            out[i] = S16ToF32(
                static_cast<i16>(static_cast<u16>(static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8))));
            break;
        case SampleFormat::S24: out[i] = S24ToF32(ReadS24(p)); break;
        case SampleFormat::S32: out[i] = S32ToF32(static_cast<i32>(ReadU32(p))); break;
        case SampleFormat::F32:
        {
            const u32 bits = ReadU32(p);
            MemCopy(&out[i], &bits, sizeof(bits));
            break;
        }
        }
    }
    return true;
}

bool ConvertFromF32(SampleFormat format, Span<const f32> in, MutableByteSpan out)
{
    const usize width = BytesPerSample(format);
    if (width == 0 || out.Size() != width * in.Size())
    {
        return false;
    }
    byte* p = out.Data();
    for (usize i = 0; i < in.Size(); ++i, p += width)
    {
        switch (format)
        {
        case SampleFormat::U8: p[0] = static_cast<byte>(F32ToU8(in[i])); break;
        case SampleFormat::S16:
        {
            const u32 raw = static_cast<u32>(static_cast<u16>(F32ToS16(in[i])));
            p[0] = static_cast<byte>(raw & 0xFFu);
            p[1] = static_cast<byte>((raw >> 8) & 0xFFu);
            break;
        }
        case SampleFormat::S24:
        {
            const u32 raw = static_cast<u32>(F32ToS24(in[i])) & 0xFFFFFFu;
            p[0] = static_cast<byte>(raw & 0xFFu);
            p[1] = static_cast<byte>((raw >> 8) & 0xFFu);
            p[2] = static_cast<byte>((raw >> 16) & 0xFFu);
            break;
        }
        case SampleFormat::S32: WriteU32(p, static_cast<u32>(F32ToS32(in[i]))); break;
        case SampleFormat::F32:
        {
            u32 bits = 0;
            MemCopy(&bits, &in[i], sizeof(bits));
            WriteU32(p, bits);
            break;
        }
        }
    }
    return true;
}

bool Interleave(Span<const f32> planar, Span<f32> out, u32 channels)
{
    if (channels == 0 || planar.Size() != out.Size() || planar.Size() % channels != 0)
    {
        return false;
    }
    const usize frames = planar.Size() / channels;
    for (u32 c = 0; c < channels; ++c)
    {
        for (usize f = 0; f < frames; ++f)
        {
            out[f * channels + c] = planar[static_cast<usize>(c) * frames + f];
        }
    }
    return true;
}

bool Deinterleave(Span<const f32> interleaved, Span<f32> planar, u32 channels)
{
    if (channels == 0 || interleaved.Size() != planar.Size() || interleaved.Size() % channels != 0)
    {
        return false;
    }
    const usize frames = interleaved.Size() / channels;
    for (u32 c = 0; c < channels; ++c)
    {
        for (usize f = 0; f < frames; ++f)
        {
            planar[static_cast<usize>(c) * frames + f] = interleaved[f * channels + c];
        }
    }
    return true;
}

ChannelMatrix BuildChannelMatrix(ChannelLayout from, ChannelLayout to)
{
    if (to != ChannelLayout::Mono || from == ChannelLayout::Mono)
    {
        return Build(from, to);
    }
    // To mono: through stereo, averaging the two sides.
    const ChannelMatrix stereo = Build(from, ChannelLayout::Stereo);
    ChannelMatrix m;
    m.inputs = stereo.inputs;
    m.outputs = 1;
    for (u32 i = 0; i < m.inputs; ++i)
    {
        m.gain[0][i] = 0.5f * (stereo.gain[0][i] + stereo.gain[1][i]);
    }
    return m;
}

bool ApplyChannelMatrix(const ChannelMatrix& matrix, Span<const f32> in, Span<f32> out, usize frames)
{
    if (in.Size() < frames * matrix.inputs || out.Size() < frames * matrix.outputs)
    {
        return false;
    }
    for (usize f = 0; f < frames; ++f)
    {
        for (u32 o = 0; o < matrix.outputs; ++o)
        {
            f32 sum = 0.0f;
            for (u32 i = 0; i < matrix.inputs; ++i)
            {
                sum += in[f * matrix.inputs + i] * matrix.gain[o][i];
            }
            out[f * matrix.outputs + o] = sum;
        }
    }
    return true;
}

}  // namespace vse::audio
