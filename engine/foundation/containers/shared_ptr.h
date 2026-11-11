#pragma once

#include "sync/atomic.h"
#include "lang/traits.h"
#include "lang/utility.h"

#include "mem/heap_allocator.h"
#include "types.h"

//! Reference-counted owning pointer for objects with several owners. [[UniquePtr]] is the default;
//! use this only where ownership is genuinely shared.
namespace engine_placeholder
{

/// Reference count safe to change from several threads at once; the default policy of [[SharedPtr]].
struct AtomicRefCount
{
    /// Number of owners; starts at one (the creator).
    Atomic<usize> count{1};
    /// Adds an owner.
    void Increment() { count.FetchAdd(1, MemoryOrder::Relaxed); }
    /// Removes an owner; returns true if it was the last.
    bool Decrement() { return count.FetchSub(1, MemoryOrder::AcqRel) == 1; }
    /// Returns the current number of owners (a snapshot).
    usize Load() const { return count.Load(MemoryOrder::Relaxed); }
};

/// Reference count for pointers that never leave one thread; no atomic operations.
struct PlainRefCount
{
    /// Number of owners; starts at one (the creator).
    usize count = 1;
    /// Adds an owner.
    void Increment() { ++count; }
    /// Removes an owner; returns true if it was the last.
    bool Decrement() { return --count == 0; }
    /// Returns the current number of owners.
    usize Load() const { return count; }
};

namespace detail
{
/// Passkey for the adopting [[SharedPtr]] constructor: only [[MakeShared]] and this header make one.
struct SharedAdopt
{
    explicit SharedAdopt() = default;
};
}  // namespace detail

/// Control block shared by every [[SharedPtr]] to one object; type-erased so a pointer to a derived
/// type converts to a pointer to its base.
template <typename Count> struct SharedBlock
{
    /// The owner count.
    Count refs;
    /// Destroys the object and frees the block (called when the last owner goes).
    void (*destroy)(SharedBlock*) = nullptr;
};

/// Shares ownership of one `T`, destroying it when the last owner goes away.
///
/// The object and its count live in one heap allocation made by [[MakeShared]]. `Count` selects the
/// policy: [[AtomicRefCount]] (the default) lets owners on different threads copy and release their
/// own pointers concurrently; [[PlainRefCount]] is cheaper for pointers confined to one thread. The
/// pointed-to object itself is not synchronized by either. There are no weak pointers.
///
/// - Thread-safety: thread-safe for copying and destroying distinct pointers that share one object when
///   `Count` is `AtomicRefCount`; not-thread-safe with `PlainRefCount`. One `SharedPtr` object is never safe to
///   modify from two threads.
/// - Ownership: shares ownership of the object; it is destroyed when the last owner is released.
template <typename T, typename Count = AtomicRefCount> class SharedPtr
{
public:
    /// Creates an empty pointer.
    constexpr SharedPtr() = default;
    /// Creates an empty pointer.
    constexpr SharedPtr(decltype(nullptr)) {}

    /// Becomes another owner of `other`'s object.
    SharedPtr(const SharedPtr& other) : ptr_(other.ptr_), block_(other.block_) { Retain(); }

    /// Becomes another owner of the object of a pointer to a derived type.
    template <typename U>
        requires IsConvertible<U*, T*>
    SharedPtr(const SharedPtr<U, Count>& other) : ptr_(other.ptr_), block_(other.block_)
    {
        Retain();
    }

    /// Takes over `other`'s share, leaving `other` empty.
    SharedPtr(SharedPtr&& other) noexcept : ptr_(other.ptr_), block_(other.block_)
    {
        other.ptr_ = nullptr;
        other.block_ = nullptr;
    }

    /// Takes over the share of a pointer to a derived type, leaving `other` empty.
    template <typename U>
        requires IsConvertible<U*, T*>
    SharedPtr(SharedPtr<U, Count>&& other) noexcept : ptr_(other.ptr_), block_(other.block_)
    {
        other.ptr_ = nullptr;
        other.block_ = nullptr;
    }

    /// Releases the current share, then becomes another owner of `other`'s object.
    SharedPtr& operator=(const SharedPtr& other)
    {
        SharedPtr copy(other);
        Swap(copy);
        return *this;
    }

    /// Releases the current share, then takes over `other`'s, leaving `other` empty.
    SharedPtr& operator=(SharedPtr&& other) noexcept
    {
        SharedPtr moved(Move(other));
        Swap(moved);
        return *this;
    }

    /// Adopts a freshly made object and its block; only [[MakeShared]] can call it (it needs the passkey).
    ///
    /// - Param key: the passkey.
    /// - Param ptr: the object, inside the block's storage.
    /// - Param block: its control block, with a count of one.
    SharedPtr(detail::SharedAdopt key, T* ptr, SharedBlock<Count>* block) : ptr_(ptr), block_(block) { (void)key; }

    /// Releases this share.
    ~SharedPtr() { Reset(); }

    /// Releases this share and leaves the pointer empty.
    void Reset()
    {
        SharedBlock<Count>* block = block_;
        ptr_ = nullptr;
        block_ = nullptr;
        if (block != nullptr && block->refs.Decrement())
        {
            block->destroy(block);
        }
    }

    /// Returns whether the pointer is empty.
    friend bool operator==(const SharedPtr& p, decltype(nullptr)) { return p.ptr_ == nullptr; }
    /// Returns whether the pointer shares an object.
    friend bool operator!=(const SharedPtr& p, decltype(nullptr)) { return p.ptr_ != nullptr; }

    /// Returns the shared pointer; null if empty.
    T* Get() const { return ptr_; }
    /// Returns the shared object; the pointer must not be empty (not checked).
    T* operator->() const { return ptr_; }
    /// Returns the shared object; the pointer must not be empty (not checked).
    T& operator*() const { return *ptr_; }
    /// Returns whether an object is shared.
    explicit operator bool() const { return ptr_ != nullptr; }
    /// Returns how many pointers share the object (0 if empty). Only a snapshot while other threads run.
    usize UseCount() const { return block_ != nullptr ? block_->refs.Load() : 0; }

private:
    template <typename U, typename C> friend class SharedPtr;

    void Retain()
    {
        if (block_ != nullptr)
        {
            block_->refs.Increment();
        }
    }
    void Swap(SharedPtr& other)
    {
        T* p = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = p;
        SharedBlock<Count>* b = block_;
        block_ = other.block_;
        other.block_ = b;
    }

    T* ptr_ = nullptr;
    SharedBlock<Count>* block_ = nullptr;
};

/// A [[SharedPtr]] confined to one thread, with a non-atomic count.
template <typename T> using LocalSharedPtr = SharedPtr<T, PlainRefCount>;

namespace detail
{

// The object and its control block in one allocation.
template <typename T, typename Count> struct SharedStorage : SharedBlock<Count>
{
    T value;
    template <typename... Args> explicit SharedStorage(Args&&... args) : value(Forward<Args>(args)...)
    {
        this->destroy = &Destroy;
    }
    static void Destroy(SharedBlock<Count>* block)
    {
        SharedStorage* self = static_cast<SharedStorage*>(block);
        self->~SharedStorage();
        HeapAllocator::Default().Free(static_cast<void*>(self), sizeof(SharedStorage), alignof(SharedStorage));
    }
};

}  // namespace detail

/// Allocates a `T` and its control block together on the heap and returns the first owner.
///
/// - Param args: forwarded to the constructor.
/// - Returns: the owning pointer, or an empty one if the heap is exhausted.
template <typename T, typename Count = AtomicRefCount, typename... Args> SharedPtr<T, Count> MakeShared(Args&&... args)
{
    using Storage = detail::SharedStorage<T, Count>;
    void* mem = HeapAllocator::Default().Alloc(sizeof(Storage), alignof(Storage));
    if (mem == nullptr)
    {
        return SharedPtr<T, Count>();
    }
    Storage* storage = new (mem) Storage(Forward<Args>(args)...);
    return SharedPtr<T, Count>(detail::SharedAdopt(), &storage->value, storage);
}

}  // namespace engine_placeholder
