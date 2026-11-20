#include "script/component_bindings.h"

#include "algorithm.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "ecs/component.h"
#include "lang/scalar.h"
#include "script/component_catalog.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "storage/field_walk.h"

namespace vse::script
{

namespace
{

using storage::FieldDesc;
using storage::FieldKind;
using storage::RegisteredComponent;

PartitionContext* ComponentContext()
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->world == nullptr || context->components == nullptr)
    {
        RaiseScriptError("no components are available to this script");
        return nullptr;
    }
    return context;
}

void Raise(StringView a, StringView b = StringView(), StringView c = StringView(), StringView d = StringView(),
           StringView e = StringView(), StringView f = StringView())
{
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(a);
    text.Append(b);
    text.Append(c);
    text.Append(d);
    text.Append(e);
    text.Append(f);
    RaiseScriptError(text.View());
}

// A component of the engine below 1000 belongs to the engine's own bindings.
bool Writable(const RegisteredComponent& rc)
{
    return rc.Id() >= kFirstScriptWritableComponentId;
}

const RegisteredComponent* Resolve(const PartitionContext& context, StringView name)
{
    const RegisteredComponent* rc = context.components->FindByName(name);
    if (rc == nullptr)
    {
        Raise("no component named `", name, "`");
        return nullptr;
    }
    if (!rc->IsStored() || rc->EcsId() == ecs::kInvalidComponent)
    {
        Raise("the component `", name, "` is not available to scripts (derived or runtime-only)");
        return nullptr;
    }
    return rc;
}

// A scratch object of a component, for decoding a record before it touches the world.
class Scratch
{
public:
    explicit Scratch(ecs::ComponentId id) : info_(&ecs::ComponentRegistry::Global().Info(id))
    {
        if (info_->size != 0)
        {
            memory_ = HeapAllocator::Default().Alloc(info_->size, info_->align);
        }
        if (info_->default_construct != nullptr && (info_->size == 0 || memory_ != nullptr))
        {
            if (info_->size != 0)
            {
                info_->default_construct(memory_);
            }
            ready_ = true;
        }
    }
    ~Scratch()
    {
        if (ready_ && info_->size != 0)
        {
            info_->destroy(memory_);
        }
        if (memory_ != nullptr)
        {
            HeapAllocator::Default().Free(memory_, info_->size, info_->align);
        }
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    bool Ready() const { return ready_; }
    void* Memory() const { return memory_; }
    const ecs::TypeInfo& Info() const { return *info_; }

private:
    const ecs::TypeInfo* info_;
    void* memory_ = nullptr;
    bool ready_ = false;
};

// ------------------------------------------------------------------ stored form to values

bool ReadFieldValue(const FieldDesc& f, bytes::ByteReader& in, Value* out);

bool ReadScalar(FieldKind kind, bytes::ByteReader& in, Value* out)
{
    bytes::ByteError e = bytes::ByteError::Ok;
    switch (kind)
    {
    case FieldKind::U8:
    {
        u8 v = 0;
        e = in.ReadU8(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::U16:
    {
        u16 v = 0;
        e = in.ReadU16(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::U32:
    {
        u32 v = 0;
        e = in.ReadU32(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::U64:
    case FieldKind::ResourceRef:
    {
        u64 v = 0;
        e = in.ReadU64(&v);
        *out = Value::FromInt(static_cast<i64>(v));  // an id above 2^63 shows as its two's-complement number
        break;
    }
    case FieldKind::I8:
    {
        i8 v = 0;
        e = in.ReadI8(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::I16:
    {
        i16 v = 0;
        e = in.ReadI16(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::I32:
    case FieldKind::Enum:
    {
        i32 v = 0;
        e = in.ReadI32(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::I64:
    {
        i64 v = 0;
        e = in.ReadI64(&v);
        *out = Value::FromInt(v);
        break;
    }
    case FieldKind::F32:
    {
        f32 v = 0;
        e = in.ReadF32(&v);
        *out = Value::FromFloat(v);
        break;
    }
    case FieldKind::F64:
    {
        f64 v = 0;
        e = in.ReadF64(&v);
        *out = Value::FromFloat(v);
        break;
    }
    case FieldKind::Bool:
    {
        bool v = false;
        e = in.ReadBool(&v);
        *out = Value::FromBool(v);
        break;
    }
    default: return false;
    }
    return e == bytes::ByteError::Ok;
}

bool ReadFloats(bytes::ByteReader& in, f32* out, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (in.ReadF32(&out[i]) != bytes::ByteError::Ok)
        {
            return false;
        }
    }
    return true;
}

// Enum fields are stored as their underlying integer, whose width the field does not record: the walkers of the
// registry (`SkipField`) know it, and so do we, through the memory size of the member.
bool ReadEnum(const FieldDesc& f, bytes::ByteReader& in, Value* out)
{
    const bool is_signed = f.options.has_range && f.options.range_min < 0;
    switch (f.memory_size)
    {
    case 1: return ReadScalar(is_signed ? FieldKind::I8 : FieldKind::U8, in, out);
    case 2: return ReadScalar(is_signed ? FieldKind::I16 : FieldKind::U16, in, out);
    case 4: return ReadScalar(FieldKind::I32, in, out);
    default: return ReadScalar(FieldKind::I64, in, out);
    }
}

bool ReadFieldValue(const FieldDesc& f, bytes::ByteReader& in, Value* out)
{
    switch (f.kind)
    {
    case FieldKind::Enum: return ReadEnum(f, in, out);
    case FieldKind::Vec2:
    {
        f32 v[2];
        if (!ReadFloats(in, v, 2))
        {
            return false;
        }
        *out = Value::FromVec2(Vec2(v[0], v[1]));
        return true;
    }
    case FieldKind::Vec3:
    {
        f32 v[3];
        if (!ReadFloats(in, v, 3))
        {
            return false;
        }
        *out = Value::FromVec3(Vec3(v[0], v[1], v[2]));
        return true;
    }
    case FieldKind::Vec4:
    {
        f32 v[4];
        if (!ReadFloats(in, v, 4))
        {
            return false;
        }
        *out = Value::FromVec4(Vec4(v[0], v[1], v[2], v[3]));
        return true;
    }
    case FieldKind::Quat:
    {
        f32 v[4];
        if (!ReadFloats(in, v, 4))
        {
            return false;
        }
        *out = Value::FromQuat(Quaternion(v[0], v[1], v[2], v[3]));
        return true;
    }
    case FieldKind::Mat4:
    {
        f32 v[16];
        if (!ReadFloats(in, v, 16))
        {
            return false;
        }
        *out = Value::MakeArray();
        for (usize i = 0; i < 16; ++i)
        {
            out->Push(Value::FromFloat(v[i]));
        }
        return true;
    }
    case FieldKind::Aabb:
    {
        f32 v[6];
        if (!ReadFloats(in, v, 6))
        {
            return false;
        }
        *out = Value::MakeMap();
        out->Set("min", Value::FromVec3(Vec3(v[0], v[1], v[2])));
        out->Set("max", Value::FromVec3(Vec3(v[3], v[4], v[5])));
        return true;
    }
    case FieldKind::String:
    {
        u64 length = 0;
        ByteSpan raw;
        if (in.ReadLength(0xFFFFFFFFull, &length) != bytes::ByteError::Ok ||
            in.ReadBytes(static_cast<usize>(length), &raw) != bytes::ByteError::Ok)
        {
            return false;
        }
        *out = Value::FromText(StringView(reinterpret_cast<const char*>(raw.Data()), raw.Size()));
        return true;
    }
    case FieldKind::Array:
    {
        u64 count = 0;
        if (in.ReadLength(f.element_count, &count) != bytes::ByteError::Ok)
        {
            return false;
        }
        FieldDesc element = f;
        element.kind = f.element_kind;
        element.memory_size = f.element_size;
        *out = Value::MakeArray();
        for (u64 i = 0; i < count; ++i)
        {
            Value item;
            if (!ReadFieldValue(element, in, &item) || !out->Push(Move(item)))
            {
                return false;
            }
        }
        return true;
    }
    default: return ReadScalar(f.kind, in, out);
    }
}

bool Exposed(const FieldDesc& f)
{
    return f.kind != FieldKind::EntityRef && !(f.kind == FieldKind::Array && f.element_kind == FieldKind::EntityRef);
}

// The record as a table of its fields; fields with no script form (entity references) are left out.
bool RecordToTable(const RegisteredComponent& rc, ByteSpan record, Value* out)
{
    bytes::ByteReader in(record);
    *out = Value::MakeMap();
    for (const FieldDesc& f : rc.Fields())
    {
        if (!Exposed(f))
        {
            if (storage::SkipField(f, in, storage::CodecLimits()).Failed())
            {
                return false;
            }
            continue;
        }
        Value v;
        if (!ReadFieldValue(f, in, &v) || !out->Set(StringView(f.name), Move(v)))
        {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ values to stored form

bool ToInteger(const Value& v, i64 lowest, i64 highest, i64* out)
{
    if (v.Kind() == ValueKind::Int)
    {
        *out = v.AsInt();
    }
    else if (v.Kind() == ValueKind::Float)
    {
        const f64 f = v.AsFloat();
        if (!(f >= -9.0e15 && f <= 9.0e15) || Floor(f) != f)
        {
            return false;
        }
        *out = static_cast<i64>(f);
    }
    else
    {
        return false;
    }
    return *out >= lowest && *out <= highest;
}

bool FiniteNumber(const Value& v, f64* out)
{
    if (!v.IsNumber())
    {
        return false;
    }
    *out = v.ToDouble();
    return *out == *out && *out - *out == 0.0;
}

bool FiniteVec(const f32* lanes, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!(lanes[i] == lanes[i]) || !(lanes[i] - lanes[i] == 0.0f))
        {
            return false;
        }
    }
    return true;
}

// Writes one script value in the stored form of `f`; false with `*why` set when it does not fit.
bool WriteFieldValue(const FieldDesc& f, const Value& v, bytes::ByteWriter& out, const char** why)
{
    i64 n = 0;
    f64 d = 0;
    switch (f.kind)
    {
    case FieldKind::U8:
        if (!ToInteger(v, 0, 0xFF, &n))
        {
            *why = "an integer from 0 to 255";
            return false;
        }
        out.WriteU8(static_cast<u8>(n));
        return true;
    case FieldKind::U16:
        if (!ToInteger(v, 0, 0xFFFF, &n))
        {
            *why = "an integer from 0 to 65535";
            return false;
        }
        out.WriteU16(static_cast<u16>(n));
        return true;
    case FieldKind::U32:
        if (!ToInteger(v, 0, 0xFFFFFFFFll, &n))
        {
            *why = "an integer from 0 to 4294967295";
            return false;
        }
        out.WriteU32(static_cast<u32>(n));
        return true;
    case FieldKind::U64:
    case FieldKind::ResourceRef:
        if (!ToInteger(v, 0, 0x7FFFFFFFFFFFFFFFll, &n))
        {
            *why = "a non-negative integer";
            return false;
        }
        out.WriteU64(static_cast<u64>(n));
        return true;
    case FieldKind::I8:
        if (!ToInteger(v, -128, 127, &n))
        {
            *why = "an integer from -128 to 127";
            return false;
        }
        out.WriteI8(static_cast<i8>(n));
        return true;
    case FieldKind::I16:
        if (!ToInteger(v, -32768, 32767, &n))
        {
            *why = "an integer from -32768 to 32767";
            return false;
        }
        out.WriteI16(static_cast<i16>(n));
        return true;
    case FieldKind::I32:
        if (!ToInteger(v, -2147483647ll - 1, 2147483647ll, &n))
        {
            *why = "a 32-bit integer";
            return false;
        }
        out.WriteI32(static_cast<i32>(n));
        return true;
    case FieldKind::I64:
        if (!ToInteger(v, -0x7FFFFFFFFFFFFFFFll - 1, 0x7FFFFFFFFFFFFFFFll, &n))
        {
            *why = "an integer";
            return false;
        }
        out.WriteI64(n);
        return true;
    case FieldKind::Enum:
        if (!ToInteger(v, -2147483647ll - 1, 2147483647ll, &n) ||
            (f.options.has_range && (n < f.options.range_min || n > f.options.range_max)))
        {
            *why = "an integer inside the field's range";
            return false;
        }
        switch (f.memory_size)
        {
        case 1: out.WriteU8(static_cast<u8>(n)); break;
        case 2: out.WriteU16(static_cast<u16>(n)); break;
        case 4: out.WriteI32(static_cast<i32>(n)); break;
        default: out.WriteI64(n); break;
        }
        return true;
    case FieldKind::F32:
        if (!FiniteNumber(v, &d) || d > 3.4028234e38 || d < -3.4028234e38)
        {
            *why = "a finite number that fits 32 bits";
            return false;
        }
        out.WriteF32(static_cast<f32>(d));
        return true;
    case FieldKind::F64:
        if (!FiniteNumber(v, &d))
        {
            *why = "a finite number";
            return false;
        }
        out.WriteF64(d);
        return true;
    case FieldKind::Bool:
        if (v.Kind() != ValueKind::Bool)
        {
            *why = "a boolean";
            return false;
        }
        out.WriteBool(v.AsBool());
        return true;
    case FieldKind::Vec2:
    {
        if (v.Kind() != ValueKind::Vec2)
        {
            *why = "a vec2";
            return false;
        }
        const Vec2 x = v.AsVec2();
        const f32 lanes[2] = {x.x, x.y};
        if (!FiniteVec(lanes, 2))
        {
            *why = "finite numbers";
            return false;
        }
        out.WriteF32(x.x);
        out.WriteF32(x.y);
        return true;
    }
    case FieldKind::Vec3:
    {
        if (v.Kind() != ValueKind::Vec3)
        {
            *why = "a vec3";
            return false;
        }
        const Vec3 x = v.AsVec3();
        const f32 lanes[3] = {x.x, x.y, x.z};
        if (!FiniteVec(lanes, 3))
        {
            *why = "finite numbers";
            return false;
        }
        for (f32 l : lanes)
        {
            out.WriteF32(l);
        }
        return true;
    }
    case FieldKind::Vec4:
    {
        if (v.Kind() != ValueKind::Vec4)
        {
            *why = "a vec4";
            return false;
        }
        const Vec4 x = v.AsVec4();
        const f32 lanes[4] = {x.x, x.y, x.z, x.w};
        if (!FiniteVec(lanes, 4))
        {
            *why = "finite numbers";
            return false;
        }
        for (f32 l : lanes)
        {
            out.WriteF32(l);
        }
        return true;
    }
    case FieldKind::Quat:
    {
        if (v.Kind() != ValueKind::Quat)
        {
            *why = "a quat";
            return false;
        }
        const Quaternion x = v.AsQuat();
        const f32 lanes[4] = {x.x, x.y, x.z, x.w};
        if (!FiniteVec(lanes, 4))
        {
            *why = "finite numbers";
            return false;
        }
        for (f32 l : lanes)
        {
            out.WriteF32(l);
        }
        return true;
    }
    case FieldKind::Mat4:
    {
        if (v.Kind() != ValueKind::Array || v.Size() != 16)
        {
            *why = "an array of 16 numbers";
            return false;
        }
        for (usize i = 0; i < 16; ++i)
        {
            if (!FiniteNumber(v.At(i), &d) || d > 3.4028234e38 || d < -3.4028234e38)
            {
                *why = "an array of 16 finite numbers";
                return false;
            }
            out.WriteF32(static_cast<f32>(d));
        }
        return true;
    }
    case FieldKind::Aabb:
    {
        const Value* lo = v.Kind() == ValueKind::Map ? v.Find("min") : nullptr;
        const Value* hi = v.Kind() == ValueKind::Map ? v.Find("max") : nullptr;
        if (lo == nullptr || hi == nullptr || lo->Kind() != ValueKind::Vec3 || hi->Kind() != ValueKind::Vec3)
        {
            *why = "a table with `min` and `max` vec3";
            return false;
        }
        const Vec3 a = lo->AsVec3();
        const Vec3 b = hi->AsVec3();
        const f32 lanes[6] = {a.x, a.y, a.z, b.x, b.y, b.z};
        if (!FiniteVec(lanes, 6))
        {
            *why = "finite numbers";
            return false;
        }
        for (f32 l : lanes)
        {
            out.WriteF32(l);
        }
        return true;
    }
    case FieldKind::String:
    {
        if (v.Kind() != ValueKind::Text)
        {
            *why = "text";
            return false;
        }
        const StringView text = v.Text();
        if (text.Length() > f.options.max_bytes)
        {
            *why = "shorter text";
            return false;
        }
        out.WriteVarint(text.Length());
        out.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length()));
        return true;
    }
    case FieldKind::Array:
    {
        if (v.Kind() != ValueKind::Array || v.Size() != f.element_count)
        {
            *why = "an array of the field's length";
            return false;
        }
        FieldDesc element = f;
        element.kind = f.element_kind;
        element.memory_size = f.element_size;
        out.WriteVarint(v.Size());
        for (usize i = 0; i < v.Size(); ++i)
        {
            if (!WriteFieldValue(element, v.At(i), out, why))
            {
                return false;
            }
        }
        return true;
    }
    default: *why = "a value scripts cannot write"; return false;
    }
}

// Builds the new record of `rc`: the fields named in `changes` come from the script (checked), the rest are copied from
// `old_record`. A key that names no field is an error, so a misspelled field never silently does nothing.
bool BuildRecord(const RegisteredComponent& rc, ByteSpan old_record, const Value& changes, bytes::ByteWriter* out)
{
    if (changes.Kind() == ValueKind::Map)
    {
        for (usize i = 0; i < changes.Size(); ++i)
        {
            bool known = false;
            for (const FieldDesc& f : rc.Fields())
            {
                known = known || (StringView(f.name) == changes.KeyAt(i) && Exposed(f));
            }
            if (!known)
            {
                Raise("the component `", StringView(rc.Name()), "` has no field `", changes.KeyAt(i), "`");
                return false;
            }
        }
    }
    else if (changes.Kind() != ValueKind::Nil)
    {
        Raise("the fields of `", StringView(rc.Name()), "` must be a table");
        return false;
    }
    bytes::ByteReader old(old_record);
    for (const FieldDesc& f : rc.Fields())
    {
        const usize start = old.Position();
        if (storage::SkipField(f, old, storage::CodecLimits()).Failed())
        {
            Raise("the record of `", StringView(rc.Name()), "` is damaged");
            return false;
        }
        const Value* wanted = changes.Kind() == ValueKind::Map ? changes.Find(StringView(f.name)) : nullptr;
        if (wanted == nullptr || wanted->IsNil())
        {
            out->WriteBytes(old_record.Subspan(start, old.Position() - start));
            continue;
        }
        const char* why = "";
        if (!WriteFieldValue(f, *wanted, *out, &why))
        {
            Raise("the field `", StringView(f.name), "` of `", StringView(rc.Name()), "` must be ", StringView(why));
            return false;
        }
    }
    return true;
}

// Whether a record decodes into a real component (enum ranges, UTF-8, ...): the check that the merge would pass; then
// the component's own validator (a target above `script` registers one), whose reason goes to `*why`.
bool Decodes(const PartitionContext& context, const RegisteredComponent& rc, ByteSpan record, const char** why)
{
    *why = nullptr;
    if (rc.IsTag())
    {
        return record.Size() == 0;
    }
    Scratch scratch(rc.EcsId());
    if (!scratch.Ready())
    {
        return false;
    }
    bytes::ByteReader in(record);
    if (!rc.Decode(in, scratch.Memory(), storage::CodecLimits()).Ok() || in.Remaining() != 0)
    {
        return false;
    }
    const ComponentValidator validator = context.components->ValidatorFor(rc.Id());
    if (validator != nullptr)
    {
        *why = validator(scratch.Memory());
    }
    return *why == nullptr;
}

// ------------------------------------------------------------------ reading what the script sees

enum class Presence : u8
{
    Absent,
    Present,
};

// The record of `rc` on `entity` as this script sees it: the script's own write, else the tick-start world.
Presence ReadRecord(const PartitionContext& context, ecs::Entity entity, const RegisteredComponent& rc,
                    Vector<u8>* record)
{
    record->Clear();
    const ecs::ComponentId id = rc.EcsId();
    if (context.overlay != nullptr)
    {
        const Value* own = context.overlay->Find(entity, kFieldComponentBase + id);
        if (own != nullptr)
        {
            if (own->Kind() == ValueKind::Bytes)
            {
                const Span<const u8> bytes = own->Bytes();
                for (usize i = 0; i < bytes.Size(); ++i)
                {
                    record->Push(bytes[i]);
                }
                return Presence::Present;
            }
            return Presence::Absent;
        }
    }
    if (entity.IsPlaceholder() || !context.world->HasById(entity, id))
    {
        return Presence::Absent;
    }
    if (!rc.IsTag())
    {
        const void* cell = context.world->CellOf(entity, id);
        if (cell == nullptr)
        {
            return Presence::Absent;
        }
        bytes::ByteWriter out;
        if (rc.Encode(cell, out, storage::CodecLimits()).Failed())
        {
            return Presence::Absent;
        }
        const ByteSpan encoded = out.Bytes();
        for (usize i = 0; i < encoded.Size(); ++i)
        {
            record->Push(static_cast<u8>(encoded[i]));
        }
    }
    return Presence::Present;
}

// The record a new component starts with: the default-constructed object, encoded.
bool DefaultRecord(const RegisteredComponent& rc, Vector<u8>* record)
{
    record->Clear();
    if (rc.IsTag())
    {
        return true;
    }
    Scratch scratch(rc.EcsId());
    if (!scratch.Ready())
    {
        return false;
    }
    bytes::ByteWriter out;
    if (rc.Encode(scratch.Memory(), out, storage::CodecLimits()).Failed())
    {
        return false;
    }
    const ByteSpan encoded = out.Bytes();
    for (usize i = 0; i < encoded.Size(); ++i)
    {
        record->Push(static_cast<u8>(encoded[i]));
    }
    return true;
}

bool RecordCommand(PartitionContext* context, u16 kind, ecs::Entity target, u32 stored_id, const u8* record,
                   usize record_size)
{
    if (context->commands == nullptr)
    {
        RaiseScriptError("this script cannot change the world (no command buffer)");
        return false;
    }
    Vector<u8> payload;
    payload.Reserve(4 + record_size);
    for (u32 i = 0; i < 4; ++i)
    {
        payload.Push(static_cast<u8>(stored_id >> (8 * i)));
    }
    for (usize i = 0; i < record_size; ++i)
    {
        payload.Push(record[i]);
    }
    if (!context->commands->Record(kind, context->current_entity_key, target, payload.Data(),
                                   static_cast<u32>(payload.Size())))
    {
        RaiseScriptError("the command buffer is full");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ bindings

bool ComponentExists(StringView name)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return false;
    }
    const RegisteredComponent* rc = context->components->FindByName(name);
    return rc != nullptr && rc->IsStored();
}

bool ComponentHas(ecs::Entity e, StringView name)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return false;
    }
    const RegisteredComponent* rc = Resolve(*context, name);
    if (rc == nullptr)
    {
        return false;
    }
    Vector<u8> record;
    return ReadRecord(*context, e, *rc, &record) == Presence::Present;
}

Value ComponentGet(ecs::Entity e, StringView name)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return Value();
    }
    const RegisteredComponent* rc = Resolve(*context, name);
    if (rc == nullptr)
    {
        return Value();
    }
    Vector<u8> record;
    if (ReadRecord(*context, e, *rc, &record) != Presence::Present)
    {
        return Value();
    }
    Value table;
    if (!RecordToTable(*rc, ByteSpan(reinterpret_cast<const byte*>(record.Data()), record.Size()), &table))
    {
        Raise("the component `", name, "` cannot be shown to scripts");
        return Value();
    }
    return table;
}

// Shared by set and add: builds, checks and records the new record, and remembers it as this script's own write.
void WriteComponent(PartitionContext* context, ecs::Entity e, const RegisteredComponent& rc, ByteSpan base,
                    const Value& changes, u16 kind)
{
    bytes::ByteWriter out;
    if (!BuildRecord(rc, base, changes, &out))
    {
        return;
    }
    const ByteSpan record = out.Bytes();
    const char* why = nullptr;
    if (!Decodes(*context, rc, record, &why))
    {
        if (why != nullptr)
        {
            Raise("the values given for `", StringView(rc.Name()), "` are not valid: ", StringView(why));
        }
        else
        {
            Raise("the values given for `", StringView(rc.Name()), "` are not valid for it");
        }
        return;
    }
    if (!RecordCommand(context, kind, e, rc.Id(), reinterpret_cast<const u8*>(record.Data()), record.Size()))
    {
        return;
    }
    if (context->overlay != nullptr)
    {
        context->overlay->Put(
            e, kFieldComponentBase + rc.EcsId(),
            Value::FromBytes(Span<const u8>(reinterpret_cast<const u8*>(record.Data()), record.Size())));
    }
}

void ComponentSet(ecs::Entity e, StringView name, Value fields)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return;
    }
    const RegisteredComponent* rc = Resolve(*context, name);
    if (rc == nullptr)
    {
        return;
    }
    if (!Writable(*rc))
    {
        Raise("the component `", name, "` belongs to the engine; use its own functions");
        return;
    }
    Vector<u8> base;
    if (ReadRecord(*context, e, *rc, &base) != Presence::Present)
    {
        Raise("the entity has no `", name, "` (use component.add)");
        return;
    }
    WriteComponent(context, e, *rc, ByteSpan(reinterpret_cast<const byte*>(base.Data()), base.Size()), fields,
                   kCommandComponentSet);
}

void ComponentAdd(ecs::Entity e, StringView name, Value fields)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return;
    }
    const RegisteredComponent* rc = Resolve(*context, name);
    if (rc == nullptr)
    {
        return;
    }
    if (!Writable(*rc))
    {
        Raise("the component `", name, "` belongs to the engine; use its own functions");
        return;
    }
    Vector<u8> base;
    if (ReadRecord(*context, e, *rc, &base) == Presence::Present)
    {
        Raise("the entity has `", name, "` already (use component.set)");
        return;
    }
    if (!DefaultRecord(*rc, &base))
    {
        Raise("the component `", name, "` cannot be created by scripts (it has no default)");
        return;
    }
    WriteComponent(context, e, *rc, ByteSpan(reinterpret_cast<const byte*>(base.Data()), base.Size()), fields,
                   kCommandComponentAdd);
}

bool ComponentRemove(ecs::Entity e, StringView name)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return false;
    }
    const RegisteredComponent* rc = Resolve(*context, name);
    if (rc == nullptr)
    {
        return false;
    }
    if (!Writable(*rc))
    {
        Raise("the component `", name, "` belongs to the engine; use its own functions");
        return false;
    }
    Vector<u8> record;
    if (ReadRecord(*context, e, *rc, &record) != Presence::Present)
    {
        return false;
    }
    if (!RecordCommand(context, kCommandComponentRemove, e, rc->Id(), nullptr, 0))
    {
        return false;
    }
    if (context->overlay != nullptr)
    {
        context->overlay->Put(e, kFieldComponentBase + rc->EcsId(), Value::FromBool(false));
    }
    return true;
}

void ComponentDefine(StringView name, Value fields)
{
    PartitionContext* context = ComponentContext();
    if (context == nullptr)
    {
        return;
    }
    String<HeapAllocator> error(HeapAllocator::Default());
    if (!context->components->Define(name, fields, &error))
    {
        Raise("component `", name, "`: ", error.View());
    }
}

// ------------------------------------------------------------------ queries

bool ResolveSet(const PartitionContext& context, const Value& names, ecs::ComponentSet* set)
{
    if (names.Kind() != ValueKind::Array || names.Size() == 0 || names.Size() > 8)
    {
        RaiseScriptError("a query lists one to eight component names");
        return false;
    }
    for (usize i = 0; i < names.Size(); ++i)
    {
        if (names.At(i).Kind() != ValueKind::Text)
        {
            RaiseScriptError("a query lists component names (text)");
            return false;
        }
        const RegisteredComponent* rc = context.components->FindByName(names.At(i).Text());
        if (rc == nullptr || rc->EcsId() == ecs::kInvalidComponent)
        {
            Raise("no component named `", names.At(i).Text(), "`");
            return false;
        }
        set->Add(rc->EcsId());
    }
    return true;
}

// The entities that have every component of `set`, with a stable id above `after`, in stable id order, at most `limit`;
// `partition_only` keeps those the calling partition owns.
Value RunQuery(const PartitionContext& context, const ecs::ComponentSet& set, u64 after, usize limit,
               bool partition_only)
{
    const u32 partition = context.commands != nullptr ? context.commands->Partition() : 0;
    Vector<u64> ids;
    for (u32 a = 0; a < context.world->ArchetypeCount(); ++a)
    {
        const ecs::Archetype& archetype = context.world->ArchetypeAt(a);
        if (!set.IsSubsetOf(archetype.Components()))
        {
            continue;
        }
        for (u32 c = 0; c < archetype.ChunkCount(); ++c)
        {
            const Span<ecs::Entity> entities = archetype.Entities(archetype.ChunkAt(c));
            for (usize r = 0; r < entities.Size(); ++r)
            {
                const u64 id = entities[r].ToU64();
                if (id > after && (!partition_only || PartitionOf(id, context.partitions) == partition))
                {
                    ids.Push(id);
                }
            }
        }
    }
    // Only the first `limit` (the page) are returned: select and sort those (container audit Day 9: every match was
    // sorted for every page). The ids are unique, so the page is the one a full sort gives.
    PartialSort(Span<u64>(ids.Data(), ids.Size()), limit, [](u64 x, u64 y) { return x < y; });
    Value out = Value::MakeArray();
    const usize count = ids.Size() < limit ? ids.Size() : limit;
    for (usize i = 0; i < count; ++i)
    {
        if (!out.Push(Value::FromEntity(ecs::Entity::FromU64(ids[i]))))
        {
            break;
        }
    }
    return out;
}

Value QueryEach(Value names)
{
    PartitionContext* context = ComponentContext();
    ecs::ComponentSet set;
    if (context == nullptr || !ResolveSet(*context, names, &set))
    {
        return Value();
    }
    Value out = RunQuery(*context, set, 0, kMaxQueryResults + 1, true);
    if (out.Size() > kMaxQueryResults)
    {
        RaiseScriptError("the partition has too many matching entities for one call; use query.page");
        return Value();
    }
    return out;
}

Value QueryAll(Value names)
{
    PartitionContext* context = ComponentContext();
    ecs::ComponentSet set;
    if (context == nullptr || !ResolveSet(*context, names, &set))
    {
        return Value();
    }
    Value out = RunQuery(*context, set, 0, kMaxQueryResults + 1, false);
    if (out.Size() > kMaxQueryResults)
    {
        RaiseScriptError("too many matching entities for one call; use query.page");
        return Value();
    }
    return out;
}

Value QueryPage(Value names, Value after, i64 limit)
{
    PartitionContext* context = ComponentContext();
    ecs::ComponentSet set;
    if (context == nullptr || !ResolveSet(*context, names, &set))
    {
        return Value();
    }
    u64 start = 0;
    if (after.Kind() == ValueKind::Entity)
    {
        start = after.AsEntity().ToU64();
    }
    else if (!after.IsNil())
    {
        RaiseScriptError("`after` is an entity or nil");
        return Value();
    }
    if (limit < 1 || limit > static_cast<i64>(kMaxQueryResults))
    {
        RaiseScriptError("the page size is from 1 to 60000");
        return Value();
    }
    return RunQuery(*context, set, start, static_cast<usize>(limit), false);
}

// ------------------------------------------------------------------ merge

bool ReadStoredId(Span<const u8> payload, u32* id)
{
    if (payload.Size() < 4)
    {
        return false;
    }
    *id = static_cast<u32>(payload[0]) | (static_cast<u32>(payload[1]) << 8) | (static_cast<u32>(payload[2]) << 16) |
          (static_cast<u32>(payload[3]) << 24);
    return true;
}

const RegisteredComponent* MergeComponent(MergeContext& context, Span<const u8> payload)
{
    const MergeServices* services = static_cast<const MergeServices*>(context.User());
    u32 id = 0;
    if (services == nullptr || services->components == nullptr || !ReadStoredId(payload, &id))
    {
        return nullptr;
    }
    const RegisteredComponent* rc = services->components->FindById(id);
    // The same rules as the bindings: a command that names an engine component is never applied.
    return rc != nullptr && rc->IsStored() && rc->EcsId() != ecs::kInvalidComponent && Writable(*rc) ? rc : nullptr;
}

// Replaces the live component with the decoded record; the record was checked when the script recorded it, but the
// decode runs on a scratch object first so a record that fails now leaves the world as it was.
bool StoreRecord(MergeContext& context, ecs::Entity target, const RegisteredComponent& rc, Span<const u8> record)
{
    if (rc.IsTag())
    {
        return true;
    }
    Scratch scratch(rc.EcsId());
    if (!scratch.Ready())
    {
        return false;
    }
    bytes::ByteReader in(ByteSpan(reinterpret_cast<const byte*>(record.Data()), record.Size()));
    if (rc.Decode(in, scratch.Memory(), storage::CodecLimits()).Failed() || in.Remaining() != 0)
    {
        return false;
    }
    void* cell = context.World()->CellOf(target, rc.EcsId());
    if (cell == nullptr)
    {
        return false;
    }
    scratch.Info().destroy(cell);
    scratch.Info().move_construct(cell, scratch.Memory());
    return true;
}

bool ApplyComponentSet(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    const RegisteredComponent* rc = MergeComponent(context, payload);
    if (rc == nullptr || !context.World()->HasById(target, rc->EcsId()))
    {
        return false;
    }
    return StoreRecord(context, target, *rc, Span<const u8>(payload.Data() + 4, payload.Size() - 4));
}

bool ApplyComponentAdd(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    const RegisteredComponent* rc = MergeComponent(context, payload);
    if (rc == nullptr)
    {
        return false;
    }
    if (!context.World()->HasById(target, rc->EcsId()))
    {
        if (context.World()->AddDefault(target, rc->EcsId()) != ecs::Status::Ok)
        {
            return false;
        }
    }
    return StoreRecord(context, target, *rc, Span<const u8>(payload.Data() + 4, payload.Size() - 4));
}

bool ApplyComponentRemove(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    const RegisteredComponent* rc = MergeComponent(context, payload);
    return rc != nullptr && context.World()->RemoveById(target, rc->EcsId()) == ecs::Status::Ok;
}

}  // namespace

bool RegisterComponentCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandComponentSet, CommandKind{"component.set", &ApplyComponentSet, false, true});
    ok &= kinds->Register(kCommandComponentAdd, CommandKind{"component.add", &ApplyComponentAdd, false, true});
    ok &= kinds->Register(kCommandComponentRemove, CommandKind{"component.remove", &ApplyComponentRemove, false, true});
    return ok;
}

bool RegisterComponentBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&ComponentExists>("component", "exists", ThreadRule::WorldRead, "name",
                                          "Returns whether a stored component of this name is registered.");
    ok &= registry->Add<&ComponentHas>("component", "has", ThreadRule::WorldRead, "e, name",
                                       "Returns whether the entity has the component, as this script sees it (its "
                                       "own adds and removes this tick included).");
    ok &= registry->Add<&ComponentGet>(
        "component", "get", ThreadRule::WorldRead, "e, name",
        "Returns a table with a copy of the component's fields (floats as numbers, vectors as vec2/3/4, "
        "quaternions as quat, enums and resource ids as integers), or nil when the entity lacks it. Writing to the "
        "table changes nothing: use component.set.");
    ok &= registry->Add<&ComponentSet>(
        "component", "set", ThreadRule::WorldWrite, "e, name, fields",
        "Changes the fields named in `fields` of a component the entity has, at the sync point; every value is "
        "checked against its field's type and range, and an unknown field, a wrong type or an engine component is "
        "an error naming the component and the field. This script sees the new values at once.");
    ok &= registry->Add<&ComponentAdd>(
        "component", "add", ThreadRule::WorldWrite, "e, name, fields",
        "Adds the component at the sync point, starting from its defaults and then the values in `fields` (nil for "
        "none); an entity that has it already is an error (use component.set).");
    ok &= registry->Add<&ComponentRemove>("component", "remove", ThreadRule::WorldWrite, "e, name",
                                          "Removes the component at the sync point; returns whether the entity had "
                                          "it.");
    ok &= registry->Add<&ComponentDefine>(
        "component", "define", ThreadRule::WorldWrite, "name, fields",
        "Defines a plain-data component `name` with `fields`, a table of field name to type name (f32, f64, i32, "
        "i64, u32, bool, vec2, vec3, vec4, quat), while scripts load; it is registered with the engine and saved "
        "like any other. Defining it again the same way is fine, differently or after the first tick an error.");
    ok &= registry->Add<&QueryEach>(
        "query", "each", ThreadRule::WorldRead, "names",
        "Returns the entities of this partition that have every component in `names` (a list of up to eight), in "
        "stable id order, as of the tick start. The scripts of one entity's partition see the same entities on any "
        "thread count.");
    ok &= registry->Add<&QueryAll>(
        "query", "all", ThreadRule::WorldRead, "names",
        "Returns every entity that has all the components in `names`, in stable id order, as of the tick start: "
        "the expensive call (it walks the whole world); up to 60000, else an error (use query.page).");
    ok &= registry->Add<&QueryPage>(
        "query", "page", ThreadRule::WorldRead, "names, after, limit",
        "Like query.all but returns at most `limit` entities (1 to 60000) with a stable id above `after` (an entity "
        "from the previous page, or nil for the first), so a world of any size can be walked in pages.");
    return ok;
}

}  // namespace vse::script
