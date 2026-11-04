#pragma once

#include <new>
#include <utility>

#include "log/assert.h"
#include "types.h"

//! Growable array whose storage comes from a caller-supplied allocator.
//!
//! Old backing blocks are never freed individually on growth; they are
//! reclaimed in bulk when the allocator is reset.
namespace engine_placeholder {

/// Growable contiguous array of `T` with storage taken from an allocator.
///
/// `Allocator` must provide `void* Alloc(usize size, usize align)` returning
/// nullptr on exhaustion; [[ArenaAllocator]] satisfies this. Capacity starts at
/// 4 elements on first push and doubles. Growth moves elements into a new
/// block and abandons the old one inside the allocator (none of the engine
/// allocators can free an arbitrary block), so repeated growth wastes
/// allocator memory until it is reset. Growing invalidates references and
/// pointers to elements.
///
/// Thread-safety: not-thread-safe. Synchronize externally if shared.
/// Ownership: the array destroys its elements but never frees storage; the
///   allocator must outlive the array and owns the memory.
template <typename T, typename Allocator>
class Array {
public:
    /// Creates an empty array that allocates from `allocator`.
    ///
    /// Param allocator: allocator to draw storage from; held by pointer and
    ///   must outlive the array. No memory is allocated until the first growth.
    explicit Array(Allocator& allocator) : allocator_(&allocator) {}

    /// Destroys all elements. Does not release storage.
    ~Array() {
        DestroyRange(0, size_);
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    /// Takes over `other`'s elements and storage, leaving `other` empty with zero capacity.
    Array(Array&& other) noexcept
        : allocator_(other.allocator_), data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    /// Destroys this array's elements, then takes over `other`'s elements,
    /// storage and allocator, leaving `other` empty with zero capacity.
    ///
    /// This array's old storage is abandoned, not freed.
    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            DestroyRange(0, size_);
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
    /// Param new_capacity: required capacity in elements.
    /// Aborts: the allocator returns nullptr (out of memory).
    void Reserve(usize new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }
        void* mem = allocator_->Alloc(new_capacity * sizeof(T), alignof(T));
        ENGINE_ASSERT(mem != nullptr, "Array::Reserve: allocator out of memory");
        T* new_data = static_cast<T*>(mem);
        for (usize i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        data_ = new_data;
        capacity_ = new_capacity;
    }

    /// Appends a copy of `value`, growing if full.
    ///
    /// Param value: element to copy; it must not refer to an element of this
    ///   array because growth may invalidate it.
    /// Aborts: growth is needed and the allocator is out of memory.
    void Push(const T& value) {
        GrowIfNeeded();
        new (data_ + size_) T(value);
        ++size_;
    }

    /// Appends `value` by move, growing if full.
    ///
    /// Param value: element to move from; it must not refer to an element of
    ///   this array because growth may invalidate it.
    /// Aborts: growth is needed and the allocator is out of memory.
    void Push(T&& value) {
        GrowIfNeeded();
        new (data_ + size_) T(std::move(value));
        ++size_;
    }

    /// Destroys the last element.
    ///
    /// Aborts: the array is empty.
    void Pop() {
        ENGINE_ASSERT(size_ > 0, "Array::Pop: called on an empty array");
        --size_;
        data_[size_].~T();
    }

    /// Inserts a copy of `value` before position `index`, shifting later
    /// elements up by one.
    ///
    /// Param index: destination position; `Size()` appends. Must be at most
    ///   `Size()` (not checked).
    /// Param value: element to copy; must not refer to an element of this array.
    /// Aborts: growth is needed and the allocator is out of memory.
    void Insert(usize index, const T& value) {
        GrowIfNeeded();
        if (size_ > index) {
            new (data_ + size_) T(std::move(data_[size_ - 1]));
            for (usize i = size_ - 1; i > index; --i) {
                data_[i] = std::move(data_[i - 1]);
            }
            data_[index] = value;
        } else {
            new (data_ + size_) T(value);
        }
        ++size_;
    }

    /// Removes the element at `index`, shifting later elements down by one.
    ///
    /// Param index: position to remove; must be less than `Size()` (not checked).
    void Remove(usize index) {
        for (usize i = index; i + 1 < size_; ++i) {
            data_[i] = std::move(data_[i + 1]);
        }
        --size_;
        data_[size_].~T();
    }

    /// Returns a reference to the element at `index`.
    ///
    /// Param index: element position; must be less than `Size()` (not checked).
    T& operator[](usize index) { return data_[index]; }
    /// Returns a const reference to the element at `index`.
    ///
    /// Param index: element position; must be less than `Size()` (not checked).
    const T& operator[](usize index) const { return data_[index]; }

    /// Returns the number of elements.
    usize Size() const { return size_; }
    /// Returns the number of elements the current storage can hold without growing.
    usize Capacity() const { return capacity_; }

private:
    void GrowIfNeeded() {
        if (size_ < capacity_) {
            return;
        }
        Reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }

    void DestroyRange(usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            data_[i].~T();
        }
    }

    Allocator* allocator_ = nullptr;
    T* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;
};

} // namespace engine_placeholder
