#include "script/component_catalog.h"

#include "cstr/mem_ops.h"
#include "ecs/component.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "sync/mutex.h"

namespace engine_placeholder::script
{

namespace
{

struct FieldType
{
    const char* name;
    storage::FieldKind kind;
    usize size;
    usize align;
};

const FieldType kFieldTypes[] = {
    {"f32", storage::FieldKind::F32, 4, 4},
    {"f64", storage::FieldKind::F64, 8, 8},
    {"i32", storage::FieldKind::I32, 4, 4},
    {"i64", storage::FieldKind::I64, 8, 8},
    {"u32", storage::FieldKind::U32, 4, 4},
    {"bool", storage::FieldKind::Bool, 1, 1},
    {"vec2", storage::FieldKind::Vec2, sizeof(Vec2), alignof(Vec2)},
    {"vec3", storage::FieldKind::Vec3, sizeof(Vec3), alignof(Vec3)},
    {"vec4", storage::FieldKind::Vec4, sizeof(Vec4), alignof(Vec4)},
    {"quat", storage::FieldKind::Quat, sizeof(Quaternion), alignof(Quaternion)},
};

const FieldType* FindFieldType(StringView name)
{
    for (const FieldType& t : kFieldTypes)
    {
        if (name == StringView(t.name))
        {
            return &t;
        }
    }
    return nullptr;
}

bool IsIdentifier(StringView text, usize max_length)
{
    if (text.Length() == 0 || text.Length() > max_length)
    {
        return false;
    }
    for (usize i = 0; i < text.Length(); ++i)
    {
        const char c = text[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !(digit && i != 0))
        {
            return false;
        }
    }
    return true;
}

u32 NameHash(StringView name)
{
    u32 hash = 2166136261u;
    for (usize i = 0; i < name.Length(); ++i)
    {
        hash ^= static_cast<u8>(name[i]);
        hash *= 16777619u;
    }
    return hash | 0x80000000u;  // above every engine id, and never 0
}

template <class T> void ReadAt(const void* component, usize offset, T* out)
{
    MemCopy(out, static_cast<const u8*>(component) + offset, sizeof(T));
}
template <class T> void WriteAt(void* component, usize offset, const T& value)
{
    MemCopy(static_cast<u8*>(component) + offset, &value, sizeof(T));
}

storage::Status EncodeField(const storage::FieldDesc& desc, const void* component, bytes::ByteWriter& out,
                            const storage::CodecLimits&)
{
    const usize at = desc.memory_offset;
    switch (desc.kind)
    {
    case storage::FieldKind::F32:
    {
        f32 v;
        ReadAt(component, at, &v);
        out.WriteF32(v);
        break;
    }
    case storage::FieldKind::F64:
    {
        f64 v;
        ReadAt(component, at, &v);
        out.WriteF64(v);
        break;
    }
    case storage::FieldKind::I32:
    {
        i32 v;
        ReadAt(component, at, &v);
        out.WriteI32(v);
        break;
    }
    case storage::FieldKind::I64:
    {
        i64 v;
        ReadAt(component, at, &v);
        out.WriteI64(v);
        break;
    }
    case storage::FieldKind::U32:
    {
        u32 v;
        ReadAt(component, at, &v);
        out.WriteU32(v);
        break;
    }
    case storage::FieldKind::Bool:
    {
        bool v;
        ReadAt(component, at, &v);
        out.WriteBool(v);
        break;
    }
    case storage::FieldKind::Vec2:
    {
        Vec2 v;
        ReadAt(component, at, &v);
        out.WriteF32(v.x);
        out.WriteF32(v.y);
        break;
    }
    case storage::FieldKind::Vec3:
    {
        Vec3 v;
        ReadAt(component, at, &v);
        storage::detail::EncodeVec3(v, out);
        break;
    }
    case storage::FieldKind::Vec4:
    {
        Vec4 v;
        ReadAt(component, at, &v);
        storage::detail::EncodeVec4(v, out);
        break;
    }
    case storage::FieldKind::Quat:
    {
        Quaternion v;
        ReadAt(component, at, &v);
        out.WriteF32(v.x);
        out.WriteF32(v.y);
        out.WriteF32(v.z);
        out.WriteF32(v.w);
        break;
    }
    default: return storage::Status::Make(storage::ErrorCode::Invalid, "a script component field has no stored form");
    }
    return storage::Status::Success();
}

storage::Status DecodeField(const storage::FieldDesc& desc, bytes::ByteReader& in, void* component,
                            const storage::CodecLimits&)
{
    const usize at = desc.memory_offset;
    bytes::ByteError e = bytes::ByteError::Ok;
    switch (desc.kind)
    {
    case storage::FieldKind::F32:
    {
        f32 v = 0;
        e = in.ReadF32(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::F64:
    {
        f64 v = 0;
        e = in.ReadF64(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::I32:
    {
        i32 v = 0;
        e = in.ReadI32(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::I64:
    {
        i64 v = 0;
        e = in.ReadI64(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::U32:
    {
        u32 v = 0;
        e = in.ReadU32(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::Bool:
    {
        bool v = false;
        e = in.ReadBool(&v);
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::Vec2:
    {
        Vec2 v;
        e = in.ReadF32(&v.x);
        if (e == bytes::ByteError::Ok)
        {
            e = in.ReadF32(&v.y);
        }
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::Vec3:
    {
        Vec3 v;
        const storage::Status s = storage::detail::DecodeVec3(&v, in);
        if (s.Failed())
        {
            return s;
        }
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::Vec4:
    {
        Vec4 v;
        const storage::Status s = storage::detail::DecodeVec4(&v, in);
        if (s.Failed())
        {
            return s;
        }
        WriteAt(component, at, v);
        break;
    }
    case storage::FieldKind::Quat:
    {
        Quaternion v;
        e = in.ReadF32(&v.x);
        if (e == bytes::ByteError::Ok)
        {
            e = in.ReadF32(&v.y);
        }
        if (e == bytes::ByteError::Ok)
        {
            e = in.ReadF32(&v.z);
        }
        if (e == bytes::ByteError::Ok)
        {
            e = in.ReadF32(&v.w);
        }
        WriteAt(component, at, v);
        break;
    }
    default: return storage::Status::Make(storage::ErrorCode::Invalid, "a script component field has no stored form");
    }
    return storage::detail::FromByteError(e);
}

// The type-erased constructors of the ECS take no size, so a script component is padded to one of a few sizes and each
// size has its functions.
template <usize N> void ZeroConstruct(void* where)
{
    MemSet(where, 0, N);
}
template <usize N> void CopyConstruct(void* destination, void* source)
{
    MemCopy(destination, source, N);
}
void NoDestroy(void*) {}

struct SizeClass
{
    usize bytes;
    void (*construct)(void*);
    void (*move)(void*, void*);
};

const SizeClass kSizeClasses[] = {
    {16, &ZeroConstruct<16>, &CopyConstruct<16>},    {32, &ZeroConstruct<32>, &CopyConstruct<32>},
    {64, &ZeroConstruct<64>, &CopyConstruct<64>},    {128, &ZeroConstruct<128>, &CopyConstruct<128>},
    {256, &ZeroConstruct<256>, &CopyConstruct<256>},
};

bool Less(StringView a, StringView b)
{
    return a < b;
}

}  // namespace

struct ComponentCatalog::Definition
{
    String<HeapAllocator> name{HeapAllocator::Default()};
    Vector<String<HeapAllocator>> field_names;  // kept for the identical-definition check and the descriptions
    Vector<storage::FieldDesc> fields;
    usize bytes = 0;
    u32 stored_id = 0;
    ecs::ComponentId ecs_id = ecs::kInvalidComponent;
};

ComponentCatalog::ComponentCatalog(storage::ComponentRegistry* registry) : registry_(registry) {}

ComponentCatalog::~ComponentCatalog() = default;

const storage::RegisteredComponent* ComponentCatalog::FindByName(StringView name) const
{
    if (IsFrozen())
    {
        return registry_->FindByName(name);
    }
    LockGuard<Mutex> guard(lock_);
    return registry_->FindByName(name);
}

const storage::RegisteredComponent* ComponentCatalog::FindById(u32 id) const
{
    if (IsFrozen())
    {
        return registry_->FindById(id);
    }
    LockGuard<Mutex> guard(lock_);
    return registry_->FindById(id);
}

const storage::RegisteredComponent* ComponentCatalog::FindByEcsId(ecs::ComponentId id) const
{
    if (IsFrozen())
    {
        return registry_->FindByEcsId(id);
    }
    LockGuard<Mutex> guard(lock_);
    return registry_->FindByEcsId(id);
}

usize ComponentCatalog::DefinedCount() const
{
    LockGuard<Mutex> guard(lock_);
    return definitions_.Size();
}

namespace
{

void Fail(String<HeapAllocator>* error, StringView text)
{
    if (error != nullptr)
    {
        *error = String<HeapAllocator>(HeapAllocator::Default(), text);
    }
}

}  // namespace

bool ComponentCatalog::Define(StringView name, const Value& fields, String<HeapAllocator>* error)
{
    if (!IsIdentifier(name, 48))
    {
        Fail(error, "a component name is letters, digits and `_`, not starting with a digit, at most 48 bytes");
        return false;
    }
    if (fields.Kind() != ValueKind::Map)
    {
        Fail(error, "the fields must be a table of field name to type name");
        return false;
    }
    if (fields.Size() > kMaxScriptComponentFields)
    {
        Fail(error, "a component has at most 32 fields");
        return false;
    }
    // The fields in name order: the layout never depends on the order the script's table was walked in.
    struct Wanted
    {
        StringView name;
        const FieldType* type = nullptr;
    };
    Wanted wanted[kMaxScriptComponentFields];
    usize count = 0;
    for (usize i = 0; i < fields.Size(); ++i)
    {
        const StringView field_name = fields.KeyAt(i);
        const Value& type_name = fields.At(i);
        if (!IsIdentifier(field_name, 32))
        {
            Fail(error, "a field name is letters, digits and `_`, not starting with a digit, at most 32 bytes");
            return false;
        }
        const FieldType* type = type_name.Kind() == ValueKind::Text ? FindFieldType(type_name.Text()) : nullptr;
        if (type == nullptr)
        {
            String<HeapAllocator> text(HeapAllocator::Default());
            text.Append("the type of field `");
            text.Append(field_name);
            text.Append("` must be one of f32, f64, i32, i64, u32, bool, vec2, vec3, vec4, quat");
            Fail(error, text.View());
            return false;
        }
        usize at = count;
        while (at > 0 && Less(field_name, wanted[at - 1].name))
        {
            wanted[at] = wanted[at - 1];
            --at;
        }
        wanted[at] = Wanted{field_name, type};
        ++count;
    }
    for (usize i = 1; i < count; ++i)
    {
        if (wanted[i].name == wanted[i - 1].name)
        {
            Fail(error, "two fields have one name");
            return false;
        }
    }

    LockGuard<Mutex> guard(lock_);
    for (usize di = 0; di < definitions_.Size(); ++di)
    {
        const UniquePtr<Definition>& d = definitions_[di];
        if (d->name.View() != name)
        {
            continue;
        }
        bool same = d->field_names.Size() == count;
        for (usize i = 0; same && i < count; ++i)
        {
            same = d->field_names[i].View() == wanted[i].name && d->fields[i].kind == wanted[i].type->kind;
        }
        if (!same)
        {
            Fail(error, "the component is defined already with other fields");
        }
        return same;
    }
    if (IsFrozen())
    {
        Fail(error, "components are defined while scripts load, before the first tick");
        return false;
    }
    if (registry_->FindByName(name) != nullptr)
    {
        Fail(error, "the name belongs to an engine component");
        return false;
    }
    const u32 stored_id = NameHash(name);
    if (registry_->FindById(stored_id) != nullptr)
    {
        Fail(error, "the name collides with another component's stored id; rename the component");
        return false;
    }

    UniquePtr<Definition> def = MakeUnique<Definition>();
    def->name.Append(name);
    def->stored_id = stored_id;
    usize offset = 0;
    usize align = 1;
    for (usize i = 0; i < count; ++i)
    {
        const FieldType& t = *wanted[i].type;
        offset = (offset + t.align - 1) / t.align * t.align;
        String<HeapAllocator> field_name(HeapAllocator::Default());
        field_name.Append(wanted[i].name);
        def->field_names.Push(Move(field_name));
        offset += 0;
        storage::FieldDesc f;
        f.name = nullptr;  // set below, after the names stop moving
        f.kind = t.kind;
        f.memory_size = t.size;
        f.memory_offset = offset;
        f.encode = &EncodeField;
        f.decode = &DecodeField;
        def->fields.Push(f);
        offset += t.size;
        align = t.align > align ? t.align : align;
    }
    for (usize i = 0; i < count; ++i)
    {
        def->fields[i].name = def->field_names[i].CStr();
    }
    const bool is_tag = count == 0;
    if (!is_tag)
    {
        offset = (offset + align - 1) / align * align;
        const SizeClass* size_class = nullptr;
        for (const SizeClass& c : kSizeClasses)
        {
            if (offset <= c.bytes)
            {
                size_class = &c;
                break;
            }
        }
        if (size_class == nullptr)
        {
            Fail(error, "a component is at most 256 bytes");
            return false;
        }
        def->bytes = size_class->bytes;
        ecs::TypeInfo info;
        info.size = size_class->bytes;
        info.align = align;
        info.is_tag = false;
        info.trivially_relocatable = true;
        info.move_construct = size_class->move;
        info.destroy = &NoDestroy;
        info.default_construct = size_class->construct;
        info.name = def->name.CStr();
        def->ecs_id = ecs::ComponentRegistry::Global().Register(info);
    }
    else
    {
        ecs::TypeInfo info;
        info.is_tag = true;
        info.trivially_relocatable = true;
        info.move_construct = &CopyConstruct<0>;
        info.destroy = &NoDestroy;
        info.default_construct = &ZeroConstruct<0>;
        info.name = def->name.CStr();
        def->ecs_id = ecs::ComponentRegistry::Global().Register(info);
    }
    if (def->ecs_id == ecs::kInvalidComponent)
    {
        Fail(error, "the engine has no room for another component type (128)");
        return false;
    }
    storage::ComponentDesc desc;
    desc.id = stored_id;
    desc.name = def->name.CStr();
    desc.version = 1;
    desc.oldest_version = 1;
    desc.flags = storage::kComponentStored;
    const storage::Status s = registry_->RegisterDynamic(
        desc, Span<const storage::FieldDesc>(def->fields.Data(), def->fields.Size()), def->ecs_id, is_tag);
    if (s.Failed())
    {
        Fail(error, StringView(s.Message()));
        return false;
    }
    definitions_.Push(Move(def));
    return true;
}

}  // namespace engine_placeholder::script
