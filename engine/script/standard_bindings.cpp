#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "string/string.h"
#include "script/bindings.h"
#include "script/component_bindings.h"
#include "script/entity_bindings.h"
#include "script/event_bindings.h"
#include "script/settings_bindings.h"
#include "script/task_bindings.h"

namespace vse::script
{

namespace
{

f32 NumClamp(f32 x, f32 lo, f32 hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

f32 NumLerp(f32 a, f32 b, f32 t)
{
    return a + (b - a) * t;
}

f32 NumSqrt(f32 x)
{
    return x < 0.0f ? 0.0f : Sqrt(x);
}

// The vector library works in double precision, like Lua's numbers, so positions far from the origin keep their digits.
Vec3d VecNew(f64 x, f64 y, f64 z)
{
    return Vec3d(x, y, z);
}

f64 VecDot(Vec3d a, Vec3d b)
{
    return a.Dot(b);
}

Vec3d VecCross(Vec3d a, Vec3d b)
{
    return a.Cross(b);
}

f64 VecLength(Vec3d a)
{
    return a.Length();
}

f64 VecDistance(Vec3d a, Vec3d b)
{
    return (a - b).Length();
}

Vec3d VecNormalize(Vec3d a)
{
    const f64 length = a.Length();
    return length > 0.0 ? Vec3d(a.x / length, a.y / length, a.z / length) : Vec3d(0.0, 0.0, 0.0);
}

Vec3d VecLerp(Vec3d a, Vec3d b, f64 t)
{
    return a + (b - a) * t;
}

Quaternion QuatIdentity()
{
    return Quaternion::Identity();
}

Quaternion QuatFromAxisAngle(Vec3d axis, f32 radians)
{
    return axis.Length() > 0.0 ? Quaternion::FromAxisAngle(axis.Narrow(), radians) : Quaternion::Identity();
}

Quaternion QuatMul(Quaternion a, Quaternion b)
{
    return a * b;
}

Vec3d QuatRotate(Quaternion q, Vec3d v)
{
    return Vec3d(q.RotateVector(v.Narrow()));  // a rotation is single precision: rotate offsets, not far positions
}

f32 NumSign(f32 x)
{
    return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
}

f32 NumSmoothstep(f32 edge0, f32 edge1, f32 x)
{
    if (!(edge1 > edge0))
    {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const f32 t = NumClamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 NumApproach(f32 current, f32 target, f32 step)
{
    const f32 amount = step < 0.0f ? -step : step;
    if (current < target)
    {
        return current + amount < target ? current + amount : target;
    }
    return current - amount > target ? current - amount : target;
}

Vec2 Vec2New(f32 x, f32 y)
{
    return Vec2(x, y);
}

f32 Vec2Dot(Vec2 a, Vec2 b)
{
    return a.Dot(b);
}

f32 Vec2Length(Vec2 a)
{
    return a.Length();
}

Vec2 Vec2Normalize(Vec2 a)
{
    const f32 length = a.Length();
    return length > 0.0f ? Vec2(a.x / length, a.y / length) : Vec2(0.0f, 0.0f);
}

Vec2 Vec2Lerp(Vec2 a, Vec2 b, f32 t)
{
    return a + (b - a) * t;
}

Vec4 Vec4New(f32 x, f32 y, f32 z, f32 w)
{
    return Vec4(x, y, z, w);
}

f32 Vec4Dot(Vec4 a, Vec4 b)
{
    return a.Dot(b);
}

f32 Vec4Length(Vec4 a)
{
    return a.Length();
}

Vec4 Vec4Normalize(Vec4 a)
{
    const f32 length = a.Length();
    return length > 0.0f ? Vec4(a.x / length, a.y / length, a.z / length, a.w / length) : Vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

Vec4 Vec4Lerp(Vec4 a, Vec4 b, f32 t)
{
    return a + (b - a) * t;
}

Quaternion QuatSlerp(Quaternion a, Quaternion b, f32 t)
{
    return Slerp(a, b, t);
}

Quaternion QuatFromEuler(f32 x, f32 y, f32 z)
{
    const Quaternion qx = Quaternion::FromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), x);
    const Quaternion qy = Quaternion::FromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), y);
    const Quaternion qz = Quaternion::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), z);
    return qz * qy * qx;
}

Quaternion QuatConjugate(Quaternion q)
{
    return q.Conjugate();
}

Quaternion QuatNormalize(Quaternion q)
{
    const f32 length = Sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return length > 0.0f ? Quaternion(q.x / length, q.y / length, q.z / length, q.w / length) : Quaternion::Identity();
}

Mat4 MatIdentity()
{
    return Mat4::Identity();
}

Mat4 MatFromTrs(Vec3 translation, Quaternion rotation, Vec3 scale)
{
    const Quaternion q = QuatNormalize(rotation);
    return Mat4::FromBasis(q.RotateVector(Vec3(scale.x, 0.0f, 0.0f)), q.RotateVector(Vec3(0.0f, scale.y, 0.0f)),
                           q.RotateVector(Vec3(0.0f, 0.0f, scale.z)), translation);
}

Mat4 MatMul(Mat4 a, Mat4 b)
{
    return a * b;
}

Vec3 MatTransformPoint(Mat4 m, Vec3 p)
{
    const Vec4 r = m * Vec4(p, 1.0f);
    return Vec3(r.x, r.y, r.z);
}

Vec3 MatTransformDirection(Mat4 m, Vec3 d)
{
    const Vec4 r = m * Vec4(d, 0.0f);
    return Vec3(r.x, r.y, r.z);
}

Mat4 MatTranspose(Mat4 m)
{
    return m.Transpose();
}

// The view matrix of a camera at `eye` looking at `target` (right-handed, the camera looks down -Z). A zero
// distance, or `up` parallel to the view direction, gives the identity.
Mat4 MatLookAt(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 to = target - eye;
    const f32 to_length = to.Length();
    if (!(to_length > 0.0f))
    {
        return Mat4::Identity();
    }
    const Vec3 f(to.x / to_length, to.y / to_length, to.z / to_length);
    const Vec3 side = f.Cross(up);
    const f32 side_length = side.Length();
    if (!(side_length > 0.0f))
    {
        return Mat4::Identity();
    }
    const Vec3 s(side.x / side_length, side.y / side_length, side.z / side_length);
    const Vec3 u = s.Cross(f);
    Mat4 m = Mat4::Identity();
    m.col[0] = Vec4(s.x, u.x, -f.x, 0.0f);
    m.col[1] = Vec4(s.y, u.y, -f.y, 0.0f);
    m.col[2] = Vec4(s.z, u.z, -f.z, 0.0f);
    m.col[3] = Vec4(-s.Dot(eye), -u.Dot(eye), f.Dot(eye), 1.0f);
    return m;
}

// ---- bytes: packing and unpacking binary data (little-endian)

struct PackItem
{
    char letter;  // B b H h I i f d c (fixed bytes) s (one length byte, then that many)
    u32 count;    // `c`: the number of bytes
};

constexpr usize kMaxPackItems = 4096;
constexpr u32 kMaxFixedBytes = 1u << 20;

// Reads the format into `items`; returns false with `*error` set (naming the letter and its position).
bool ParsePackFormat(StringView format, Vector<PackItem>* items, StringBuilder<HeapAllocator>* error)
{
    usize at = 0;
    while (at < format.Length())
    {
        const char c = format[at];
        if (c == ' ')
        {
            ++at;
            continue;
        }
        PackItem item{c, 0};
        const usize letter_at = at++;
        if (c == 'c')
        {
            u64 n = 0;
            bool digits = false;
            while (at < format.Length() && format[at] >= '0' && format[at] <= '9')
            {
                n = n * 10 + static_cast<u64>(format[at] - '0');
                digits = true;
                if (n > kMaxFixedBytes)
                {
                    break;
                }
                ++at;
            }
            if (!digits || n == 0 || n > kMaxFixedBytes)
            {
                error->Append(StringView("format letter `c` at position "));
                error->AppendInt(static_cast<i64>(letter_at));
                error->Append(StringView(" needs a size from 1 to 1048576 (`c16`)"));
                return false;
            }
            item.count = static_cast<u32>(n);
        }
        else if (c == 's')
        {
            if (at >= format.Length() || format[at] != '1')
            {
                error->Append(StringView("format letter `s` at position "));
                error->AppendInt(static_cast<i64>(letter_at));
                error->Append(StringView(" is `s1` (a length byte, then the text)"));
                return false;
            }
            ++at;
        }
        else if (!(c == 'B' || c == 'b' || c == 'H' || c == 'h' || c == 'I' || c == 'i' || c == 'f' || c == 'd'))
        {
            error->Append(StringView("format letter `"));
            error->Append(StringView(&c, 1));
            error->Append(StringView("` at position "));
            error->AppendInt(static_cast<i64>(letter_at));
            error->Append(StringView(" is not one of B b H h I i f d c<n> s1"));
            return false;
        }
        if (items->Size() >= kMaxPackItems)
        {
            error->Append(StringView("the format has more than 4096 items"));
            return false;
        }
        items->Push(item);
    }
    return true;
}

u32 PackItemSize(const PackItem& item)
{
    switch (item.letter)
    {
    case 'B':
    case 'b': return 1;
    case 'H':
    case 'h': return 2;
    case 'I':
    case 'i':
    case 'f': return 4;
    case 'd': return 8;
    case 'c': return item.count;
    default: return 0;  // s1: its size is in the data
    }
}

Outcome BytesError(StringView name, StringView text)
{
    StringBuilder<HeapAllocator> message(HeapAllocator::Default());
    message.Append(StringView("bytes."));
    message.Append(name);
    message.Append(StringView(": "));
    message.Append(text);
    return Outcome(Status::RuntimeError, message.View());
}

// The bytes of a byte blob or a string (a Lua string is binary-safe).
bool PackSource(const Value& v, Span<const u8>* out)
{
    if (v.Kind() == ValueKind::Bytes)
    {
        *out = v.Bytes();
        return true;
    }
    if (v.Kind() == ValueKind::Text)
    {
        const StringView text = v.Text();
        *out = Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Length());
        return true;
    }
    return false;
}

u64 ReadLittle(const u8* p, u32 n)
{
    u64 v = 0;
    for (u32 i = 0; i < n; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

Outcome BytesUnpack(void*, Span<const Value> args, Vector<Value>* results)
{
    Span<const u8> data;
    if (!PackSource(args[0], &data))
    {
        return BytesError(StringView("unpack"), StringView("`data` is a byte blob or a string"));
    }
    if (args[1].Kind() != ValueKind::Text)
    {
        return BytesError(StringView("unpack"), StringView("`format` is a string"));
    }
    u64 offset = 0;
    if (args.Size() > 3)
    {
        return BytesError(StringView("unpack"), StringView("takes `data`, `format` and an optional `offset`"));
    }
    if (args.Size() == 3 && !args[2].IsNil())
    {
        if (!args[2].IsNumber() || !(args[2].ToDouble() >= 0.0) || args[2].ToDouble() > 4294967295.0 ||
            args[2].ToDouble() != Floor(args[2].ToDouble()))
        {
            return BytesError(StringView("unpack"), StringView("`offset` is a whole number of bytes, 0 or more"));
        }
        offset = static_cast<u64>(args[2].ToDouble());
    }
    Vector<PackItem> items;
    StringBuilder<HeapAllocator> why(HeapAllocator::Default());
    if (!ParsePackFormat(args[1].Text(), &items, &why))
    {
        return BytesError(StringView("unpack"), why.View());
    }
    for (usize i = 0; i < items.Size(); ++i)
    {
        const PackItem& item = items[i];
        u64 need = PackItemSize(item);
        if (item.letter == 's')
        {
            if (offset >= data.Size())
            {
                need = 1;
            }
            else
            {
                need = 1u + data.Data()[offset];
            }
        }
        if (offset > data.Size() || need > data.Size() - offset)
        {
            StringBuilder<HeapAllocator> text(HeapAllocator::Default());
            text.Append(StringView("format letter `"));
            text.Append(StringView(&item.letter, 1));
            text.Append(StringView("` at offset "));
            text.AppendInt(static_cast<i64>(offset));
            text.Append(StringView(" needs "));
            text.AppendInt(static_cast<i64>(need));
            text.Append(StringView(" bytes but the data has "));
            text.AppendInt(static_cast<i64>(offset > data.Size() ? 0 : data.Size() - offset));
            text.Append(StringView(" left"));
            return BytesError(StringView("unpack"), text.View());
        }
        const u8* p = data.Data() + offset;
        switch (item.letter)
        {
        case 'B': results->Push(Value::FromInt(static_cast<i64>(p[0]))); break;
        case 'b': results->Push(Value::FromInt(static_cast<i64>(static_cast<i8>(p[0])))); break;
        case 'H': results->Push(Value::FromInt(static_cast<i64>(ReadLittle(p, 2)))); break;
        case 'h': results->Push(Value::FromInt(static_cast<i64>(static_cast<i16>(ReadLittle(p, 2))))); break;
        case 'I': results->Push(Value::FromInt(static_cast<i64>(ReadLittle(p, 4)))); break;
        case 'i': results->Push(Value::FromInt(static_cast<i64>(static_cast<i32>(ReadLittle(p, 4))))); break;
        case 'f':
        {
            const u32 bits = static_cast<u32>(ReadLittle(p, 4));
            f32 f = 0.0f;
            MemCopy(&f, &bits, 4);
            results->Push(Value::FromFloat(static_cast<f64>(f)));
            break;
        }
        case 'd':
        {
            const u64 bits = ReadLittle(p, 8);
            f64 d = 0.0;
            MemCopy(&d, &bits, 8);
            results->Push(Value::FromFloat(d));
            break;
        }
        case 'c': results->Push(Value::FromText(StringView(reinterpret_cast<const char*>(p), item.count))); break;
        default:  // s1
            results->Push(Value::FromText(StringView(reinterpret_cast<const char*>(p + 1), need - 1)));
            break;
        }
        offset += need;
    }
    results->Push(Value::FromInt(static_cast<i64>(offset)));
    return Outcome();
}

void PutLittle(Vector<u8>* out, u64 v, u32 n)
{
    for (u32 i = 0; i < n; ++i)
    {
        out->Push(static_cast<u8>(v >> (8 * i)));
    }
}

Outcome BytesPack(void*, Span<const Value> args, Vector<Value>* results)
{
    if (args[0].Kind() != ValueKind::Text)
    {
        return BytesError(StringView("pack"), StringView("`format` is a string"));
    }
    Vector<PackItem> items;
    StringBuilder<HeapAllocator> why(HeapAllocator::Default());
    if (!ParsePackFormat(args[0].Text(), &items, &why))
    {
        return BytesError(StringView("pack"), why.View());
    }
    if (args.Size() - 1 != items.Size())
    {
        StringBuilder<HeapAllocator> text(HeapAllocator::Default());
        text.Append(StringView("the format has "));
        text.AppendInt(static_cast<i64>(items.Size()));
        text.Append(StringView(" items but "));
        text.AppendInt(static_cast<i64>(args.Size() - 1));
        text.Append(StringView(" values were given"));
        return BytesError(StringView("pack"), text.View());
    }
    Vector<u8> out;
    for (usize i = 0; i < items.Size(); ++i)
    {
        const PackItem& item = items[i];
        const Value& v = args[i + 1];
        StringBuilder<HeapAllocator> where(HeapAllocator::Default());
        where.Append(StringView("value "));
        where.AppendInt(static_cast<i64>(i + 1));
        where.Append(StringView(" (format letter `"));
        where.Append(StringView(&item.letter, 1));
        where.Append(StringView("`) "));
        if (item.letter == 'c' || item.letter == 's')
        {
            Span<const u8> source;
            if (!PackSource(v, &source))
            {
                where.Append(StringView("is a string or a byte blob"));
                return BytesError(StringView("pack"), where.View());
            }
            if (item.letter == 'c' ? source.Size() != item.count : source.Size() > 255)
            {
                where.Append(StringView(item.letter == 'c' ? "must be exactly the size of its `c` item"
                                                           : "is longer than 255 bytes"));
                return BytesError(StringView("pack"), where.View());
            }
            if (item.letter == 's')
            {
                out.Push(static_cast<u8>(source.Size()));
            }
            for (usize b = 0; b < source.Size(); ++b)
            {
                out.Push(source.Data()[b]);
            }
            continue;
        }
        if (!v.IsNumber())
        {
            where.Append(StringView("is a number"));
            return BytesError(StringView("pack"), where.View());
        }
        const f64 number = v.ToDouble();
        if (item.letter == 'f')
        {
            const f32 f = static_cast<f32>(number);
            u32 bits = 0;
            MemCopy(&bits, &f, 4);
            PutLittle(&out, bits, 4);
        }
        else if (item.letter == 'd')
        {
            u64 bits = 0;
            MemCopy(&bits, &number, 8);
            PutLittle(&out, bits, 8);
        }
        else
        {
            const bool is_signed = item.letter == 'b' || item.letter == 'h' || item.letter == 'i';
            const u32 size = PackItemSize(item);
            const f64 low = is_signed ? -static_cast<f64>(u64{1} << (8 * size - 1)) : 0.0;
            const f64 high = is_signed ? static_cast<f64>(u64{1} << (8 * size - 1)) - 1.0
                                       : static_cast<f64>((u64{1} << (8 * size)) - 1);
            if (!(number >= low && number <= high) || number != Floor(number))
            {
                where.Append(StringView("is not a whole number that fits the letter"));
                return BytesError(StringView("pack"), where.View());
            }
            PutLittle(&out, static_cast<u64>(static_cast<i64>(number)), size);
        }
    }
    results->Push(Value::FromBytes(Span<const u8>(out.Data(), out.Size())));
    return Outcome();
}

Value BytesSize(StringView format)
{
    Vector<PackItem> items;
    StringBuilder<HeapAllocator> why(HeapAllocator::Default());
    if (!ParsePackFormat(format, &items, &why))
    {
        StringBuilder<HeapAllocator> text(HeapAllocator::Default());
        text.Append(StringView("bytes.size: "));
        text.Append(why.View());
        RaiseScriptError(text.View());
        return Value();
    }
    u64 total = 0;
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (items[i].letter == 's')
        {
            return Value();  // the size is in the data
        }
        total += PackItemSize(items[i]);
    }
    return Value::FromInt(static_cast<i64>(total));
}

}  // namespace

bool RegisterStandardBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&NumClamp>("num", "clamp", ThreadRule::Pure, "x, lo, hi",
                                   "Limits `x` to the range `lo` to `hi`.");
    ok &= registry->Add<&NumLerp>("num", "lerp", ThreadRule::Pure, "a, b, t", "Returns `a + (b - a) * t`.");
    ok &= registry->Add<&NumSqrt>("num", "sqrt", ThreadRule::Pure, "x",
                                  "Returns the square root of `x`, or 0 for a negative `x`.");
    ok &= registry->Add<&VecNew>("vec3", "new", ThreadRule::Pure, "x, y, z", "Makes a vector.");
    ok &= registry->Add<&VecDot>("vec3", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&VecCross>("vec3", "cross", ThreadRule::Pure, "a, b",
                                   "Returns the cross product `a x b` (right-handed).");
    ok &= registry->Add<&VecLength>("vec3", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&VecDistance>("vec3", "distance", ThreadRule::Pure, "a, b",
                                      "Returns the distance between two points.");
    ok &= registry->Add<&VecNormalize>("vec3", "normalize", ThreadRule::Pure, "a",
                                       "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&VecLerp>("vec3", "lerp", ThreadRule::Pure, "a, b, t",
                                  "Interpolates linearly between two vectors.");
    ok &= registry->Add<&QuatIdentity>("quat", "identity", ThreadRule::Pure, "",
                                       "Returns the rotation that does nothing.");
    ok &= registry->Add<&QuatFromAxisAngle>(
        "quat", "from_axis_angle", ThreadRule::Pure, "axis, radians",
        "Returns the rotation of `radians` about `axis` (identity for a zero axis).");
    ok &= registry->Add<&QuatMul>("quat", "mul", ThreadRule::Pure, "a, b",
                                  "Returns the rotation that applies `b` first and then `a`.");
    ok &= registry->Add<&QuatRotate>("quat", "rotate", ThreadRule::Pure, "q, v", "Rotates the vector `v` by `q`.");
    ok &= registry->Add<&NumSign>("num", "sign", ThreadRule::Pure, "x", "Returns -1, 0 or 1 by the sign of `x`.");
    ok &= registry->Add<&NumSmoothstep>("num", "smoothstep", ThreadRule::Pure, "edge0, edge1, x",
                                        "Returns 0 below `edge0`, 1 above `edge1` and a smooth curve between.");
    ok &= registry->Add<&NumApproach>("num", "approach", ThreadRule::Pure, "current, target, step",
                                      "Moves `current` toward `target` by at most `step` and never past it.");
    ok &= registry->Add<&Vec2New>("vec2", "new", ThreadRule::Pure, "x, y", "Makes a 2D vector.");
    ok &= registry->Add<&Vec2Dot>("vec2", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&Vec2Length>("vec2", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&Vec2Normalize>("vec2", "normalize", ThreadRule::Pure, "a",
                                        "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&Vec2Lerp>("vec2", "lerp", ThreadRule::Pure, "a, b, t",
                                   "Interpolates linearly between two vectors.");
    ok &= registry->Add<&Vec4New>("vec4", "new", ThreadRule::Pure, "x, y, z, w", "Makes a 4D vector.");
    ok &= registry->Add<&Vec4Dot>("vec4", "dot", ThreadRule::Pure, "a, b", "Returns the dot product.");
    ok &= registry->Add<&Vec4Length>("vec4", "length", ThreadRule::Pure, "a", "Returns the Euclidean length.");
    ok &= registry->Add<&Vec4Normalize>("vec4", "normalize", ThreadRule::Pure, "a",
                                        "Returns the vector scaled to unit length; the zero vector stays zero.");
    ok &= registry->Add<&Vec4Lerp>("vec4", "lerp", ThreadRule::Pure, "a, b, t",
                                   "Interpolates linearly between two vectors.");
    ok &= registry->Add<&QuatSlerp>("quat", "slerp", ThreadRule::Pure, "a, b, t",
                                    "Interpolates between two rotations along the shorter arc (uses `acos`, `sin`: not "
                                    "bit-reproducible across machines).");
    ok &= registry->Add<&QuatFromEuler>(
        "quat", "from_euler", ThreadRule::Pure, "x, y, z",
        "Returns the rotation that turns `x` radians about X, then `y` about Y, then `z` "
        "about Z (uses `sin`, `cos`: not bit-reproducible across machines).");
    ok &= registry->Add<&QuatConjugate>("quat", "conjugate", ThreadRule::Pure, "q",
                                        "Returns the opposite rotation of a unit rotation.");
    ok &= registry->Add<&QuatNormalize>("quat", "normalize", ThreadRule::Pure, "q",
                                        "Scales the rotation to unit length (identity for a zero rotation).");
    ok &= registry->Add<&MatIdentity>("mat4", "identity", ThreadRule::Pure, "",
                                      "Returns the identity matrix (16 numbers, column-major).");
    ok &= registry->Add<&MatFromTrs>("mat4", "from_trs", ThreadRule::Pure, "translation, rotation, scale",
                                     "Returns the matrix that scales, then rotates, then translates.");
    ok &= registry->Add<&MatMul>("mat4", "mul", ThreadRule::Pure, "a, b", "Returns `a * b`: applies `b` first.");
    ok &= registry->Add<&MatTransformPoint>("mat4", "transform_point", ThreadRule::Pure, "m, p",
                                            "Applies the matrix to a point (w = 1).");
    ok &= registry->Add<&MatTransformDirection>("mat4", "transform_direction", ThreadRule::Pure, "m, d",
                                                "Applies the matrix to a direction (w = 0: no translation).");
    ok &= registry->Add<&MatTranspose>("mat4", "transpose", ThreadRule::Pure, "m", "Returns the transposed matrix.");
    ok &= registry->Add<&MatLookAt>(
        "mat4", "look_at", ThreadRule::Pure, "eye, target, up",
        "Returns the view matrix of a camera at `eye` looking at `target` (right-handed, "
        "looking down -Z); the identity when the view direction is zero or parallel to `up`.");
    ok &= registry->AddDynamic(
        "bytes", "unpack", ThreadRule::Pure, "data, format, ...",
        "Reads values from `data` (a byte blob or a string) as `format` says, little-endian, from `offset` (the optional "
        "third argument, 0-based; 0 by default), and returns them followed by the offset after the last one. Format "
        "letters: `B` `H` `I` unsigned 8, 16, 32 bits; `b` `h` `i` signed; `f` and `d` 32 and 64-bit floats (a NaN, "
        "an infinity and -0 come back as they were); `c<n>` n bytes as a string; `s1` a length byte and that many bytes "
        "as a string; spaces are ignored. Reading up to the exact end is fine; one byte past it is an error that names "
        "the offset and the letter.",
        &BytesUnpack, nullptr);
    ok &= registry->AddDynamic(
        "bytes", "pack", ThreadRule::Pure, "format, ...",
        "Returns a byte blob of the values after `format` (the letters of `bytes.unpack`), little-endian; the number "
        "of values must be the number of items, an integer must fit its letter, a `c<n>` string must be exactly n "
        "bytes and an `s1` string at most 255.",
        &BytesPack, nullptr);
    ok &= registry->Add<&BytesSize>("bytes", "size", ThreadRule::Pure, "format",
                                    "Returns the number of bytes `format` takes, or nil when it has an `s1` item "
                                    "(its size is in the data).");
    ok &= RegisterRuntimeBindings(registry);
    ok &= RegisterSettingsBindings(registry);
    ok &= RegisterEntityBindings(registry);
    ok &= RegisterComponentBindings(registry);
    ok &= RegisterEventBindings(registry);
    ok &= RegisterTaskBindings(registry);
    return ok;
}

}  // namespace vse::script
