#include "script/value.h"
#include "containers/hash.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::script
{

namespace
{

// FNV-1a over a canonical byte encoding: integers little-endian, floats by bit pattern, so the hash is the same on
// every machine.
class Hasher
{
public:
    void Byte(u8 b) { h_.AddByte(b); }
    void U64(u64 v) { h_.AddU64Bytes(v); }
    void F32(f32 v)
    {
        u32 bits = 0;
        MemCopy(&bits, &v, sizeof bits);
        U64(bits);
    }
    void F64(f64 v)
    {
        u64 bits = 0;
        MemCopy(&bits, &v, sizeof bits);
        U64(bits);
    }
    void Bytes(const char* p, usize n)
    {
        U64(n);
        for (usize i = 0; i < n; ++i)
        {
            Byte(static_cast<u8>(p[i]));
        }
    }
    u64 Result() const { return h_.Value(); }

private:
    Fnv1a64Builder h_{kFnv1a64LegacyBasis};
};

bool SameBits(f32 a, f32 b)
{
    u32 x = 0;
    u32 y = 0;
    MemCopy(&x, &a, sizeof x);
    MemCopy(&y, &b, sizeof y);
    return x == y;
}

bool SameBits(f64 a, f64 b)
{
    u64 x = 0;
    u64 y = 0;
    MemCopy(&x, &a, sizeof x);
    MemCopy(&y, &b, sizeof y);
    return x == y;
}

}  // namespace

Value::Value() : text_(HeapAllocator::Default()) {}

Value::Value(const Value& other) : text_(HeapAllocator::Default())
{
    CopyFrom(other);
}

Value::Value(Value&& other) noexcept
    : kind_(other.kind_), depth_(other.depth_), bool_(other.bool_), int_(other.int_), float_(other.float_),
      entity_(other.entity_), text_(Move(other.text_)), bytes_(Move(other.bytes_)), items_(Move(other.items_)),
      keys_(Move(other.keys_)), key_index_(Move(other.key_index_))
{
    for (u32 i = 0; i < 4; ++i)
    {
        lanes_[i] = other.lanes_[i];
    }
    for (u32 i = 0; i < 3; ++i)
    {
        wide_[i] = other.wide_[i];
    }
    other.Clear();
}

Value& Value::operator=(const Value& other)
{
    if (this != &other)
    {
        Value copy(other);
        *this = Move(copy);
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept
{
    if (this != &other)
    {
        kind_ = other.kind_;
        depth_ = other.depth_;
        bool_ = other.bool_;
        int_ = other.int_;
        float_ = other.float_;
        entity_ = other.entity_;
        for (u32 i = 0; i < 4; ++i)
        {
            lanes_[i] = other.lanes_[i];
        }
        for (u32 i = 0; i < 3; ++i)
        {
            wide_[i] = other.wide_[i];
        }
        text_ = Move(other.text_);
        bytes_ = Move(other.bytes_);
        items_ = Move(other.items_);
        keys_ = Move(other.keys_);
        key_index_ = Move(other.key_index_);
        other.Clear();
    }
    return *this;
}

Value::~Value() = default;

void Value::Clear()
{
    kind_ = ValueKind::Nil;
    depth_ = 1;
    bool_ = false;
    int_ = 0;
    float_ = 0.0;
    for (u32 i = 0; i < 4; ++i)
    {
        lanes_[i] = 0.0f;
    }
    for (u32 i = 0; i < 3; ++i)
    {
        wide_[i] = 0.0;
    }
    entity_ = ecs::Entity();
    text_ = String<HeapAllocator>(HeapAllocator::Default());
    bytes_ = Vector<u8>();
    items_ = Vector<Value>();
    keys_ = Vector<String<HeapAllocator>>();
    key_index_.Reset();
}

void Value::CopyFrom(const Value& other)
{
    kind_ = other.kind_;
    depth_ = other.depth_;
    bool_ = other.bool_;
    int_ = other.int_;
    float_ = other.float_;
    entity_ = other.entity_;
    for (u32 i = 0; i < 4; ++i)
    {
        lanes_[i] = other.lanes_[i];
    }
    for (u32 i = 0; i < 3; ++i)
    {
        wide_[i] = other.wide_[i];
    }
    text_ = String<HeapAllocator>(HeapAllocator::Default(), other.text_.View());
    bytes_.Append(other.bytes_.Data(), other.bytes_.Size());
    items_.Reserve(other.items_.Size());
    for (usize i = 0; i < other.items_.Size(); ++i)
    {
        items_.Push(Value(other.items_[i]));
    }
    keys_.Reserve(other.keys_.Size());
    for (usize i = 0; i < other.keys_.Size(); ++i)
    {
        keys_.Push(String<HeapAllocator>(HeapAllocator::Default(), other.keys_[i].View()));
    }
    key_index_.Reset();
    if (keys_.Size() > kIndexedKeys)
    {
        IndexKeys();
    }
}

void Value::IndexKeys()
{
    key_index_ = MakeUnique<HashMap<StringView, u32, HeapAllocator>>(HeapAllocator::Default());
    key_index_->Reserve(keys_.Size());
    for (usize i = 0; i < keys_.Size(); ++i)
    {
        key_index_->Insert(keys_[i].View(), static_cast<u32>(i));
    }
}

Value Value::FromBool(bool value)
{
    Value v;
    v.kind_ = ValueKind::Bool;
    v.bool_ = value;
    return v;
}

Value Value::FromInt(i64 value)
{
    Value v;
    v.kind_ = ValueKind::Int;
    v.int_ = value;
    return v;
}

Value Value::FromFloat(f64 value)
{
    Value v;
    v.kind_ = ValueKind::Float;
    v.float_ = value;
    return v;
}

Value Value::FromText(StringView text)
{
    Value v;
    v.kind_ = ValueKind::Text;
    v.text_ = String<HeapAllocator>(HeapAllocator::Default(), text);
    return v;
}

Value Value::FromVec2(Vec2 value)
{
    Value v;
    v.kind_ = ValueKind::Vec2;
    v.lanes_[0] = value.x;
    v.lanes_[1] = value.y;
    return v;
}

Value Value::FromVec3(Vec3 value)
{
    return FromVec3d(Vec3d(value));
}

Value Value::FromVec3d(Vec3d value)
{
    Value v;
    v.kind_ = ValueKind::Vec3;
    v.wide_[0] = value.x;
    v.wide_[1] = value.y;
    v.wide_[2] = value.z;
    return v;
}

Value Value::FromVec4(Vec4 value)
{
    Value v;
    v.kind_ = ValueKind::Vec4;
    v.lanes_[0] = value.x;
    v.lanes_[1] = value.y;
    v.lanes_[2] = value.z;
    v.lanes_[3] = value.w;
    return v;
}

Value Value::FromQuat(Quaternion value)
{
    Value v;
    v.kind_ = ValueKind::Quat;
    v.lanes_[0] = value.x;
    v.lanes_[1] = value.y;
    v.lanes_[2] = value.z;
    v.lanes_[3] = value.w;
    return v;
}

Value Value::FromEntity(ecs::Entity value)
{
    Value v;
    v.kind_ = ValueKind::Entity;
    v.entity_ = value;
    return v;
}

Value Value::FromHandle(HandleType type, u64 id)
{
    Value v;
    v.kind_ = ValueKind::Handle;
    v.entity_.index = static_cast<u32>(type);
    v.int_ = static_cast<i64>(id);
    return v;
}

Value Value::FromBytes(Span<const u8> bytes)
{
    Value v;
    v.kind_ = ValueKind::Bytes;
    v.bytes_.Reserve(bytes.Size());
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        v.bytes_.Push(bytes[i]);
    }
    return v;
}

Value Value::MakeArray()
{
    Value v;
    v.kind_ = ValueKind::Array;
    return v;
}

Value Value::MakeMap()
{
    Value v;
    v.kind_ = ValueKind::Map;
    return v;
}

bool Value::AsBool() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Bool, "script::Value::AsBool: the value is not a boolean");
    return bool_;
}

i64 Value::AsInt() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Int, "script::Value::AsInt: the value is not an integer");
    return int_;
}

f64 Value::AsFloat() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Float, "script::Value::AsFloat: the value is not a float");
    return float_;
}

f64 Value::ToDouble() const
{
    ENGINE_ASSERT(IsNumber(), "script::Value::ToDouble: the value is not a number");
    return kind_ == ValueKind::Int ? static_cast<f64>(int_) : float_;
}

StringView Value::Text() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Text, "script::Value::Text: the value is not a text");
    return text_.View();
}

Vec2 Value::AsVec2() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Vec2, "script::Value::AsVec2: the value is not a Vec2");
    return Vec2(lanes_[0], lanes_[1]);
}

Vec3 Value::AsVec3() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Vec3, "script::Value::AsVec3: the value is not a Vec3");
    return AsVec3d().Narrow();
}

Vec3d Value::AsVec3d() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Vec3, "script::Value::AsVec3d: the value is not a Vec3");
    return Vec3d(wide_[0], wide_[1], wide_[2]);
}

Vec4 Value::AsVec4() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Vec4, "script::Value::AsVec4: the value is not a Vec4");
    return Vec4(lanes_[0], lanes_[1], lanes_[2], lanes_[3]);
}

Quaternion Value::AsQuat() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Quat, "script::Value::AsQuat: the value is not a quaternion");
    Quaternion q;
    q.x = lanes_[0];
    q.y = lanes_[1];
    q.z = lanes_[2];
    q.w = lanes_[3];
    return q;
}

HandleType Value::AsHandleType() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Handle, "script::Value::AsHandleType: the value is not a handle");
    return static_cast<HandleType>(entity_.index);
}

u64 Value::AsHandleId() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Handle, "script::Value::AsHandleId: the value is not a handle");
    return static_cast<u64>(int_);
}

ecs::Entity Value::AsEntity() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Entity, "script::Value::AsEntity: the value is not an entity");
    return entity_;
}

Span<const u8> Value::Bytes() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Bytes, "script::Value::Bytes: the value is not a blob");
    return Span<const u8>(bytes_.Data(), bytes_.Size());
}

usize Value::Size() const
{
    ENGINE_ASSERT(kind_ == ValueKind::Array || kind_ == ValueKind::Map,
                  "script::Value::Size: the value is not an array or a map");
    return items_.Size();
}

const Value& Value::At(usize index) const
{
    ENGINE_ASSERT(kind_ == ValueKind::Array || kind_ == ValueKind::Map,
                  "script::Value::At: the value is not an array or a map");
    ENGINE_ASSERT(index < items_.Size(), "script::Value::At: index out of range");
    return items_[index];
}

StringView Value::KeyAt(usize index) const
{
    ENGINE_ASSERT(kind_ == ValueKind::Map, "script::Value::KeyAt: the value is not a map");
    ENGINE_ASSERT(index < keys_.Size(), "script::Value::KeyAt: index out of range");
    return keys_[index].View();
}

const Value* Value::Find(StringView key) const
{
    ENGINE_ASSERT(kind_ == ValueKind::Map, "script::Value::Find: the value is not a map");
    if (key_index_.Get() != nullptr)
    {
        const u32* at = key_index_->Find(key);
        return at != nullptr ? &items_[*at] : nullptr;
    }
    for (usize i = 0; i < keys_.Size(); ++i)
    {
        if (keys_[i].View() == key)
        {
            return &items_[i];
        }
    }
    return nullptr;
}

bool Value::Push(Value&& item)
{
    ENGINE_ASSERT(kind_ == ValueKind::Array, "script::Value::Push: the value is not an array");
    if (items_.Size() >= kMaxContainerItems || u32{item.depth_} + 1 > kMaxValueDepth)
    {
        return false;
    }
    if (item.depth_ + 1 > depth_)
    {
        depth_ = static_cast<u8>(item.depth_ + 1);
    }
    items_.Push(Move(item));
    return true;
}

bool Value::Set(StringView key, Value&& item)
{
    ENGINE_ASSERT(kind_ == ValueKind::Map, "script::Value::Set: the value is not a map");
    if (u32{item.depth_} + 1 > kMaxValueDepth)
    {
        return false;
    }
    usize existing = keys_.Size();
    if (key_index_.Get() != nullptr)
    {
        if (const u32* at = key_index_->Find(key))
        {
            existing = *at;
        }
    }
    else
    {
        for (usize i = 0; i < keys_.Size(); ++i)
        {
            if (keys_[i].View() == key)
            {
                existing = i;
                break;
            }
        }
    }
    if (existing < keys_.Size())
    {
        if (item.depth_ + 1 > depth_)
        {
            depth_ = static_cast<u8>(item.depth_ + 1);
        }
        items_[existing] = Move(item);
        return true;
    }
    if (items_.Size() >= kMaxContainerItems)
    {
        return false;
    }
    if (item.depth_ + 1 > depth_)
    {
        depth_ = static_cast<u8>(item.depth_ + 1);
    }
    keys_.Push(String<HeapAllocator>(HeapAllocator::Default(), key));
    items_.Push(Move(item));
    if (key_index_.Get() != nullptr)
    {
        key_index_->Insert(keys_[keys_.Size() - 1].View(), static_cast<u32>(keys_.Size() - 1));
    }
    else if (keys_.Size() > kIndexedKeys)
    {
        IndexKeys();
    }
    return true;
}

namespace
{

struct CheckState
{
    const ValueLimits* limits;
    u32 nodes = 0;
};

}  // namespace

static ValueCheck CheckNode(const Value& v, CheckState& state)
{
    if (++state.nodes > state.limits->max_nodes)
    {
        return ValueCheck::TooManyNodes;
    }
    const bool finite = state.limits->require_finite;
    switch (v.Kind())
    {
    case ValueKind::Float:
        if (finite && !IsFinite(v.AsFloat()))
        {
            return ValueCheck::NotFinite;
        }
        break;
    case ValueKind::Vec2:
        if (finite && (!IsFinite(v.AsVec2().x) || !IsFinite(v.AsVec2().y)))
        {
            return ValueCheck::NotFinite;
        }
        break;
    case ValueKind::Vec3:
        if (finite && (!IsFinite(v.AsVec3d().x) || !IsFinite(v.AsVec3d().y) || !IsFinite(v.AsVec3d().z)))
        {
            return ValueCheck::NotFinite;
        }
        break;
    case ValueKind::Vec4:
        if (finite &&
            (!IsFinite(v.AsVec4().x) || !IsFinite(v.AsVec4().y) || !IsFinite(v.AsVec4().z) || !IsFinite(v.AsVec4().w)))
        {
            return ValueCheck::NotFinite;
        }
        break;
    case ValueKind::Quat:
        if (finite &&
            (!IsFinite(v.AsQuat().x) || !IsFinite(v.AsQuat().y) || !IsFinite(v.AsQuat().z) || !IsFinite(v.AsQuat().w)))
        {
            return ValueCheck::NotFinite;
        }
        break;
    case ValueKind::Text:
        if (v.Text().Length() > state.limits->max_text_bytes)
        {
            return ValueCheck::TextTooLong;
        }
        break;
    case ValueKind::Bytes:
        if (v.Bytes().Size() > state.limits->max_blob_bytes)
        {
            return ValueCheck::BlobTooLarge;
        }
        break;
    case ValueKind::Array:
    case ValueKind::Map:
        for (usize i = 0; i < v.Size(); ++i)
        {
            if (v.Kind() == ValueKind::Map && v.KeyAt(i).Length() > state.limits->max_text_bytes)
            {
                return ValueCheck::TextTooLong;
            }
            const ValueCheck inner = CheckNode(v.At(i), state);
            if (inner != ValueCheck::Ok)
            {
                return inner;
            }
        }
        break;
    case ValueKind::Nil:
    case ValueKind::Bool:
    case ValueKind::Int:
    case ValueKind::Entity:
    case ValueKind::Handle: break;
    }
    return ValueCheck::Ok;
}

ValueCheck Value::Check(const ValueLimits& limits) const
{
    if (depth_ > limits.max_depth)
    {
        return ValueCheck::TooDeep;
    }
    CheckState state;
    state.limits = &limits;
    return CheckNode(*this, state);
}

bool Value::Equals(const Value& other) const
{
    if (kind_ != other.kind_)
    {
        return false;
    }
    switch (kind_)
    {
    case ValueKind::Nil: return true;
    case ValueKind::Bool: return bool_ == other.bool_;
    case ValueKind::Int: return int_ == other.int_;
    case ValueKind::Float: return SameBits(float_, other.float_);
    case ValueKind::Text: return text_.View() == other.text_.View();
    case ValueKind::Vec3:
        return SameBits(wide_[0], other.wide_[0]) && SameBits(wide_[1], other.wide_[1]) &&
               SameBits(wide_[2], other.wide_[2]);
    case ValueKind::Vec2:
    case ValueKind::Vec4:
    case ValueKind::Quat:
        for (u32 i = 0; i < 4; ++i)
        {
            if (!SameBits(lanes_[i], other.lanes_[i]))
            {
                return false;
            }
        }
        return true;
    case ValueKind::Entity:
        return entity_.index == other.entity_.index && entity_.generation == other.entity_.generation;
    case ValueKind::Handle: return entity_.index == other.entity_.index && int_ == other.int_;
    case ValueKind::Bytes:
        return bytes_.Size() == other.bytes_.Size() &&
               MemCompare(bytes_.Data(), other.bytes_.Data(), bytes_.Size()) == 0;
    case ValueKind::Array:
    case ValueKind::Map:
        if (items_.Size() != other.items_.Size())
        {
            return false;
        }
        for (usize i = 0; i < items_.Size(); ++i)
        {
            if (kind_ == ValueKind::Map && !(keys_[i].View() == other.keys_[i].View()))
            {
                return false;
            }
            if (!items_[i].Equals(other.items_[i]))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

static void HashNode(const Value& v, Hasher& h)
{
    h.Byte(static_cast<u8>(v.Kind()));
    switch (v.Kind())
    {
    case ValueKind::Nil: break;
    case ValueKind::Bool: h.Byte(v.AsBool() ? 1 : 0); break;
    case ValueKind::Int: h.U64(static_cast<u64>(v.AsInt())); break;
    case ValueKind::Float: h.F64(v.AsFloat()); break;
    case ValueKind::Text: h.Bytes(v.Text().Data(), v.Text().Length()); break;
    case ValueKind::Vec2:
        h.F32(v.AsVec2().x);
        h.F32(v.AsVec2().y);
        break;
    case ValueKind::Vec3:
        h.F64(v.AsVec3d().x);
        h.F64(v.AsVec3d().y);
        h.F64(v.AsVec3d().z);
        break;
    case ValueKind::Vec4:
        h.F32(v.AsVec4().x);
        h.F32(v.AsVec4().y);
        h.F32(v.AsVec4().z);
        h.F32(v.AsVec4().w);
        break;
    case ValueKind::Quat:
        h.F32(v.AsQuat().x);
        h.F32(v.AsQuat().y);
        h.F32(v.AsQuat().z);
        h.F32(v.AsQuat().w);
        break;
    case ValueKind::Handle:
        h.Byte(static_cast<u8>(v.AsHandleType()));
        h.U64(v.AsHandleId());
        break;
    case ValueKind::Entity: h.U64((static_cast<u64>(v.AsEntity().generation) << 32) | v.AsEntity().index); break;
    case ValueKind::Bytes: h.Bytes(reinterpret_cast<const char*>(v.Bytes().Data()), v.Bytes().Size()); break;
    case ValueKind::Array:
    case ValueKind::Map:
        h.U64(v.Size());
        for (usize i = 0; i < v.Size(); ++i)
        {
            if (v.Kind() == ValueKind::Map)
            {
                h.Bytes(v.KeyAt(i).Data(), v.KeyAt(i).Length());
            }
            HashNode(v.At(i), h);
        }
        break;
    }
}

u64 Value::Hash() const
{
    Hasher h;
    HashNode(*this, h);
    return h.Result();
}

}  // namespace vse::script
