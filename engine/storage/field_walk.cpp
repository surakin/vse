#include "storage/field_walk.h"

namespace vse::storage
{
namespace
{

Status Truncated()
{
    return Status::Make(ErrorCode::Corrupt, "the record is truncated");
}

Status FromBytes(bytes::ByteError e)
{
    switch (e)
    {
    case bytes::ByteError::Ok: return Status::Success();
    case bytes::ByteError::Truncated: return Truncated();
    case bytes::ByteError::TooLong: return Status::Make(ErrorCode::LimitExceeded, "a length is above its limit");
    case bytes::ByteError::Invalid: return Status::Make(ErrorCode::Corrupt, "the record holds an invalid value");
    }
    return Status::Make(ErrorCode::Corrupt, "the record holds an invalid value");
}

// The fixed size in bytes of a value of `kind`, or 0 for a kind with a length of its own. An enum is as wide as its
// underlying integer, which the caller passes.
usize FixedSize(FieldKind kind, usize memory_size)
{
    switch (kind)
    {
    case FieldKind::U8:
    case FieldKind::I8:
    case FieldKind::Bool: return 1;
    case FieldKind::U16:
    case FieldKind::I16: return 2;
    case FieldKind::U32:
    case FieldKind::I32:
    case FieldKind::F32: return 4;
    case FieldKind::U64:
    case FieldKind::I64:
    case FieldKind::F64:
    case FieldKind::ResourceRef:
    case FieldKind::Vec2: return 8;
    case FieldKind::Vec3: return 12;
    case FieldKind::Vec4:
    case FieldKind::Quat:
    case FieldKind::EntityRef: return 16;
    case FieldKind::Aabb: return 24;
    case FieldKind::Mat4: return 64;
    case FieldKind::Enum: return memory_size;
    case FieldKind::String:
    case FieldKind::Array: return 0;
    }
    return 0;
}

Status SkipValue(FieldKind kind, usize memory_size, bytes::ByteReader& in, const CodecLimits& limits,
                 const FieldDesc* array)
{
    const usize fixed = FixedSize(kind, memory_size);
    if (fixed != 0)
    {
        ByteSpan skipped;
        return FromBytes(in.ReadBytes(fixed, &skipped));
    }
    if (kind == FieldKind::String)
    {
        u64 length = 0;
        ENGINE_TRY(FromBytes(in.ReadLength(limits.max_string_bytes, &length)));
        ByteSpan skipped;
        return FromBytes(in.ReadBytes(static_cast<usize>(length), &skipped));
    }
    if (kind == FieldKind::Array && array != nullptr)
    {
        u64 count = 0;
        ENGINE_TRY(FromBytes(in.ReadLength(limits.max_array_elements, &count)));
        if (count != array->element_count)
        {
            return Status::Make(ErrorCode::Corrupt, "an array has another element count than its type");
        }
        if (array->element_kind == FieldKind::Array)
        {
            return Status::Make(ErrorCode::Invalid, "an array of arrays cannot be walked");
        }
        for (u64 i = 0; i < count; ++i)
        {
            ENGINE_TRY(SkipValue(array->element_kind, array->element_size, in, limits, nullptr));
        }
        return Status::Success();
    }
    return Status::Make(ErrorCode::Invalid, "a field of this kind cannot be walked");
}

}  // namespace

Status SkipField(const FieldDesc& field, bytes::ByteReader& in, const CodecLimits& limits)
{
    return SkipValue(field.kind, field.memory_size, in, limits, &field);
}

Status ReadReferences(const RegisteredComponent& component, ByteSpan record, const CodecLimits& limits,
                      Vector<ReferenceValue>* out)
{
    if (!component.IsStored())
    {
        return Status::Make(ErrorCode::Invalid, "the component is not stored");
    }
    bytes::ByteReader in(record);
    const Span<const FieldDesc> fields = component.Fields();
    for (usize i = 0; i < fields.Size(); ++i)
    {
        const FieldDesc& f = fields[i];
        if (f.kind == FieldKind::ResourceRef)
        {
            ReferenceValue value;
            value.field_index = static_cast<u32>(i);
            value.field = &f;
            ENGINE_TRY(FromBytes(in.ReadU64(&value.id)));
            out->Push(value);
            continue;
        }
        ENGINE_TRY(SkipField(f, in, limits));
    }
    if (in.Remaining() != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "bytes are left over after the last field");
    }
    return Status::Success();
}

}  // namespace vse::storage
