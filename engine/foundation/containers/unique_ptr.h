#pragma once

#include "lang/traits.h"
#include "lang/utility.h"

#include "debug/compiler.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! Owning pointer to one object, the engine's replacement for a raw `new` / `delete` pair.
//! [[Array]] and [[Vector]] own runs of elements; [[SharedPtr]] shares one object.
namespace engine_placeholder {

/// The default deleter of [[UniquePtr]]: runs the destructor and returns the storage to the heap.
///
/// A `DefaultDelete<Base>` may delete a `Derived` through a virtual destructor, provided the object
/// was made by [[MakeUnique]] (heap storage), `Base` is the first (or only) base so the pointers
/// coincide, and `Derived` is not aligned beyond 16 bytes.
///
/// - Thread-safety: thread-safe; stateless.
/// - Ownership: takes the pointer it is given.
template <typename T>
struct DefaultDelete {
    /// Creates the deleter.
    constexpr DefaultDelete() = default;
    /// Converts from the deleter of a derived type, so `UniquePtr<Derived>` moves into `UniquePtr<Base>`.
    template <typename U>
        requires IsConvertible<U*, T*>
    constexpr DefaultDelete(const DefaultDelete<U>&) {
        static_assert(IsSame<T, U> || HasVirtualDestructor<T>, "DefaultDelete<Base>: deleting a derived object needs a virtual destructor on Base");
        static_assert(alignof(U) <= 16, "DefaultDelete<Base>: a derived type aligned beyond 16 bytes would be released with the wrong alignment");
    }

    /// Destroys `ptr` and frees its storage.
    ///
    /// - Param ptr: object made by [[MakeUnique]], or null (ignored).
    /// - Ownership: takes the object; it is destroyed and its storage freed.
    void operator()(T* ptr) const {
        if (ptr != nullptr) {
            ptr->~T();
            HeapAllocator::Default().Free(static_cast<void*>(ptr), sizeof(T), alignof(T));
        }
    }
};

/// Owns at most one `T` and destroys it with `Deleter` when it goes out of scope.
///
/// Move-only. `Deleter` is any callable taking `T*` (a Vulkan handle wrapper, an arena that only runs
/// the destructor); an empty deleter adds no size. A pointer type with a custom deleter should get its
/// own `using` alias, such as `DevicePtr`, so call sites do not spell the deleter out.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: owns the object; it is destroyed by `Reset`, by assignment and by the destructor.
template <typename T, typename Deleter = DefaultDelete<T>>
class UniquePtr {
public:
    /// Creates an empty pointer.
    constexpr UniquePtr() = default;
    /// Creates an empty pointer.
    constexpr UniquePtr(decltype(nullptr)) {}

    /// Takes ownership of `ptr`, which `deleter` must be able to release.
    ///
    /// - Param ptr: object to own, or null.
    /// - Param deleter: releases the object; copied into the pointer.
    explicit UniquePtr(T* ptr, Deleter deleter = Deleter()) : deleter_(Move(deleter)), ptr_(ptr) {}

    /// Destroys the owned object, if any.
    ~UniquePtr() { Reset(); }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    /// Takes over `other`'s object, leaving `other` empty.
    UniquePtr(UniquePtr&& other) noexcept : deleter_(Move(other.deleter_)), ptr_(other.Release()) {}

    /// Takes over the object of a pointer to a derived type, leaving `other` empty.
    template <typename U, typename E>
        requires(IsConvertible<U*, T*> && IsConstructible<Deleter, E &&>)
    UniquePtr(UniquePtr<U, E>&& other) noexcept : deleter_(Move(other.GetDeleter())), ptr_(other.Release()) {}

    /// Destroys the current object, then takes over `other`'s, leaving `other` empty.
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            Reset();
            deleter_ = Move(other.deleter_);
            ptr_ = other.Release();
        }
        return *this;
    }

    /// Destroys the owned object and leaves the pointer empty.
    UniquePtr& operator=(decltype(nullptr)) {
        Reset();
        return *this;
    }

    /// Returns whether the pointer is empty.
    friend bool operator==(const UniquePtr& p, decltype(nullptr)) { return p.ptr_ == nullptr; }
    /// Returns whether the pointer owns an object.
    friend bool operator!=(const UniquePtr& p, decltype(nullptr)) { return p.ptr_ != nullptr; }

    /// Returns the owned pointer without giving up ownership; null if empty.
    T* Get() const { return ptr_; }
    /// Returns the owned object.
    ///
    /// - Aborts: never checked; the pointer must not be empty.
    T* operator->() const { return ptr_; }
    /// Returns the owned object; the pointer must not be empty (not checked).
    T& operator*() const { return *ptr_; }
    /// Returns whether an object is owned.
    explicit operator bool() const { return ptr_ != nullptr; }

    /// Gives up ownership and returns the object; the caller must release it with the deleter.
    T* Release() {
        T* released = ptr_;
        ptr_ = nullptr;
        return released;
    }

    /// Destroys the owned object (if any) and takes ownership of `ptr`.
    ///
    /// - Param ptr: new object to own, or null to just release the old one.
    void Reset(T* ptr = nullptr) {
        T* old = ptr_;
        ptr_ = ptr;
        if (old != nullptr) {
            deleter_(old);
        }
    }

    /// Returns the deleter.
    Deleter& GetDeleter() { return deleter_; }
    /// Returns the deleter.
    const Deleter& GetDeleter() const { return deleter_; }

private:
    ENGINE_NO_UNIQUE_ADDRESS Deleter deleter_{};
    T* ptr_ = nullptr;
};

/// Allocates a `T` on the heap, constructs it from `args` and returns an owning pointer to it.
///
/// `Deleter` must release heap storage obtained this way (the default does). If the constructor is
/// private, make `MakeUnique` a friend or give the class a public constructor.
///
/// - Param args: forwarded to the constructor.
/// - Returns: the owning pointer, or an empty one if the heap is exhausted.
template <typename T, typename Deleter = DefaultDelete<T>, typename... Args>
UniquePtr<T, Deleter> MakeUnique(Args&&... args) {
    void* mem = HeapAllocator::Default().Alloc(sizeof(T), alignof(T));
    if (mem == nullptr) {
        return UniquePtr<T, Deleter>();
    }
    return UniquePtr<T, Deleter>(new (mem) T(Forward<Args>(args)...));
}

} // namespace engine_placeholder
