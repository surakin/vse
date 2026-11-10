#pragma once

// Text helpers for tests written against the engine's own types (Phase 10): an owned heap text, a mutable character
// buffer, and a few builders, so a test never needs std::string or std::vector.

#include "cstr/varargs.h"
#include "lang/initializer_list.h"
#include "cstr/format.h"

#include "algorithm.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "debug/compiler.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

namespace engine_test {

using engine_placeholder::HeapAllocator;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::Vector;

using Text = engine_placeholder::String<HeapAllocator>;

/// An owned copy of `view`.
inline Text MakeText(StringView view = StringView()) { return Text(HeapAllocator::Default(), view); }

/// `count` copies of `c`.
inline Text Repeated(usize count, char c) {
    Text out = MakeText();
    for (usize i = 0; i < count; ++i) out.Append(c);
    return out;
}

/// printf-style construction.
inline Text Format(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);
inline Text Format(const char* format, ...) {
    engine_placeholder::VarArgs args;
    ENGINE_VA_START(args, format);
    engine_placeholder::VarArgs again;
    ENGINE_VA_COPY(again, args);
    const int needed = engine_placeholder::VFormatTo(nullptr, 0, format, args);
    ENGINE_VA_END(args);
    Text out = MakeText();
    if (needed > 0) {
        Vector<char> buffer;
        buffer.Resize(static_cast<usize>(needed) + 1);
        engine_placeholder::VFormatTo(buffer.Data(), buffer.Size(), format, again);
        out.AppendRawBytes(buffer.Data(), static_cast<usize>(needed));
    }
    ENGINE_VA_END(again);
    return out;
}

/// `a` followed by `b` (and `c`).
inline Text Concat(StringView a, StringView b, StringView c = StringView()) {
    Text out = MakeText(a);
    out.Append(b);
    out.Append(c);
    return out;
}

/// A mutable buffer of `count` NUL characters.
inline Vector<char> Chars(usize count) {
    Vector<char> buffer;
    buffer.Resize(count);
    return buffer;
}

/// A buffer of `count` bytes, all `fill`.
inline Vector<engine_placeholder::byte> ByteBuffer(usize count, engine_placeholder::byte fill = 0) {
    Vector<engine_placeholder::byte> buffer;
    buffer.Resize(count);
    for (usize i = 0; i < count; ++i) buffer[i] = fill;
    return buffer;
}

/// True if the two byte buffers have the same size and the same bytes.
inline bool SameBytes(const Vector<engine_placeholder::byte>& a, const Vector<engine_placeholder::byte>& b) {
    if (a.Size() != b.Size()) return false;
    for (usize i = 0; i < a.Size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/// A view of the first `count` characters of a buffer.
inline StringView View(const Vector<char>& buffer, usize count) { return StringView(buffer.Data(), count); }
inline StringView View(const char* data, usize count) { return StringView(data, count); }

/// A copyable list of plain values with a braced-list constructor (`{1, 2, 3}`), so a test struct can carry expected values in a designated
/// initializer. Compares element by element; `Sort` orders it ascending with `operator<`.
template <typename T>
class ValueList {
public:
    ValueList() = default;
    ValueList(engine_placeholder::InitializerList<T> items) {
        for (const T& item : items) items_.Push(item);
    }
    ValueList(const ValueList& other) { CopyFrom(other); }
    ValueList& operator=(const ValueList& other) {
        if (this != &other) {
            items_.Clear();
            CopyFrom(other);
        }
        return *this;
    }
    ValueList(ValueList&&) noexcept = default;
    ValueList& operator=(ValueList&&) noexcept = default;

    void Push(const T& value) { items_.Push(value); }
    usize Size() const { return items_.Size(); }
    bool Empty() const { return items_.Empty(); }
    T& operator[](usize i) { return items_[i]; }
    const T& operator[](usize i) const { return items_[i]; }
    /// How many entries equal `value`.
    usize Count(const T& value) const {
        usize n = 0;
        for (usize i = 0; i < items_.Size(); ++i) n += items_[i] == value ? 1 : 0;
        return n;
    }
    /// Orders the entries ascending by `operator<`.
    void Sort() { engine_placeholder::Sort(engine_placeholder::Span<T>(items_.Data(), items_.Size())); }
    bool operator==(const ValueList& other) const {
        if (items_.Size() != other.items_.Size()) return false;
        for (usize i = 0; i < items_.Size(); ++i) {
            if (!(items_[i] == other.items_[i])) return false;
        }
        return true;
    }

private:
    void CopyFrom(const ValueList& other) {
        for (usize i = 0; i < other.items_.Size(); ++i) items_.Push(other.items_[i]);
    }
    Vector<T> items_;
};

/// An ordered list of owned texts (what a test records while it walks something).
class TextList {
public:
    void Push(StringView v) { items_.Push(MakeText(v)); }
    usize Size() const { return items_.Size(); }
    bool Empty() const { return items_.Empty(); }
    void Clear() { items_.Clear(); }
    const Text& operator[](usize i) const { return items_[i]; }
    const Text& Back() const { return items_[items_.Size() - 1]; }
    /// True if the list holds exactly `expected`, in order.
    bool SameAs(engine_placeholder::InitializerList<const char*> expected) const {
        if (items_.Size() != expected.size()) return false;
        usize i = 0;
        for (const char* e : expected) {
            if (!(items_[i++].View() == StringView(e))) return false;
        }
        return true;
    }
    /// How many entries equal `v`.
    usize Count(StringView v) const {
        usize n = 0;
        for (usize i = 0; i < items_.Size(); ++i) n += items_[i].View() == v ? 1 : 0;
        return n;
    }

private:
    Vector<Text> items_;
};

/// A set of owned texts kept sorted bytewise.
class TextSet {
public:
    /// Adds `v`; returns false if it was already there.
    bool Insert(StringView v) {
        const usize at = LowerBound(v);
        if (at < items_.Size() && items_[at].View() == v) return false;
        items_.Insert(at, MakeText(v));
        return true;
    }
    bool Has(StringView v) const {
        const usize at = LowerBound(v);
        return at < items_.Size() && items_[at].View() == v;
    }
    usize Size() const { return items_.Size(); }
    bool Empty() const { return items_.Empty(); }
    /// True if the set holds exactly `expected` (in any order, without repeats).
    bool SameAs(engine_placeholder::InitializerList<const char*> expected) const {
        if (items_.Size() != expected.size()) return false;
        TextSet seen; // the expected names must be distinct, or a repeat could hide a missing entry
        for (const char* e : expected) {
            if (!Has(StringView(e)) || !seen.Insert(StringView(e))) return false;
        }
        return true;
    }

private:
    usize LowerBound(StringView v) const {
        usize lo = 0, hi = items_.Size();
        while (lo < hi) {
            const usize mid = lo + (hi - lo) / 2;
            if (items_[mid].View().Compare(v) < 0) lo = mid + 1; else hi = mid;
        }
        return lo;
    }
    Vector<Text> items_;
};

} // namespace engine_test
