#include "asset/block_compress.h"

#include "asset/texture_mips.h"
#include "cstr/mem_ops.h"
#include "jobs/parallel_for.h"

namespace engine_placeholder::asset
{
namespace
{

// ---- shared helpers ----

// Floor division of a signed numerator by a positive denominator.
i64 BcFloorDiv(i64 num, i64 den)
{
    const i64 q = num / den;
    return (num % den != 0 && num < 0) ? q - 1 : q;
}

// The nearest integer to num / den (halves round up), den > 0.
i64 BcDivRound(i64 num, i64 den)
{
    return BcFloorDiv(num * 2 + den, den * 2);
}

i32 BcClamp255(i64 v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : static_cast<i32>(v));
}

// ---- BC4 channel (an 8-byte block of one channel) ----

// The eight palette values of the endpoint pair: a0 > a1 gives eight steps, otherwise six plus 0 and 255.
void Bc4Palette(i32 a0, i32 a1, i32* pal)
{
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1)
    {
        for (i32 i = 1; i <= 6; ++i)
        {
            pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
        }
    }
    else
    {
        for (i32 i = 1; i <= 4; ++i)
        {
            pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
        }
        pal[6] = 0;
        pal[7] = 255;
    }
}

void Bc4DecodeChannel(const byte* block, byte* out16)
{
    i32 pal[8];
    Bc4Palette(block[0], block[1], pal);
    u64 bits = 0;
    for (u32 i = 0; i < 6; ++i)
    {
        bits |= static_cast<u64>(block[2 + i]) << (8 * i);
    }
    for (u32 i = 0; i < 16; ++i)
    {
        out16[i] = static_cast<byte>(pal[(bits >> (3 * i)) & 7]);
    }
}

struct Bc4Fit
{
    u32 error = 0xffffffffu;
    u64 bits = 0;
    i32 a0 = 0;
    i32 a1 = 0;
};

Bc4Fit Bc4Evaluate(const i32* v, i32 a0, i32 a1)
{
    i32 pal[8];
    Bc4Palette(a0, a1, pal);
    Bc4Fit fit;
    fit.a0 = a0;
    fit.a1 = a1;
    fit.error = 0;
    fit.bits = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        u32 best = 0;
        i32 best_d = (v[i] - pal[0]) * (v[i] - pal[0]);
        for (u32 k = 1; k < 8; ++k)
        {
            const i32 d = (v[i] - pal[k]) * (v[i] - pal[k]);
            if (d < best_d)
            {
                best_d = d;
                best = k;
            }
        }
        fit.error += static_cast<u32>(best_d);
        fit.bits |= static_cast<u64>(best) << (3 * i);
    }
    return fit;
}

// Least-squares endpoints for the indices of `fit`. Returns false when the system is singular.
bool Bc4Refine(const i32* v, const Bc4Fit& fit, i32* out_a, i32* out_b)
{
    const bool eight = fit.a0 > fit.a1;
    const i64 den = eight ? 7 : 5;
    i64 sww = 0;
    i64 suu = 0;
    i64 swu = 0;
    i64 swp = 0;
    i64 sup = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 code = static_cast<u32>((fit.bits >> (3 * i)) & 7);
        i64 x;
        if (code == 0)
        {
            x = 0;
        }
        else if (code == 1)
        {
            x = den;
        }
        else if (!eight && code >= 6)
        {
            continue;  // the fixed 0 and 255 do not move with the endpoints
        }
        else
        {
            x = static_cast<i64>(code) - 1;
        }
        const i64 w = den - x;
        sww += w * w;
        suu += x * x;
        swu += w * x;
        swp += w * v[i];
        sup += x * v[i];
    }
    const i64 det = sww * suu - swu * swu;
    if (det == 0)
    {
        return false;
    }
    *out_a = BcClamp255(BcDivRound(den * (swp * suu - sup * swu), det));
    *out_b = BcClamp255(BcDivRound(den * (sup * sww - swp * swu), det));
    return true;
}

void Bc4EncodeChannel(const i32* v, BcQuality quality, byte* out)
{
    i32 lo = v[0];
    i32 hi = v[0];
    for (u32 i = 1; i < 16; ++i)
    {
        lo = v[i] < lo ? v[i] : lo;
        hi = v[i] > hi ? v[i] : hi;
    }
    Bc4Fit best = Bc4Evaluate(v, hi, lo);
    if (lo != hi && best.error != 0)
    {
        const Bc4Fit six = Bc4Evaluate(v, lo, hi);
        if (six.error < best.error)
        {
            best = six;
        }
    }
    const u32 passes = quality == BcQuality::Fast ? 0 : (quality == BcQuality::Normal ? 2 : 4);
    for (u32 pass = 0; pass < passes && best.error != 0; ++pass)
    {
        i32 a = 0;
        i32 b = 0;
        if (!Bc4Refine(v, best, &a, &b))
        {
            break;
        }
        Bc4Fit next = Bc4Evaluate(v, a, b);
        const Bc4Fit swapped = Bc4Evaluate(v, b, a);
        if (swapped.error < next.error)
        {
            next = swapped;
        }
        if (next.error >= best.error)
        {
            break;
        }
        best = next;
    }
    out[0] = static_cast<byte>(best.a0);
    out[1] = static_cast<byte>(best.a1);
    for (u32 i = 0; i < 6; ++i)
    {
        out[2 + i] = static_cast<byte>((best.bits >> (8 * i)) & 0xff);
    }
}

// ---- BC1 color block (8 bytes) ----

i32 BcExpand(u32 v, u32 bits)
{
    return bits == 5 ? static_cast<i32>((v << 3) | (v >> 2)) : static_cast<i32>((v << 2) | (v >> 4));
}

void Bc1Unpack(u16 c, i32* rgb)
{
    rgb[0] = BcExpand((c >> 11) & 31u, 5);
    rgb[1] = BcExpand((c >> 5) & 63u, 6);
    rgb[2] = BcExpand(c & 31u, 5);
}

// The palette of the endpoint pair; `four` selects the 4-color form. Alpha is 255 except the 3-color form's code 3.
void Bc1Palette(u16 c0, u16 c1, bool four, i32 (*pal)[4])
{
    Bc1Unpack(c0, pal[0]);
    Bc1Unpack(c1, pal[1]);
    for (u32 c = 0; c < 3; ++c)
    {
        if (four)
        {
            pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
            pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
        }
        else
        {
            pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
            pal[3][c] = 0;
        }
    }
    for (u32 k = 0; k < 4; ++k)
    {
        pal[k][3] = 255;
    }
    if (!four)
    {
        pal[3][3] = 0;
    }
}

// `force_four` is set for the color half of BC2 and BC3, which never use the 3-color form.
void Bc1DecodeColor(const byte* block, bool force_four, byte* rgba)
{
    const u16 c0 = static_cast<u16>(block[0] | (block[1] << 8));
    const u16 c1 = static_cast<u16>(block[2] | (block[3] << 8));
    i32 pal[4][4];
    Bc1Palette(c0, c1, force_four || c0 > c1, pal);
    const u32 bits = static_cast<u32>(block[4]) | (static_cast<u32>(block[5]) << 8) |
                     (static_cast<u32>(block[6]) << 16) | (static_cast<u32>(block[7]) << 24);
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 code = (bits >> (2 * i)) & 3;
        for (u32 c = 0; c < 4; ++c)
        {
            rgba[i * 4 + c] = static_cast<byte>(pal[code][c]);
        }
    }
}

// The 5- or 6-bit value whose expansion is nearest to the 8-bit `v` (the lower one on a tie).
u32 BcQuantize(i32 v, u32 bits)
{
    const i32 top = (1 << bits) - 1;
    i32 q = (v * top + 127) / 255;
    i32 best = q;
    i32 best_d = 1 << 30;
    for (i32 cand = q - 1; cand <= q + 1; ++cand)
    {
        if (cand < 0 || cand > top)
        {
            continue;
        }
        const i32 d = BcExpand(static_cast<u32>(cand), bits) - v;
        const i32 dd = d * d;
        if (dd < best_d)
        {
            best_d = dd;
            best = cand;
        }
    }
    return static_cast<u32>(best);
}

u16 Bc1Quantize(const i32* rgb)
{
    return static_cast<u16>((BcQuantize(rgb[0], 5) << 11) | (BcQuantize(rgb[1], 6) << 5) | BcQuantize(rgb[2], 5));
}

struct Bc1Block
{
    i32 pix[16][3];
    bool valid[16];
    u32 count = 0;  // valid texels
    bool has_transparent = false;
};

struct Bc1Fit
{
    u32 error = 0xffffffffu;
    u32 bits = 0;
    u16 c0 = 0;
    u16 c1 = 0;
    bool four = false;
};

// Fits the quantized endpoints `a`, `b` (any order): the pair is ordered for the block's mode and the nearest palette
// code is chosen for every valid texel.
Bc1Fit Bc1Evaluate(const Bc1Block& blk, u16 a, u16 b)
{
    Bc1Fit fit;
    u16 c0 = a;
    u16 c1 = b;
    if (blk.has_transparent)
    {
        if (c0 > c1)
        {
            c0 = b;
            c1 = a;
        }
        fit.four = false;
    }
    else
    {
        if (c0 < c1)
        {
            c0 = b;
            c1 = a;
        }
        fit.four = c0 > c1;
    }
    fit.c0 = c0;
    fit.c1 = c1;
    i32 pal[4][4];
    Bc1Palette(c0, c1, fit.four, pal);
    const u32 colors = fit.four ? 4 : 3;
    fit.error = 0;
    fit.bits = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        u32 code = 3;
        if (blk.valid[i])
        {
            i32 best_d = 1 << 30;
            for (u32 k = 0; k < colors; ++k)
            {
                const i32 d0 = blk.pix[i][0] - pal[k][0];
                const i32 d1 = blk.pix[i][1] - pal[k][1];
                const i32 d2 = blk.pix[i][2] - pal[k][2];
                const i32 d = d0 * d0 + d1 * d1 + d2 * d2;
                if (d < best_d)
                {
                    best_d = d;
                    code = k;
                }
            }
            fit.error += static_cast<u32>(best_d);
        }
        fit.bits |= code << (2 * i);
    }
    return fit;
}

// Least-squares endpoints (8-bit per channel) for the codes of `fit`.
bool Bc1Refine(const Bc1Block& blk, const Bc1Fit& fit, i32* ea, i32* eb)
{
    const i64 den = fit.four ? 3 : 2;
    // Position along the line, in steps of 1/den, of each code.
    const i64 pos4[4] = {0, 3, 1, 2};
    const i64 pos3[4] = {0, 2, 1, 0};
    i64 sww = 0;
    i64 suu = 0;
    i64 swu = 0;
    i64 swp[3] = {0, 0, 0};
    i64 sup[3] = {0, 0, 0};
    for (u32 i = 0; i < 16; ++i)
    {
        if (!blk.valid[i])
        {
            continue;
        }
        const u32 code = (fit.bits >> (2 * i)) & 3;
        const i64 x = fit.four ? pos4[code] : pos3[code];
        const i64 w = den - x;
        sww += w * w;
        suu += x * x;
        swu += w * x;
        for (u32 c = 0; c < 3; ++c)
        {
            swp[c] += w * blk.pix[i][c];
            sup[c] += x * blk.pix[i][c];
        }
    }
    const i64 det = sww * suu - swu * swu;
    if (det == 0)
    {
        return false;
    }
    for (u32 c = 0; c < 3; ++c)
    {
        ea[c] = BcClamp255(BcDivRound(den * (swp[c] * suu - sup[c] * swu), det));
        eb[c] = BcClamp255(BcDivRound(den * (sup[c] * sww - swp[c] * swu), det));
    }
    return true;
}

void Bc1Write(const Bc1Fit& fit, byte* out)
{
    out[0] = static_cast<byte>(fit.c0 & 0xff);
    out[1] = static_cast<byte>(fit.c0 >> 8);
    out[2] = static_cast<byte>(fit.c1 & 0xff);
    out[3] = static_cast<byte>(fit.c1 >> 8);
    for (u32 i = 0; i < 4; ++i)
    {
        out[4 + i] = static_cast<byte>((fit.bits >> (8 * i)) & 0xff);
    }
}

void Bc1EncodeBlock(const byte* rgba, const BcEncodeOptions& options, byte* out)
{
    Bc1Block blk;
    i32 min_c[3] = {255, 255, 255};
    i32 max_c[3] = {0, 0, 0};
    i64 sum[3] = {0, 0, 0};
    for (u32 i = 0; i < 16; ++i)
    {
        blk.valid[i] = !(options.punchthrough_alpha && rgba[i * 4 + 3] < 128);
        blk.has_transparent = blk.has_transparent || !blk.valid[i];
        for (u32 c = 0; c < 3; ++c)
        {
            blk.pix[i][c] = rgba[i * 4 + c];
        }
        if (!blk.valid[i])
        {
            continue;
        }
        ++blk.count;
        for (u32 c = 0; c < 3; ++c)
        {
            min_c[c] = blk.pix[i][c] < min_c[c] ? blk.pix[i][c] : min_c[c];
            max_c[c] = blk.pix[i][c] > max_c[c] ? blk.pix[i][c] : max_c[c];
            sum[c] += blk.pix[i][c];
        }
    }
    if (blk.count == 0)
    {
        Bc1Fit fit;
        fit.c0 = 0;
        fit.c1 = 0;
        fit.four = false;
        fit.bits = 0xffffffffu;
        Bc1Write(fit, out);
        return;
    }
    i32 mean[3];
    for (u32 c = 0; c < 3; ++c)
    {
        mean[c] = static_cast<i32>(BcDivRound(sum[c], blk.count));
    }
    // Principal axis of the colors by a few power iterations on the covariance, in integers.
    i64 cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (u32 i = 0; i < 16; ++i)
    {
        if (!blk.valid[i])
        {
            continue;
        }
        i64 d[3];
        for (u32 c = 0; c < 3; ++c)
        {
            d[c] = blk.pix[i][c] - mean[c];
        }
        for (u32 r = 0; r < 3; ++r)
        {
            for (u32 c = 0; c < 3; ++c)
            {
                cov[r][c] += d[r] * d[c];
            }
        }
    }
    i64 axis[3] = {max_c[0] - min_c[0], max_c[1] - min_c[1], max_c[2] - min_c[2]};
    for (u32 iter = 0; iter < 4; ++iter)
    {
        i64 next[3];
        i64 m = 0;
        for (u32 r = 0; r < 3; ++r)
        {
            next[r] = cov[r][0] * axis[0] + cov[r][1] * axis[1] + cov[r][2] * axis[2];
            const i64 a = next[r] < 0 ? -next[r] : next[r];
            m = a > m ? a : m;
        }
        if (m == 0)
        {
            break;
        }
        for (u32 r = 0; r < 3; ++r)
        {
            axis[r] = next[r] * 1024 / m;
        }
    }
    i64 lo_d = 0;
    i64 hi_d = 0;
    u32 lo_i = 16;
    u32 hi_i = 16;
    for (u32 i = 0; i < 16; ++i)
    {
        if (!blk.valid[i])
        {
            continue;
        }
        i64 d = 0;
        for (u32 c = 0; c < 3; ++c)
        {
            d += axis[c] * (blk.pix[i][c] - mean[c]);
        }
        if (lo_i == 16 || d < lo_d)
        {
            lo_d = d;
            lo_i = i;
        }
        if (hi_i == 16 || d > hi_d)
        {
            hi_d = d;
            hi_i = i;
        }
    }
    // The range fit: the two extreme colors along the axis, and the same pair pulled in by 1/8 and 1/4 of the span (the
    // extremes overshoot when the colors scatter around the axis); the lowest error wins, the first on a tie.
    Bc1Fit best = Bc1Evaluate(blk, Bc1Quantize(blk.pix[hi_i]), Bc1Quantize(blk.pix[lo_i]));
    for (i32 inset = 8; inset >= 4 && best.error != 0; inset -= 4)
    {
        i32 pa[3];
        i32 pb[3];
        for (u32 c = 0; c < 3; ++c)
        {
            const i32 span = blk.pix[hi_i][c] - blk.pix[lo_i][c];
            pa[c] = blk.pix[hi_i][c] - static_cast<i32>(BcDivRound(span, inset));
            pb[c] = blk.pix[lo_i][c] + static_cast<i32>(BcDivRound(span, inset));
        }
        const Bc1Fit candidate = Bc1Evaluate(blk, Bc1Quantize(pa), Bc1Quantize(pb));
        if (candidate.error < best.error)
        {
            best = candidate;
        }
    }
    const u32 passes = options.quality == BcQuality::Fast ? 0 : (options.quality == BcQuality::Normal ? 2 : 4);
    for (u32 pass = 0; pass < passes && best.error != 0; ++pass)
    {
        i32 ea[3];
        i32 eb[3];
        if (!Bc1Refine(blk, best, ea, eb))
        {
            break;
        }
        const Bc1Fit next = Bc1Evaluate(blk, Bc1Quantize(ea), Bc1Quantize(eb));
        if (next.error >= best.error)
        {
            break;
        }
        best = next;
    }
    Bc1Write(best, out);
}

// ---- BC7 ----

// The per-mode facts of the format: subsets, partition bits, rotation bits, index-selection bits, color and alpha
// endpoint bits, the p-bit kind (0 none, 1 one per endpoint, 2 one per subset) and the index widths.
struct Bc7Mode
{
    u8 subsets;
    u8 partition_bits;
    u8 rotation_bits;
    u8 selector_bits;
    u8 color_bits;
    u8 alpha_bits;
    u8 pbit_kind;
    u8 index_bits;
    u8 index2_bits;
};

constexpr Bc7Mode kBc7Modes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 3, 0}, {2, 6, 0, 0, 6, 0, 2, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 2, 0}, {2, 6, 0, 0, 7, 0, 1, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 2, 2}, {1, 0, 0, 0, 7, 7, 1, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 2, 0},
};

// The partitions: for two subsets a mask of the texels in subset 1, for three two bits per texel; the anchor texel
// of each later subset (the texel whose index loses its top bit). Taken from the format's published tables.
constexpr u16 kBc7Partition2[64] = {
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8,
    0xff00, 0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110,
    0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e, 0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696,
    0xa55a, 0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660, 0x0272, 0x04e4, 0x4e40, 0x2720,
    0xc936, 0x936c, 0x39c6, 0x639c, 0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22,
};
constexpr u8 kBc7Anchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2,  2, 8,
    8,  15, 2,  8,  2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2, 8,  2, 2,
    2,  15, 15, 6,  6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2, 15,
};
constexpr u32 kBc7Partition3[64] = {
    0xaa685050u, 0x6a5a5040u, 0x5a5a4200u, 0x5450a0a8u, 0xa5a50000u, 0xa0a05050u, 0x5555a0a0u, 0x5a5a5050u,
    0xaa550000u, 0xaa555500u, 0xaaaa5500u, 0x90909090u, 0x94949494u, 0xa4a4a4a4u, 0xa9a59450u, 0x2a0a4250u,
    0xa5945040u, 0x0a425054u, 0xa5a5a500u, 0x55a0a0a0u, 0xa8a85454u, 0x6a6a4040u, 0xa4a45000u, 0x1a1a0500u,
    0x0050a4a4u, 0xaaa59090u, 0x14696914u, 0x69691400u, 0xa08585a0u, 0xaa821414u, 0x50a4a450u, 0x6a5a0200u,
    0xa9a58000u, 0x5090a0a8u, 0xa8a09050u, 0x24242424u, 0x00aa5500u, 0x24924924u, 0x24499224u, 0x50a50a50u,
    0x500aa550u, 0xaaaa4444u, 0x66660000u, 0xa5a0a5a0u, 0x50a050a0u, 0x69286928u, 0x44aaaa44u, 0x66666600u,
    0xaa444444u, 0x54a854a8u, 0x95809580u, 0x96969600u, 0xa85454a8u, 0x80959580u, 0xaa141414u, 0x96960000u,
    0xaaaa1414u, 0xa05050a0u, 0xa0a5a5a0u, 0x96000000u, 0x40804080u, 0xa9a8a9a8u, 0xaaaaaa44u, 0x2a4a5254u,
};
constexpr u8 kBc7Anchor3a[64] = {
    3, 3,  15, 15, 8, 3,  15, 15, 8,  8, 6,  6, 6,  5,  3,  3,  3, 3,  8, 15, 3, 3, 6, 10, 5, 8,  8, 6,  8,  5,  15, 15,
    8, 15, 3,  5,  6, 10, 8,  15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5,  5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3,  3,
};
constexpr u8 kBc7Anchor3b[64] = {
    15, 8, 8, 3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,  15, 8,  15, 3,  15, 8,
    15, 8, 3, 15, 6,  10, 15, 15, 10, 8,  15, 3,  15, 10, 10, 8,  9,  10, 6,  15, 8,  15,
    3,  6, 6, 8,  15, 3,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8,
};

constexpr u8 kBc7Weights2[4] = {0, 21, 43, 64};
constexpr u8 kBc7Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr u8 kBc7Weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

u32 Bc7Weight(u32 bits, u32 index)
{
    return bits == 2 ? kBc7Weights2[index] : (bits == 3 ? kBc7Weights3[index] : kBc7Weights4[index]);
}

// Reads bit fields from the 128-bit block, least significant bit first.
class Bc7Reader
{
public:
    explicit Bc7Reader(const byte* block) : block_(block) {}
    u32 Get(u32 bits)
    {
        u32 v = 0;
        for (u32 i = 0; i < bits; ++i, ++pos_)
        {
            v |= static_cast<u32>((block_[pos_ >> 3] >> (pos_ & 7)) & 1u) << i;
        }
        return v;
    }

private:
    const byte* block_;
    u32 pos_ = 0;
};

u32 Bc7SubsetOf(u32 subsets, u32 partition, u32 texel)
{
    if (subsets == 1)
    {
        return 0;
    }
    if (subsets == 2)
    {
        return (kBc7Partition2[partition] >> texel) & 1u;
    }
    return (kBc7Partition3[partition] >> (2 * texel)) & 3u;
}

bool Bc7IsAnchor(u32 subsets, u32 partition, u32 texel)
{
    if (texel == 0)
    {
        return true;
    }
    if (subsets == 2)
    {
        return kBc7Anchor2[partition] == texel;
    }
    if (subsets == 3)
    {
        return kBc7Anchor3a[partition] == texel || kBc7Anchor3b[partition] == texel;
    }
    return false;
}

i32 Bc7Interpolate(i32 a, i32 b, u32 weight)
{
    return ((64 - static_cast<i32>(weight)) * a + static_cast<i32>(weight) * b + 32) >> 6;
}

void Bc7Decode(const byte* block, byte* rgba)
{
    u32 mode = 0;
    while (mode < 8 && ((block[0] >> mode) & 1u) == 0)
    {
        ++mode;
    }
    if (mode == 8)
    {
        MemSet(rgba, 0, 64);  // the reserved mode: transparent black
        return;
    }
    const Bc7Mode& m = kBc7Modes[mode];
    Bc7Reader in(block);
    in.Get(mode + 1);
    const u32 partition = in.Get(m.partition_bits);
    const u32 rotation = in.Get(m.rotation_bits);
    const u32 selector = in.Get(m.selector_bits);
    const u32 endpoints = 2u * m.subsets;
    i32 ep[6][4];
    for (u32 c = 0; c < 3; ++c)
    {
        for (u32 e = 0; e < endpoints; ++e)
        {
            ep[e][c] = static_cast<i32>(in.Get(m.color_bits));
        }
    }
    for (u32 e = 0; e < endpoints; ++e)
    {
        ep[e][3] = m.alpha_bits != 0 ? static_cast<i32>(in.Get(m.alpha_bits)) : 0;
    }
    u32 pbit[6] = {0, 0, 0, 0, 0, 0};
    if (m.pbit_kind == 1)
    {
        for (u32 e = 0; e < endpoints; ++e)
        {
            pbit[e] = in.Get(1);
        }
    }
    else if (m.pbit_kind == 2)
    {
        for (u32 s = 0; s < m.subsets; ++s)
        {
            pbit[2 * s] = pbit[2 * s + 1] = in.Get(1);
        }
    }
    for (u32 e = 0; e < endpoints; ++e)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            const u32 bits = c < 3 ? m.color_bits : m.alpha_bits;
            if (bits == 0)
            {
                ep[e][c] = 255;
                continue;
            }
            u32 v = static_cast<u32>(ep[e][c]);
            u32 total = bits;
            if (m.pbit_kind != 0)
            {
                v = (v << 1) | pbit[e];
                total = bits + 1;
            }
            ep[e][c] = static_cast<i32>((v << (8 - total)) | (v >> (2 * total - 8)));
        }
    }
    // The indices: the first set, then (modes 4 and 5) the second set.
    u32 index[16];
    u32 index2[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 bits = m.index_bits - (Bc7IsAnchor(m.subsets, partition, i) ? 1u : 0u);
        index[i] = in.Get(bits);
    }
    for (u32 i = 0; i < 16 && m.index2_bits != 0; ++i)
    {
        const u32 bits = m.index2_bits - (i == 0 ? 1u : 0u);
        index2[i] = in.Get(bits);
    }
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 s = Bc7SubsetOf(m.subsets, partition, i);
        const i32* a = ep[2 * s];
        const i32* b = ep[2 * s + 1];
        u32 color_weight;
        u32 alpha_weight;
        if (m.index2_bits == 0)
        {
            color_weight = alpha_weight = Bc7Weight(m.index_bits, index[i]);
        }
        else if (selector == 0)
        {
            color_weight = Bc7Weight(m.index_bits, index[i]);
            alpha_weight = Bc7Weight(m.index2_bits, index2[i]);
        }
        else
        {
            color_weight = Bc7Weight(m.index2_bits, index2[i]);
            alpha_weight = Bc7Weight(m.index_bits, index[i]);
        }
        i32 px[4];
        for (u32 c = 0; c < 3; ++c)
        {
            px[c] = Bc7Interpolate(a[c], b[c], color_weight);
        }
        px[3] = Bc7Interpolate(a[3], b[3], alpha_weight);
        if (rotation != 0)
        {
            const i32 t = px[3];
            px[3] = px[rotation - 1];
            px[rotation - 1] = t;
        }
        for (u32 c = 0; c < 4; ++c)
        {
            rgba[i * 4 + c] = static_cast<byte>(px[c]);
        }
    }
}

// The mode 6 encoder: one subset, RGBA, 7-bit endpoints each with its own p-bit, 4-bit indices.
struct Bc7Fit
{
    u32 error = 0xffffffffu;
    i32 end[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};  // the decoded 8-bit endpoint values
    u32 pbit[2] = {0, 0};
    u32 index[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};

// The best fit for one choice of the two p-bits: endpoints rounded to the 7-bit grid, the nearest of the 16 steps.
Bc7Fit Bc7EvaluatePbits(const i32 (*pix)[4], const i32* t0, const i32* t1, u32 p0, u32 p1)
{
    Bc7Fit fit;
    fit.pbit[0] = p0;
    fit.pbit[1] = p1;
    for (u32 c = 0; c < 4; ++c)
    {
        for (u32 e = 0; e < 2; ++e)
        {
            const i32 t = e == 0 ? t0[c] : t1[c];
            const i32 p = static_cast<i32>(e == 0 ? p0 : p1);
            i32 q = t >= p ? (t - p + 1) >> 1 : 0;
            q = q > 127 ? 127 : q;
            fit.end[e][c] = 2 * q + p;
        }
    }
    i32 pal[16][4];
    for (u32 k = 0; k < 16; ++k)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            pal[k][c] = Bc7Interpolate(fit.end[0][c], fit.end[1][c], kBc7Weights4[k]);
        }
    }
    fit.error = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        i32 best_d = 1 << 30;
        u32 best = 0;
        for (u32 k = 0; k < 16; ++k)
        {
            i32 d = 0;
            for (u32 c = 0; c < 4; ++c)
            {
                const i32 e = pix[i][c] - pal[k][c];
                d += e * e;
            }
            if (d < best_d)
            {
                best_d = d;
                best = k;
            }
        }
        fit.index[i] = best;
        fit.error += static_cast<u32>(best_d);
    }
    return fit;
}

// The best of the allowed p-bit combinations (bit `combo` of `allowed`; the first on a tie), with the anchor texel's
// index brought below 8 by swapping the endpoints.
Bc7Fit Bc7Evaluate(const i32 (*pix)[4], const i32* t0, const i32* t1, u32 allowed)
{
    Bc7Fit best;
    for (u32 combo = 0; combo < 4; ++combo)
    {
        if (((allowed >> combo) & 1u) == 0)
        {
            continue;
        }
        const Bc7Fit fit = Bc7EvaluatePbits(pix, t0, t1, combo & 1u, combo >> 1);
        if (fit.error < best.error)
        {
            best = fit;
        }
    }
    if (best.index[0] >= 8)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            const i32 t = best.end[0][c];
            best.end[0][c] = best.end[1][c];
            best.end[1][c] = t;
        }
        const u32 p = best.pbit[0];
        best.pbit[0] = best.pbit[1];
        best.pbit[1] = p;
        for (u32 i = 0; i < 16; ++i)
        {
            best.index[i] = 15 - best.index[i];
        }
    }
    return best;
}

void Bc7WriteBits(byte* block, u32* pos, u32 value, u32 bits)
{
    for (u32 i = 0; i < bits; ++i, ++*pos)
    {
        if (((value >> i) & 1u) != 0)
        {
            block[*pos >> 3] = static_cast<byte>(block[*pos >> 3] | (1u << (*pos & 7)));
        }
    }
}

void Bc7EncodeBlock(const byte* rgba, const BcEncodeOptions& options, byte* out)
{
    i32 pix[16][4];
    i32 lo_c[4] = {255, 255, 255, 255};
    i32 hi_c[4] = {0, 0, 0, 0};
    i64 sum[4] = {0, 0, 0, 0};
    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            pix[i][c] = (c == 3 && options.drop_alpha) ? 255 : rgba[i * 4 + c];
            lo_c[c] = pix[i][c] < lo_c[c] ? pix[i][c] : lo_c[c];
            hi_c[c] = pix[i][c] > hi_c[c] ? pix[i][c] : hi_c[c];
            sum[c] += pix[i][c];
        }
    }
    i32 mean[4];
    for (u32 c = 0; c < 4; ++c)
    {
        mean[c] = static_cast<i32>(BcDivRound(sum[c], 16));
    }
    i64 cov[4][4];
    for (u32 r = 0; r < 4; ++r)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            cov[r][c] = 0;
        }
    }
    for (u32 i = 0; i < 16; ++i)
    {
        for (u32 r = 0; r < 4; ++r)
        {
            for (u32 c = 0; c < 4; ++c)
            {
                cov[r][c] += static_cast<i64>(pix[i][r] - mean[r]) * (pix[i][c] - mean[c]);
            }
        }
    }
    i64 axis[4];
    for (u32 c = 0; c < 4; ++c)
    {
        axis[c] = hi_c[c] - lo_c[c];
    }
    for (u32 iter = 0; iter < 4; ++iter)
    {
        i64 next[4];
        i64 m = 0;
        for (u32 r = 0; r < 4; ++r)
        {
            next[r] = cov[r][0] * axis[0] + cov[r][1] * axis[1] + cov[r][2] * axis[2] + cov[r][3] * axis[3];
            const i64 a = next[r] < 0 ? -next[r] : next[r];
            m = a > m ? a : m;
        }
        if (m == 0)
        {
            break;
        }
        for (u32 r = 0; r < 4; ++r)
        {
            axis[r] = next[r] * 1024 / m;
        }
    }
    u32 lo_i = 0;
    u32 hi_i = 0;
    i64 lo_d = 0;
    i64 hi_d = 0;
    for (u32 i = 0; i < 16; ++i)
    {
        i64 d = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            d += axis[c] * (pix[i][c] - mean[c]);
        }
        if (i == 0 || d < lo_d)
        {
            lo_d = d;
            lo_i = i;
        }
        if (i == 0 || d > hi_d)
        {
            hi_d = d;
            hi_i = i;
        }
    }
    // An opaque block keeps both p-bits at 1, so that alpha 255 is exact (an endpoint is `2 * q + p`); any other block
    // tries all four combinations.
    const u32 allowed = (lo_c[3] == 255) ? 8u : 15u;
    // The range fit: the extremes and the same pair pulled in by 1/16 and 1/8 of the span.
    Bc7Fit best = Bc7Evaluate(pix, pix[hi_i], pix[lo_i], allowed);
    for (i32 inset = 16; inset >= 8 && best.error != 0; inset -= 8)
    {
        i32 pa[4];
        i32 pb[4];
        for (u32 c = 0; c < 4; ++c)
        {
            const i32 span = pix[hi_i][c] - pix[lo_i][c];
            pa[c] = pix[hi_i][c] - static_cast<i32>(BcDivRound(span, inset));
            pb[c] = pix[lo_i][c] + static_cast<i32>(BcDivRound(span, inset));
        }
        const Bc7Fit candidate = Bc7Evaluate(pix, pa, pb, allowed);
        if (candidate.error < best.error)
        {
            best = candidate;
        }
    }
    const u32 passes = options.quality == BcQuality::Fast ? 0 : (options.quality == BcQuality::Normal ? 2 : 6);
    for (u32 pass = 0; pass < passes && best.error != 0; ++pass)
    {
        // Least squares for the endpoints given the indices (positions are the 64ths weights).
        i64 sww = 0;
        i64 suu = 0;
        i64 swu = 0;
        i64 swp[4] = {0, 0, 0, 0};
        i64 sup[4] = {0, 0, 0, 0};
        for (u32 i = 0; i < 16; ++i)
        {
            const i64 x = kBc7Weights4[best.index[i]];
            const i64 w = 64 - x;
            sww += w * w;
            suu += x * x;
            swu += w * x;
            for (u32 c = 0; c < 4; ++c)
            {
                swp[c] += w * pix[i][c];
                sup[c] += x * pix[i][c];
            }
        }
        const i64 det = sww * suu - swu * swu;
        if (det == 0)
        {
            break;
        }
        i32 ea[4];
        i32 eb[4];
        for (u32 c = 0; c < 4; ++c)
        {
            ea[c] = BcClamp255(BcDivRound(64 * (swp[c] * suu - sup[c] * swu), det));
            eb[c] = BcClamp255(BcDivRound(64 * (sup[c] * sww - swp[c] * swu), det));
        }
        const Bc7Fit next = Bc7Evaluate(pix, ea, eb, allowed);
        if (next.error >= best.error)
        {
            break;
        }
        best = next;
    }
    MemSet(out, 0, 16);
    u32 pos = 0;
    Bc7WriteBits(out, &pos, 0x40, 7);  // mode 6
    for (u32 c = 0; c < 4; ++c)
    {
        Bc7WriteBits(out, &pos, static_cast<u32>(best.end[0][c]) >> 1, 7);
        Bc7WriteBits(out, &pos, static_cast<u32>(best.end[1][c]) >> 1, 7);
    }
    Bc7WriteBits(out, &pos, best.pbit[0], 1);
    Bc7WriteBits(out, &pos, best.pbit[1], 1);
    for (u32 i = 0; i < 16; ++i)
    {
        Bc7WriteBits(out, &pos, best.index[i], i == 0 ? 3 : 4);
    }
}

// ---- the public block functions ----

void Bc2DecodeAlpha(const byte* block, byte* alpha16)
{
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 nibble = (block[i / 2] >> (4 * (i % 2))) & 15u;
        alpha16[i] = static_cast<byte>(nibble * 17);
    }
}

struct BcEncodeContext
{
    BcKind kind;
    u32 width;
    u32 height;
    u32 blocks_wide;
    const byte* rgba;
    const BcEncodeOptions* options;
    byte* out;
};

void BcEncodeRows(void* user, usize begin, usize end)
{
    const BcEncodeContext& ctx = *static_cast<const BcEncodeContext*>(user);
    const u32 block_bytes = BcBlockBytes(ctx.kind);
    for (usize by = begin; by < end; ++by)
    {
        for (u32 bx = 0; bx < ctx.blocks_wide; ++bx)
        {
            byte texels[64];
            for (u32 y = 0; y < 4; ++y)
            {
                u32 sy = static_cast<u32>(by) * 4 + y;
                sy = sy < ctx.height ? sy : ctx.height - 1;
                for (u32 x = 0; x < 4; ++x)
                {
                    u32 sx = bx * 4 + x;
                    sx = sx < ctx.width ? sx : ctx.width - 1;
                    MemCopy(texels + (y * 4 + x) * 4, ctx.rgba + (static_cast<usize>(sy) * ctx.width + sx) * 4, 4);
                }
            }
            EncodeBlock(ctx.kind, texels, *ctx.options, ctx.out + (by * ctx.blocks_wide + bx) * block_bytes);
        }
    }
}

}  // namespace

u32 BcBlockBytes(BcKind kind)
{
    return (kind == BcKind::Bc1 || kind == BcKind::Bc4) ? 8 : 16;
}

bool IsBcFormat(rhi::Format format)
{
    return format == rhi::Format::BC1_UNorm || format == rhi::Format::BC1_SRGB || format == rhi::Format::BC4_UNorm ||
           format == rhi::Format::BC5_UNorm || format == rhi::Format::BC7_UNorm || format == rhi::Format::BC7_SRGB;
}

BcKind BcKindOf(rhi::Format format)
{
    ENGINE_ASSERT(IsBcFormat(format), "BcKindOf: the format is not one the minter compresses");
    switch (format)
    {
    case rhi::Format::BC4_UNorm: return BcKind::Bc4;
    case rhi::Format::BC5_UNorm: return BcKind::Bc5;
    case rhi::Format::BC7_UNorm:
    case rhi::Format::BC7_SRGB: return BcKind::Bc7;
    default: return BcKind::Bc1;
    }
}

void DecodeBlock(BcKind kind, const byte* block, byte* rgba)
{
    switch (kind)
    {
    case BcKind::Bc1: Bc1DecodeColor(block, false, rgba); return;
    case BcKind::Bc2:
    case BcKind::Bc3:
    {
        Bc1DecodeColor(block + 8, true, rgba);
        byte alpha[16];
        if (kind == BcKind::Bc2)
        {
            Bc2DecodeAlpha(block, alpha);
        }
        else
        {
            Bc4DecodeChannel(block, alpha);
        }
        for (u32 i = 0; i < 16; ++i)
        {
            rgba[i * 4 + 3] = alpha[i];
        }
        return;
    }
    case BcKind::Bc7: Bc7Decode(block, rgba); return;
    case BcKind::Bc4:
    case BcKind::Bc5:
    {
        byte red[16];
        byte green[16];
        Bc4DecodeChannel(block, red);
        if (kind == BcKind::Bc5)
        {
            Bc4DecodeChannel(block + 8, green);
        }
        for (u32 i = 0; i < 16; ++i)
        {
            rgba[i * 4 + 0] = red[i];
            rgba[i * 4 + 1] = kind == BcKind::Bc5 ? green[i] : byte{0};
            rgba[i * 4 + 2] = 0;
            rgba[i * 4 + 3] = 255;
        }
        return;
    }
    }
}

Status DecodeImage(BcKind kind, u32 width, u32 height, ByteSpan blocks, Vector<byte>* rgba)
{
    if (width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize)
    {
        return Status::Make(ErrorCode::Invalid, "the image size is outside 1 to 16384");
    }
    const u32 bw = (width + 3) / 4;
    const u32 bh = (height + 3) / 4;
    const u32 bytes = BcBlockBytes(kind);
    if (blocks.Size() != static_cast<usize>(bw) * bh * bytes)
    {
        return Status::Make(ErrorCode::Invalid, "the block data does not have the size of the image");
    }
    Vector<byte> out;
    if (!out.TryResizeUninitialized(static_cast<usize>(width) * height * 4))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (u32 by = 0; by < bh; ++by)
    {
        for (u32 bx = 0; bx < bw; ++bx)
        {
            byte texels[64];
            DecodeBlock(kind, blocks.Data() + (static_cast<usize>(by) * bw + bx) * bytes, texels);
            for (u32 y = 0; y < 4 && by * 4 + y < height; ++y)
            {
                for (u32 x = 0; x < 4 && bx * 4 + x < width; ++x)
                {
                    MemCopy(out.Data() + (static_cast<usize>(by * 4 + y) * width + bx * 4 + x) * 4,
                            texels + (y * 4 + x) * 4, 4);
                }
            }
        }
    }
    *rgba = Move(out);
    return Status::Success();
}

void EncodeBlock(BcKind kind, const byte* rgba, const BcEncodeOptions& options, byte* block)
{
    ENGINE_ASSERT(kind == BcKind::Bc1 || kind == BcKind::Bc4 || kind == BcKind::Bc5 || kind == BcKind::Bc7,
                  "EncodeBlock: only BC1, BC4, BC5 and BC7 can be encoded");
    if (kind == BcKind::Bc7)
    {
        Bc7EncodeBlock(rgba, options, block);
        return;
    }
    if (kind == BcKind::Bc1)
    {
        Bc1EncodeBlock(rgba, options, block);
        return;
    }
    const u32 channels = kind == BcKind::Bc5 ? 2 : 1;
    for (u32 ch = 0; ch < channels; ++ch)
    {
        i32 v[16];
        for (u32 i = 0; i < 16; ++i)
        {
            v[i] = rgba[i * 4 + ch];
        }
        Bc4EncodeChannel(v, options.quality, block + ch * 8);
    }
}

Status EncodeImage(BcKind kind, u32 width, u32 height, ByteSpan rgba, const BcEncodeOptions& options,
                   jobs::Scheduler* scheduler, Vector<byte>* blocks)
{
    if (kind != BcKind::Bc1 && kind != BcKind::Bc4 && kind != BcKind::Bc5 && kind != BcKind::Bc7)
    {
        return Status::Make(ErrorCode::Invalid, "only BC1, BC4, BC5 and BC7 can be encoded");
    }
    if (width == 0 || height == 0 || width > kMaxTextureSize || height > kMaxTextureSize ||
        rgba.Size() != static_cast<usize>(width) * height * 4)
    {
        return Status::Make(ErrorCode::Invalid, "the image size is outside 1 to 16384 or the pixels do not match it");
    }
    BcEncodeContext ctx;
    ctx.kind = kind;
    ctx.width = width;
    ctx.height = height;
    ctx.blocks_wide = (width + 3) / 4;
    const u32 rows = (height + 3) / 4;
    ctx.rgba = rgba.Data();
    ctx.options = &options;
    Vector<byte> out;
    if (!out.TryResizeUninitialized(static_cast<usize>(ctx.blocks_wide) * rows * BcBlockBytes(kind)))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    ctx.out = out.Data();
    if (scheduler != nullptr)
    {
        jobs::ParallelForBlocking(*scheduler, rows, 1, &BcEncodeRows, &ctx);
    }
    else
    {
        BcEncodeRows(&ctx, 0, rows);
    }
    *blocks = Move(out);
    return Status::Success();
}

}  // namespace engine_placeholder::asset
