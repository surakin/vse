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
