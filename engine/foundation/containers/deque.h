#pragma once

#include "lang/new.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! [[Deque]]: a growable ring of elements, pushed and popped at both ends in O(1), for
//! the queues that were written as `Vector::Remove(0)`.
namespace vse
{

template <typename T, typename Allocator> class Deque;

namespace detail
{

// The range-for iterator of a Deque: front to back.
template <typename D, typename T> class DequeIterator
{
public:
    DequeIterator(D* deque, usize index) : deque_(deque), index_(index) {}

    T& operator*() const { return (*deque_)[index_]; }
    T* operator->() const { return &(*deque_)[index_]; }

    DequeIterator& operator++()
    {
        ++index_;
        return *this;
    }

    bool operator==(const DequeIterator& other) const { return index_ == other.index_; }
    bool operator!=(const DequeIterator& other) const { return index_ != other.index_; }

private:
    D* deque_;
    usize index_;
};

}  // namespace detail

/// A double-ended queue of `T` in one growable ring: `PushBack`, `PushFront`, `PopFront` and `PopBack` are O(1)
/// (amortized for the pushes), and `operator[]` counts from the front.
///
/// The capacity is a power of two (at least 8), allocated on the first push and doubled when full; growth moves the
/// elements into a new block, front first, and invalidates references to them. `T` may be move-only.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: the deque destroys its elements; its storage comes from the allocator, freed when the allocator has
///   `Free` (as [[Vector]]), else abandoned inside it; the allocator must outlive the deque.
template <typename T, typename Allocator = HeapAllocator> class Deque
{
public:
    /// Creates an empty deque on `allocator`; nothing is allocated until the first push.
    ///
    /// - Param allocator: held by pointer; must outlive the deque.
    explicit Deque(Allocator& allocator) : allocator_(&allocator) {}

    /// Creates an empty deque on the shared default allocator.
    ///
    /// Only available when the allocator has a static `Default()`, as [[HeapAllocator]] does.
    Deque()
        requires requires { Allocator::Default(); }
        : allocator_(&Allocator::Default())
    {
    }

    /// Destroys the elements and releases the storage.
    ~Deque()
    {
        Clear();
        Release();
    }

    Deque(const Deque&) = delete;
    Deque& operator=(const Deque&) = delete;

    /// Takes over `other`'s elements and storage, leaving it empty.
    Deque(Deque&& other) noexcept
        : allocator_(other.allocator_), data_(other.data_), capacity_(other.capacity_), head_(other.head_),
          size_(other.size_)
    {
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.head_ = 0;
        other.size_ = 0;
    }

    /// Destroys this deque's elements, then takes over `other`'s elements, storage and allocator.
    Deque& operator=(Deque&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            Release();
            allocator_ = other.allocator_;
            data_ = other.data_;
            capacity_ = other.capacity_;
            head_ = other.head_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.capacity_ = 0;
            other.head_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    /// Appends `value` at the back.
    ///
    /// - Param value: copied or moved in; it must not be an element of this deque.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename U>
        requires IsConstructible<T, U&&>
    void PushBack(U&& value)
    {
        GrowIfFull();
        ConstructAt<T>(Slot(size_), Forward<U>(value));
        ++size_;
    }

    /// Inserts `value` at the front.
    ///
    /// - Param value: copied or moved in; it must not be an element of this deque.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename U>
        requires IsConstructible<T, U&&>
    void PushFront(U&& value)
    {
        GrowIfFull();
        head_ = (head_ + capacity_ - 1) & (capacity_ - 1);
        ConstructAt<T>(data_ + head_, Forward<U>(value));
        ++size_;
    }

    /// Removes the front element and returns it.
    ///
    /// - Returns: the element that was at the front.
    /// - Aborts: the deque is empty.
    T PopFront()
    {
        ENGINE_ASSERT(size_ > 0, "Deque::PopFront: the deque is empty");
        T value(Move(data_[head_]));
        data_[head_].~T();
        head_ = (head_ + 1) & (capacity_ - 1);
        --size_;
        return value;
    }

    /// Removes the back element and returns it.
    ///
    /// - Returns: the element that was at the back.
    /// - Aborts: the deque is empty.
    T PopBack()
    {
        ENGINE_ASSERT(size_ > 0, "Deque::PopBack: the deque is empty");
        T* slot = Slot(size_ - 1);
        T value(Move(*slot));
        slot->~T();
        --size_;
        return value;
    }

    /// Returns the front element.
    ///
    /// - Aborts: the deque is empty.
    T& Front()
    {
        ENGINE_ASSERT(size_ > 0, "Deque::Front: the deque is empty");
        return data_[head_];
    }
    /// Returns the front element of a const deque.
    ///
    /// - Aborts: the deque is empty.
    const T& Front() const
    {
        ENGINE_ASSERT(size_ > 0, "Deque::Front: the deque is empty");
        return data_[head_];
    }
    /// Returns the back element.
    ///
    /// - Aborts: the deque is empty.
    T& Back()
    {
        ENGINE_ASSERT(size_ > 0, "Deque::Back: the deque is empty");
        return *Slot(size_ - 1);
    }
    /// Returns the back element of a const deque.
    ///
    /// - Aborts: the deque is empty.
    const T& Back() const
    {
        ENGINE_ASSERT(size_ > 0, "Deque::Back: the deque is empty");
        return *Slot(size_ - 1);
    }

    /// Returns the element `index` places from the front.
    ///
    /// - Param index: position from the front.
    /// - Aborts: `index` is not less than `Size()`.
    T& operator[](usize index)
    {
        ENGINE_ASSERT(index < size_, "Deque::operator[]: index out of range");
        return *Slot(index);
    }
    /// Returns the element `index` places from the front of a const deque.
    ///
    /// - Param index: position from the front.
    /// - Aborts: `index` is not less than `Size()`.
    const T& operator[](usize index) const
    {
        ENGINE_ASSERT(index < size_, "Deque::operator[]: index out of range");
        return *Slot(index);
    }

    /// Destroys every element and keeps the storage.
    void Clear()
    {
        for (usize i = 0; i < size_; ++i)
        {
            Slot(i)->~T();
        }
        head_ = 0;
        size_ = 0;
    }

    /// Makes room for `count` elements without growing again.
    ///
    /// - Param count: the number of elements to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count)
    {
        if (count > capacity_)
        {
            usize cap = capacity_ == 0 ? 8 : capacity_;
            while (cap < count)
            {
                cap *= 2;
            }
            Regrow(cap);
        }
    }

    /// Returns the number of elements.
    usize Size() const { return size_; }
    /// Returns true when there are no elements.
    bool Empty() const { return size_ == 0; }
    /// Returns the number of elements the storage holds without growing.
    usize Capacity() const { return capacity_; }

    /// The front element, for a range-for (front to back).
    ///
    /// - Internal: the range-for protocol.
    detail::DequeIterator<Deque, T> begin() { return detail::DequeIterator<Deque, T>(this, 0); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    detail::DequeIterator<Deque, T> end() { return detail::DequeIterator<Deque, T>(this, size_); }
    /// The front element of a const deque, for a range-for.
    ///
    /// - Internal: the range-for protocol.
    detail::DequeIterator<const Deque, const T> begin() const
    {
        return detail::DequeIterator<const Deque, const T>(this, 0);
    }
    /// The end of a range-for over a const deque.
    ///
    /// - Internal: the range-for protocol.
    detail::DequeIterator<const Deque, const T> end() const
    {
        return detail::DequeIterator<const Deque, const T>(this, size_);
    }

private:
    T* Slot(usize index) const { return data_ + ((head_ + index) & (capacity_ - 1)); }

    void GrowIfFull()
    {
        if (size_ == capacity_)
        {
            Regrow(capacity_ == 0 ? 8 : capacity_ * 2);
        }
    }

    // Moves the elements, front first, into a new block of `new_capacity` (a power of two).
    void Regrow(usize new_capacity)
    {
        ENGINE_ASSERT(new_capacity <= static_cast<usize>(-1) / sizeof(T), "Deque: the size overflows");
        T* fresh = static_cast<T*>(allocator_->Alloc(new_capacity * sizeof(T), alignof(T)));
        ENGINE_ASSERT(fresh != nullptr, "Deque: allocator out of memory");
        for (usize i = 0; i < size_; ++i)
        {
            T* from = Slot(i);
            ConstructAt<T>(fresh + i, Move(*from));
            from->~T();
        }
        Release();
        data_ = fresh;
        capacity_ = new_capacity;
        head_ = 0;
    }

    void Release()
    {
        if constexpr (requires(Allocator& a) { a.Free(static_cast<void*>(nullptr), usize{0}, usize{0}); })
        {
            if (data_ != nullptr)
            {
                allocator_->Free(data_, capacity_ * sizeof(T), alignof(T));
            }
        }
        data_ = nullptr;
    }

    Allocator* allocator_ = nullptr;
    T* data_ = nullptr;
    usize capacity_ = 0;  // a power of two, or 0 before the first push
    usize head_ = 0;      // the slot of the front element
    usize size_ = 0;
};

}  // namespace vse
