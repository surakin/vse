#include "asset/texture_mips.h"

#include "asset/block_compress.h"
#include "cstr/mem_ops.h"

namespace vse::asset
{
namespace
{

// The sRGB decode of each 8-bit value as a linear 16-bit value: round(65535 * EOTF(v / 255)).
constexpr u16 kSrgbToLinear16[256] = {
    0,     20,    40,    60,    80,    99,    119,   139,   159,   179,   199,   219,   241,   264,   288,   313,
    340,   367,   396,   427,   458,   491,   526,   562,   599,   637,   677,   718,   761,   805,   851,   898,
    947,   997,   1048,  1101,  1156,  1212,  1270,  1330,  1391,  1453,  1517,  1583,  1651,  1720,  1790,  1863,
    1937,  2013,  2090,  2170,  2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,  2961,  3058,  3157,  3258,
    3360,  3464,  3570,  3678,  3788,  3900,  4014,  4129,  4247,  4366,  4488,  4611,  4736,  4864,  4993,  5124,
    5257,  5392,  5530,  5669,  5810,  5953,  6099,  6246,  6395,  6547,  6700,  6856,  7014,  7174,  7335,  7500,
    7666,  7834,  8004,  8177,  8352,  8528,  8708,  8889,  9072,  9258,  9445,  9635,  9828,  10022, 10219, 10417,
    10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
    14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
    23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
    28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
    41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
    48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
};

constexpr i64 kKaiserTaps[6] = {-5, 24, 109, 109, 24, -5};

bool MipIsHalf(rhi::Format f)
{
    return f == rhi::Format::R16_Float || f == rhi::Format::RGBA16_Float;
}

u32 MipChannels(rhi::Format f)
{
    switch (f)
    {
    case rhi::Format::R8_UNorm:
    case rhi::Format::R16_Float: return 1;
    case rhi::Format::RG8_UNorm: return 2;
    default: return 4;
    }
}

bool MipRgba8(rhi::Format f)
{
    return f == rhi::Format::RGBA8_UNorm || f == rhi::Format::RGBA8_SRGB;
}

i64 MipRoundDiv(i64 n, i64 d)
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

// The 8-bit sRGB value whose decode is nearest to `linear` (a boundary between two values goes to the lower one).
byte MipEncodeSrgb(i64 linear)
{
    if (linear <= 0)
    {
        return 0;
    }
    if (linear >= 65535)
    {
        return 255;
    }
    u32 lo = 0;
    u32 hi = 255;
    while (lo < hi)
    {
        const u32 mid = (lo + hi) / 2;
        const i64 boundary = (static_cast<i64>(kSrgbToLinear16[mid]) + static_cast<i64>(kSrgbToLinear16[mid + 1])) / 2;
        if (linear <= boundary)
        {
            hi = mid;
        }
        else
        {
            lo = mid + 1;
        }
    }
    return static_cast<byte>(lo);
}

byte MipEncodeLinear8(i64 linear16)
{
    if (linear16 <= 0)
    {
        return 0;
    }
    if (linear16 >= 65535)
    {
        return 255;
    }
    return static_cast<byte>((linear16 * 255 + 32767) / 65535);
}

// A half-float texel as an integer count of 2^-24 (every finite half is one). False for an infinity or a NaN.
bool MipHalfToUnits(u32 half, i64* out)
{
    const u32 exponent = (half >> 10) & 31;
    const u32 mantissa = half & 1023;
    if (exponent == 31)
    {
        return false;
    }
    i64 units = exponent == 0 ? static_cast<i64>(mantissa) : (static_cast<i64>(1024 + mantissa) << (exponent - 1));
    if ((half & 0x8000u) != 0)
    {
        units = -units;
    }
    *out = units;
    return true;
}

// The half float nearest to `units * 2^-24` (ties to even), clamped to the largest finite value.
u32 MipUnitsToHalf(i64 units)
{
    const u32 sign = units < 0 ? 0x8000u : 0u;
    const u64 m = static_cast<u64>(units < 0 ? -units : units);
    if (m == 0)
    {
        return 0;
    }
    u32 p = 0;
    while ((m >> (p + 1)) != 0)
    {
        ++p;
    }
    if (p < 10)
    {
        return sign | static_cast<u32>(m);  // a subnormal: exact
    }
    i32 exponent = static_cast<i32>(p) - 24;
    const u32 shift = p - 10;
    u64 mantissa = m >> shift;
    if (shift > 0)
    {
        const u64 rest = m & ((u64{1} << shift) - 1);
        const u64 half_way = u64{1} << (shift - 1);
        if (rest > half_way || (rest == half_way && (mantissa & 1) != 0))
        {
            ++mantissa;
        }
    }
    if (mantissa == 2048)
    {
        mantissa = 1024;
        ++exponent;
    }
    if (exponent > 15)
    {
        return sign | 0x7bffu;
    }
    return sign | (static_cast<u32>(exponent + 15) << 10) | static_cast<u32>(mantissa - 1024);
}

// The source texels and weights of destination texel `i` along one axis.
struct MipAxisTaps
{
    i64 position[6] = {};
    i64 weight[6] = {};
    u32 count = 0;
    i64 total = 0;
};

MipAxisTaps MipTapsFor(MipFilter filter, u32 source, u32 dest, u32 i)
{
    MipAxisTaps t;
    const u32 lo = static_cast<u32>(static_cast<u64>(i) * source / dest);
    const u32 hi = static_cast<u32>(static_cast<u64>(i + 1) * source / dest);
    if (source == dest)
    {
        t.position[0] = lo;
        t.weight[0] = 1;
        t.count = 1;
        t.total = 1;
        return t;
    }
    if (filter == MipFilter::Box)
    {
        for (u32 x = lo; x < hi; ++x)
        {
            t.position[t.count] = x;
            t.weight[t.count] = 1;
            ++t.count;
        }
        t.total = static_cast<i64>(t.count);
        return t;
    }
    for (u32 k = 0; k < 6; ++k)
    {
        i64 x = static_cast<i64>(lo) - 2 + static_cast<i64>(k);
        x = x < 0 ? 0 : (x > static_cast<i64>(source) - 1 ? static_cast<i64>(source) - 1 : x);
        t.position[k] = x;
        t.weight[k] = kKaiserTaps[k];
    }
    t.count = 6;
    t.total = 256;
    return t;
}

// Reads one texel into working values: a linear 16-bit value (half floats: a count of 2^-24) per channel, alpha as a
// 16-bit value; with `weighted` the colors are multiplied by the 8-bit alpha and alpha stays 8-bit.
bool MipReadTexel(rhi::Format format, const byte* p, bool weighted, i64* v)
{
    switch (format)
    {
    case rhi::Format::R16_Float: return MipHalfToUnits(static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8), &v[0]);
    case rhi::Format::RGBA16_Float:
        for (u32 c = 0; c < 4; ++c)
        {
            if (!MipHalfToUnits(static_cast<u32>(p[2 * c]) | (static_cast<u32>(p[2 * c + 1]) << 8), &v[c]))
            {
                return false;
            }
        }
        return true;
    case rhi::Format::R8_UNorm: v[0] = static_cast<i64>(p[0]) * 257; return true;
    case rhi::Format::RG8_UNorm:
        v[0] = static_cast<i64>(p[0]) * 257;
        v[1] = static_cast<i64>(p[1]) * 257;
        return true;
    default: break;
    }
    const bool srgb = format == rhi::Format::RGBA8_SRGB;
    for (u32 c = 0; c < 3; ++c)
    {
        const i64 linear = srgb ? static_cast<i64>(kSrgbToLinear16[p[c]]) : static_cast<i64>(p[c]) * 257;
        v[c] = weighted ? linear * static_cast<i64>(p[3]) : linear;
    }
    v[3] = weighted ? static_cast<i64>(p[3]) : static_cast<i64>(p[3]) * 257;
    return true;
}

// Writes the filtered sums `s` (each a sum of weighted texels whose weights total `total`) as a texel.
void MipWriteTexel(rhi::Format format, byte* p, bool weighted, const i64* s, i64 total)
{
    switch (format)
    {
    case rhi::Format::R16_Float:
    {
        const u32 h = MipUnitsToHalf(MipRoundDiv(s[0], total));
        p[0] = static_cast<byte>(h);
        p[1] = static_cast<byte>(h >> 8);
        return;
    }
    case rhi::Format::RGBA16_Float:
        for (u32 c = 0; c < 4; ++c)
        {
            const u32 h = MipUnitsToHalf(MipRoundDiv(s[c], total));
            p[2 * c] = static_cast<byte>(h);
            p[2 * c + 1] = static_cast<byte>(h >> 8);
        }
        return;
    case rhi::Format::R8_UNorm: p[0] = MipEncodeLinear8(MipRoundDiv(s[0], total)); return;
    case rhi::Format::RG8_UNorm:
        p[0] = MipEncodeLinear8(MipRoundDiv(s[0], total));
        p[1] = MipEncodeLinear8(MipRoundDiv(s[1], total));
        return;
    default: break;
    }
    const bool srgb = format == rhi::Format::RGBA8_SRGB;
    if (weighted)
    {
        const i64 alpha_sum = s[3];  // the weighted sum of the 8-bit alpha
        i64 a = MipRoundDiv(alpha_sum, total);
        a = a < 0 ? 0 : (a > 255 ? 255 : a);
        for (u32 c = 0; c < 3; ++c)
        {
            const i64 linear = alpha_sum > 0 ? MipRoundDiv(s[c], alpha_sum) : 0;
            p[c] = srgb ? MipEncodeSrgb(linear) : MipEncodeLinear8(linear);
        }
        p[3] = static_cast<byte>(a);
        return;
    }
    for (u32 c = 0; c < 3; ++c)
    {
        const i64 linear = MipRoundDiv(s[c], total);
        p[c] = srgb ? MipEncodeSrgb(linear) : MipEncodeLinear8(linear);
    }
    p[3] = MipEncodeLinear8(MipRoundDiv(s[3], total));
}

// One level from the one above it; false when a half-float texel is not finite.
bool MipDownsample(rhi::Format format, const MipOptions& options, const byte* src, u32 sw, u32 sh, byte* dst, u32 dw,
                   u32 dh, Vector<i64>* scratch)
{
    const u32 channels = MipChannels(format);
    const u32 texel = MipIsHalf(format) ? channels * 2 : channels;
    const bool weighted = MipRgba8(format) && options.alpha == MipAlpha::Straight;
    // The taps of every destination column and row, made once.
    Vector<MipAxisTaps> columns;
    Vector<MipAxisTaps> rows;
    columns.Resize(dw);
    rows.Resize(dh);
    for (u32 x = 0; x < dw; ++x)
    {
        columns[x] = MipTapsFor(options.filter, sw, dw, x);
    }
    for (u32 y = 0; y < dh; ++y)
    {
        rows[y] = MipTapsFor(options.filter, sh, dh, y);
    }
    // Horizontal pass: dw x sh texels of channel sums.
    for (u32 y = 0; y < sh; ++y)
    {
        for (u32 x = 0; x < dw; ++x)
        {
            const MipAxisTaps& taps = columns[x];
            i64 sum[4] = {0, 0, 0, 0};
            for (u32 k = 0; k < taps.count; ++k)
            {
                i64 v[4] = {0, 0, 0, 0};
                const byte* p = src + (static_cast<usize>(y) * sw + static_cast<usize>(taps.position[k])) * texel;
                if (!MipReadTexel(format, p, weighted, v))
                {
                    return false;
                }
                for (u32 c = 0; c < channels; ++c)
                {
                    sum[c] += v[c] * taps.weight[k];
                }
            }
            i64* out = scratch->Data() + (static_cast<usize>(y) * dw + x) * channels;
            for (u32 c = 0; c < channels; ++c)
            {
                out[c] = sum[c];
            }
        }
    }
    // Vertical pass over the sums; the total weight of a texel is the product of its two axes' totals.
    for (u32 y = 0; y < dh; ++y)
    {
        const MipAxisTaps& vt = rows[y];
        for (u32 x = 0; x < dw; ++x)
        {
            i64 sum[4] = {0, 0, 0, 0};
            for (u32 k = 0; k < vt.count; ++k)
            {
                const i64* in = scratch->Data() + (static_cast<usize>(vt.position[k]) * dw + x) * channels;
                for (u32 c = 0; c < channels; ++c)
                {
                    sum[c] += in[c] * vt.weight[k];
                }
            }
            MipWriteTexel(format, dst + (static_cast<usize>(y) * dw + x) * texel, weighted, sum,
                          columns[x].total * vt.total);
        }
    }
    return true;
}

}  // namespace

bool IsMipFormat(rhi::Format format)
{
    return MipRgba8(format) || format == rhi::Format::R8_UNorm || format == rhi::Format::RG8_UNorm || MipIsHalf(format);
}

bool IsTextureFormat(rhi::Format format)
{
    return IsMipFormat(format) || IsBcFormat(format);
}

u32 FullMipCount(u32 width, u32 height)
{
    u32 largest = width > height ? width : height;
    u32 count = 1;
    while (largest > 1)
    {
        largest >>= 1;
        ++count;
    }
    return count;
}

u64 TightRowBytes(rhi::Format format, u32 width)
{
    if (IsBcFormat(format))
    {
        return static_cast<u64>((width + 3) / 4) * rhi::FormatInfo(format).bytes_per_block;
    }
    if (!IsMipFormat(format) || width == 0)
    {
        return 0;
    }
    return static_cast<u64>(width) * (MipIsHalf(format) ? MipChannels(format) * 2 : MipChannels(format));
}

u32 LevelRows(rhi::Format format, u32 height)
{
    return IsBcFormat(format) ? (height + 3) / 4 : height;
}

u64 TightLevelBytes(rhi::Format format, u32 width, u32 height)
{
    if (!IsTextureFormat(format) || width == 0 || height == 0)
    {
        return 0;
    }
    if (IsBcFormat(format))
    {
        return TightRowBytes(format, width) * LevelRows(format, height);
    }
    const u32 channels = MipChannels(format);
    return static_cast<u64>(width) * height * (MipIsHalf(format) ? channels * 2 : channels);
}

Status PremultiplyAlphaRgba8(bool srgb, MutableByteSpan pixels)
{
    if (pixels.Size() % 4 != 0)
    {
        return Status::Make(ErrorCode::Invalid, "RGBA8 pixels are a whole number of 4-byte texels");
    }
    for (usize i = 0; i < pixels.Size(); i += 4)
    {
        byte* p = pixels.Data() + i;
        const i64 a = p[3];
        for (u32 c = 0; c < 3; ++c)
        {
            const i64 linear = srgb ? static_cast<i64>(kSrgbToLinear16[p[c]]) : static_cast<i64>(p[c]) * 257;
            const i64 premultiplied = MipRoundDiv(linear * a, 255);
            p[c] = srgb ? MipEncodeSrgb(premultiplied) : MipEncodeLinear8(premultiplied);
        }
    }
    return Status::Success();
}

Status ConvertRgba8(rhi::Format format, bool source_srgb, u32 width, u32 height, ByteSpan rgba, Vector<byte>* out)
{
    if (!IsMipFormat(format) || width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize ||
        rgba.Size() != static_cast<u64>(width) * height * 4)
    {
        return Status::Make(ErrorCode::Invalid, "the format is unsupported or the pixels do not match the size");
    }
    const usize texels = static_cast<usize>(width) * height;
    Vector<byte> result;
    if (!result.TryResizeUninitialized(static_cast<usize>(TightLevelBytes(format, width, height))))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    const u32 channels = MipChannels(format);
    for (usize t = 0; t < texels; ++t)
    {
        const byte* p = rgba.Data() + t * 4;
        if (MipIsHalf(format))
        {
            for (u32 c = 0; c < channels; ++c)
            {
                i64 units;
                if (c < 3 && source_srgb)
                {
                    units = MipRoundDiv(static_cast<i64>(kSrgbToLinear16[p[c]]) * (i64{1} << 24), 65535);
                }
                else
                {
                    units = MipRoundDiv(static_cast<i64>(p[c]) * (i64{1} << 24), 255);
                }
                const u32 h = MipUnitsToHalf(units);
                byte* o = result.Data() + (t * channels + c) * 2;
                o[0] = static_cast<byte>(h);
                o[1] = static_cast<byte>(h >> 8);
            }
        }
        else
        {
            for (u32 c = 0; c < channels; ++c)
            {
                result.Data()[t * channels + c] = p[c];
            }
        }
    }
    *out = static_cast<Vector<byte>&&>(result);
    return Status::Success();
}

Status GenerateMipChain(rhi::Format format, u32 width, u32 height, ByteSpan level0, const MipOptions& options,
                        Vector<byte>* chain)
{
    if (!IsMipFormat(format) || width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize ||
        level0.Size() != TightLevelBytes(format, width, height))
    {
        return Status::Make(ErrorCode::Invalid, "the format is unsupported or the level 0 size does not match");
    }
    if (options.alpha == MipAlpha::Straight && MipIsHalf(format))
    {
        return Status::Make(ErrorCode::Invalid,
                            "straight alpha filtering is for 8-bit formats; use premultiply or ignore for half floats");
    }
    const u32 levels = FullMipCount(width, height);
    u64 total = 0;
    u32 w = width;
    u32 h = height;
    for (u32 level = 0; level < levels; ++level)
    {
        total += TightLevelBytes(format, w, h);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    // The widest intermediate: half the width of level 0 by its height.
    const usize scratch_size = static_cast<usize>(width > 1 ? width / 2 : 1) * height * MipChannels(format);
    Vector<i64> scratch;
    Vector<byte> result;
    if (!result.TryResizeUninitialized(static_cast<usize>(total)) || !scratch.TryResize(scratch_size))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    MemCopy(result.Data(), level0.Data(), level0.Size());
    usize offset = 0;
    w = width;
    h = height;
    for (u32 level = 1; level < levels; ++level)
    {
        const usize bytes = static_cast<usize>(TightLevelBytes(format, w, h));
        const u32 dw = w > 1 ? w / 2 : 1;
        const u32 dh = h > 1 ? h / 2 : 1;
        if (!MipDownsample(format, options, result.Data() + offset, w, h, result.Data() + offset + bytes, dw, dh,
                           &scratch))
        {
            return Status::Make(ErrorCode::Invalid, "a half-float texel is infinite or not a number");
        }
        offset += bytes;
        w = dw;
        h = dh;
    }
    *chain = static_cast<Vector<byte>&&>(result);
    return Status::Success();
}

}  // namespace vse::asset
