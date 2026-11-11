#pragma once

//! Field descriptions (Phase 19 Day 256): [[storage::Field]] describes one member of a component by pointer to member, and carries the functions that
//! write it to and read it from the canonical encoding (`foundation/bytes/`). A member whose type has no encoding fails to compile: nothing is skipped silently.

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/array.h"
#include "lang/traits.h"
#include "math/geometry.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "mem/heap_allocator.h"
#include "storage/entity_ref.h"
#include "storage/status.h"
#include "string/string.h"
#include "string/utf8.h"
#include "types.h"

namespace engine_placeholder::storage {

/// The upper bounds a decode (and an encode) holds every length to, so a damaged or hostile record cannot ask for unbounded memory. [[storage::LimitsFromSettings]] reads them from
/// the settings `storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements` and `storage.max_components_per_entity`.
///
/// Thread-safety: thread-compatible plain data.
struct CodecLimits {
    u64 max_record_bytes = 16u << 20;       ///< The most bytes of one component record (checked before anything is decoded or upgraded).
    u64 max_string_bytes = 1u << 20;        ///< The most bytes of one string field.
    u64 max_array_elements = 1u << 16;      ///< The most elements of one array field.
    u64 max_components_per_entity = 128;    ///< The most component records of one entity (checked by the entity container, Day 260).
};

/// The kinds of field the registry can store.
enum class FieldKind : u8 {
    U8,        ///< An unsigned 8-bit integer.
    U16,       ///< An unsigned 16-bit integer, little-endian.
    U32,       ///< An unsigned 32-bit integer, little-endian.
    U64,       ///< An unsigned 64-bit integer, little-endian.
    I8,        ///< A signed 8-bit integer.
    I16,       ///< A signed 16-bit integer, little-endian.
    I32,       ///< A signed 32-bit integer, little-endian.
    I64,       ///< A signed 64-bit integer, little-endian.
    F32,       ///< A 32-bit float as its bit pattern.
    F64,       ///< A 64-bit float as its bit pattern.
    Bool,      ///< One validated byte.
    Vec2,      ///< Two `f32`.
    Vec3,      ///< Three `f32`.
    Vec4,      ///< Four `f32`.
    Quat,      ///< Four `f32` (x, y, z, w).
    Mat4,      ///< Sixteen `f32`, column by column.
    Aabb,      ///< Two `Vec3` (min, max).
    Enum,      ///< An enumeration as its underlying integer, checked against a declared range.
    String,    ///< A bounded varint length and UTF-8 text, validated.
    Array,     ///< A fixed-size [[Array]]: its count as a varint, then the elements.
    EntityRef, ///< An [[storage::EntityRef]]: 16 bytes.
};

/// Per-field settings that the type alone does not give.
///
/// Thread-safety: thread-compatible plain data.
struct FieldOptions {
    bool has_range = false; ///< Whether `range_min` and `range_max` were declared (required for an enum field, ignored for others).
    i64 range_min = 0;      ///< The smallest valid value of an enum field.
    i64 range_max = 0;      ///< The largest valid value of an enum field.
    u64 max_bytes = 0xFFFFFFFFull; ///< The most bytes of a string field (also held to [[storage::CodecLimits::max_string_bytes]]).

    /// Options for an enum field whose valid values run from `min` to `max` (inclusive).
    static constexpr FieldOptions Enum(i64 min, i64 max) {
        FieldOptions o;
        o.has_range = true;
        o.range_min = min;
        o.range_max = max;
        return o;
    }
    /// Options for a string field of at most `max_bytes` bytes.
    static constexpr FieldOptions Text(u64 max_bytes) {
        FieldOptions o;
        o.max_bytes = max_bytes;
        return o;
    }
};

struct FieldDesc;

namespace detail {

template <class V>
inline constexpr bool kAlwaysFalse = sizeof(V) == 0;

template <class V>
struct IsArrayField {
    static constexpr bool value = false;
};
template <class T, usize N>
struct IsArrayField<Array<T, N>> {
    static constexpr bool value = true;
    using Element = T;
};

template <class V>
constexpr bool NeedsRange() {
    if constexpr (IsEnum<V>) return true;
    else if constexpr (IsArrayField<V>::value) return NeedsRange<typename IsArrayField<V>::Element>();
    else return false;
}

template <class V>
inline constexpr bool kIsString = IsSame<V, String<HeapAllocator>>;

inline Status FromByteError(bytes::ByteError error) {
    switch (error) {
    case bytes::ByteError::Ok: return Status::Success();
    case bytes::ByteError::Truncated: return Status::Make(ErrorCode::Corrupt, "the record is truncated");
    case bytes::ByteError::TooLong: return Status::Make(ErrorCode::LimitExceeded, "a length is above its limit");
    case bytes::ByteError::Invalid: return Status::Make(ErrorCode::Corrupt, "the record holds an invalid value");
    }
    return Status::Make(ErrorCode::Corrupt, "the record holds an invalid value");
}

template <class V>
constexpr FieldKind IntegerKind() {
    if constexpr (sizeof(V) == 1) return IsSigned<V> ? FieldKind::I8 : FieldKind::U8;
    else if constexpr (sizeof(V) == 2) return IsSigned<V> ? FieldKind::I16 : FieldKind::U16;
    else if constexpr (sizeof(V) == 4) return IsSigned<V> ? FieldKind::I32 : FieldKind::U32;
    else return IsSigned<V> ? FieldKind::I64 : FieldKind::U64;
}

template <class V>
constexpr FieldKind KindOf() {
    if constexpr (IsSame<V, bool>) return FieldKind::Bool;
    else if constexpr (IsSame<V, char>) {
        static_assert(kAlwaysFalse<V>, "storage::Field: a plain char has no stored form; use u8 or i8");
        return FieldKind::U8;
    } else if constexpr (IsIntegral<V>) return IntegerKind<V>();
    else if constexpr (IsSame<V, f32>) return FieldKind::F32;
    else if constexpr (IsSame<V, f64>) return FieldKind::F64;
    else if constexpr (IsSame<V, Vec2>) return FieldKind::Vec2;
    else if constexpr (IsSame<V, Vec3>) return FieldKind::Vec3;
    else if constexpr (IsSame<V, Vec4>) return FieldKind::Vec4;
    else if constexpr (IsSame<V, Quaternion>) return FieldKind::Quat;
    else if constexpr (IsSame<V, Mat4>) return FieldKind::Mat4;
    else if constexpr (IsSame<V, Aabb>) return FieldKind::Aabb;
    else if constexpr (IsEnum<V>) return FieldKind::Enum;
    else if constexpr (kIsString<V>) return FieldKind::String;
    else if constexpr (IsArrayField<V>::value) return FieldKind::Array;
    else if constexpr (IsSame<V, EntityRef> || IsSame<V, EntityKey>) return FieldKind::EntityRef;
    else {
        static_assert(kAlwaysFalse<V>, "storage::Field: this member type has no stored form (see storage::FieldKind); give it a supported type or leave the component to a custom codec");
        return FieldKind::U8;
    }
}

template <class V, class Fixed>
Status ReadInteger(bytes::ByteReader& in, V* value, bytes::ByteError (bytes::ByteReader::*read)(Fixed*)) {
    Fixed raw{};
    const bytes::ByteError e = (in.*read)(&raw);
    if (e != bytes::ByteError::Ok) return FromByteError(e);
    *value = static_cast<V>(raw);
    return Status::Success();
}

template <class V>
Status EncodeValue(const V& value, const FieldDesc& desc, bytes::ByteWriter& out, const CodecLimits& limits);
template <class V>
Status DecodeValue(V* value, const FieldDesc& desc, bytes::ByteReader& in, const CodecLimits& limits);

inline void EncodeVec3(const Vec3& v, bytes::ByteWriter& out) {
    out.WriteF32(v.x);
    out.WriteF32(v.y);
    out.WriteF32(v.z);
}
inline Status DecodeVec3(Vec3* v, bytes::ByteReader& in) {
    Vec3 r;
    bytes::ByteError e = in.ReadF32(&r.x);
    if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.y);
    if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.z);
    if (e != bytes::ByteError::Ok) return FromByteError(e);
    *v = r;
    return Status::Success();
}
inline void EncodeVec4(const Vec4& v, bytes::ByteWriter& out) {
    out.WriteF32(v.x);
    out.WriteF32(v.y);
    out.WriteF32(v.z);
    out.WriteF32(v.w);
}
inline Status DecodeVec4(Vec4* v, bytes::ByteReader& in) {
    Vec4 r;
    bytes::ByteError e = in.ReadF32(&r.x);
    if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.y);
    if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.z);
    if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.w);
    if (e != bytes::ByteError::Ok) return FromByteError(e);
    *v = r;
    return Status::Success();
}

} // namespace detail

/// One described member of a component: its name, kind, options and the two functions that move it between the component's memory and the encoding.
/// Made by [[storage::Field]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `name` is borrowed from static storage (a string literal).
struct FieldDesc {
    const char* name = "";                     ///< The field's name.
    FieldKind kind = FieldKind::U8;            ///< What the field is.
    FieldOptions options;                      ///< Enum range or string bound.
    usize memory_size = 0;                     ///< `sizeof` of the member in the component.
    bool needs_range = false;                  ///< Whether the member is an enum (or an array of enums), so the registration needs `options.has_range`.
    /// Writes the member of the component at `component`. On failure nothing is promised about what `out` holds (the caller truncates).
    Status (*encode)(const FieldDesc& desc, const void* component, bytes::ByteWriter& out, const CodecLimits& limits) = nullptr;
    /// Reads the member into the constructed component at `component`. On failure the member may be partly written (an array stops at the element that failed); the caller discards the component.
    Status (*decode)(const FieldDesc& desc, bytes::ByteReader& in, void* component, const CodecLimits& limits) = nullptr;
};

namespace detail {

template <class V>
Status EncodeValue(const V& value, const FieldDesc& desc, bytes::ByteWriter& out, const CodecLimits& limits) {
    if constexpr (IsSame<V, bool>) {
        out.WriteBool(value);
    } else if constexpr (IsEnum<V>) {
        using U = UnderlyingType<V>;
        const U raw = static_cast<U>(value);
        if (!desc.options.has_range) return Status::Make(ErrorCode::Invalid, "an enum field has no declared range");
        if (static_cast<i64>(raw) < desc.options.range_min || static_cast<i64>(raw) > desc.options.range_max) {
            return Status::Make(ErrorCode::Invalid, "an enum value is outside its declared range");
        }
        return EncodeValue<U>(raw, desc, out, limits);
    } else if constexpr (IsIntegral<V>) {
        constexpr FieldKind k = IntegerKind<V>();
        if constexpr (k == FieldKind::U8) out.WriteU8(value);
        else if constexpr (k == FieldKind::U16) out.WriteU16(value);
        else if constexpr (k == FieldKind::U32) out.WriteU32(value);
        else if constexpr (k == FieldKind::U64) out.WriteU64(value);
        else if constexpr (k == FieldKind::I8) out.WriteI8(value);
        else if constexpr (k == FieldKind::I16) out.WriteI16(value);
        else if constexpr (k == FieldKind::I32) out.WriteI32(value);
        else out.WriteI64(value);
    } else if constexpr (IsSame<V, f32>) {
        out.WriteF32(value);
    } else if constexpr (IsSame<V, f64>) {
        out.WriteF64(value);
    } else if constexpr (IsSame<V, Vec2>) {
        out.WriteF32(value.x);
        out.WriteF32(value.y);
    } else if constexpr (IsSame<V, Vec3>) {
        EncodeVec3(value, out);
    } else if constexpr (IsSame<V, Vec4>) {
        EncodeVec4(value, out);
    } else if constexpr (IsSame<V, Quaternion>) {
        out.WriteF32(value.x);
        out.WriteF32(value.y);
        out.WriteF32(value.z);
        out.WriteF32(value.w);
    } else if constexpr (IsSame<V, Mat4>) {
        for (usize c = 0; c < 4; ++c) EncodeVec4(value.col[c], out);
    } else if constexpr (IsSame<V, Aabb>) {
        EncodeVec3(value.min, out);
        EncodeVec3(value.max, out);
    } else if constexpr (kIsString<V>) {
        const u64 cap = desc.options.max_bytes < limits.max_string_bytes ? desc.options.max_bytes : limits.max_string_bytes;
        if (value.Length() > cap) return Status::Make(ErrorCode::LimitExceeded, "a string is longer than its limit");
        if (!IsValidUtf8(value.View())) return Status::Make(ErrorCode::Invalid, "a string is not valid UTF-8");
        out.WriteVarint(value.Length());
        out.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(value.CStr()), value.Length()));
    } else if constexpr (IsArrayField<V>::value) {
        if (V::Size() > limits.max_array_elements) return Status::Make(ErrorCode::LimitExceeded, "an array has more elements than the limit");
        out.WriteVarint(V::Size());
        for (usize i = 0; i < V::Size(); ++i) {
            const Status s = EncodeValue(value[i], desc, out, limits);
            if (s.Failed()) return s;
        }
    } else if constexpr (IsSame<V, EntityKey>) {
        out.WriteU64(value.high);
        out.WriteU64(value.low);
    } else if constexpr (IsSame<V, EntityRef>) {
        out.WriteU64(value.key.high);
        out.WriteU64(value.key.low);
    } else {
        static_assert(kAlwaysFalse<V>, "storage::Field: this member type has no stored form");
    }
    return Status::Success();
}

template <class V>
Status DecodeValue(V* value, const FieldDesc& desc, bytes::ByteReader& in, const CodecLimits& limits) {
    if constexpr (IsSame<V, bool>) {
        return FromByteError(in.ReadBool(value));
    } else if constexpr (IsEnum<V>) {
        using U = UnderlyingType<V>;
        if (!desc.options.has_range) return Status::Make(ErrorCode::Invalid, "an enum field has no declared range");
        U raw{};
        const Status s = DecodeValue<U>(&raw, desc, in, limits);
        if (s.Failed()) return s;
        if (static_cast<i64>(raw) < desc.options.range_min || static_cast<i64>(raw) > desc.options.range_max) {
            return Status::Make(ErrorCode::Corrupt, "an enum value is outside its declared range");
        }
        *value = static_cast<V>(raw);
        return Status::Success();
    } else if constexpr (IsIntegral<V>) {
        constexpr FieldKind k = IntegerKind<V>();
        // Read into the fixed-width type and convert, so the member may be any integer type of that size (`long` or `long long` for a 64-bit value).
        if constexpr (k == FieldKind::U8) return ReadInteger<V, u8>(in, value, &bytes::ByteReader::ReadU8);
        else if constexpr (k == FieldKind::U16) return ReadInteger<V, u16>(in, value, &bytes::ByteReader::ReadU16);
        else if constexpr (k == FieldKind::U32) return ReadInteger<V, u32>(in, value, &bytes::ByteReader::ReadU32);
        else if constexpr (k == FieldKind::U64) return ReadInteger<V, u64>(in, value, &bytes::ByteReader::ReadU64);
        else if constexpr (k == FieldKind::I8) return ReadInteger<V, i8>(in, value, &bytes::ByteReader::ReadI8);
        else if constexpr (k == FieldKind::I16) return ReadInteger<V, i16>(in, value, &bytes::ByteReader::ReadI16);
        else if constexpr (k == FieldKind::I32) return ReadInteger<V, i32>(in, value, &bytes::ByteReader::ReadI32);
        else return ReadInteger<V, i64>(in, value, &bytes::ByteReader::ReadI64);
    } else if constexpr (IsSame<V, f32>) {
        return FromByteError(in.ReadF32(value));
    } else if constexpr (IsSame<V, f64>) {
        return FromByteError(in.ReadF64(value));
    } else if constexpr (IsSame<V, Vec2>) {
        Vec2 r;
        bytes::ByteError e = in.ReadF32(&r.x);
        if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.y);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        *value = r;
        return Status::Success();
    } else if constexpr (IsSame<V, Vec3>) {
        return DecodeVec3(value, in);
    } else if constexpr (IsSame<V, Vec4>) {
        return DecodeVec4(value, in);
    } else if constexpr (IsSame<V, Quaternion>) {
        Quaternion r;
        bytes::ByteError e = in.ReadF32(&r.x);
        if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.y);
        if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.z);
        if (e == bytes::ByteError::Ok) e = in.ReadF32(&r.w);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        *value = r;
        return Status::Success();
    } else if constexpr (IsSame<V, Mat4>) {
        Mat4 r;
        for (usize c = 0; c < 4; ++c) {
            const Status s = DecodeVec4(&r.col[c], in);
            if (s.Failed()) return s;
        }
        *value = r;
        return Status::Success();
    } else if constexpr (IsSame<V, Aabb>) {
        Aabb r;
        Status s = DecodeVec3(&r.min, in);
        if (s.Ok()) s = DecodeVec3(&r.max, in);
        if (s.Failed()) return s;
        *value = r;
        return Status::Success();
    } else if constexpr (kIsString<V>) {
        const u64 cap = desc.options.max_bytes < limits.max_string_bytes ? desc.options.max_bytes : limits.max_string_bytes;
        u64 length = 0;
        bytes::ByteError e = in.ReadLength(cap, &length);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        ByteSpan raw;
        e = in.ReadBytes(static_cast<usize>(length), &raw);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        const StringView text(reinterpret_cast<const char*>(raw.Data()), raw.Size());
        if (!IsValidUtf8(text)) {
            return Status::Make(ErrorCode::Corrupt, "a string is not valid UTF-8");
        }
        *value = V(HeapAllocator::Default(), text);
        return Status::Success();
    } else if constexpr (IsArrayField<V>::value) {
        u64 count = 0;
        const bytes::ByteError e = in.ReadLength(limits.max_array_elements, &count);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        if (count != V::Size()) return Status::Make(ErrorCode::Corrupt, "an array has another element count than its type");
        for (usize i = 0; i < V::Size(); ++i) {
            const Status s = DecodeValue(&(*value)[i], desc, in, limits);
            if (s.Failed()) return s;
        }
        return Status::Success();
    } else if constexpr (IsSame<V, EntityKey>) {
        EntityKey k;
        bytes::ByteError e = in.ReadU64(&k.high);
        if (e == bytes::ByteError::Ok) e = in.ReadU64(&k.low);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        *value = k;
        return Status::Success();
    } else if constexpr (IsSame<V, EntityRef>) {
        EntityRef r;
        bytes::ByteError e = in.ReadU64(&r.key.high);
        if (e == bytes::ByteError::Ok) e = in.ReadU64(&r.key.low);
        if (e != bytes::ByteError::Ok) return FromByteError(e);
        *value = r;
        return Status::Success();
    } else {
        static_assert(kAlwaysFalse<V>, "storage::Field: this member type has no stored form");
        return Status::Success();
    }
}

template <class M>
struct MemberTraits;
template <class C, class V>
struct MemberTraits<V C::*> {
    using Class = C;
    using Value = V;
};

} // namespace detail

/// Describes the member `Member` of a component (`Field<&Health::current>("current")`). The member's type decides the kind: integers, floats, `bool`, `Vec2`,
/// `Vec3`, `Vec4`, `Quaternion`, `Mat4`, `Aabb`, enums, `String<HeapAllocator>`, `Array<T, N>` of those, [[storage::EntityRef]] and [[storage::EntityKey]]. Any other type is a compile error.
///
/// The member is a template argument, not a run-time value, so the encode and decode functions are plain function pointers with the member baked in.
///
/// Param name: the field's name, a string literal.
/// Param options: required for an enum field ([[storage::FieldOptions::Enum]]), optional bound for a string ([[storage::FieldOptions::Text]]).
/// Ownership: `name` is borrowed; pass a literal.
/// Fails: never at run time; a member without a stored form does not compile.
template <auto Member>
FieldDesc Field(const char* name, FieldOptions options = FieldOptions()) {
    using Traits = detail::MemberTraits<decltype(Member)>;
    using C = typename Traits::Class;
    using V = typename Traits::Value;
    FieldDesc d;
    d.name = name;
    d.kind = detail::KindOf<V>();
    d.options = options;
    d.memory_size = sizeof(V);
    d.needs_range = detail::NeedsRange<V>();
    d.encode = [](const FieldDesc& desc, const void* component, bytes::ByteWriter& out, const CodecLimits& limits) {
        return detail::EncodeValue<V>(static_cast<const C*>(component)->*Member, desc, out, limits);
    };
    d.decode = [](const FieldDesc& desc, bytes::ByteReader& in, void* component, const CodecLimits& limits) {
        return detail::DecodeValue<V>(&(static_cast<C*>(component)->*Member), desc, in, limits);
    };
    return d;
}

} // namespace engine_placeholder::storage
