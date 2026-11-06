#pragma once

#include <cstring>
#include <new>
#include <type_traits>

#include "log/assert.h"
#include "types.h"

//! A small growable array of trivially copyable values on the global heap, for backend bookkeeping.
//!
//! The foundation's `Vector` takes its storage from an arena and never frees on growth; the backend needs
//! arrays that grow and shrink for the life of the device (free lists, block lists), so it has this one.

namespace engine_placeholder::rhi::detail {

template <typename T>
class GrowArray {
    static_assert(std::is_trivially_copyable_v<T>, "GrowArray holds trivially copyable values only");

public:
    GrowArray() = default;
    ~GrowArray() { ::operator delete(data_); }
    GrowArray(const GrowArray&) = delete;
    GrowArray& operator=(const GrowArray&) = delete;
    GrowArray(GrowArray&& other) : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    usize Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    T& operator[](usize i) {
        ENGINE_ASSERT(i < size_, "GrowArray: index out of range");
        return data_[i];
    }
    const T& operator[](usize i) const {
        ENGINE_ASSERT(i < size_, "GrowArray: index out of range");
        return data_[i];
    }
    T& Back() { return (*this)[size_ - 1]; }

    void Push(const T& value) { Insert(size_, value); }

    void Insert(usize index, const T& value) {
        ENGINE_ASSERT(index <= size_, "GrowArray: insert index out of range");
        const T copy = value; // `value` may be an element of this array: Grow frees it and the shift moves it
        Grow(size_ + 1);
        if (index < size_) {
            std::memmove(data_ + index + 1, data_ + index, (size_ - index) * sizeof(T));
        }
        data_[index] = copy;
        ++size_;
    }

    void Erase(usize index) {
        ENGINE_ASSERT(index < size_, "GrowArray: erase index out of range");
        if (index + 1 < size_) {
            std::memmove(data_ + index, data_ + index + 1, (size_ - index - 1) * sizeof(T));
        }
        --size_;
    }

    // Removes the first `count` elements.
    void EraseFront(usize count) {
        ENGINE_ASSERT(count <= size_, "GrowArray: erase count out of range");
        if (count > 0 && count < size_) {
            std::memmove(data_, data_ + count, (size_ - count) * sizeof(T));
        }
        size_ -= count;
    }

    void Clear() { size_ = 0; }

private:
    void Grow(usize needed) {
        if (needed <= capacity_) {
            return;
        }
        usize new_capacity = capacity_ == 0 ? 8 : capacity_ * 2;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }
        T* new_data = static_cast<T*>(::operator new(new_capacity * sizeof(T)));
        if (size_ > 0) {
            std::memcpy(new_data, data_, size_ * sizeof(T));
        }
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = new_capacity;
    }

    T* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;
};

} // namespace engine_placeholder::rhi::detail
