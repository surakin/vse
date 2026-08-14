#pragma once

#include <new>
#include <utility>

#include "types.h"

// Growable array over storage obtained from an `Allocator`, which must
// provide `void* Alloc(usize size, usize align)` — Day 4's
// ArenaAllocator satisfies this. Growth is geometric (starts at 4,
// doubles). PLAN.md assumption #2: hand-rolled rather than
// std::vector.
//
// Old backing blocks are not individually freed on resize — none of
// this project's allocators support freeing an arbitrary block yet
// (Arena is bulk-alloc/bulk-reset only, Pool is fixed-slot-size, Stack
// is LIFO-only), so a resize just abandons the old block within the
// allocator. This is a deliberate, accepted tradeoff for arena-backed
// growable containers (bounded waste, reclaimed in bulk on Reset()),
// not an oversight.
namespace engine_placeholder {

template <typename T, typename Allocator>
class Array {
public:
    explicit Array(Allocator& allocator) : allocator_(&allocator) {}

    ~Array() {
        DestroyRange(0, size_);
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    Array(Array&& other) noexcept
        : allocator_(other.allocator_), data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

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

    void Reserve(usize new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }
        void* mem = allocator_->Alloc(new_capacity * sizeof(T), alignof(T));
        T* new_data = static_cast<T*>(mem);
        for (usize i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        data_ = new_data;
        capacity_ = new_capacity;
    }

    void Push(const T& value) {
        GrowIfNeeded();
        new (data_ + size_) T(value);
        ++size_;
    }

    void Push(T&& value) {
        GrowIfNeeded();
        new (data_ + size_) T(std::move(value));
        ++size_;
    }

    void Pop() {
        --size_;
        data_[size_].~T();
    }

    // Precondition: index <= Size().
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

    // Precondition: index < Size().
    void Remove(usize index) {
        for (usize i = index; i + 1 < size_; ++i) {
            data_[i] = std::move(data_[i + 1]);
        }
        --size_;
        data_[size_].~T();
    }

    T& operator[](usize index) { return data_[index]; }
    const T& operator[](usize index) const { return data_[index]; }

    usize Size() const { return size_; }
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
