#pragma once

// Text helpers shared by the docgen tests (Phase 16): `Str`, a copyable owned string with the operations these tests use
// (concatenation, searching, comparison), `StrList`, an ordered list of them, and `Num`, number-to-text. They sit on the
// engine's `String`/`Vector` through tests/support/test_text.h, so no docgen test spells a standard type.

#include <catch2/catch_tostring.hpp>

#include "containers/vector.h"
#include "lang/initializer_list.h"
#include "lang/utility.h"
#include "string/string.h"
#include "../support/catch_engine_types.h"
#include "../support/test_text.h"
#include "types.h"

namespace docgen_test {

using engine_placeholder::StringView;
using engine_placeholder::usize;

class Str {
public:
    static constexpr usize npos = ~usize{0};

    Str() : text_(engine_test::MakeText()) {}
    Str(const char* c_str) : text_(engine_test::MakeText(StringView(c_str))) {}
    Str(const char* data, usize length) : text_(engine_test::MakeText(StringView(data, length))) {}
    Str(usize count, char c) : text_(engine_test::Repeated(count, c)) {}
    explicit Str(StringView view) : text_(engine_test::MakeText(view)) {}
    Str(const Str& other) : text_(engine_test::MakeText(other.View())) {}
    Str(Str&&) noexcept = default;
    Str& operator=(const Str& other) {
        if (this != &other) text_ = engine_test::MakeText(other.View());
        return *this;
    }
    Str& operator=(Str&&) noexcept = default;

    const char* CStr() const { return text_.CStr(); }
    const char* Data() const { return text_.CStr(); }
    usize Length() const { return text_.Length(); }
    usize Size() const { return text_.Length(); }
    bool Empty() const { return text_.Length() == 0; }
    StringView View() const { return text_.View(); }
    operator StringView() const { return text_.View(); }
    char operator[](usize i) const { return text_.CStr()[i]; }

    Str& operator+=(StringView v) {
        text_.Append(v);
        return *this;
    }
    void Push(char c) { text_.Append(c); }
    Str& operator+=(char c) {
        text_.Append(c);
        return *this;
    }

    /// The first occurrence of `pattern` at or after `from`, or npos.
    usize Find(StringView pattern, usize from = 0) const {
        if (from > Length()) return npos;
        if (pattern.Length() == 0) return from;
        const StringView rest = View().Substring(from);
        const usize at = rest.Find(pattern);
        return at == rest.Length() ? npos : from + at;
    }
    usize Find(char c, usize from = 0) const {
        if (from > Length()) return npos;
        const StringView rest = View().Substring(from);
        const usize at = rest.IndexOf(c);
        return at == rest.Length() ? npos : from + at;
    }
    /// The last occurrence of `pattern` starting at or before `from`, or npos.
    usize RFind(StringView pattern, usize from = npos) const {
        if (pattern.Length() > Length()) return npos;
        usize start = Length() - pattern.Length();
        if (from < start) start = from;
        for (usize i = start + 1; i-- > 0;) {
            if (View().Substring(i, pattern.Length()) == pattern) return i;
        }
        return npos;
    }
    bool Contains(StringView pattern) const { return Find(pattern) != npos; }
    bool StartsWith(StringView prefix) const { return View().StartsWith(prefix); }
    bool EndsWith(StringView suffix) const { return View().EndsWith(suffix); }

    /// Truncates to `length` bytes, or pads with `fill`.
    void Resize(usize length, char fill = '\0') {
        if (length <= Length()) {
            *this = Str(View().Substring(0, length));
            return;
        }
        Str out(*this);
        for (usize i = Length(); i < length; ++i) out.Push(fill);
        *this = engine_placeholder::Move(out);
    }
    /// Compares the bytes [start, start + count) (clamped to the end) with `other`: negative, zero or positive.
    int Compare(usize start, usize count, StringView other) const {
        if (start > Length()) start = Length();
        if (count > Length() - start) count = Length() - start;
        return View().Substring(start, count).Compare(other);
    }
    /// Removes `count` bytes from `start` (clamped to the end).
    void Erase(usize start, usize count) {
        if (start > Length()) start = Length();
        if (count > Length() - start) count = Length() - start;
        Str out(View().Substring(0, start));
        out += View().Substring(start + count);
        *this = engine_placeholder::Move(out);
    }
    /// Inserts `v` before byte `at`.
    void Insert(usize at, StringView v) {
        if (at > Length()) at = Length();
        Str out(View().Substring(0, at));
        out += v;
        out += View().Substring(at);
        *this = engine_placeholder::Move(out);
    }
    /// The bytes [start, start + count) (clamped to the end), as an owned string.
    Str Substr(usize start, usize count = npos) const {
        if (start > Length()) start = Length();
        if (count > Length() - start) count = Length() - start;
        return Str(View().Substring(start, count));
    }

private:
    engine_test::Text text_;
};

inline Str operator+(const Str& a, StringView b) {
    Str out(a);
    out += b;
    return out;
}
inline Str operator+(StringView a, const Str& b) {
    Str out(a);
    out += b.View();
    return out;
}
inline Str operator+(const Str& a, const char* b) { return a + StringView(b); }
inline Str operator+(const char* a, const Str& b) { return StringView(a) + b; }
inline Str operator+(const Str& a, const Str& b) {
    Str out(a);
    out += b.View();
    return out;
}
inline Str operator+(const Str& a, char b) {
    Str out(a);
    out += b;
    return out;
}
inline Str operator+(char a, const Str& b) {
    Str out;
    out += a;
    out += b.View();
    return out;
}

inline bool operator==(const Str& a, const Str& b) { return a.View() == b.View(); }
inline bool operator!=(const Str& a, const Str& b) { return !(a == b); }
inline bool operator<(const Str& a, const Str& b) { return a.View() < b.View(); }

/// A number as text (decimal).
template <typename T>
Str Num(T value) {
    if constexpr (static_cast<T>(-1) < static_cast<T>(0)) {
        return Str(engine_test::Format("%lld", static_cast<long long>(value)).View());
    } else {
        return Str(engine_test::Format("%llu", static_cast<unsigned long long>(value)).View());
    }
}

/// An ordered list of owned strings.
class StrList {
public:
    StrList() = default;
    StrList(const StrList& other) {
        for (usize i = 0; i < other.Size(); ++i) items_.Push(other[i]);
    }
    StrList(StrList&&) noexcept = default;
    StrList& operator=(const StrList& other) {
        if (this != &other) {
            StrList copy(other);
            items_ = engine_placeholder::Move(copy.items_);
        }
        return *this;
    }
    StrList& operator=(StrList&&) noexcept = default;
    StrList(engine_placeholder::InitializerList<const char*> items) {
        for (const char* item : items) items_.Push(Str(item));
    }

    void Push(const Str& s) { items_.Push(s); }
    void Push(StringView v) { items_.Push(Str(v)); }
    void Pop() { items_.Pop(); }
    void Clear() { items_.Clear(); }
    usize Size() const { return items_.Size(); }
    bool Empty() const { return items_.Empty(); }
    const Str& operator[](usize i) const { return items_[i]; }
    Str& operator[](usize i) { return items_[i]; }
    const Str& Back() const { return items_[items_.Size() - 1]; }
    const Str* begin() const { return items_.Data(); }
    const Str* end() const { return items_.Data() + items_.Size(); }
    /// The index of the first entry equal to `v`, or Size().
    usize IndexOf(StringView v) const {
        for (usize i = 0; i < items_.Size(); ++i) {
            if (items_[i].View() == v) return i;
        }
        return items_.Size();
    }
    bool Has(StringView v) const { return IndexOf(v) != items_.Size(); }
    /// How many entries equal `v`.
    usize Count(StringView v) const {
        usize n = 0;
        for (usize i = 0; i < items_.Size(); ++i) n += items_[i].View() == v ? 1 : 0;
        return n;
    }
    /// Sorts bytewise.
    void Sort() {
        for (usize i = 1; i < items_.Size(); ++i) {
            for (usize j = i; j > 0 && items_[j] < items_[j - 1]; --j) engine_placeholder::Swap(items_[j], items_[j - 1]);
        }
    }
    /// Reorders into the next lexicographic permutation; false (and sorted) after the last one.
    bool NextPermutation() {
        if (items_.Size() < 2) return false;
        usize i = items_.Size() - 1;
        while (i > 0 && !(items_[i - 1] < items_[i])) --i;
        if (i == 0) {
            Reverse(0);
            return false;
        }
        usize j = items_.Size() - 1;
        while (!(items_[i - 1] < items_[j])) --j;
        engine_placeholder::Swap(items_[i - 1], items_[j]);
        Reverse(i);
        return true;
    }
    /// Moves the first `by` entries to the end, keeping their order.
    void RotateLeft(usize by) {
        StrList out;
        for (usize i = 0; i < items_.Size(); ++i) out.Push(items_[(i + by) % items_.Size()]);
        items_ = engine_placeholder::Move(out.items_);
    }

    friend bool operator==(const StrList& a, const StrList& b) {
        if (a.Size() != b.Size()) return false;
        for (usize i = 0; i < a.Size(); ++i) {
            if (!(a[i] == b[i])) return false;
        }
        return true;
    }
    friend bool operator!=(const StrList& a, const StrList& b) { return !(a == b); }

private:
    void Reverse(usize from) {
        for (usize lo = from, hi = items_.Size(); lo + 1 < hi; ++lo, --hi) engine_placeholder::Swap(items_[lo], items_[hi - 1]);
    }
    engine_placeholder::Vector<Str> items_;
};

/// Two strings (a relative path and its content, in these tests).
struct StrPair {
    Str first;
    Str second;
};

/// An ordered list of pairs, built from a braced list.
class StrPairList {
public:
    StrPairList() = default;
    StrPairList(engine_placeholder::InitializerList<StrPair> items) {
        for (const StrPair& item : items) items_.Push(item);
    }
    void Push(const StrPair& pair) { items_.Push(pair); }
    void Push(const Str& first, const Str& second) { items_.Push(StrPair{first, second}); }
    usize Size() const { return items_.Size(); }
    const StrPair& operator[](usize i) const { return items_[i]; }
    const StrPair* begin() const { return items_.Data(); }
    const StrPair* end() const { return items_.Data() + items_.Size(); }

private:
    engine_placeholder::Vector<StrPair> items_;
};

} // namespace docgen_test

namespace Catch {

// Catch2 takes whatever `convert` returns as its text type; `auto` keeps the standard type out of this file.
template <>
struct StringMaker<docgen_test::Str> {
    static auto convert(const docgen_test::Str& s) { return StringMaker<engine_placeholder::StringView>::convert(s.View()); }
};

template <>
struct StringMaker<docgen_test::StrList> {
    static auto convert(const docgen_test::StrList& list) {
        docgen_test::Str joined = "{";
        for (engine_placeholder::usize i = 0; i < list.Size(); ++i) {
            joined += i ? ", \"" : "\"";
            joined += list[i].View();
            joined += '"';
        }
        joined += '}';
        return StringMaker<docgen_test::Str>::convert(joined);
    }
};

} // namespace Catch
