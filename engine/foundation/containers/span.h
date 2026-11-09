#pragma once

#include "lang/traits.h"

#include "containers/array.h"
#include "containers/vector.h"
#include "log/assert.h"
#include "string_view.h"
#include "types.h"
#include "cstr/mem_ops.h"

//! Non-owning views of contiguous elements: [[Span]], [[ByteSpan]] and [[MutableByteSpan]].
//!
//! A span is a pointer and a length. It owns nothing, allocates nothing and is cheap to pass by value; the viewed memory must outlive it. Functions that read or write a run of elements
//! take a span, so a caller can hand them a [[Vector]], an [[Array]], a C array or a pointer and a length without converting.
namespace engine_placeholder {

/// A view of `Size()` consecutive elements of type `T` starting at `Data()`.
///
/// `Span<const T>` reads, `Span<T>` reads and writes; a `Span<T>` converts to a `Span<const T>` but never the other way. It converts implicitly from a [[Vector]], an [[Array]] (also const), a C array and
/// another span that only adds `const`. Indexing, [[Span::First]], [[Span::Last]] and `Subspan` always check their bounds (a header cannot use `ENGINE_CHECK_SLOW`, whose setting differs per translation unit).
///
/// Thread-safety: thread-compatible. Reading is safe from many threads while the viewed elements are not modified.
/// Ownership: does not own the elements; the owner must keep them alive and unmoved (a [[Vector]] that grows invalidates spans of it).
template <typename T>
class Span {
public:
    /// Creates an empty span with a null data pointer.
    constexpr Span() = default;
    /// Creates a span of `size` elements starting at `data`.
    ///
    /// Param data: first element; may be null only when `size` is 0.
    /// Param size: number of elements.
    /// Aborts: `data` is null and `size` is not 0.
    constexpr Span(T* data, usize size) : data_(data), size_(size) {
        ENGINE_ASSERT(data != nullptr || size == 0, "Span: a null pointer needs size 0");
    }
    /// Creates a span of a C array.
    template <typename U, usize N>
        requires IsConvertible<U (*)[], T (*)[]>
    constexpr Span(U (&array)[N]) : data_(array), size_(N) {}
    /// Converts a span of `U` to a span of `T` when only `const` is added.
    template <typename U>
        requires(IsConvertible<U (*)[], T (*)[]> && !IsSame<U, T>)
    constexpr Span(const Span<U>& other) : data_(other.Data()), size_(other.Size()) {}
    /// Creates a span of the elements of a vector.
    template <typename U, typename A>
        requires IsConvertible<U (*)[], T (*)[]>
    Span(Vector<U, A>& vector) : data_(vector.Data()), size_(vector.Size()) {}
    /// Creates a read-only span of the elements of a const vector.
    template <typename U, typename A>
        requires(IsConst<T> && IsConvertible<const U (*)[], T (*)[]>)
    Span(const Vector<U, A>& vector) : data_(vector.Data()), size_(vector.Size()) {}
    /// Creates a span of the elements of an array.
    template <typename U, usize N>
        requires IsConvertible<U (*)[], T (*)[]>
    constexpr Span(Array<U, N>& array) : data_(array.items), size_(N) {}
    /// Creates a read-only span of the elements of a const array.
    template <typename U, usize N>
        requires(IsConst<T> && IsConvertible<const U (*)[], T (*)[]>)
    constexpr Span(const Array<U, N>& array) : data_(array.items), size_(N) {}

    /// Returns the pointer to the first element (null for a default span).
    constexpr T* Data() const { return data_; }
    /// Returns the number of elements.
    constexpr usize Size() const { return size_; }
    /// Returns the size in bytes.
    constexpr usize SizeBytes() const { return size_ * sizeof(T); }
    /// Returns true if the span has no elements.
    constexpr bool Empty() const { return size_ == 0; }

    /// Returns the element at `index`.
    ///
    /// Param index: element position; must be less than `Size()`.
    /// Aborts: `index` is not less than `Size()`.
    constexpr T& operator[](usize index) const {
        ENGINE_ASSERT(index < size_, "Span::operator[]: index out of range");
        return data_[index];
    }

    /// Returns a pointer to the first element, for range-based `for`.
    constexpr T* begin() const { return data_; }
    /// Returns a pointer one past the last element, for range-based `for`.
    constexpr T* end() const { return data_ + size_; }

    /// Returns the first `count` elements.
    ///
    /// Param count: at most `Size()`.
    /// Aborts: `count` is greater than `Size()`.
    constexpr Span First(usize count) const {
        ENGINE_ASSERT(count <= size_, "Span::First: count is greater than the size");
        return Span(data_, count);
    }
    /// Returns the last `count` elements.
    ///
    /// Param count: at most `Size()`.
    /// Aborts: `count` is greater than `Size()`.
    constexpr Span Last(usize count) const {
        ENGINE_ASSERT(count <= size_, "Span::Last: count is greater than the size");
        return Span(data_ + (size_ - count), count);
    }
    /// Returns `count` elements starting at `offset`.
    ///
    /// Param offset: at most `Size()`.
    /// Param count: at most `Size() - offset`.
    /// Aborts: the range is not inside the span.
    constexpr Span Subspan(usize offset, usize count) const {
        ENGINE_ASSERT(offset <= size_ && count <= size_ - offset, "Span::Subspan: the range is outside the span");
        return Span(data_ + offset, count);
    }
    /// Returns the elements from `offset` to the end.
    ///
    /// Param offset: at most `Size()`.
    /// Aborts: `offset` is greater than `Size()`.
    constexpr Span Subspan(usize offset) const {
        ENGINE_ASSERT(offset <= size_, "Span::Subspan: the offset is past the end");
        return Span(data_ + offset, size_ - offset);
    }

private:
    T* data_ = nullptr;
    usize size_ = 0;
};

/// A read-only view of bytes.
using ByteSpan = Span<const byte>;
/// A writable view of bytes.
using MutableByteSpan = Span<byte>;

/// Views the object representation of a trivially copyable value as bytes.
///
/// Param value: the object; it must outlive the span.
template <typename T>
ByteSpan AsBytes(const T& value) {
    static_assert(IsTriviallyCopyable<T>, "AsBytes: the type must be trivially copyable");
    return ByteSpan(reinterpret_cast<const byte*>(&value), sizeof(T));
}

/// Views the elements of a span of trivially copyable values as bytes.
template <typename T>
ByteSpan AsBytes(Span<T> elements) {
    static_assert(IsTriviallyCopyable<RemoveConst<T>>, "AsBytes: the element type must be trivially copyable");
    return ByteSpan(reinterpret_cast<const byte*>(elements.Data()), elements.SizeBytes());
}

/// Views the characters of a string view as bytes (no encoding change).
inline ByteSpan AsByteSpan(StringView text) { return ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length()); }

/// Views bytes as text (no validation).
inline StringView ToStringView(ByteSpan bytes) { return StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()); }

/// Appends copies of `items` to the end of `target`; the vector grows geometrically, so repeated calls are amortized. A string literal
/// converts to a span that includes its terminating NUL.
///
/// Param items: elements to copy; they must not be elements of `target`.
/// Aborts: `items` overlaps `target`, or growth is needed and the allocator is out of memory.
template <typename T, typename A>
void Append(Vector<T, A>& target, TypeIdentity<Span<const T>> items) {
    const T* begin = target.Data();
    ENGINE_ASSERT(target.Size() == 0 || items.Size() == 0 || items.Data() + items.Size() <= begin || items.Data() >= begin + target.Size(),
                  "Append: the source overlaps the vector");
    const usize needed = target.Size() + items.Size();
    if (needed > target.Capacity()) {
        const usize doubled = target.Capacity() * 2;
        target.Reserve(needed > doubled ? needed : doubled);
    }
    if constexpr (IsTriviallyCopyable<T>) {
        if (items.Size() != 0) {
            const usize old_size = target.Size();
            target.Resize(old_size + items.Size());
            MemCopy(target.Data() + old_size, items.Data(), items.SizeBytes());
        }
    } else {
        for (usize i = 0; i < items.Size(); ++i) target.Push(items[i]);
    }
}

/// Replaces the contents of `target` with copies of `items`.
///
/// Param items: elements to copy; they must not be elements of `target`.
/// Aborts: `items` overlaps `target`, or growth is needed and the allocator is out of memory.
template <typename T, typename A>
void Assign(Vector<T, A>& target, TypeIdentity<Span<const T>> items) {
    const T* begin = target.Data();
    ENGINE_ASSERT(target.Size() == 0 || items.Size() == 0 || items.Data() + items.Size() <= begin || items.Data() >= begin + target.Size(),
                  "Assign: the source overlaps the vector");
    target.Clear();
    Append(target, items);
}

} // namespace engine_placeholder
