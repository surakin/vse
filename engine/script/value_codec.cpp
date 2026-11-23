#include "script/value_codec.h"

#include "lang/scalar.h"
#include "lang/utility.h"

namespace vse::script
{

namespace
{

enum Tag : u8
{
    kNil = 0,
    kFalse = 1,
    kTrue = 2,
    kInt = 3,
    kFloat = 4,
    kText = 5,
    kVec2 = 6,
    kVec3 = 7,
    kVec4 = 8,
    kQuat = 9,
    kEntity = 10,
    kBytes = 11,
    kArray = 12,
    kMap = 13,
    kTagCount = 14,
};

constexpr u8 kRawEntities = 0;
constexpr u8 kKeyedEntities = 1;

void WriteText(bytes::ByteWriter* out, StringView text)
{
    out->WriteVarint(text.Length());
    out->WriteBytes(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length()));
}

void Encode(const Value& v, bytes::ByteWriter* out, const storage::KeyIndex* index)
{
    switch (v.Kind())
    {
    case ValueKind::Nil: out->WriteU8(kNil); break;
    case ValueKind::Bool: out->WriteU8(v.AsBool() ? kTrue : kFalse); break;
    case ValueKind::Int:
        out->WriteU8(kInt);
        out->WriteI64(v.AsInt());
        break;
    case ValueKind::Float:
        out->WriteU8(kFloat);
        out->WriteF64(v.AsFloat());
        break;
    case ValueKind::Text:
        out->WriteU8(kText);
        WriteText(out, v.Text());
        break;
    case ValueKind::Vec2:
    {
        const Vec2 a = v.AsVec2();
        out->WriteU8(kVec2);
        out->WriteF32(a.x);
        out->WriteF32(a.y);
        break;
    }
    case ValueKind::Vec3:
    {
        const Vec3d a = v.AsVec3d();
        out->WriteU8(kVec3);
        out->WriteF64(a.x);
        out->WriteF64(a.y);
        out->WriteF64(a.z);
        break;
    }
    case ValueKind::Vec4:
    {
        const Vec4 a = v.AsVec4();
        out->WriteU8(kVec4);
        out->WriteF32(a.x);
        out->WriteF32(a.y);
        out->WriteF32(a.z);
        out->WriteF32(a.w);
        break;
    }
    case ValueKind::Quat:
    {
        const Quaternion a = v.AsQuat();
        out->WriteU8(kQuat);
        out->WriteF32(a.x);
        out->WriteF32(a.y);
        out->WriteF32(a.z);
        out->WriteF32(a.w);
        break;
    }
    case ValueKind::Entity:
    {
        out->WriteU8(kEntity);
        if (index != nullptr)
        {
            const storage::EntityKey key = index->KeyOf(v.AsEntity());
            out->WriteU64(key.high);
            out->WriteU64(key.low);
        }
        else
        {
            out->WriteU64(v.AsEntity().ToU64());
        }
        break;
    }
    case ValueKind::Bytes:
    {
        const Span<const u8> b = v.Bytes();
        out->WriteU8(kBytes);
        out->WriteVarint(b.Size());
        out->WriteBytes(ByteSpan(reinterpret_cast<const byte*>(b.Data()), b.Size()));
        break;
    }
    case ValueKind::Array:
        out->WriteU8(kArray);
        out->WriteVarint(v.Size());
        for (usize i = 0; i < v.Size(); ++i)
        {
            Encode(v.At(i), out, index);
        }
        break;
    case ValueKind::Map:
        out->WriteU8(kMap);
        out->WriteVarint(v.Size());
        for (usize i = 0; i < v.Size(); ++i)
        {
            WriteText(out, v.KeyAt(i));
            Encode(v.At(i), out, index);
        }
        break;
    }
}

struct Reader
{
    bytes::ByteReader* in;
    const storage::KeyIndex* index;
    ValueLimits limits;
    bool keyed;
    u32 nodes = 0;
};

ValueRecordStatus Fail(bytes::ByteError error)
{
    return error == bytes::ByteError::Truncated ? ValueRecordStatus::Truncated : ValueRecordStatus::Corrupt;
}

ValueRecordStatus ReadText(Reader* r, String<HeapAllocator>* out)
{
    u64 length = 0;
    const bytes::ByteError e = r->in->ReadLength(r->limits.max_text_bytes, &length);
    if (e == bytes::ByteError::TooLong)
    {
        return ValueRecordStatus::OverLimit;
    }
    if (e != bytes::ByteError::Ok)
    {
        return Fail(e);
    }
    ByteSpan bytes;
    const bytes::ByteError b = r->in->ReadBytes(static_cast<usize>(length), &bytes);
    if (b != bytes::ByteError::Ok)
    {
        return Fail(b);
    }
    *out = String<HeapAllocator>(HeapAllocator::Default(),
                                 StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()));
    return ValueRecordStatus::Ok;
}

ValueRecordStatus Decode(Reader* r, Value* out, u32 depth)
{
    if (depth > r->limits.max_depth || depth > kMaxValueDepth)
    {
        return ValueRecordStatus::OverLimit;
    }
    if (++r->nodes > r->limits.max_nodes)
    {
        return ValueRecordStatus::OverLimit;
    }
    u8 tag = 0;
    bytes::ByteError e = r->in->ReadU8(&tag);
    if (e != bytes::ByteError::Ok)
    {
        return Fail(e);
    }
    f32 f[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (tag)
    {
    case kNil: *out = Value(); return ValueRecordStatus::Ok;
    case kFalse: *out = Value::FromBool(false); return ValueRecordStatus::Ok;
    case kTrue: *out = Value::FromBool(true); return ValueRecordStatus::Ok;
    case kInt:
    {
        i64 v = 0;
        e = r->in->ReadI64(&v);
        if (e != bytes::ByteError::Ok)
        {
            return Fail(e);
        }
        *out = Value::FromInt(v);
        return ValueRecordStatus::Ok;
    }
    case kFloat:
    {
        f64 v = 0;
        e = r->in->ReadF64(&v);
        if (e != bytes::ByteError::Ok)
        {
            return Fail(e);
        }
        if (r->limits.require_finite && !IsFinite(v))
        {
            return ValueRecordStatus::OverLimit;
        }
        *out = Value::FromFloat(v);
        return ValueRecordStatus::Ok;
    }
    case kText:
    {
        String<HeapAllocator> text(HeapAllocator::Default());
        const ValueRecordStatus s = ReadText(r, &text);
        if (s != ValueRecordStatus::Ok)
        {
            return s;
        }
        *out = Value::FromText(text.View());
        return ValueRecordStatus::Ok;
    }
    case kVec3:
    {
        f64 d[3] = {0.0, 0.0, 0.0};
        for (u32 i = 0; i < 3; ++i)
        {
            e = r->in->ReadF64(&d[i]);
            if (e != bytes::ByteError::Ok)
            {
                return Fail(e);
            }
            if (r->limits.require_finite && !IsFinite(d[i]))
            {
                return ValueRecordStatus::OverLimit;
            }
        }
        *out = Value::FromVec3d(Vec3d(d[0], d[1], d[2]));
        return ValueRecordStatus::Ok;
    }
    case kVec2:
    case kVec4:
    case kQuat:
    {
        const u32 count = tag == kVec2 ? 2 : 4;
        for (u32 i = 0; i < count; ++i)
        {
            e = r->in->ReadF32(&f[i]);
            if (e != bytes::ByteError::Ok)
            {
                return Fail(e);
            }
            if (r->limits.require_finite && !IsFinite(f[i]))
            {
                return ValueRecordStatus::OverLimit;
            }
        }
        if (tag == kVec2)
        {
            *out = Value::FromVec2(Vec2{f[0], f[1]});
        }
        else if (tag == kVec4)
        {
            *out = Value::FromVec4(Vec4{f[0], f[1], f[2], f[3]});
        }
        else
        {
            Quaternion q;
            q.x = f[0];
            q.y = f[1];
            q.z = f[2];
            q.w = f[3];
            *out = Value::FromQuat(q);
        }
        return ValueRecordStatus::Ok;
    }
    case kEntity:
    {
        if (r->keyed)
        {
            storage::EntityKey key;
            e = r->in->ReadU64(&key.high);
            if (e == bytes::ByteError::Ok)
            {
                e = r->in->ReadU64(&key.low);
            }
            if (e != bytes::ByteError::Ok)
            {
                return Fail(e);
            }
            ecs::Entity found;
            if (r->index != nullptr && !key.IsNull())
            {
                found = r->index->Find(key);
            }
            *out = Value::FromEntity(found);
        }
        else
        {
            u64 packed = 0;
            e = r->in->ReadU64(&packed);
            if (e != bytes::ByteError::Ok)
            {
                return Fail(e);
            }
            *out = Value::FromEntity(ecs::Entity::FromU64(packed));
        }
        return ValueRecordStatus::Ok;
    }
    case kBytes:
    {
        u64 length = 0;
        e = r->in->ReadLength(r->limits.max_blob_bytes, &length);
        if (e == bytes::ByteError::TooLong)
        {
            return ValueRecordStatus::OverLimit;
        }
        if (e != bytes::ByteError::Ok)
        {
            return Fail(e);
        }
        ByteSpan data;
        e = r->in->ReadBytes(static_cast<usize>(length), &data);
        if (e != bytes::ByteError::Ok)
        {
            return Fail(e);
        }
        *out = Value::FromBytes(Span<const u8>(reinterpret_cast<const u8*>(data.Data()), data.Size()));
        return ValueRecordStatus::Ok;
    }
    case kArray:
    case kMap:
    {
        u64 count = 0;
        e = r->in->ReadLength(kMaxContainerItems, &count);
        if (e == bytes::ByteError::TooLong)
        {
            return ValueRecordStatus::OverLimit;
        }
        if (e != bytes::ByteError::Ok)
        {
            return Fail(e);
        }
        if (count > r->in->Remaining())
        {
            return ValueRecordStatus::Truncated;  // every item takes at least one byte
        }
        Value container = tag == kArray ? Value::MakeArray() : Value::MakeMap();
        for (u64 i = 0; i < count; ++i)
        {
            String<HeapAllocator> key(HeapAllocator::Default());
            if (tag == kMap)
            {
                const ValueRecordStatus ks = ReadText(r, &key);
                if (ks != ValueRecordStatus::Ok)
                {
                    return ks;
                }
            }
            Value item;
            const ValueRecordStatus s = Decode(r, &item, depth + 1);
            if (s != ValueRecordStatus::Ok)
            {
                return s;
            }
            const bool added = tag == kArray ? container.Push(Move(item)) : container.Set(key.View(), Move(item));
            if (!added)
            {
                return ValueRecordStatus::Corrupt;  // a duplicate key makes the map smaller than its count
            }
        }
        if (tag == kMap && container.Size() != count)
        {
            return ValueRecordStatus::Corrupt;
        }
        *out = Move(container);
        return ValueRecordStatus::Ok;
    }
    default: return ValueRecordStatus::Corrupt;
    }
}

}  // namespace

const char* ValueRecordStatusName(ValueRecordStatus status)
{
    switch (status)
    {
    case ValueRecordStatus::Ok: return "ok";
    case ValueRecordStatus::Truncated: return "truncated";
    case ValueRecordStatus::Corrupt: return "corrupt";
    case ValueRecordStatus::OverLimit: return "over the limits";
    }
    return "corrupt";
}

void EncodeValueRecord(const Value& value, bytes::ByteWriter* out, const storage::KeyIndex* index)
{
    out->WriteU8(kValueRecordVersion);
    out->WriteU8(index != nullptr ? kKeyedEntities : kRawEntities);
    Encode(value, out, index);
}

ValueRecordStatus DecodeValueRecord(bytes::ByteReader* in, Value* out, const storage::KeyIndex* index,
                                    const ValueLimits& limits)
{
    bytes::ByteReader copy = *in;
    u8 version = 0;
    u8 mode = 0;
    bytes::ByteError e = copy.ReadU8(&version);
    if (e == bytes::ByteError::Ok)
    {
        e = copy.ReadU8(&mode);
    }
    if (e != bytes::ByteError::Ok)
    {
        return Fail(e);
    }
    if (version != kValueRecordVersion || mode > kKeyedEntities)
    {
        return ValueRecordStatus::Corrupt;
    }
    Reader r{&copy, index, limits, mode == kKeyedEntities};
    Value value;
    const ValueRecordStatus s = Decode(&r, &value, 1);
    if (s != ValueRecordStatus::Ok)
    {
        return s;
    }
    *out = Move(value);
    *in = copy;
    return ValueRecordStatus::Ok;
}

}  // namespace vse::script
