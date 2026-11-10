#pragma once

//! Component types: what the world knows about a type stored in an entity's rows, registered without run-time type information.

#include "lang/new.h"
#include "lang/traits.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// A component type's number: an index into the process-wide registry. Stable for the process, not across processes.
using ComponentId = u16;

/// The id of a type the registry refused (it was full), or of "no component".
inline constexpr ComponentId kInvalidComponent = 0xFFFFu;

/// The most component types a process registers.
inline constexpr u32 kMaxComponentTypes = 128;

/// The largest alignment a component may require (one cache line).
inline constexpr usize kMaxComponentAlign = 64;

/// What the storage needs to know about a component type.
///
/// Thread-safety: thread-compatible plain data; the entries of the registry are immutable after registration.
/// Ownership: `name` is borrowed from static storage.
struct TypeInfo {
    /// `sizeof(T)`, 0 for a tag.
    usize size = 0;
    /// `alignof(T)`, 1 for a tag.
    usize align = 1;
    /// True for an empty type: it marks an entity and occupies no memory in a chunk.
    bool is_tag = false;
    /// True when the bytes can be copied to move an object (trivially copyable and destructible): columns of such types move with `MemCopy`.
    bool trivially_relocatable = false;
    /// Move-constructs the object at `destination` from `source` (does not destroy the source).
    void (*move_construct)(void* destination, void* source) = nullptr;
    /// Destroys the object at the address.
    void (*destroy)(void* object) = nullptr;
    /// Default-constructs an object at the address, or null when the type has no default constructor.
    void (*default_construct)(void* where) = nullptr;
    /// The type's `kComponentName` when it declares one, else "component#N".
    const char* name = nullptr;
};

/// The process-wide table of component types, filled when a type is first used.
///
/// Thread-safety: thread-safe. `Register` serializes; `Info` and `Count` may be called while another thread registers (an entry is
///   published before the count that covers it).
/// Ownership: owns the entries and their names; references from `Info` stay valid for the life of the registry.
class ComponentRegistry {
public:
    /// An empty registry (tests use their own; programs use `Global()`).
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    /// The registry every [[ecs::ComponentOf]] uses.
    static ComponentRegistry& Global();

    /// Adds a type and returns its id.
    ///
    /// Param info: the type; `name` may be null (then "component#N" is made).
    /// Returns: the new id, or [[ecs::kInvalidComponent]] when [[ecs::kMaxComponentTypes]] types are registered (counted in `Refused`).
    ComponentId Register(const TypeInfo& info);

    /// The entry for `id`.
    /// Aborts: `id` is not a registered id.
    const TypeInfo& Info(ComponentId id) const;

    /// The number of registered types.
    u32 Count() const;

    /// The number of `Register` calls refused because the registry was full.
    u64 Refused() const;

private:
    Mutex lock_;
    TypeInfo entries_[kMaxComponentTypes];
    char names_[kMaxComponentTypes][16];
    Atomic<u32> count_{0};
    Atomic<u64> refused_{0};
};

namespace detail {

template <class T>
concept HasComponentName = requires { static_cast<const char*>(T::kComponentName); };

template <class T>
void MoveConstructComponent(void* destination, void* source) {
    ConstructAt<T>(destination, static_cast<T&&>(*static_cast<T*>(source)));
}
template <class T>
void DestroyComponent(void* object) {
    static_cast<T*>(object)->~T();
}
template <class T>
void DefaultConstructComponent(void* where) {
    ConstructAt<T>(where);
}

template <class T>
ComponentId RegisterComponent() {
    static_assert(!IsReference<T>, "a component type must not be a reference");
    static_assert(!IsArray<T>, "a component type must not be an array; wrap it in a struct");
    static_assert(!IsConst<T> && !IsVolatile<T>, "a component type must not be const or volatile");
    static_assert(IsMoveConstructible<T>, "a component type must be move constructible");
    static_assert(IsDestructible<T>, "a component type must be destructible");
    static_assert(alignof(T) <= kMaxComponentAlign, "a component type must not require more than 64-byte alignment");
    TypeInfo info;
    info.is_tag = IsEmpty<T>;
    info.size = info.is_tag ? 0 : sizeof(T);
    info.align = info.is_tag ? 1 : alignof(T);
    info.trivially_relocatable = IsTriviallyCopyable<T> && IsTriviallyDestructible<T>;
    info.move_construct = &MoveConstructComponent<T>;
    info.destroy = &DestroyComponent<T>;
    if constexpr (IsDefaultConstructible<T>) {
        info.default_construct = &DefaultConstructComponent<T>;
    }
    if constexpr (HasComponentName<T>) {
        info.name = static_cast<const char*>(T::kComponentName);
    }
    return ComponentRegistry::Global().Register(info);
}

} // namespace detail

/// The id of component type `T`, registering it on first use.
///
/// `T` must be move constructible and destructible, not a reference, array, const or volatile type, and aligned to at most 64 bytes
/// (each is a compile-time error). An empty type is a tag. A type may declare `static constexpr const char* kComponentName` to be named in logs.
///
/// Returns: the id, or [[ecs::kInvalidComponent]] when the registry was full when `T` was first used (the same answer every time).
/// Thread-safety: thread-safe.
template <class T>
ComponentId ComponentOf() {
    static const ComponentId id = detail::RegisterComponent<T>();
    return id;
}

} // namespace engine_placeholder::ecs
