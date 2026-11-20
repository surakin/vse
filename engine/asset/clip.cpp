#include "asset/clip.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::asset
{
namespace
{

constexpr usize kClpHeaderBytes = 96;
constexpr usize kClpTrackBytes = 72;
constexpr usize kClpEventBytes = 16;
constexpr u32 kClpFlagsMask = kClipLooping | kClipAdditive | kClipRootMotion;
constexpr f64 kClpRotRange = 0.70710678118654752440;
constexpr f64 kClpRadToDeg = 57.295779513082320876798;

enum ClpChannel : u32
{
    kClpT = 0,
    kClpR = 1,
    kClpS = 2,
};

void ClpStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u64 ClpLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

void ClpStoreF32(byte* p, f32 v)
{
    MemCopy(p, &v, 4);
}

f32 ClpLoadF32(const byte* p)
{
    f32 v;
    MemCopy(&v, p, 4);
    return v;
}

usize ClpPad4(usize n)
{
    return (n + 3) & ~static_cast<usize>(3);
}

bool ClpFinite(const f32* values, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!IsFinite(values[i]))
        {
            return false;
        }
    }
    return true;
}

Status ClpInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status ClpCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

// The byte size of a keyed channel's block.
usize ClpBlockBytes(u32 channel, usize count)
{
    return (channel == kClpR ? 0 : 24) + ClpPad4(count * 2) + ClpPad4(count * 6);
}

// A keyed channel's block located inside the key section.
struct ClpBlock
{
    const byte* min_extent = nullptr;  // translation and scale only
    const byte* times = nullptr;
    const byte* values = nullptr;
    u32 count = 0;
};

ClpBlock ClpLocate(const byte* keys, u32 channel, u32 count, u32 offset)
{
    ClpBlock b;
    b.count = count;
    const byte* p = keys + offset;
    if (channel != kClpR)
    {
        b.min_extent = p;
        p += 24;
    }
    b.times = p;
    b.values = p + ClpPad4(static_cast<usize>(count) * 2);
    return b;
}

u32 ClpTimeAt(const ClpBlock& b, u32 i)
{
    return static_cast<u32>(ClpLoad(b.times + i * 2, 2));
}

void ClpDecodeVec(const ClpBlock& b, u32 i, f64* out)
{
    for (u32 c = 0; c < 3; ++c)
    {
        const f64 min = ClpLoadF32(b.min_extent + c * 4);
        const f64 extent = ClpLoadF32(b.min_extent + 12 + c * 4);
        const u32 q = static_cast<u32>(ClpLoad(b.values + i * 6 + c * 2, 2));
        out[c] = static_cast<f32>(min + extent * (static_cast<f64>(q) / 65535.0));
    }
}

void ClpDecodeRot(const ClpBlock& b, u32 i, f64* out)
{
    const byte* p = b.values + i * 6;
    const u32 w0 = static_cast<u32>(ClpLoad(p, 2));
    const u32 index = w0 & 3;
    int v0 = static_cast<int>(w0 >> 2);
    if ((v0 & 0x2000) != 0)
    {
        v0 -= 0x4000;
    }
    int v1 = static_cast<int>(ClpLoad(p + 2, 2));
    int v2 = static_cast<int>(ClpLoad(p + 4, 2));
    v1 = v1 >= 32768 ? v1 - 65536 : v1;
    v2 = v2 >= 32768 ? v2 - 65536 : v2;
    const f64 c[3] = {static_cast<f64>(v0) / 8191.0 * kClpRotRange, static_cast<f64>(v1) / 32767.0 * kClpRotRange,
                      static_cast<f64>(v2) / 32767.0 * kClpRotRange};
    const f64 rest = 1.0 - c[0] * c[0] - c[1] * c[1] - c[2] * c[2];
    const f64 dropped = rest > 0.0 ? Sqrt(rest) : 0.0;
    u32 k = 0;
    for (u32 j = 0; j < 4; ++j)
    {
        out[j] = j == index ? dropped : c[k++];
    }
}

// Finds the keys around a quantized time and the interpolation factor.
void ClpSegment(const ClpBlock& b, f64 tq, u32* lo, u32* hi, f64* u, u32* hint = nullptr)
{
    const f64 first = static_cast<f64>(ClpTimeAt(b, 0));
    const f64 last = static_cast<f64>(ClpTimeAt(b, b.count - 1));
    if (tq <= first)
    {
        *lo = *hi = 0;
        *u = 0;
        return;
    }
    if (tq >= last)
    {
        *lo = *hi = b.count - 1;
        *u = 0;
        return;
    }
    u32 a = 0;
    u32 z = b.count - 1;
    if (hint != nullptr)
    {
        // Steady playback stays in the segment of the last call or moves to the next one.
        const u32 h = *hint;
        if (h + 1 < b.count && static_cast<f64>(ClpTimeAt(b, h)) <= tq && tq < static_cast<f64>(ClpTimeAt(b, h + 1)))
        {
            a = h;
            z = h + 1;
        }
        else if (h + 2 < b.count && static_cast<f64>(ClpTimeAt(b, h + 1)) <= tq &&
                 tq < static_cast<f64>(ClpTimeAt(b, h + 2)))
        {
            a = h + 1;
            z = h + 2;
        }
    }
    while (z - a > 1)
    {
        const u32 mid = (a + z) / 2;
        if (static_cast<f64>(ClpTimeAt(b, mid)) <= tq)
        {
            a = mid;
        }
        else
        {
            z = mid;
        }
    }
    *lo = a;
    *hi = z;
    if (hint != nullptr)
    {
        *hint = a;
    }
    const f64 t0 = static_cast<f64>(ClpTimeAt(b, a));
    const f64 t1 = static_cast<f64>(ClpTimeAt(b, z));
    *u = (tq - t0) / (t1 - t0);
}

void ClpNlerp(const f64* a, const f64* b, f64 u, f64* out)
{
    f64 dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const f64 sign = dot < 0.0 ? -1.0 : 1.0;
    f64 q[4];
    f64 length_squared = 0;
    for (u32 c = 0; c < 4; ++c)
    {
        q[c] = a[c] * (1.0 - u) + sign * b[c] * u;
        length_squared += q[c] * q[c];
    }
    const f64 inv = length_squared > 0.0 ? 1.0 / Sqrt(length_squared) : 1.0;
    for (u32 c = 0; c < 4; ++c)
    {
        out[c] = q[c] * inv;
    }
}

// The source side of the reduction and of the measurement: keys in double precision.
struct ClpSrc
{
    Vector<f64> time;
    Vector<Array<f64, 4>> value;  // 3 or 4 components used
    u32 dims = 3;
};

void ClpInterp(const ClpSrc& s, f64 t, f64* out)
{
    const usize n = s.time.Size();
    usize lo_bound = 0;  // the first key with time >= t, by bisection (a channel holds up to 65535 keys)
    usize hi = n;
    while (lo_bound < hi)
    {
        const usize mid = lo_bound + (hi - lo_bound) / 2;
        if (s.time[mid] < t)
        {
            lo_bound = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    hi = lo_bound;
    if (hi == 0 || hi == n)
    {
        const usize k = hi == 0 ? 0 : n - 1;
        for (u32 c = 0; c < s.dims; ++c)
        {
            out[c] = s.value[k][c];
        }
        return;
    }
    const usize lo = hi - 1;
    const f64 u = (t - s.time[lo]) / (s.time[hi] - s.time[lo]);
    if (s.dims == 4)
    {
        f64 a[4], b[4];
        for (u32 c = 0; c < 4; ++c)
        {
            a[c] = s.value[lo][c];
            b[c] = s.value[hi][c];
        }
        ClpNlerp(a, b, u, out);
    }
    else
    {
        for (u32 c = 0; c < 3; ++c)
        {
            out[c] = s.value[lo][c] * (1.0 - u) + s.value[hi][c] * u;
        }
    }
}

f64 ClpAngleDegrees(const f64* a, const f64* b)
{
    f64 dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    dot = Abs(dot);
    dot = dot > 1.0 ? 1.0 : dot;
    return 2.0 * Acos(dot) * kClpRadToDeg;
}

f64 ClpExtent(const ClpSrc& s)
{
    f64 extent = 0;
    for (u32 c = 0; c < s.dims; ++c)
    {
        f64 lo = s.value[0][c];
        f64 hi = lo;
        for (usize i = 1; i < s.value.Size(); ++i)
        {
            lo = s.value[i][c] < lo ? s.value[i][c] : lo;
            hi = s.value[i][c] > hi ? s.value[i][c] : hi;
        }
        extent = hi - lo > extent ? hi - lo : extent;
    }
    return extent;
}

// The error of dropping key `j` between kept keys `a` and `b`: degrees for rotations, absolute for vectors.
f64 ClpDropError(const ClpSrc& s, usize a, usize j, usize b)
{
    const f64 u = (s.time[j] - s.time[a]) / (s.time[b] - s.time[a]);
    f64 got[4];
    if (s.dims == 4)
    {
        f64 x[4], y[4];
        for (u32 c = 0; c < 4; ++c)
        {
            x[c] = s.value[a][c];
            y[c] = s.value[b][c];
        }
        ClpNlerp(x, y, u, got);
        f64 want[4];
        for (u32 c = 0; c < 4; ++c)
        {
            want[c] = s.value[j][c];
        }
        return ClpAngleDegrees(got, want);
    }
    f64 worst = 0;
    for (u32 c = 0; c < 3; ++c)
    {
        got[c] = s.value[a][c] * (1.0 - u) + s.value[b][c] * u;
        const f64 d = Abs(got[c] - s.value[j][c]);
        worst = d > worst ? d : worst;
    }
    return worst;
}

// Greedy split reduction: keep the first and last key, then repeatedly keep the dropped key with the largest error
// (the lowest index on a tie) until every dropped key is within `tolerance`.
void ClpReduce(const ClpSrc& s, f64 tolerance, Vector<u8>* keep)
{
    const usize n = s.time.Size();
    keep->Resize(n);
    for (usize i = 0; i < n; ++i)
    {
        (*keep)[i] = 0;
    }
    (*keep)[0] = 1;
    (*keep)[n - 1] = 1;
    Vector<usize> stack;
    stack.Push(0);
    stack.Push(n - 1);
    while (stack.Size() != 0)
    {
        const usize b = stack[stack.Size() - 1];
        stack.Pop();
        const usize a = stack[stack.Size() - 1];
        stack.Pop();
        if (b - a < 2)
        {
            continue;
        }
        usize worst = a + 1;
        f64 worst_error = -1.0;
        for (usize j = a + 1; j < b; ++j)
        {
            const f64 e = ClpDropError(s, a, j, b);
            if (e > worst_error)
            {
                worst_error = e;
                worst = j;
            }
        }
        if (worst_error > tolerance)
        {
            (*keep)[worst] = 1;
            stack.Push(a);
            stack.Push(worst);
            stack.Push(worst);
            stack.Push(b);
        }
    }
}

bool ClpIsConstant(const ClpSrc& s)
{
    if (s.dims == 4)
    {
        f64 first[4];
        for (u32 c = 0; c < 4; ++c)
        {
            first[c] = s.value[0][c];
        }
        for (usize i = 1; i < s.value.Size(); ++i)
        {
            f64 v[4];
            for (u32 c = 0; c < 4; ++c)
            {
                v[c] = s.value[i][c];
            }
            if (ClpAngleDegrees(first, v) > 1e-4)
            {
                return false;
            }
        }
        return true;
    }
    f64 scale = 1.0;
    for (usize i = 0; i < s.value.Size(); ++i)
    {
        for (u32 c = 0; c < 3; ++c)
        {
            scale = Abs(s.value[i][c]) > scale ? Abs(s.value[i][c]) : scale;
        }
    }
    return ClpExtent(s) <= 1e-7 * scale;
}

// One channel ready to write: constant, or keyed with quantized times and encoded values.
struct ClpPlanChannel
{
    bool present = false;
    bool constant = false;
    f64 constant_value[4] = {0, 0, 0, 1};
    Vector<byte> block;
    u32 count = 0;
};

void ClpPutU16(byte* p, int v)
{
    ClpStore(p, static_cast<u64>(static_cast<u32>(v) & 0xffffu), 2);
}

int ClpRound(f64 v)
{
    return static_cast<int>(Floor(v + 0.5));
}

int ClpClamp(int v, int limit)
{
    return v > limit ? limit : (v < -limit ? -limit : v);
}

ClpPlanChannel ClpPlan(const ClpSrc& src, u32 channel, f64 ticks_per_second, f64 tolerance_rot,
                       f64 tolerance_vec_fraction)
{
    ClpPlanChannel out;
    if (src.time.Size() == 0)
    {
        return out;
    }
    out.present = true;
    if (src.time.Size() == 1 || ClpIsConstant(src))
    {
        out.constant = true;
        for (u32 c = 0; c < src.dims; ++c)
        {
            out.constant_value[c] = src.value[0][c];
        }
        return out;
    }
    const f64 extent = ClpExtent(src);
    Vector<u8> keep;
    ClpReduce(src, channel == kClpR ? tolerance_rot : tolerance_vec_fraction * extent, &keep);
    // Quantize the times; a key that lands on the same step as the one before is dropped.
    Vector<usize> kept;
    Vector<u32> tq;
    for (usize i = 0; i < src.time.Size(); ++i)
    {
        if (keep[i] == 0)
        {
            continue;
        }
        const u32 q = static_cast<u32>(ClpRound(src.time[i] * ticks_per_second));
        if (tq.Size() != 0 && q <= tq[tq.Size() - 1])
        {
            if (i + 1 == src.time.Size())
            {
                // The last key always stays: it replaces the key it collided with.
                kept[kept.Size() - 1] = i;
                tq[tq.Size() - 1] = q > 65535 ? 65535 : q;
            }
            continue;
        }
        kept.Push(i);
        tq.Push(q > 65535 ? 65535 : q);
    }
    if (kept.Size() < 2)
    {
        out.constant = true;
        for (u32 c = 0; c < src.dims; ++c)
        {
            out.constant_value[c] = src.value[kept[0]][c];
        }
        return out;
    }
    const usize n = kept.Size();
    out.count = static_cast<u32>(n);
    out.block.Resize(ClpBlockBytes(channel, n));
    MemSet(out.block.Data(), 0, out.block.Size());
    byte* p = out.block.Data();
    f64 min[3] = {0, 0, 0};
    f64 ext[3] = {0, 0, 0};
    if (channel != kClpR)
    {
        for (u32 c = 0; c < 3; ++c)
        {
            f64 lo = src.value[kept[0]][c];
            f64 hi = lo;
            for (usize i = 1; i < n; ++i)
            {
                const f64 v = src.value[kept[i]][c];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
            min[c] = static_cast<f32>(lo);
            ext[c] = static_cast<f32>(hi) - min[c];
            ClpStoreF32(p + c * 4, static_cast<f32>(min[c]));
            ClpStoreF32(p + 12 + c * 4, static_cast<f32>(ext[c]));
        }
        p += 24;
    }
    for (usize i = 0; i < n; ++i)
    {
        ClpStore(p + i * 2, tq[i], 2);
    }
    byte* values = p + ClpPad4(n * 2);
    for (usize i = 0; i < n; ++i)
    {
        byte* v = values + i * 6;
        if (channel == kClpR)
        {
            f64 q[4];
            for (u32 c = 0; c < 4; ++c)
            {
                q[c] = src.value[kept[i]][c];
            }
            u32 index = 0;
            for (u32 c = 1; c < 4; ++c)
            {
                if (Abs(q[c]) > Abs(q[index]))
                {
                    index = c;
                }
            }
            const f64 sign = q[index] < 0.0 ? -1.0 : 1.0;
            f64 rest[3];
            u32 k = 0;
            for (u32 c = 0; c < 4; ++c)
            {
                if (c != index)
                {
                    rest[k++] = q[c] * sign;
                }
            }
            const int a = ClpClamp(ClpRound(rest[0] / kClpRotRange * 8191.0), 8191);
            const u32 word = ((static_cast<u32>(a) & 0x3fffu) << 2) | index;
            ClpStore(v, word, 2);
            ClpPutU16(v + 2, ClpClamp(ClpRound(rest[1] / kClpRotRange * 32767.0), 32767));
            ClpPutU16(v + 4, ClpClamp(ClpRound(rest[2] / kClpRotRange * 32767.0), 32767));
        }
        else
        {
            for (u32 c = 0; c < 3; ++c)
            {
                f64 q = 0;
                if (ext[c] > 0.0)
                {
                    q = Floor((src.value[kept[i]][c] - min[c]) / ext[c] * 65535.0 + 0.5);
                    q = q < 0.0 ? 0.0 : (q > 65535.0 ? 65535.0 : q);
                }
                ClpStore(v + c * 2, static_cast<u64>(q), 2);
            }
        }
    }
    return out;
}

ClpSrc ClpFromKeys3(const Vector<ClipKey3>& keys)
{
    ClpSrc s;
    s.dims = 3;
    for (usize i = 0; i < keys.Size(); ++i)
    {
        s.time.Push(keys[i].time);
        Array<f64, 4> v = {{keys[i].value[0], keys[i].value[1], keys[i].value[2], 0}};
        s.value.Push(v);
    }
    return s;
}

ClpSrc ClpFromKeys4(const Vector<ClipKey4>& keys)
{
    ClpSrc s;
    s.dims = 4;
    for (usize i = 0; i < keys.Size(); ++i)
    {
        s.time.Push(keys[i].time);
        f64 q[4];
        f64 length_squared = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            q[c] = keys[i].value[c];
            length_squared += q[c] * q[c];
        }
        const f64 inv = 1.0 / Sqrt(length_squared);
        Array<f64, 4> v;
        for (u32 c = 0; c < 4; ++c)
        {
            v[c] = q[c] * inv;
        }
        if (i > 0)
        {
            const Array<f64, 4>& prev = s.value[i - 1];
            if (prev[0] * v[0] + prev[1] * v[1] + prev[2] * v[2] + prev[3] * v[3] < 0.0)
            {
                for (u32 c = 0; c < 4; ++c)
                {
                    v[c] = -v[c];
                }
            }
        }
        s.value.Push(v);
    }
    return s;
}

struct ClpTrackPlan
{
    u32 joint = 0;
    ClpSrc source[3];
    ClpPlanChannel channel[3];
};

Status ClpValidateKeys(const Vector<f64>& times, f64 duration)
{
    for (usize i = 0; i < times.Size(); ++i)
    {
        if (!(times[i] >= 0.0) || times[i] > duration || (i > 0 && times[i] <= times[i - 1]))
        {
            return ClpInvalid("key times must increase strictly and lie within [0, duration]");
        }
    }
    return Status::Success();
}

// The key time unit: the clip's sample rate when every key time lies on that grid (keys sampled at the authoring rate
// then quantize exactly), otherwise the finest unit that fits the duration in 16 bits.
f32 ClpChooseTimeScale(const ClipBuildInput& input)
{
    const f64 rate = input.sample_rate;
    bool grid = static_cast<f64>(input.duration) * rate <= 65535.0;
    for (usize i = 0; grid && i < input.tracks.Size(); ++i)
    {
        const ClipTrackSource& t = input.tracks[i];
        for (usize k = 0; grid && k < t.translation.Size(); ++k)
        {
            const f64 v = static_cast<f64>(t.translation[k].time) * rate;
            grid = Abs(v - Floor(v + 0.5)) <= 1e-3;
        }
        for (usize k = 0; grid && k < t.rotation.Size(); ++k)
        {
            const f64 v = static_cast<f64>(t.rotation[k].time) * rate;
            grid = Abs(v - Floor(v + 0.5)) <= 1e-3;
        }
        for (usize k = 0; grid && k < t.scale.Size(); ++k)
        {
            const f64 v = static_cast<f64>(t.scale[k].time) * rate;
            grid = Abs(v - Floor(v + 0.5)) <= 1e-3;
        }
    }
    return grid ? input.sample_rate : static_cast<f32>(65535.0 / static_cast<f64>(input.duration));
}

Status ClpAssemble(const TypeRegistry& types, ResourceId id, const ClipBuildInput& input,
                   const Vector<ClpTrackPlan>& plans, f32 ticks_per_second, f32 rotation_error, f32 translation_error,
                   Vector<byte>* out, u32 target)
{
    const usize track_count = plans.Size();
    Vector<byte> table;
    table.Resize(track_count * kClpTrackBytes);
    MemSet(table.Data(), 0, table.Size());
    Vector<byte> keys;
    for (usize t = 0; t < track_count; ++t)
    {
        byte* e = table.Data() + t * kClpTrackBytes;
        ClpStore(e, plans[t].joint, 4);
        u32 mask = 0;
        for (u32 c = 0; c < 3; ++c)
        {
            const ClpPlanChannel& ch = plans[t].channel[c];
            if (!ch.present)
            {
                continue;
            }
            if (ch.constant)
            {
                mask |= 1u << (4 + c);
                const u32 dims = c == kClpR ? 4 : 3;
                const u32 at = c == kClpT ? 8 : (c == kClpR ? 20 : 36);
                for (u32 k = 0; k < dims; ++k)
                {
                    ClpStoreF32(e + at + k * 4, static_cast<f32>(ch.constant_value[k]));
                }
            }
            else
            {
                mask |= 1u << c;
                ClpStore(e + 48 + c * 8, ch.count, 4);
                ClpStore(e + 52 + c * 8, keys.Size(), 4);
                const usize at = keys.Size();
                keys.Resize(at + ch.block.Size());
                MemCopy(keys.Data() + at, ch.block.Data(), ch.block.Size());
            }
        }
        ClpStore(e + 4, mask, 4);
    }
    Vector<byte> events;
    events.Resize(input.events.Size() * kClpEventBytes);
    MemSet(events.Data(), 0, events.Size());
    for (usize i = 0; i < input.events.Size(); ++i)
    {
        ClpStoreF32(events.Data() + i * kClpEventBytes, input.events[i].time);
        ClpStore(events.Data() + i * kClpEventBytes + 4, input.events[i].name_hash, 4);
        ClpStore(events.Data() + i * kClpEventBytes + 8, input.events[i].payload, 4);
    }
    Vector<byte> header;
    header.Resize(kClpHeaderBytes);
    MemSet(header.Data(), 0, header.Size());
    byte* h = header.Data();
    ClpStoreF32(h, input.duration);
    ClpStoreF32(h + 4, input.sample_rate);
    ClpStore(h + 8, input.flags, 4);
    ClpStore(h + 12, input.joint_count, 4);
    ClpStore(h + 16, track_count, 4);
    ClpStore(h + 20, input.events.Size(), 4);
    MemCopy(h + 24, input.skeleton_hash.bytes, 16);
    ClpStoreF32(h + 40, rotation_error);
    ClpStoreF32(h + 44, translation_error);
    ClpStoreF32(h + 72, ticks_per_second);
    for (u32 c = 0; c < 3; ++c)
    {
        ClpStoreF32(h + 48 + c * 4, input.bounds_min[c]);
        ClpStoreF32(h + 60 + c * 4, input.bounds_max[c]);
    }

    BlobWriter writer(types, types::kAnimation, id, target);
    if (!input.skeleton.IsNone())
    {
        TypeInfo info;
        if (!types.Find(types::kSkeleton, &info))
        {
            return Status::Make(ErrorCode::TypeMismatch, "the skeleton type is not registered");
        }
        Dependency dependency;
        dependency.id = input.skeleton;
        dependency.type = types::kSkeleton;
        dependency.type_version = info.current_version;
        dependency.role = kClipRoleSkeleton;
        writer.AddDependency(dependency);
    }
    ENGINE_TRY(writer.AddSection(kClipSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kClipSectionTracks, ByteSpan(table.Data(), table.Size())));
    ENGINE_TRY(writer.AddSection(kClipSectionKeys, ByteSpan(keys.Data(), keys.Size())));
    ENGINE_TRY(writer.AddSection(kClipSectionEvents, ByteSpan(events.Data(), events.Size())));
    if (input.weights.target_count != 0)
    {
        // The weight track (glTF coverage Day 9): target count, key count, 8 zero bytes, the times, then the values.
        const ClipWeightTrack& w = input.weights;
        Vector<byte> section;
        section.Resize(16 + (w.times.Size() + w.values.Size()) * 4);
        MemSet(section.Data(), 0, section.Size());
        ClpStore(section.Data(), w.target_count, 4);
        ClpStore(section.Data() + 4, w.times.Size(), 4);
        for (usize k = 0; k < w.times.Size(); ++k)
        {
            ClpStoreF32(section.Data() + 16 + k * 4, w.times[k]);
        }
        for (usize k = 0; k < w.values.Size(); ++k)
        {
            ClpStoreF32(section.Data() + 16 + (w.times.Size() + k) * 4, w.values[k]);
        }
        ENGINE_TRY(writer.AddSection(kClipSectionWeights, ByteSpan(section.Data(), section.Size())));
    }
    return writer.Finish(out);
}

}  // namespace

Status BuildClipBlob(const TypeRegistry& types, ResourceId id, const ClipBuildInput& input, Vector<byte>* out,
                     u32 target, f32* rotation_degrees, f32* translation)
{
    if (!IsFinite(input.duration) || input.duration <= 0.0f || !IsFinite(input.sample_rate) ||
        input.sample_rate <= 0.0f)
    {
        return ClpInvalid("the duration and the sample rate must be finite and above 0");
    }
    if ((input.flags & ~kClpFlagsMask) != 0)
    {
        return ClpInvalid("a clip flag is not defined by this version");
    }
    const ClipWeightTrack& w = input.weights;
    const bool weights_only = input.tracks.Size() == 0 && w.target_count != 0 && input.skeleton.IsNone() &&
                              input.joint_count == 0;
    if (!weights_only && (input.skeleton.IsNone() || input.joint_count == 0 || input.joint_count > 256))
    {
        return ClpInvalid("a clip needs its skeleton id and a joint count of 1 to 256 (or only a weight track)");
    }
    if (w.target_count != 0)
    {
        if (w.target_count > 64 || w.times.Size() == 0 || w.times.Size() > kMaxClipKeys ||
            w.values.Size() != w.times.Size() * w.target_count)
        {
            return ClpInvalid("a weight track needs 1 to 64 weights, keys, and one value per weight per key");
        }
        for (usize k = 0; k < w.times.Size(); ++k)
        {
            if (!IsFinite(w.times[k]) || w.times[k] < 0.0f || w.times[k] > input.duration ||
                (k > 0 && w.times[k] <= w.times[k - 1]))
            {
                return ClpInvalid("the weight track's times must increase within [0, duration]");
            }
        }
        if (!ClpFinite(w.values.Data(), w.values.Size()))
        {
            return ClpInvalid("a weight is not finite");
        }
    }
    if (!ClpFinite(input.bounds_min.Data(), 3) || !ClpFinite(input.bounds_max.Data(), 3))
    {
        return ClpInvalid("the clip bounds are not finite");
    }
    if (input.tracks.Size() == 0 && input.events.Size() == 0 && w.target_count == 0)
    {
        return ClpInvalid("a clip needs at least one track, event or weight track");
    }
    if (input.tracks.Size() > kMaxClipTracks || input.events.Size() > kMaxClipEvents)
    {
        return Status::Make(ErrorCode::LimitExceeded, "a clip count is above the limit of this version");
    }
    if (!(input.limits.rotation_degrees > 0.0f) || !(input.limits.translation > 0.0f))
    {
        return ClpInvalid("the error limits must be above 0");
    }
    const f64 duration = input.duration;
    for (usize i = 0; i < input.events.Size(); ++i)
    {
        const ClipEvent& e = input.events[i];
        if (!IsFinite(e.time) || e.time < 0.0f || e.time > input.duration ||
            (i > 0 && e.time < input.events[i - 1].time))
        {
            return ClpInvalid("events must be in time order within [0, duration]");
        }
    }

    // Order the tracks by joint and plan every channel.
    Vector<usize> order;
    for (usize i = 0; i < input.tracks.Size(); ++i)
    {
        const ClipTrackSource& t = input.tracks[i];
        if (t.joint >= input.joint_count)
        {
            return ClpInvalid("a track's joint is at or past the skeleton's joint count");
        }
        if (t.translation.Size() > kMaxClipKeys || t.rotation.Size() > kMaxClipKeys || t.scale.Size() > kMaxClipKeys)
        {
            return Status::Make(ErrorCode::LimitExceeded, "a channel holds more keys than the format allows");
        }
        usize k = order.Size();
        order.Push(i);
        while (k > 0 && input.tracks[order[k - 1]].joint >= t.joint)
        {
            if (input.tracks[order[k - 1]].joint == t.joint)
            {
                return ClpInvalid("two tracks animate the same joint");
            }
            order[k] = order[k - 1];
            --k;
        }
        order[k] = i;
    }
    const f32 ticks_per_second = ClpChooseTimeScale(input);
    const f64 ticks = ticks_per_second;
    Vector<ClpTrackPlan> plans;
    const f64 tolerance_rot = 0.6 * input.limits.rotation_degrees;
    const f64 tolerance_vec = 0.5 * input.limits.translation;
    for (usize o = 0; o < order.Size(); ++o)
    {
        const ClipTrackSource& t = input.tracks[order[o]];
        plans.Push(ClpTrackPlan());
        ClpTrackPlan& plan = plans[plans.Size() - 1];
        plan.joint = t.joint;
        for (usize i = 0; i < t.translation.Size(); ++i)
        {
            if (!ClpFinite(t.translation[i].value.Data(), 3) || !IsFinite(t.translation[i].time))
            {
                return ClpInvalid("a translation key is not finite");
            }
        }
        for (usize i = 0; i < t.scale.Size(); ++i)
        {
            if (!ClpFinite(t.scale[i].value.Data(), 3) || !IsFinite(t.scale[i].time))
            {
                return ClpInvalid("a scale key is not finite");
            }
        }
        for (usize i = 0; i < t.rotation.Size(); ++i)
        {
            const f32* q = t.rotation[i].value.Data();
            if (!ClpFinite(q, 4) || !IsFinite(t.rotation[i].time))
            {
                return ClpInvalid("a rotation key is not finite");
            }
            const f64 l2 = static_cast<f64>(q[0]) * q[0] + static_cast<f64>(q[1]) * q[1] +
                           static_cast<f64>(q[2]) * q[2] + static_cast<f64>(q[3]) * q[3];
            if (Abs(Sqrt(l2) - 1.0) > 1e-2)
            {
                return ClpInvalid("a rotation key is not a unit quaternion");
            }
        }
        plan.source[kClpT] = ClpFromKeys3(t.translation);
        plan.source[kClpR] = ClpFromKeys4(t.rotation);
        plan.source[kClpS] = ClpFromKeys3(t.scale);
        for (u32 c = 0; c < 3; ++c)
        {
            ENGINE_TRY(ClpValidateKeys(plan.source[c].time, duration));
            plan.channel[c] = ClpPlan(plan.source[c], c, ticks, tolerance_rot, tolerance_vec);
        }
    }

    // First pass with no recorded errors, then measure, then the real blob.
    Vector<byte> first;
    ENGINE_TRY(ClpAssemble(types, id, input, plans, ticks_per_second, 0.0f, 0.0f, &first, target));
    ClipView view;
    ENGINE_TRY(view.Open(ByteSpan(first.Data(), first.Size()), types));
    f64 rot_error = 0;
    f64 vec_error = 0;
    for (u32 t = 0; t < view.TrackCount(); ++t)
    {
        for (u32 c = 0; c < 3; ++c)
        {
            const ClpSrc& src = plans[t].source[c];
            if (src.time.Size() == 0)
            {
                continue;
            }
            const f64 extent = ClpExtent(src);
            const f64 denominator = extent > 1e-3 ? extent : 1e-3;
            Vector<f64> times;
            for (usize i = 0; i < src.time.Size(); ++i)
            {
                times.Push(src.time[i]);
                if (i + 1 < src.time.Size())
                {
                    for (u32 m = 1; m <= 8; ++m)
                    {
                        times.Push(src.time[i] + (src.time[i + 1] - src.time[i]) * (static_cast<f64>(m) / 9.0));
                    }
                }
            }
            for (usize i = 0; i < times.Size(); ++i)
            {
                f64 want[4];
                ClpInterp(src, times[i], want);
                const ClipSample got = view.Sample(t, static_cast<f32>(times[i]));
                if (c == kClpR)
                {
                    const f64 g[4] = {got.rotation[0], got.rotation[1], got.rotation[2], got.rotation[3]};
                    const f64 e = ClpAngleDegrees(g, want);
                    rot_error = e > rot_error ? e : rot_error;
                }
                else
                {
                    const f32* v = c == kClpT ? got.translation.Data() : got.scale.Data();
                    for (u32 k = 0; k < 3; ++k)
                    {
                        const f64 e = Abs(static_cast<f64>(v[k]) - want[k]) / denominator;
                        vec_error = e > vec_error ? e : vec_error;
                    }
                }
            }
        }
    }
    if (rotation_degrees != nullptr)
    {
        *rotation_degrees = static_cast<f32>(rot_error);
    }
    if (translation != nullptr)
    {
        *translation = static_cast<f32>(vec_error);
    }
    if (rot_error > input.limits.rotation_degrees || vec_error > input.limits.translation)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the clip's measured error is above the limit");
    }
    return ClpAssemble(types, id, input, plans, ticks_per_second, static_cast<f32>(rot_error),
                       static_cast<f32>(vec_error), out, target);
}

Status ClipView::Open(ByteSpan data, const TypeRegistry& types)
{
    *this = ClipView();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kAnimation)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not an animation clip");
    }
    ByteSpan header;
    if (!blob.FindSection(kClipSectionHeader, &header) || header.Size() != kClpHeaderBytes)
    {
        return ClpCorrupt("the clip header section is missing or has the wrong size");
    }
    const byte* h = header.Data();
    const f32 duration = ClpLoadF32(h);
    const f32 rate = ClpLoadF32(h + 4);
    const u64 flags = ClpLoad(h + 8, 4);
    const u64 joints = ClpLoad(h + 12, 4);
    const u64 tracks = ClpLoad(h + 16, 4);
    const u64 events = ClpLoad(h + 20, 4);
    const f32 rot_error = ClpLoadF32(h + 40);
    const f32 tr_error = ClpLoadF32(h + 44);
    f32 bounds[6];
    MemCopy(bounds, h + 48, 24);
    if (!IsFinite(duration) || duration <= 0.0f || !IsFinite(rate) || rate <= 0.0f || !IsFinite(rot_error) ||
        !IsFinite(tr_error) || rot_error < 0.0f || tr_error < 0.0f || !ClpFinite(bounds, 6) || bounds[0] > bounds[3] ||
        bounds[1] > bounds[4] || bounds[2] > bounds[5])
    {
        return ClpCorrupt("a clip header value is not finite or is out of range");
    }
    const f32 ticks = ClpLoadF32(h + 72);
    if (!IsFinite(ticks) || ticks <= 0.0f || static_cast<f64>(duration) * ticks > 65535.5)
    {
        return ClpCorrupt("the clip's key time scale is not finite, not above 0 or too fine for 16-bit key times");
    }
    if ((flags & ~static_cast<u64>(kClpFlagsMask)) != 0 || ClpLoad(h + 76, 4) != 0 || ClpLoad(h + 80, 8) != 0 ||
        ClpLoad(h + 88, 8) != 0)
    {
        return ClpCorrupt("a clip header field holds a value this version does not define");
    }
    // The weight track (glTF coverage Day 9): checked whole, since sampling reads it without bounds checks.
    ByteSpan weights;
    u64 weight_targets = 0;
    u64 weight_keys = 0;
    if (blob.FindSection(kClipSectionWeights, &weights))
    {
        if (weights.Size() < 16)
        {
            return ClpCorrupt("the weight section is too short");
        }
        weight_targets = ClpLoad(weights.Data(), 4);
        weight_keys = ClpLoad(weights.Data() + 4, 4);
        if (weight_targets == 0 || weight_targets > 64 || weight_keys == 0 || weight_keys > kMaxClipKeys ||
            ClpLoad(weights.Data() + 8, 8) != 0 ||
            weights.Size() != 16 + (weight_keys + weight_keys * weight_targets) * 4)
        {
            return ClpCorrupt("the weight section's counts are out of range or do not match its size");
        }
        for (u64 k = 0; k < weight_keys; ++k)
        {
            const f32 t = ClpLoadF32(weights.Data() + 16 + k * 4);
            if (!IsFinite(t) || t < 0.0f || t > duration ||
                (k > 0 && !(t > ClpLoadF32(weights.Data() + 16 + (k - 1) * 4))))
            {
                return ClpCorrupt("the weight track's times do not increase within the duration");
            }
        }
        for (u64 k = 0; k < weight_keys * weight_targets; ++k)
        {
            if (!IsFinite(ClpLoadF32(weights.Data() + 16 + (weight_keys + k) * 4)))
            {
                return ClpCorrupt("a weight is not finite");
            }
        }
    }
    const bool weights_only = joints == 0 && tracks == 0 && weight_targets != 0;
    if ((!weights_only && (joints == 0 || joints > 256)) || tracks > kMaxClipTracks || tracks > joints ||
        events > kMaxClipEvents || (tracks == 0 && events == 0 && weight_targets == 0))
    {
        return ClpCorrupt("a clip count is zero, above the limit or inconsistent");
    }
    Dependency dep;
    if (weights_only)
    {
        if (blob.DependencyCount() != 0)
        {
            return ClpCorrupt("a clip of weights only declares no dependency");
        }
    }
    else
    {
        if (blob.DependencyCount() != 1)
        {
            return ClpCorrupt("a clip declares exactly one dependency, its skeleton");
        }
        dep = blob.DependencyAt(0);
        if (dep.type != types::kSkeleton || dep.role != kClipRoleSkeleton)
        {
            return ClpCorrupt("the clip dependency is not a skeleton with the skeleton role");
        }
    }
    ByteSpan table, keys, evs;
    if (!blob.FindSection(kClipSectionTracks, &table) || table.Size() != tracks * kClpTrackBytes ||
        !blob.FindSection(kClipSectionKeys, &keys) || !blob.FindSection(kClipSectionEvents, &evs) ||
        evs.Size() != events * kClpEventBytes)
    {
        return ClpCorrupt("a clip section is missing or has the wrong size");
    }
    u64 previous_joint = 0;
    for (u32 t = 0; t < tracks; ++t)
    {
        const byte* e = table.Data() + t * kClpTrackBytes;
        const u64 joint = ClpLoad(e, 4);
        const u64 mask = ClpLoad(e + 4, 4);
        if (joint >= joints || (t > 0 && joint <= previous_joint) || (mask & ~0x77ull) != 0)
        {
            return ClpCorrupt("a track's joint is out of range or not increasing, or its channel mask is invalid");
        }
        previous_joint = joint;
        for (u32 c = 0; c < 3; ++c)
        {
            const bool keyed = (mask & (1ull << c)) != 0;
            const bool constant = (mask & (1ull << (4 + c))) != 0;
            if (keyed && constant)
            {
                return ClpCorrupt("a channel is both constant and keyed");
            }
            const u64 count = ClpLoad(e + 48 + c * 8, 4);
            const u64 offset = ClpLoad(e + 52 + c * 8, 4);
            if (!keyed)
            {
                if (count != 0 || offset != 0)
                {
                    return ClpCorrupt("a channel without keys has a key count or offset");
                }
                if (constant)
                {
                    const u32 dims = c == kClpR ? 4 : 3;
                    const u32 at = c == kClpT ? 8 : (c == kClpR ? 20 : 36);
                    f32 v[4];
                    MemCopy(v, e + at, dims * 4);
                    if (!ClpFinite(v, dims))
                    {
                        return ClpCorrupt("a constant channel value is not finite");
                    }
                    if (c == kClpR)
                    {
                        const f64 l2 = static_cast<f64>(v[0]) * v[0] + static_cast<f64>(v[1]) * v[1] +
                                       static_cast<f64>(v[2]) * v[2] + static_cast<f64>(v[3]) * v[3];
                        if (Abs(Sqrt(l2) - 1.0) > 1e-3)
                        {
                            return ClpCorrupt("a constant rotation is not a unit quaternion");
                        }
                    }
                }
                continue;
            }
            if (count < 2 || count > kMaxClipKeys || offset % 4 != 0)
            {
                return ClpCorrupt("a keyed channel has fewer than 2 keys, too many keys or a misaligned offset");
            }
            const usize size = ClpBlockBytes(c, static_cast<usize>(count));
            if (offset > keys.Size() || size > keys.Size() - offset)
            {
                return ClpCorrupt("a key block lies outside the key section");
            }
            const ClpBlock b = ClpLocate(keys.Data(), c, static_cast<u32>(count), static_cast<u32>(offset));
            u32 last = 0;
            for (u32 i = 0; i < count; ++i)
            {
                const u32 q = ClpTimeAt(b, i);
                if (i > 0 && q <= last)
                {
                    return ClpCorrupt("key times do not increase");
                }
                last = q;
            }
            if (c != kClpR)
            {
                f32 me[6];
                MemCopy(me, b.min_extent, 24);
                if (!ClpFinite(me, 6) || me[3] < 0.0f || me[4] < 0.0f || me[5] < 0.0f)
                {
                    return ClpCorrupt("a channel's minimum or extent is not finite or is negative");
                }
            }
        }
    }
    f32 previous_time = 0;
    for (u32 i = 0; i < events; ++i)
    {
        const f32 time = ClpLoadF32(evs.Data() + i * kClpEventBytes);
        if (!IsFinite(time) || time < 0.0f || time > duration || (i > 0 && time < previous_time) ||
            ClpLoad(evs.Data() + i * kClpEventBytes + 12, 4) != 0)
        {
            return ClpCorrupt("an event is out of order, outside the duration or has a reserved field set");
        }
        previous_time = time;
    }

    duration_ = duration;
    ticks_per_second_ = ticks;
    sample_rate_ = rate;
    flags_ = static_cast<u32>(flags);
    joint_count_ = static_cast<u32>(joints);
    track_count_ = static_cast<u32>(tracks);
    event_count_ = static_cast<u32>(events);
    rotation_error_ = rot_error;
    translation_error_ = tr_error;
    for (u32 c = 0; c < 3; ++c)
    {
        bounds_min_[c] = bounds[c];
        bounds_max_[c] = bounds[3 + c];
    }
    skeleton_ = dep.id;
    MemCopy(skeleton_hash_.bytes, h + 24, 16);
    tracks_ = table;
    keys_ = keys;
    events_ = evs;
    weights_ = weights;
    weight_targets_ = static_cast<u32>(weight_targets);
    weight_keys_ = static_cast<u32>(weight_keys);
    return Status::Success();
}

ClipEvent ClipView::Event(u32 i) const
{
    ENGINE_ASSERT(i < event_count_, "ClipView::Event: index out of range");
    const byte* p = events_.Data() + i * kClpEventBytes;
    ClipEvent e;
    e.time = ClpLoadF32(p);
    e.name_hash = static_cast<u32>(ClpLoad(p + 4, 4));
    e.payload = static_cast<u32>(ClpLoad(p + 8, 4));
    return e;
}

u32 ClipView::TrackJoint(u32 i) const
{
    ENGINE_ASSERT(i < track_count_, "ClipView::TrackJoint: index out of range");
    return static_cast<u32>(ClpLoad(tracks_.Data() + i * kClpTrackBytes, 4));
}

u32 ClipView::KeyCount(u32 i, u32 channel) const
{
    ENGINE_ASSERT(i < track_count_ && channel < 3, "ClipView::KeyCount: index out of range");
    return static_cast<u32>(ClpLoad(tracks_.Data() + i * kClpTrackBytes + 48 + channel * 8, 4));
}

bool ClipView::IsConstant(u32 i, u32 channel) const
{
    ENGINE_ASSERT(i < track_count_ && channel < 3, "ClipView::IsConstant: index out of range");
    return (ClpLoad(tracks_.Data() + i * kClpTrackBytes + 4, 4) & (1ull << (4 + channel))) != 0;
}

ClipSample ClipView::Sample(u32 i, f32 time, u32* cursors) const
{
    ENGINE_ASSERT(i < track_count_, "ClipView::Sample: index out of range");
    const byte* e = tracks_.Data() + i * kClpTrackBytes;
    const u64 mask = ClpLoad(e + 4, 4);
    f64 t = time;
    t = !(t >= 0.0) ? 0.0 : t;  // a NaN time reads as 0
    t = t < 0.0 ? 0.0 : (t > static_cast<f64>(duration_) ? static_cast<f64>(duration_) : t);
    const f64 tq = t * static_cast<f64>(ticks_per_second_);
    ClipSample out;
    for (u32 c = 0; c < 3; ++c)
    {
        const bool keyed = (mask & (1ull << c)) != 0;
        const bool constant = (mask & (1ull << (4 + c))) != 0;
        if (!keyed && !constant)
        {
            continue;
        }
        const u32 dims = c == kClpR ? 4 : 3;
        f64 value[4] = {0, 0, 0, 0};
        if (constant)
        {
            const u32 at = c == kClpT ? 8 : (c == kClpR ? 20 : 36);
            for (u32 k = 0; k < dims; ++k)
            {
                value[k] = ClpLoadF32(e + at + k * 4);
            }
        }
        else
        {
            const ClpBlock b = ClpLocate(keys_.Data(), c, static_cast<u32>(ClpLoad(e + 48 + c * 8, 4)),
                                         static_cast<u32>(ClpLoad(e + 52 + c * 8, 4)));
            u32 lo, hi;
            f64 u;
            ClpSegment(b, tq, &lo, &hi, &u, cursors != nullptr ? &cursors[c] : nullptr);
            if (c == kClpR)
            {
                f64 a[4], z[4];
                ClpDecodeRot(b, lo, a);
                if (lo == hi)
                {
                    for (u32 k = 0; k < 4; ++k)
                    {
                        value[k] = a[k];
                    }
                }
                else
                {
                    ClpDecodeRot(b, hi, z);
                    ClpNlerp(a, z, u, value);
                }
            }
            else
            {
                f64 a[3], z[3];
                ClpDecodeVec(b, lo, a);
                ClpDecodeVec(b, hi, z);
                for (u32 k = 0; k < 3; ++k)
                {
                    value[k] = lo == hi ? a[k] : a[k] * (1.0 - u) + z[k] * u;
                }
            }
        }
        if (c == kClpT)
        {
            out.has_translation = true;
            for (u32 k = 0; k < 3; ++k)
            {
                out.translation[k] = static_cast<f32>(value[k]);
            }
        }
        else if (c == kClpR)
        {
            out.has_rotation = true;
            for (u32 k = 0; k < 4; ++k)
            {
                out.rotation[k] = static_cast<f32>(value[k]);
            }
        }
        else
        {
            out.has_scale = true;
            for (u32 k = 0; k < 3; ++k)
            {
                out.scale[k] = static_cast<f32>(value[k]);
            }
        }
    }
    return out;
}

void ClipView::SampleWeights(f32 time, f32* out, u32 count) const
{
    const u32 n = count < weight_targets_ ? count : weight_targets_;
    if (n == 0 || weight_keys_ == 0)
    {
        return;
    }
    const byte* times = weights_.Data() + 16;
    const byte* values = times + static_cast<usize>(weight_keys_) * 4;
    u32 k = 0;  // the last key at or before `time`
    if (time <= ClpLoadF32(times))
    {
        k = 0;
    }
    else
    {
        u32 lo = 0;
        u32 hi = weight_keys_;
        while (hi - lo > 1)
        {
            const u32 mid = (lo + hi) / 2;
            if (ClpLoadF32(times + static_cast<usize>(mid) * 4) <= time)
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }
        k = lo;
    }
    const u32 next = k + 1 < weight_keys_ ? k + 1 : k;
    const f32 t0 = ClpLoadF32(times + static_cast<usize>(k) * 4);
    const f32 t1 = ClpLoadF32(times + static_cast<usize>(next) * 4);
    f32 a = next != k && t1 > t0 ? (time - t0) / (t1 - t0) : 0.0f;
    a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    for (u32 i = 0; i < n; ++i)
    {
        const f32 v0 = ClpLoadF32(values + (static_cast<usize>(k) * weight_targets_ + i) * 4);
        const f32 v1 = ClpLoadF32(values + (static_cast<usize>(next) * weight_targets_ + i) * 4);
        out[i] = v0 + (v1 - v0) * a;
    }
}

Status ClipView::Bind(const SkeletonView& skeleton) const
{
    if (skeleton.Id() != skeleton_)
    {
        return ClpInvalid("the clip was made for another skeleton (id differs)");
    }
    if (skeleton.Hash() != skeleton_hash_)
    {
        return ClpInvalid("the clip was made for another skeleton (hash differs)");
    }
    if (skeleton.JointCount() != joint_count_)
    {
        return ClpInvalid("the clip was made for another skeleton (joint count differs)");
    }
    return Status::Success();
}

}  // namespace vse::asset
