#include "asset/cube_map.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "log/assert.h"

namespace vse::asset
{

namespace
{

constexpr f32 kPi = 3.14159265358979f;

f32 AtanUnit(f32 x)  // |x| <= 1
{
    const f32 x2 = x * x;
    return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
}

}  // namespace

bool HalfBitsToFloat(u32 half, f32* out)
{
    const u32 sign = (half >> 15) & 1;
    const u32 exponent = (half >> 10) & 31;
    const u32 mantissa = half & 1023;
    if (exponent == 31)
    {
        return false;
    }
    f32 value;
    if (exponent == 0)
    {
        value = static_cast<f32>(mantissa) * (1.0f / 16777216.0f);  // mantissa * 2^-24
    }
    else
    {
        const u32 bits = ((exponent + 112) << 23) | (mantissa << 13);
        MemCopy(&value, &bits, sizeof(value));
    }
    *out = sign != 0 ? -value : value;
    return true;
}

u32 FloatToHalfBits(f32 value)
{
    u32 bits;
    MemCopy(&bits, &value, sizeof(bits));
    const u32 sign = (bits >> 16) & 0x8000u;
    const u32 magnitude = bits & 0x7fffffffu;
    if (magnitude >= 0x477ff000u)  // 65520 and above rounds to infinity: clamp to 65504
    {
        return sign | 0x7bffu;
    }
    if (magnitude < 0x33000001u)  // below half of the smallest subnormal (2^-25), or exactly that: zero
    {
        return sign;
    }
    const u32 exponent = magnitude >> 23;
    if (exponent < 113)  // a subnormal half
    {
        const u32 mantissa = (magnitude & 0x7fffffu) | 0x800000u;
        const u32 shift = 126 - exponent;  // 14 to 24
        u32 half = mantissa >> shift;
        const u32 rest = mantissa & ((1u << shift) - 1);
        const u32 half_way = 1u << (shift - 1);
        if (rest > half_way || (rest == half_way && (half & 1) != 0))
        {
            ++half;
        }
        return sign | half;
    }
    u32 half = ((exponent - 112) << 10) | ((magnitude >> 13) & 1023);
    const u32 rest = magnitude & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1) != 0))
    {
        ++half;  // may carry into the exponent, which is the right rounding
    }
    return sign | half;
}

Vec3 CubeFaceDirection(u32 face, f32 u, f32 v)
{
    switch (face)
    {
    case 0: return Vec3(1.0f, -v, -u);
    case 1: return Vec3(-1.0f, -v, u);
    case 2: return Vec3(u, 1.0f, v);
    case 3: return Vec3(u, -1.0f, -v);
    case 4: return Vec3(u, -v, 1.0f);
    default: break;
    }
    ENGINE_ASSERT(face == 5, "CubeFaceDirection: face is 6 or more");
    return Vec3(-u, -v, -1.0f);
}

f32 DeterministicAtan2(f32 y, f32 x)
{
    const f32 ax = Abs(x);
    const f32 ay = Abs(y);
    if (ax == 0.0f && ay == 0.0f)
    {
        return 0.0f;
    }
    f32 angle = ay <= ax ? AtanUnit(ay / ax) : kPi * 0.5f - AtanUnit(ax / ay);
    if (x < 0.0f)
    {
        angle = kPi - angle;
    }
    return y < 0.0f ? -angle : angle;
}

void CubeDirectionToFace(const Vec3& d, u32* face, f32* u, f32* v)
{
    const f32 ax = Abs(d.x);
    const f32 ay = Abs(d.y);
    const f32 az = Abs(d.z);
    if (ax >= ay && ax >= az)
    {
        *face = d.x > 0.0f ? 0 : 1;
        *u = (d.x > 0.0f ? -d.z : d.z) / ax;
        *v = -d.y / ax;
    }
    else if (ay >= az)
    {
        *face = d.y > 0.0f ? 2 : 3;
        *u = d.x / ay;
        *v = (d.y > 0.0f ? d.z : -d.z) / ay;
    }
    else
    {
        *face = d.z > 0.0f ? 4 : 5;
        *u = (d.z > 0.0f ? d.x : -d.x) / az;
        *v = -d.y / az;
    }
}

void DeterministicSinCos(f32 radians, f32* sine, f32* cosine)
{
    // Reduce to a quadrant offset k (0..3) and r in [-pi/4, pi/4], then the Taylor polynomials of sin and cos.
    const f32 half_pi = kPi * 0.5f;
    const f32 q = radians / half_pi + 0.5f;
    u32 k = static_cast<u32>(q);
    const f32 r = radians - static_cast<f32>(k) * half_pi;
    const f32 r2 = r * r;
    const f32 sr = r * (1.0f +
                        r2 * (-1.0f / 6.0f + r2 * (1.0f / 120.0f + r2 * (-1.0f / 5040.0f + r2 * (1.0f / 362880.0f)))));
    const f32 cr = 1.0f + r2 * (-0.5f + r2 * (1.0f / 24.0f + r2 * (-1.0f / 720.0f + r2 * (1.0f / 40320.0f))));
    k &= 3u;
    switch (k)
    {
    case 0:
        *sine = sr;
        *cosine = cr;
        break;
    case 1:
        *sine = cr;
        *cosine = -sr;
        break;
    case 2:
        *sine = -sr;
        *cosine = -cr;
        break;
    default:
        *sine = -cr;
        *cosine = sr;
        break;
    }
}

Status PrefilterEnvironmentChains(u32 face_size, u32 mips, Vector<Vector<byte>>* chains)
{
    ENGINE_ASSERT(chains != nullptr, "PrefilterEnvironmentChains: chains is null");
    if (face_size == 0 || mips == 0)
    {
        return Status::Make(ErrorCode::Invalid, "the face size and the mip count must be at least 1");
    }
    u32 full = 1;
    for (u32 s = face_size; s > 1; s >>= 1)
    {
        ++full;
    }
    if (mips > full || chains->Size() != 6)
    {
        return Status::Make(ErrorCode::Invalid, "the mip count is above the full chain, or there are not six chains");
    }
    // Level offsets in a chain (8 bytes a texel).
    Vector<usize> offsets;
    Vector<u32> sizes;
    usize total = 0;
    for (u32 level = 0, s = face_size; level < full; ++level, s = s > 1 ? s >> 1 : 1)
    {
        if (!offsets.TryReserve(full) || !sizes.TryReserve(full))
        {
            return Status::Make(ErrorCode::LimitExceeded, "out of memory");
        }
        offsets.Push(total);
        sizes.Push(s);
        total += static_cast<usize>(s) * s * 8;
    }
    for (usize f = 0; f < 6; ++f)
    {
        if ((*chains)[f].Size() < total)
        {
            return Status::Make(ErrorCode::Invalid, "a face chain is shorter than the full chain");
        }
    }
    constexpr u32 kSamples = 32;
    // The sample set: GGX importance samples around +Z, fixed for every texel (the radical inverse of the index gives
    // phi).
    f32 sample_z[kSamples];  // cos(theta)'s ingredients are per roughness; keep u1 and phi here
    f32 sample_cos_phi[kSamples];
    f32 sample_sin_phi[kSamples];
    for (u32 k = 0; k < kSamples; ++k)
    {
        u32 bits = k;
        bits = (bits << 16) | (bits >> 16);
        bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
        bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
        bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
        bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
        const f32 u2 = static_cast<f32>(bits) * (1.0f / 4294967296.0f);
        sample_z[k] = (static_cast<f32>(k) + 0.5f) / static_cast<f32>(kSamples);
        DeterministicSinCos(2.0f * kPi * u2, &sample_sin_phi[k], &sample_cos_phi[k]);
    }
    for (u32 level = 1; level < mips; ++level)
    {
        const u32 s = sizes[level];
        const f32 roughness = static_cast<f32>(level) / static_cast<f32>(mips - 1);
        const f32 alpha = roughness * roughness;
        const f32 a2m1 = alpha * alpha - 1.0f;
        // The filtered level is built from the same level of the unfiltered chain, so build it all before writing.
        Vector<Vector<byte>> result;
        for (u32 face = 0; face < 6; ++face)
        {
            Vector<byte> out;
            if (!out.TryResize(static_cast<usize>(s) * s * 8))
            {
                return Status::Make(ErrorCode::LimitExceeded, "out of memory");
            }
            for (u32 y = 0; y < s; ++y)
            {
                for (u32 x = 0; x < s; ++x)
                {
                    const f32 u = 2.0f * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(s) - 1.0f;
                    const f32 v = 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(s) - 1.0f;
                    const Vec3 n = CubeFaceDirection(face, u, v).Normalized();
                    // A tangent frame around n.
                    const Vec3 up = Abs(n.y) < 0.999f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                    const Vec3 t = up.Cross(n).Normalized();
                    const Vec3 b = n.Cross(t);
                    f32 sum[3] = {0.0f, 0.0f, 0.0f};
                    f32 weight = 0.0f;
                    for (u32 k = 0; k < kSamples; ++k)
                    {
                        const f32 cos_theta = Sqrt((1.0f - sample_z[k]) / (1.0f + a2m1 * sample_z[k]));
                        const f32 sin_theta = Sqrt(1.0f - cos_theta * cos_theta);
                        const Vec3 h = t * (sin_theta * sample_cos_phi[k]) + b * (sin_theta * sample_sin_phi[k]) +
                                       n * cos_theta;
                        const Vec3 l = h * (2.0f * n.Dot(h)) - n;
                        const f32 nl = n.Dot(l);
                        if (nl <= 0.0f)
                        {
                            continue;
                        }
                        u32 lf = 0;
                        f32 lu = 0.0f;
                        f32 lv = 0.0f;
                        CubeDirectionToFace(l, &lf, &lu, &lv);
                        i32 tx = static_cast<i32>((lu * 0.5f + 0.5f) * static_cast<f32>(s));
                        i32 ty = static_cast<i32>((lv * 0.5f + 0.5f) * static_cast<f32>(s));
                        const i32 last = static_cast<i32>(s) - 1;
                        tx = tx < 0 ? 0 : (tx > last ? last : tx);
                        ty = ty < 0 ? 0 : (ty > last ? last : ty);
                        const byte* texel = (*chains)[lf].Data() + offsets[level] +
                                            (static_cast<usize>(ty) * s + tx) * 8;
                        for (u32 c = 0; c < 3; ++c)
                        {
                            f32 value = 0.0f;
                            HalfBitsToFloat(static_cast<u32>(texel[c * 2]) | (static_cast<u32>(texel[c * 2 + 1]) << 8),
                                            &value);
                            sum[c] += value * nl;
                        }
                        weight += nl;
                    }
                    byte* dst = out.Data() + (static_cast<usize>(y) * s + x) * 8;
                    const byte* own = (*chains)[face].Data() + offsets[level] + (static_cast<usize>(y) * s + x) * 8;
                    for (u32 c = 0; c < 3; ++c)
                    {
                        // No sample above the horizon (cannot happen for a normal, kept for safety): keep the texel.
                        const u32 bits = weight > 0.0f
                                             ? FloatToHalfBits(sum[c] / weight)
                                             : (static_cast<u32>(own[c * 2]) | (static_cast<u32>(own[c * 2 + 1]) << 8));
                        dst[c * 2] = static_cast<byte>(bits & 0xffu);
                        dst[c * 2 + 1] = static_cast<byte>(bits >> 8);
                    }
                    dst[6] = own[6];
                    dst[7] = own[7];
                }
            }
            if (!result.TryReserve(6))
            {
                return Status::Make(ErrorCode::LimitExceeded, "out of memory");
            }
            result.Push(Move(out));
        }
        for (u32 face = 0; face < 6; ++face)
        {
            MemCopy((*chains)[face].Data() + offsets[level], result[face].Data(), result[face].Size());
        }
    }
    return Status::Success();
}

Status EquirectangularToCubeFaces(ByteSpan rgba16f, u32 width, u32 height, u32 face_size, Vector<Vector<byte>>* faces)
{
    if (width < 2 || height < 1 || width > 16384 || height > 16384 || face_size < 1 || face_size > kMaxCubeFaceSize)
    {
        return Status::Make(ErrorCode::Invalid, "the equirectangular size or the face size is out of range");
    }
    if (rgba16f.Size() != static_cast<u64>(width) * height * 8)
    {
        return Status::Make(ErrorCode::Invalid, "the equirectangular image has the wrong number of bytes");
    }
    // The image as floats, once.
    Vector<f32> source;
    if (!source.TryResize(static_cast<usize>(width) * height * 4))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    for (usize i = 0; i < source.Size(); ++i)
    {
        const u32 half = static_cast<u32>(rgba16f[2 * i]) | (static_cast<u32>(rgba16f[2 * i + 1]) << 8);
        if (!HalfBitsToFloat(half, &source[i]))
        {
            return Status::Make(ErrorCode::Corrupt, "the equirectangular image has an infinite or NaN texel");
        }
    }
    Vector<Vector<byte>> result;
    if (!result.TryResize(6))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    const f32 n = static_cast<f32>(face_size);
    for (u32 face = 0; face < 6; ++face)
    {
        if (!result[face].TryResize(static_cast<usize>(face_size) * face_size * 8))
        {
            return Status::Make(ErrorCode::LimitExceeded, "out of memory");
        }
        byte* out = result[face].Data();
        for (u32 y = 0; y < face_size; ++y)
        {
            for (u32 x = 0; x < face_size; ++x)
            {
                const f32 u = 2.0f * (static_cast<f32>(x) + 0.5f) / n - 1.0f;
                const f32 v = 2.0f * (static_cast<f32>(y) + 0.5f) / n - 1.0f;
                const Vec3 d = CubeFaceDirection(face, u, v);
                const f32 length = Sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                const f32 dx = d.x / length;
                const f32 dy = d.y / length;
                const f32 dz = d.z / length;
                // Longitude from -Z (east is +X), latitude from straight up.
                const f32 lon = DeterministicAtan2(dx, -dz);
                const f32 horizontal = Sqrt(dx * dx + dz * dz);
                const f32 colat = DeterministicAtan2(horizontal, dy);
                const f32 fx = (0.5f + lon / (2.0f * kPi)) * static_cast<f32>(width) - 0.5f;
                const f32 fy = colat / kPi * static_cast<f32>(height) - 0.5f;
                const f32 y0f = Floor(fy);
                const f32 ty = fy - y0f;
                const f32 x0f = Floor(fx);
                const f32 tx = fx - x0f;
                i64 x0 = static_cast<i64>(x0f);
                i64 y0 = static_cast<i64>(y0f);
                i64 x1 = x0 + 1;
                i64 y1 = y0 + 1;
                const i64 w = static_cast<i64>(width);
                const i64 h = static_cast<i64>(height);
                x0 = ((x0 % w) + w) % w;  // longitude wraps
                x1 = ((x1 % w) + w) % w;
                y0 = y0 < 0 ? 0 : (y0 >= h ? h - 1 : y0);  // latitude clamps at the poles
                y1 = y1 < 0 ? 0 : (y1 >= h ? h - 1 : y1);
                for (u32 c = 0; c < 4; ++c)
                {
                    const f32 a = source[(static_cast<usize>(y0) * width + static_cast<usize>(x0)) * 4 + c];
                    const f32 b = source[(static_cast<usize>(y0) * width + static_cast<usize>(x1)) * 4 + c];
                    const f32 e = source[(static_cast<usize>(y1) * width + static_cast<usize>(x0)) * 4 + c];
                    const f32 f = source[(static_cast<usize>(y1) * width + static_cast<usize>(x1)) * 4 + c];
                    const f32 top = a + (b - a) * tx;
                    const f32 bottom = e + (f - e) * tx;
                    const u32 half = FloatToHalfBits(top + (bottom - top) * ty);
                    const usize at = (static_cast<usize>(y) * face_size + x) * 8 + c * 2;
                    out[at] = static_cast<byte>(half & 255);
                    out[at + 1] = static_cast<byte>(half >> 8);
                }
            }
        }
    }
    *faces = Move(result);
    return Status::Success();
}

}  // namespace vse::asset
