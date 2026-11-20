#pragma once

#include "cstr/mem_ops.h"
#include "lang/traits.h"
#include "lang/utility.h"

#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! Growable array that owns its storage: on the heap by default, or from a
//! caller-supplied allocator such as an arena. The fixed-size, inline
//! counterpart is [[Array]].
namespace vse
{

/// Growable contiguous array of `T`; the default allocator is the heap.
///
/// `Allocator` must provide `void* Alloc(usize size, usize align)` returning
/// nullptr on exhaustion; [[HeapAllocator]] and [[ArenaAllocator]] satisfy this.
/// If it also provides `void Free(void* ptr, usize size, usize align)` (the heap
/// does) the vector frees its old block on growth and its storage on destruction;
/// otherwise (an arena) old blocks are abandoned inside the allocator until it is
/// reset. Capacity starts at 4 elements on first push and doubles. Growing
/// invalidates references and pointers to elements. This replaces a hand-paired
/// `new T[n]` / `delete[]`: use `Resize` for a runtime-sized table.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: the vector destroys its elements. It frees storage only when the
///   allocator has `Free`; the allocator must outlive the vector.
template <typename T, typename Allocator = HeapAllocator> class Vector
{
public:
    /// Creates an empty array that allocates from `allocator`.
    ///
    /// - Param allocator: allocator to draw storage from; held by pointer and
    ///   must outlive the array. No memory is allocated until the first growth.
    explicit Vector(Allocator& allocator) : allocator_(&allocator) {}

    /// Creates an empty vector on the shared default allocator.
    ///
    /// Only available when the allocator has a static `Default()`, as [[HeapAllocator]] does.
    Vector()
        requires requires { Allocator::Default(); }
        : allocator_(&Allocator::Default())
    {
    }

    /// Destroys all elements and, if the allocator has `Free`, releases the storage.
    ~Vector()
    {
        DestroyRange(0, size_);
        ReleaseStorage();
    }

    Vector(const Vector&) = delete;
    Vector& operator=(const Vector&) = delete;

    /// Takes over `other`'s elements and storage, leaving `other` empty with zero capacity.
    Vector(Vector&& other) noexcept
        : allocator_(other.allocator_), data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    /// Destroys this array's elements, then takes over `other`'s elements,
    /// storage and allocator, leaving `other` empty with zero capacity.
    ///
    /// This vector's old storage is freed if its allocator has `Free`, else abandoned.
    Vector& operator=(Vector&& other) noexcept
    {
        if (this != &other)
        {
            DestroyRange(0, size_);
            ReleaseStorage();
            allocator_ = other.allocator_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    /// Ensures capacity for at least `new_capacity` elements, moving existing
    /// elements to a new block if it must grow.
    ///
    /// Does nothing if the capacity is already sufficient. Invalidates element
    /// references when it grows.
    ///
    /// - Param new_capacity: required capacity in elements.
    /// - Aborts: the allocator returns nullptr (out of memory).
    void Reserve(usize new_capacity)
    {
        ENGINE_ASSERT(TryReserve(new_capacity),
                      "Vector::Reserve: allocator out of memory (or an element type that cannot move was grown)");
    }

    /// Like `Reserve`, but reports allocator exhaustion instead of aborting.
    ///
    /// - Param new_capacity: required capacity in elements.
    /// - Returns: true if the capacity is now sufficient; false if the allocator is
    ///   out of memory, the byte size overflows, or `T` cannot be moved and the vector
    ///   already holds elements (the vector is unchanged in every case).
    bool TryReserve(usize new_capacity)
    {
        if (new_capacity <= capacity_)
        {
            return true;
        }
        if (new_capacity > static_cast<usize>(-1) / sizeof(T))
        {
            return false;
        }
        if constexpr (!IsMoveConstructible<T>)
        {
            if (size_ != 0)
            {
                return false;  // the elements cannot be carried to a new block
            }
        }
        void* mem = allocator_->Alloc(new_capacity * sizeof(T), alignof(T));
        if (mem == nullptr)
        {
            return false;
        }
        T* new_data = static_cast<T*>(mem);
        if constexpr (IsTriviallyCopyable<T>)
        {
            if (size_ != 0)
            {
                MemCopy(new_data, data_, size_ * sizeof(T));  // a trivially copyable element moves as its bytes
            }
        }
        else if constexpr (IsMoveConstructible<T>)
        {
            for (usize i = 0; i < size_; ++i)
            {
                new (new_data + i) T(Move(data_[i]));
                data_[i].~T();
            }
        }
        ReleaseStorage();
        data_ = new_data;
        capacity_ = new_capacity;
        return true;
    }

    /// Changes the size to `new_size`: new elements are value-initialized (zero for
    /// scalars) and a smaller size destroys the tail. An empty vector gets exactly
    /// `new_size` capacity (a table sized once); one that already holds elements grows
    /// at least geometrically, so repeated `Resize(Size() + k)` is not quadratic.
    ///
    /// - Param new_size: the new element count.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Resize(usize new_size)
    {
        ENGINE_ASSERT(TryResize(new_size),
                      "Vector::Resize: allocator out of memory (or an element type that cannot move was grown)");
    }

    /// Like `Resize`, but reports failure instead of aborting.
    ///
    /// - Param new_size: the new element count.
    /// - Returns: true on success; false if the allocator is out of memory, or `T` cannot be moved
    ///   and the vector already holds elements (the vector is unchanged).
    bool TryResize(usize new_size)
    {
        if (new_size <= size_)
        {
            DestroyRange(new_size, size_);
            size_ = new_size;
            return true;
        }
        if (!GrowTo(new_size))
        {
            return false;
        }
        for (usize i = size_; i < new_size; ++i)
        {
            new (data_ + i) T();
        }
        size_ = new_size;
        return true;
    }

    /// Like `TryResize`, but new elements are left uninitialized: for byte buffers and other
    /// trivial types that are about to be overwritten, where zero-filling first is wasted work.
    ///
    /// - Param new_size: the new element count.
    /// - Returns: true on success; false if the allocator is out of memory (the vector is unchanged).
    bool TryResizeUninitialized(usize new_size)
    {
        static_assert(IsTriviallyDefaultConstructible<T> && IsTriviallyDestructible<T>,
                      "Vector::TryResizeUninitialized: only trivial element types may be left uninitialized");
        if (new_size <= size_)
        {
            size_ = new_size;
            return true;
        }
        if (!GrowTo(new_size))
        {
            return false;
        }
        size_ = new_size;
        return true;
    }

    /// Destroys every element, keeping the storage.
    void Clear()
    {
        DestroyRange(0, size_);
        size_ = 0;
    }

    /// Destroys every element and releases the storage (if the allocator has `Free`), leaving the vector empty.
    void Reset()
    {
        DestroyRange(0, size_);
        ReleaseStorage();
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }

    /// Appends a copy of `value`, growing if full.
    ///
    /// - Param value: element to copy; it may be an element of this vector.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Push(const T& value)
    {
        if (size_ == capacity_ && InStorage(&value))
        {
            // `value` may be an element of this vector (`v.Push(v[0])`), which growing frees: copy it first.
            T copy(value);
            GrowIfNeeded();
            new (data_ + size_) T(Move(copy));
        }
        else
        {
            GrowIfNeeded();
            new (data_ + size_) T(value);
        }
        ++size_;
    }

    /// Appends `value` by move, growing if full.
    ///
    /// - Param value: element to move from; it may be an element of this vector.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Push(T&& value)
    {
        if (size_ == capacity_ && InStorage(&value))
        {
            T moved(Move(value));  // as above: `value` may live in the storage growing frees
            GrowIfNeeded();
            new (data_ + size_) T(Move(moved));
        }
        else
        {
            GrowIfNeeded();
            new (data_ + size_) T(Move(value));
        }
        ++size_;
    }

    /// Destroys the last element.
    ///
    /// - Aborts: the array is empty.
    void Pop()
    {
        ENGINE_ASSERT(size_ > 0, "Vector::Pop: called on an empty vector");
        --size_;
        data_[size_].~T();
    }

    /// Inserts a copy of `value` before position `index`, shifting later
    /// elements up by one.
    ///
    /// - Param index: destination position; `Size()` appends. Must be at most
    ///   `Size()` (not checked).
    /// - Param value: element to copy; it may be an element of this vector.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Insert(usize index, const T& value)
    {
        // `value` may be an element of this vector: growing would free it and the shift below would move it, so copy it
        // first.
        T copy(value);
        GrowIfNeeded();
        if (size_ > index)
        {
            new (data_ + size_) T(Move(data_[size_ - 1]));
            for (usize i = size_ - 1; i > index; --i)
            {
                data_[i] = Move(data_[i - 1]);
            }
            data_[index] = Move(copy);
        }
        else
        {
            new (data_ + size_) T(Move(copy));
        }
        ++size_;
    }

    /// Inserts `value` by move before position `index`, shifting later elements up by one.
    ///
    /// - Param index: destination position; `Size()` appends.
    /// - Param value: element to move in (the value is moved into a local first, so it may alias an element).
    /// - Aborts: `index` is greater than `Size()`, or growth is needed and the allocator is out of memory.
    void Insert(usize index, T&& value)
    {
        ENGINE_ASSERT(index <= size_, "Vector::Insert: index is past the end");
        T moved(Move(value));
        GrowIfNeeded();
        if (size_ > index)
        {
            new (data_ + size_) T(Move(data_[size_ - 1]));
            for (usize i = size_ - 1; i > index; --i)
            {
                data_[i] = Move(data_[i - 1]);
            }
            data_[index] = Move(moved);
        }
        else
        {
            new (data_ + size_) T(Move(moved));
        }
        ++size_;
    }

    /// Removes the element at `index`, shifting later elements down by one.
    ///
    /// - Param index: position to remove; must be less than `Size()` (not checked).
    void Remove(usize index)
    {
        for (usize i = index; i + 1 < size_; ++i)
        {
            data_[i] = Move(data_[i + 1]);
        }
        --size_;
        data_[size_].~T();
    }

    /// Removes `count` elements starting at `first`, shifting later elements down once (one pass, unlike `count` calls
    /// of `Remove`).
    ///
    /// - Param first: position of the first element to remove.
    /// - Param count: how many to remove; `first + count` must be at most `Size()`.
    /// - Aborts: `first + count` is greater than `Size()`.
    void RemoveRange(usize first, usize count)
    {
        ENGINE_ASSERT(first <= size_ && count <= size_ - first, "Vector::RemoveRange: the range is past the end");
        if (count == 0)
        {
            return;
        }
        for (usize i = first; i + count < size_; ++i)
        {
            data_[i] = Move(data_[i + count]);
        }
        DestroyRange(size_ - count, size_);
        size_ -= count;
    }

    /// Removes the element at `index` by moving the last element into its place: O(1), and the order of the others
    /// is not kept (use `Remove` where it matters).
    ///
    /// - Param index: position to remove.
    /// - Aborts: `index` is not less than `Size()`.
    void SwapRemove(usize index)
    {
        ENGINE_ASSERT(index < size_, "Vector::SwapRemove: index is past the end");
        if (index + 1 != size_)
        {
            data_[index] = Move(data_[size_ - 1]);
        }
        Pop();
    }

    /// Appends a copy of `count` elements starting at `items`, growing once; trivially copyable elements are copied as
    /// bytes.
    ///
    /// - Param items: the first element to copy; the elements may be elements of this vector.
    /// - Param count: how many to copy.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Append(const T* items, usize count)
    {
        if (count == 0)
        {
            return;
        }
        if (InStorage(items))
        {
            // The source is part of this vector, which growing frees: copy from its offset after growing.
            const usize offset = static_cast<usize>(items - data_);
            Reserve(GrowTarget(size_ + count));
            for (usize i = 0; i < count; ++i)
            {
                new (data_ + size_) T(data_[offset + i]);
                ++size_;
            }
            return;
        }
        Reserve(GrowTarget(size_ + count));
        if constexpr (IsTriviallyCopyable<T>)
        {
            MemCopy(data_ + size_, items, count * sizeof(T));
            size_ += count;
        }
        else
        {
            for (usize i = 0; i < count; ++i)
            {
                new (data_ + size_) T(items[i]);
                ++size_;
            }
        }
    }

    /// Appends a copy of every element of `items`: a [[Span]], an [[Array]], another [[Vector]] or anything with
    /// `Data()` and `Size()`.
    ///
    /// - Param items: the elements to copy.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename C>
        requires(IsConvertible<decltype(static_cast<const C*>(nullptr)->Data()), const T*> &&
                 requires(const C& c) { c.Size(); })
    void Append(const C& items)
    {
        Append(items.Data(), static_cast<usize>(items.Size()));
    }

    /// Constructs an element at the end from `args`, growing if full.
    ///
    /// - Param args: the arguments of `T`'s constructor; they must not refer to elements of this vector.
    /// - Returns: the new element; valid until the next growth.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename... Args> T& Emplace(Args&&... args)
    {
        GrowIfNeeded();
        new (data_ + size_) T(Forward<Args>(args)...);
        return data_[size_++];
    }

    /// Returns a copy of this vector on the same allocator: the explicit copy, since the copy constructor is deleted
    /// so that copies of large data are visible.
    ///
    /// - Aborts: the allocator is out of memory.
    Vector Clone() const
        requires IsCopyConstructible<T>
    {
        Vector copy(*allocator_);
        copy.Append(data_, size_);
        return copy;
    }

    /// Exchanges the elements, storage and allocator of two vectors; no element is moved or copied.
    ///
    /// - Param other: the vector to exchange with.
    void Swap(Vector& other)
    {
        vse::Swap(allocator_, other.allocator_);
        vse::Swap(data_, other.data_);
        vse::Swap(size_, other.size_);
        vse::Swap(capacity_, other.capacity_);
    }

    /// The first element, for a range-for (`for (T& item : vector)`).
    ///
    /// - Internal: the range-for protocol.
    T* begin() { return data_; }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    T* end() { return data_ + size_; }
    /// The first element of a const vector, for a range-for.
    ///
    /// - Internal: the range-for protocol.
    const T* begin() const { return data_; }
    /// The end of a range-for over a const vector.
    ///
    /// - Internal: the range-for protocol.
    const T* end() const { return data_ + size_; }

    /// Returns a reference to the element at `index`.
    ///
    /// - Param index: element position; must be less than `Size()` (not checked).
    T& operator[](usize index) { return data_[index]; }
    /// Returns a const reference to the element at `index`.
    ///
    /// - Param index: element position; must be less than `Size()` (not checked).
    const T& operator[](usize index) const { return data_[index]; }

    /// Returns a pointer to the first element; null while the capacity is zero. Invalidated by growth.
    T* Data() { return data_; }
    /// Returns a const pointer to the first element; null while the capacity is zero.
    const T* Data() const { return data_; }

    /// Returns the number of elements.
    usize Size() const { return size_; }
    /// Returns true if there are no elements.
    bool Empty() const { return size_ == 0; }
    /// Returns the number of elements the current storage can hold without growing.
    usize Capacity() const { return capacity_; }

private:
    // True if `p` points into this vector's elements (compared as addresses: the value may come from anywhere).
    bool InStorage(const T* p) const
    {
        const auto address = reinterpret_cast<uintptr_t>(p);
        const auto begin = reinterpret_cast<uintptr_t>(data_);
        return data_ != nullptr && address >= begin && address < begin + size_ * sizeof(T);
    }

    void GrowIfNeeded()
    {
        if (size_ < capacity_)
        {
            return;
        }
        Reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }

    bool GrowTo(usize new_size)
    {
        if (new_size <= capacity_)
        {
            return true;
        }
        return TryReserve(GrowTarget(new_size));
    }

    // The capacity to reserve for `new_size` elements: at least double the current one once the vector holds
    // elements, so that repeated appends are amortized O(1).
    usize GrowTarget(usize new_size) const
    {
        if (new_size <= capacity_)
        {
            return capacity_;
        }
        return size_ != 0 && capacity_ * 2 > new_size ? capacity_ * 2 : new_size;
    }

    void ReleaseStorage()
    {
        if constexpr (requires(Allocator& a) { a.Free(static_cast<void*>(nullptr), usize{0}, usize{0}); })
        {
            if (data_ != nullptr)
            {
                allocator_->Free(data_, capacity_ * sizeof(T), alignof(T));
            }
        }
    }

    void DestroyRange(usize begin, usize end)
    {
        for (usize i = begin; i < end; ++i)
        {
            data_[i].~T();
        }
    }

    Allocator* allocator_ = nullptr;
    T* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;
};

}  // namespace vse
