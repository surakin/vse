// The script value (Phase 27 Day 446): what crosses between the engine and a language state, and between states. A
// value is copied, never shared: no pointer into a state escapes, so a value can move to another thread or another
// state (docs/superpowers/specs/2026-11-18-scripting-design.md, section 4).
#pragma once

#include "containers/hash_map.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "mem/heap_allocator.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::script
{

/// The most nested containers a value may hold (a scalar is depth 1, an array of scalars depth 2). Enforced when a
/// child is added.
inline constexpr u32 kMaxValueDepth = 16;
/// The most items one array or map may hold. Enforced when an item is added.
inline constexpr usize kMaxContainerItems = usize{1} << 16;

/// What a [[script::ValueKind::Handle]] names. A new type is added at the end; the codes are stored.
///
/// - Thread-safety: thread-compatible plain data.
enum class HandleType : u8
{
    /// No type (never a valid handle).
    None = 0,
    /// A resource of the engine (a clip, a texture, ...) by its 64-bit resource id.
    Resource = 1,
};

/// A resource handle as a binding parameter or result (`Codec<ResourceHandle>`): the 64-bit resource id of a value of
/// kind `Handle` and type `Resource`.
///
/// - Thread-safety: thread-compatible plain data.
struct ResourceHandle
{
    /// The resource id (0 is none).
    u64 id = 0;
};

/// The kind of a [[script::Value]].
///
/// - Thread-safety: thread-compatible plain data.
enum class ValueKind : u8
{
    /// No value (a Lua `nil`).
    Nil,
    /// `true` or `false`.
    Bool,
    /// A signed 64-bit integer.
    Int,
    /// A 64-bit float.
    Float,
    /// UTF-8 text (not validated; the bytes are kept).
    Text,
    /// Two `f32`.
    Vec2,
    /// Three `f64` (a position travels without narrowing; `AsVec3` narrows to `f32` for the many users that want it).
    Vec3,
    /// Four `f32`.
    Vec4,
    /// A rotation: four `f32` (x, y, z, w).
    Quat,
    /// An entity id (index and generation).
    Entity,
    /// A byte blob.
    Bytes,
    /// An ordered list of values.
    Array,
    /// Text keys to values, in insertion order (so iteration is the same everywhere).
    Map,
    /// A handle: a type code and a 64-bit id of something the engine owns (a resource), which a script holds but can
    /// neither read nor forge ([[script::HandleType]]).
    Handle,
};

/// What [[Value::Check]] found wrong with a value that came from a script or a host.
///
/// - Thread-safety: thread-compatible plain data.
enum class ValueCheck : u8
{
    /// The value is within the limits.
    Ok,
    /// More containers nested than `max_depth`.
    TooDeep,
    /// More values in total (every scalar and container, recursively) than `max_nodes`.
    TooManyNodes,
    /// A text or a map key longer than `max_text_bytes`.
    TextTooLong,
    /// A blob longer than `max_blob_bytes`.
    BlobTooLarge,
    /// A float that is NaN or infinite where the caller asked for finite values only.
    NotFinite,
};

/// The limits [[Value::Check]] applies to a value that crosses a boundary. The defaults suit gameplay data; a host that
/// takes values from the network lowers them.
///
/// - Thread-safety: thread-compatible plain data.
struct ValueLimits
{
    /// The deepest nesting allowed (at most [[kMaxValueDepth]], which construction enforces anyway).
    u32 max_depth = kMaxValueDepth;
    /// The most values in total.
    u32 max_nodes = 1u << 16;
    /// The longest text or key, in bytes.
    usize max_text_bytes = usize{1} << 20;
    /// The longest blob, in bytes.
    usize max_blob_bytes = usize{16} << 20;
    /// Whether NaN and infinity are refused (floats, vectors and quaternions).
    bool require_finite = false;
};

/// One script value: nil, a boolean, an integer, a float, text, a vector of two to four floats, a rotation, an entity
/// id, a byte blob, an array or a map.
///
/// A value owns everything it holds and is copied deeply, so it can be handed to another thread or another state.
/// Nesting is bounded
/// ([[kMaxValueDepth]], [[kMaxContainerItems]]) when children are added, so copying, comparing and destroying a value
/// use bounded recursion whatever came in. Size and text limits for hostile input are applied by [[Value::Check]] at
/// the boundary.
///
/// - Thread-safety: thread-compatible: a value is not shared between threads, it is copied.
/// - Ownership: owns its text, blob, items and keys; copies are deep.
class Value
{
public:
    /// Creates nil.
    Value();
    /// Copies `other` and everything it holds.
    Value(const Value& other);
    /// Takes `other`'s contents; `other` becomes nil.
    Value(Value&& other) noexcept;
    /// Replaces this value with a copy of `other`.
    Value& operator=(const Value& other);
    /// Replaces this value with `other`'s contents; `other` becomes nil.
    Value& operator=(Value&& other) noexcept;
    /// Destroys the value and everything it holds.
    ~Value();

    /// Makes a boolean.
    static Value FromBool(bool value);
    /// Makes an integer.
    static Value FromInt(i64 value);
    /// Makes a float.
    static Value FromFloat(f64 value);
    /// Makes a text by copying `text`.
    ///
    /// - Aborts: the heap is out of memory.
    static Value FromText(StringView text);
    /// Makes a two-component vector.
    static Value FromVec2(Vec2 value);
    /// Makes a three-component vector.
    static Value FromVec3(Vec3 value);
    /// Makes a three-component vector from double-precision numbers (a world position).
    static Value FromVec3d(Vec3d value);
    /// Makes a four-component vector.
    static Value FromVec4(Vec4 value);
    /// Makes a rotation.
    static Value FromQuat(Quaternion value);
    /// Makes an entity id (any id, valid or not).
    static Value FromEntity(ecs::Entity value);
    /// Makes a handle of type `type` for the 64-bit `id`.
    ///
    /// - Fails: never.
    static Value FromHandle(HandleType type, u64 id);
    /// Makes a blob by copying `bytes`.
    ///
    /// - Aborts: the heap is out of memory.
    static Value FromBytes(Span<const u8> bytes);
    /// Makes an empty array.
    static Value MakeArray();
    /// Makes an empty map.
    static Value MakeMap();

    /// The kind of this value.
    ValueKind Kind() const { return kind_; }
    /// Whether this value is nil.
    ///
    /// - Fails: never.
    bool IsNil() const { return kind_ == ValueKind::Nil; }
    /// Whether this value is an integer or a float.
    ///
    /// - Fails: never.
    bool IsNumber() const { return kind_ == ValueKind::Int || kind_ == ValueKind::Float; }

    /// The boolean.
    ///
    /// - Aborts: the kind is not `Bool`.
    bool AsBool() const;
    /// The integer.
    ///
    /// - Aborts: the kind is not `Int`.
    i64 AsInt() const;
    /// The float.
    ///
    /// - Aborts: the kind is not `Float`.
    f64 AsFloat() const;
    /// An integer or a float as a float (an integer above 2^53 loses its low bits).
    ///
    /// - Aborts: the kind is neither `Int` nor `Float`.
    f64 ToDouble() const;
    /// The text, valid until the value changes or is destroyed.
    ///
    /// - Aborts: the kind is not `Text`.
    StringView Text() const;
    /// The two-component vector.
    ///
    /// - Aborts: the kind is not `Vec2`.
    Vec2 AsVec2() const;
    /// The three-component vector.
    ///
    /// - Aborts: the kind is not `Vec3`.
    Vec3 AsVec3() const;
    /// The three-component vector without narrowing.
    ///
    /// - Aborts: the kind is not `Vec3`.
    Vec3d AsVec3d() const;
    /// The four-component vector.
    ///
    /// - Aborts: the kind is not `Vec4`.
    Vec4 AsVec4() const;
    /// The rotation.
    ///
    /// - Aborts: the kind is not `Quat`.
    Quaternion AsQuat() const;
    /// The entity id.
    ///
    /// - Aborts: the kind is not `Entity`.
    ecs::Entity AsEntity() const;
    /// The type of a handle.
    ///
    /// - Aborts: the kind is not `Handle`.
    HandleType AsHandleType() const;
    /// The id of a handle.
    ///
    /// - Aborts: the kind is not `Handle`.
    u64 AsHandleId() const;
    /// The blob's bytes, valid until the value changes or is destroyed.
    ///
    /// - Aborts: the kind is not `Bytes`.
    Span<const u8> Bytes() const;

    /// The number of items of an array or entries of a map.
    ///
    /// - Aborts: the kind is neither `Array` nor `Map`.
    usize Size() const;
    /// Item `index` of an array, or the value of entry `index` of a map (insertion order).
    ///
    /// - Aborts: the kind is neither `Array` nor `Map`, or `index` is not less than `Size()`.
    const Value& At(usize index) const;
    /// The key of entry `index` of a map.
    ///
    /// - Aborts: the kind is not `Map`, or `index` is not less than `Size()`.
    StringView KeyAt(usize index) const;
    /// The value stored under `key` in a map, or null when there is none.
    ///
    /// - Ownership: the pointer is into this value and is valid until the map changes or is destroyed.
    /// - Aborts: the kind is not `Map`.
    const Value* Find(StringView key) const;

    /// Appends `item` to an array. The item is moved only when it is accepted.
    ///
    /// - Returns: `false` and leaves both unchanged when the array is full ([[kMaxContainerItems]]) or the item would
    /// nest deeper than
    ///   [[kMaxValueDepth]].
    /// - Aborts: the kind is not `Array`, or the heap is out of memory.
    bool Push(Value&& item);
    /// Stores `item` under `key` in a map: replaces an existing entry (keeping its position) or appends one. The item
    /// is moved only when it is accepted.
    ///
    /// - Returns: `false` and leaves both unchanged when the map is full or the item would nest deeper than
    /// [[kMaxValueDepth]].
    /// - Aborts: the kind is not `Map`, or the heap is out of memory.
    bool Set(StringView key, Value&& item);

    /// The nesting depth: 1 for a scalar, 1 plus the deepest child for a container.
    u32 Depth() const { return depth_; }
    /// Checks the value against `limits` (iteratively over a tree that is already bounded by construction).
    ///
    /// - Fails: reported by return value: the first problem found, depth first in insertion order.
    ValueCheck Check(const ValueLimits& limits) const;
    /// Whether `other` is the same kind with the same content: floats by bit pattern (so NaN equals NaN and `+0`
    /// differs from `-0`), maps entry by entry in order.
    ///
    /// - Fails: never.
    bool Equals(const Value& other) const;
    /// A 64-bit hash of the kind and the content, the same on every machine for the same value (FNV-1a over a canonical
    /// byte encoding).
    u64 Hash() const;

private:
    void CopyFrom(const Value& other);
    void Clear();

    ValueKind kind_ = ValueKind::Nil;
    u8 depth_ = 1;
    bool bool_ = false;
    i64 int_ = 0;
    f64 float_ = 0.0;
    f32 lanes_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    f64 wide_[3] = {0.0, 0.0, 0.0};  // the lanes of a `Vec3`
    ecs::Entity entity_;             // a Handle keeps its type code in `entity_.index` and its id in `int_` (as bits)
    String<HeapAllocator> text_;
    Vector<u8> bytes_;
    Vector<Value> items_;
    Vector<String<HeapAllocator>> keys_;
    // A map of more than kIndexedKeys keys also has an index from key to position (container audit Day 9: `Set` and
    // `Find` scanned every key). The views point into `keys_`'s strings, whose bytes do not move when the vector grows
    // or the value moves; it is built and kept only by non-const calls, so a shared const value is never written.
    static constexpr usize kIndexedKeys = 8;
    UniquePtr<HashMap<StringView, u32, HeapAllocator>> key_index_;
    void IndexKeys();
};

}  // namespace vse::script
