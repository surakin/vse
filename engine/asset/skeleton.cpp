#include "asset/skeleton.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::asset
{
namespace
{

constexpr usize kSkelHeaderBytes = 48;
constexpr u32 kSkelFlagNames = 1;
constexpr f64 kUnitTolerance = 1e-3;

// A 3x4 affine matrix in double precision, row-major: m[row * 4 + column], the last column the translation.
struct SkelAffine
{
    f64 m[12];
};

SkelAffine SkelLocal(const SkeletonJoint& j)
{
    const f64 x = j.rotation[0];
    const f64 y = j.rotation[1];
    const f64 z = j.rotation[2];
    const f64 w = j.rotation[3];
    const f64 r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                      2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                      2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    SkelAffine a;
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 col = 0; col < 3; ++col)
        {
            a.m[row * 4 + col] = r[row * 3 + col] * static_cast<f64>(j.scale[col]);
        }
        a.m[row * 4 + 3] = j.translation[row];
    }
    return a;
}

SkelAffine SkelMultiply(const SkelAffine& a, const SkelAffine& b)
{
    SkelAffine out;
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 col = 0; col < 4; ++col)
        {
            f64 v = 0;
            for (u32 k = 0; k < 3; ++k)
            {
                v += a.m[row * 4 + k] * b.m[k * 4 + col];
            }
            if (col == 3)
            {
                v += a.m[row * 4 + 3];
            }
            out.m[row * 4 + col] = v;
        }
    }
    return out;
}

SkelAffine SkelInvert(const SkelAffine& a)
{
    const f64* m = a.m;
    // Cofactors of the 3x3 part.
    const f64 c00 = m[5] * m[10] - m[6] * m[9];
    const f64 c01 = m[6] * m[8] - m[4] * m[10];
    const f64 c02 = m[4] * m[9] - m[5] * m[8];
    const f64 det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    const f64 inv = 1.0 / det;
    SkelAffine out;
    out.m[0] = c00 * inv;
    out.m[1] = (m[2] * m[9] - m[1] * m[10]) * inv;
    out.m[2] = (m[1] * m[6] - m[2] * m[5]) * inv;
    out.m[4] = c01 * inv;
    out.m[5] = (m[0] * m[10] - m[2] * m[8]) * inv;
    out.m[6] = (m[2] * m[4] - m[0] * m[6]) * inv;
    out.m[8] = c02 * inv;
    out.m[9] = (m[1] * m[8] - m[0] * m[9]) * inv;
    out.m[10] = (m[0] * m[5] - m[1] * m[4]) * inv;
    for (u32 row = 0; row < 3; ++row)
    {
        out.m[row * 4 + 3] = -(out.m[row * 4] * m[3] + out.m[row * 4 + 1] * m[7] + out.m[row * 4 + 2] * m[11]);
    }
    return out;
}

void SkelStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u64 SkelLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

f32 SkelLoadF32(const byte* p)
{
    f32 v;
    MemCopy(&v, p, 4);
    return v;
}

bool SkelFinite(const f32* values, usize count)
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

StringView SkelName(const SkeletonJoint& j)
{
    usize length = 0;
    while (length < j.name.Size() && j.name[length] != '\0')
    {
        ++length;
    }
    return StringView(j.name.Data(), length);  // bounded: a name filled to the last byte is not read past the array
}

Status SkelInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status SkelCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

void SkelAppendF32(Vector<byte>* out, const f32* values, usize count)
{
    const usize at = out->Size();
    out->Resize(at + count * 4);
    MemCopy(out->Data() + at, values, count * 4);
}

}  // namespace

void ComputeInverseBindMatrices(Span<SkeletonJoint> joints)
{
    Vector<SkelAffine> model;
    model.Reserve(joints.Size());
    for (usize i = 0; i < joints.Size(); ++i)
    {
        SkelAffine local = SkelLocal(joints[i]);
        if (joints[i].parent != kNoParent && joints[i].parent < i)
        {
            local = SkelMultiply(model[joints[i].parent], local);
        }
        model.Push(local);
        const SkelAffine inverse = SkelInvert(local);
        for (u32 k = 0; k < 12; ++k)
        {
            joints[i].inverse_bind[k] = static_cast<f32>(inverse.m[k]);
        }
    }
}

ContentHash ComputeSkeletonHash(Span<const SkeletonJoint> joints)
{
    Vector<byte> bytes;
    bytes.Resize(4 + joints.Size() * 2);
    SkelStore(bytes.Data(), joints.Size(), 4);
    for (usize i = 0; i < joints.Size(); ++i)
    {
        SkelStore(bytes.Data() + 4 + i * 2, joints[i].parent, 2);
    }
    for (usize i = 0; i < joints.Size(); ++i)
    {
        const StringView name = SkelName(joints[i]);
        bytes.Push(static_cast<byte>(name.Length()));
        for (usize c = 0; c < name.Length(); ++c)
        {
            bytes.Push(static_cast<byte>(name.Data()[c]));
        }
    }
    return HashBytes(ByteSpan(bytes.Data(), bytes.Size()));
}

Status BuildSkeletonBlob(const TypeRegistry& types, ResourceId id, Span<const SkeletonJoint> joints, bool include_names,
                         Vector<byte>* out, u32 target)
{
    const usize n = joints.Size();
    if (n == 0)
    {
        return SkelInvalid("a skeleton needs at least one joint");
    }
    if (n > kMaxSkeletonJoints)
    {
        return Status::Make(ErrorCode::LimitExceeded, "a skeleton holds at most 256 joints in this version");
    }
    for (usize i = 0; i < n; ++i)
    {
        const SkeletonJoint& j = joints[i];
        const StringView name = SkelName(j);
        if (name.Length() == 0 || name.Length() > kMaxJointNameBytes)
        {
            return SkelInvalid("a joint name is empty or longer than 64 bytes");
        }
        for (usize k = 0; k < i; ++k)
        {
            if (SkelName(joints[k]) == name)
            {
                return SkelInvalid("two joints have the same name");
            }
        }
        if (j.parent != kNoParent && j.parent >= i)
        {
            return SkelInvalid("a joint's parent is not before it (parents must come first)");
        }
        if (!SkelFinite(j.translation.Data(), 3) || !SkelFinite(j.rotation.Data(), 4) ||
            !SkelFinite(j.scale.Data(), 3) || !SkelFinite(j.inverse_bind.Data(), 12))
        {
            return SkelInvalid("a joint holds a value that is not finite");
        }
        f64 length_squared = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            length_squared += static_cast<f64>(j.rotation[c]) * j.rotation[c];
        }
        if (Abs(Sqrt(length_squared) - 1.0) > kUnitTolerance || j.rotation[3] < 0.0f)
        {
            return SkelInvalid("a bind rotation is not a unit quaternion with w >= 0");
        }
        if (j.scale[0] == 0.0f || j.scale[1] == 0.0f || j.scale[2] == 0.0f)
        {
            return SkelInvalid("a joint has a zero scale");
        }
    }

    // The bound of the bind pose: the model-space joint positions.
    Vector<SkelAffine> model;
    f64 center[3] = {0, 0, 0};
    for (usize i = 0; i < n; ++i)
    {
        SkelAffine m = SkelLocal(joints[i]);
        if (joints[i].parent != kNoParent)
        {
            m = SkelMultiply(model[joints[i].parent], m);
        }
        model.Push(m);
        for (u32 c = 0; c < 3; ++c)
        {
            center[c] += m.m[c * 4 + 3];
        }
    }
    for (u32 c = 0; c < 3; ++c)
    {
        center[c] /= static_cast<f64>(n);
    }
    f64 radius = 0;
    for (usize i = 0; i < n; ++i)
    {
        f64 d2 = 0;
        for (u32 c = 0; c < 3; ++c)
        {
            const f64 d = model[i].m[c * 4 + 3] - center[c];
            d2 += d * d;
        }
        radius = d2 > radius * radius ? Sqrt(d2) : radius;
    }

    const ContentHash hash = ComputeSkeletonHash(joints);
    Vector<byte> header;
    header.Resize(kSkelHeaderBytes);
    MemSet(header.Data(), 0, header.Size());
    SkelStore(header.Data(), n, 4);
    SkelStore(header.Data() + 4, include_names ? kSkelFlagNames : 0, 4);
    MemCopy(header.Data() + 8, hash.bytes, 16);
    const f32 center_f[4] = {static_cast<f32>(center[0]), static_cast<f32>(center[1]), static_cast<f32>(center[2]),
                             static_cast<f32>(radius)};
    MemCopy(header.Data() + 24, center_f, 16);

    Vector<byte> parents;
    parents.Resize(n * 2);
    Vector<byte> translations;
    Vector<byte> rotations;
    Vector<byte> scales;
    Vector<byte> inverse_bind;
    for (usize i = 0; i < n; ++i)
    {
        SkelStore(parents.Data() + i * 2, joints[i].parent, 2);
        SkelAppendF32(&translations, joints[i].translation.Data(), 3);
        SkelAppendF32(&rotations, joints[i].rotation.Data(), 4);
        SkelAppendF32(&scales, joints[i].scale.Data(), 3);
        SkelAppendF32(&inverse_bind, joints[i].inverse_bind.Data(), 12);
    }

    BlobWriter writer(types, types::kSkeleton, id, target);
    ENGINE_TRY(writer.AddSection(kSkeletonSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kSkeletonSectionParents, ByteSpan(parents.Data(), parents.Size())));
    ENGINE_TRY(writer.AddSection(kSkeletonSectionTranslations, ByteSpan(translations.Data(), translations.Size())));
    ENGINE_TRY(writer.AddSection(kSkeletonSectionRotations, ByteSpan(rotations.Data(), rotations.Size())));
    ENGINE_TRY(writer.AddSection(kSkeletonSectionScales, ByteSpan(scales.Data(), scales.Size())));
    ENGINE_TRY(writer.AddSection(kSkeletonSectionInverseBind, ByteSpan(inverse_bind.Data(), inverse_bind.Size())));
    if (include_names)
    {
        Vector<byte> hashes;
        hashes.Resize(n * 4);
        Vector<byte> names;
        names.Resize((n + 1) * 4);
        u32 offset = 0;
        for (usize i = 0; i < n; ++i)
        {
            const StringView name = SkelName(joints[i]);
            SkelStore(hashes.Data() + i * 4, JointNameHash(name), 4);
            SkelStore(names.Data() + i * 4, offset, 4);
            for (usize c = 0; c < name.Length(); ++c)
            {
                names.Push(static_cast<byte>(name.Data()[c]));
            }
            offset += static_cast<u32>(name.Length());
        }
        SkelStore(names.Data() + n * 4, offset, 4);
        ENGINE_TRY(writer.AddSection(kSkeletonSectionNameHashes, ByteSpan(hashes.Data(), hashes.Size())));
        ENGINE_TRY(writer.AddSection(kSkeletonSectionNames, ByteSpan(names.Data(), names.Size())));
    }
    return writer.Finish(out);
}

Status SkeletonView::Open(ByteSpan data, const TypeRegistry& types)
{
    *this = SkeletonView();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kSkeleton)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a skeleton");
    }
    ByteSpan header;
    if (!blob.FindSection(kSkeletonSectionHeader, &header) || header.Size() != kSkelHeaderBytes)
    {
        return SkelCorrupt("the skeleton header section is missing or has the wrong size");
    }
    const u64 count = SkelLoad(header.Data(), 4);
    const u64 flags = SkelLoad(header.Data() + 4, 4);
    if (count == 0 || count > kMaxSkeletonJoints)
    {
        return SkelCorrupt("the skeleton joint count is zero or above the limit");
    }
    if ((flags & ~static_cast<u64>(kSkelFlagNames)) != 0 || SkelLoad(header.Data() + 40, 8) != 0)
    {
        return SkelCorrupt("a skeleton header field holds a value this version does not define");
    }
    const usize n = static_cast<usize>(count);
    const bool names = (flags & kSkelFlagNames) != 0;
    ContentHash hash;
    MemCopy(hash.bytes, header.Data() + 8, 16);
    Array<f32, 3> center = {
        {SkelLoadF32(header.Data() + 24), SkelLoadF32(header.Data() + 28), SkelLoadF32(header.Data() + 32)}};
    const f32 radius = SkelLoadF32(header.Data() + 36);
    if (!SkelFinite(center.Data(), 3) || !IsFinite(radius) || radius < 0.0f)
    {
        return SkelCorrupt("the skeleton bound is not finite");
    }

    struct Need
    {
        u32 id;
        usize size;
        ByteSpan* out;
    };
    ByteSpan parents, translations, rotations, scales, inverse_bind, hashes, name_data;
    const Need needs[] = {{kSkeletonSectionParents, n * 2, &parents},
                          {kSkeletonSectionTranslations, n * 12, &translations},
                          {kSkeletonSectionRotations, n * 16, &rotations},
                          {kSkeletonSectionScales, n * 12, &scales},
                          {kSkeletonSectionInverseBind, n * kInverseBindBytes, &inverse_bind}};
    for (const Need& need : needs)
    {
        if (!blob.FindSection(need.id, need.out) || need.out->Size() != need.size)
        {
            return SkelCorrupt("a skeleton section is missing or has the wrong size");
        }
    }
    if (names)
    {
        if (!blob.FindSection(kSkeletonSectionNameHashes, &hashes) || hashes.Size() != n * 4 ||
            !blob.FindSection(kSkeletonSectionNames, &name_data) || name_data.Size() < (n + 1) * 4)
        {
            return SkelCorrupt("a skeleton names section is missing or has the wrong size");
        }
        const usize chars = name_data.Size() - (n + 1) * 4;
        u64 previous = 0;
        if (SkelLoad(name_data.Data(), 4) != 0)
        {
            return SkelCorrupt("the first skeleton name offset is not zero");
        }
        for (usize i = 0; i < n; ++i)
        {
            const u64 end = SkelLoad(name_data.Data() + (i + 1) * 4, 4);
            if (end <= previous || end - previous > kMaxJointNameBytes || end > chars)
            {
                return SkelCorrupt("a skeleton name offset is out of range or the name is empty or too long");
            }
            const StringView name(reinterpret_cast<const char*>(name_data.Data()) + (n + 1) * 4 + previous,
                                  static_cast<usize>(end - previous));
            if (SkelLoad(hashes.Data() + i * 4, 4) != JointNameHash(name))
            {
                return SkelCorrupt("a skeleton name hash does not match its name");
            }
            previous = end;
        }
        if (previous != chars)
        {
            return SkelCorrupt("the skeleton names section has trailing bytes");
        }
    }

    for (usize i = 0; i < n; ++i)
    {
        const u64 parent = SkelLoad(parents.Data() + i * 2, 2);
        if (parent != kNoParent && parent >= i)
        {
            return SkelCorrupt("a skeleton joint's parent is not before it");
        }
        f32 q[4];
        MemCopy(q, rotations.Data() + i * 16, 16);
        f64 length_squared = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            length_squared += IsFinite(q[c]) ? static_cast<f64>(q[c]) * q[c] : 1e30;
        }
        if (Abs(Sqrt(length_squared) - 1.0) > kUnitTolerance || q[3] < 0.0f)
        {
            return SkelCorrupt("a skeleton bind rotation is not a unit quaternion with w >= 0");
        }
        f32 s[3];
        MemCopy(s, scales.Data() + i * 12, 12);
        if (!SkelFinite(s, 3) || s[0] == 0.0f || s[1] == 0.0f || s[2] == 0.0f)
        {
            return SkelCorrupt("a skeleton joint has a zero or non-finite scale");
        }
        f32 t[3];
        MemCopy(t, translations.Data() + i * 12, 12);
        f32 m[12];
        MemCopy(m, inverse_bind.Data() + i * kInverseBindBytes, kInverseBindBytes);
        if (!SkelFinite(t, 3) || !SkelFinite(m, 12))
        {
            return SkelCorrupt("a skeleton joint holds a value that is not finite");
        }
    }

    joint_count_ = static_cast<u32>(n);
    hash_ = hash;
    id_ = blob.Id();
    has_names_ = names;
    center_ = center;
    radius_ = radius;
    parents_ = parents;
    translations_ = translations;
    rotations_ = rotations;
    scales_ = scales;
    inverse_bind_ = inverse_bind;
    name_hashes_ = hashes;
    names_ = name_data;
    return Status::Success();
}

u16 SkeletonView::Parent(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::Parent: joint index out of range");
    return static_cast<u16>(SkelLoad(parents_.Data() + i * 2, 2));
}

Array<f32, 3> SkeletonView::Translation(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::Translation: joint index out of range");
    Array<f32, 3> v;
    MemCopy(v.Data(), translations_.Data() + i * 12, 12);
    return v;
}

Array<f32, 4> SkeletonView::Rotation(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::Rotation: joint index out of range");
    Array<f32, 4> v;
    MemCopy(v.Data(), rotations_.Data() + i * 16, 16);
    return v;
}

Array<f32, 3> SkeletonView::Scale(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::Scale: joint index out of range");
    Array<f32, 3> v;
    MemCopy(v.Data(), scales_.Data() + i * 12, 12);
    return v;
}

Array<f32, 12> SkeletonView::InverseBind(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::InverseBind: joint index out of range");
    Array<f32, 12> v;
    MemCopy(v.Data(), inverse_bind_.Data() + i * kInverseBindBytes, kInverseBindBytes);
    return v;
}

StringView SkeletonView::Name(u32 i) const
{
    ENGINE_ASSERT(i < joint_count_, "SkeletonView::Name: joint index out of range");
    if (!has_names_)
    {
        return StringView();
    }
    const usize n = joint_count_;
    const u64 begin = SkelLoad(names_.Data() + i * 4, 4);
    const u64 end = SkelLoad(names_.Data() + (i + 1) * 4, 4);
    return StringView(reinterpret_cast<const char*>(names_.Data()) + (n + 1) * 4 + begin,
                      static_cast<usize>(end - begin));
}

bool SkeletonView::FindJoint(StringView name, u32* index) const
{
    if (!has_names_)
    {
        return false;
    }
    const u32 hash = JointNameHash(name);
    for (u32 i = 0; i < joint_count_; ++i)
    {
        if (SkelLoad(name_hashes_.Data() + i * 4, 4) == hash && Name(i) == name)
        {
            *index = i;
            return true;
        }
    }
    return false;
}

}  // namespace vse::asset
