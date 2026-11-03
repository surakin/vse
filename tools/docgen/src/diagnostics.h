#pragma once

#include <cstring>

#include "containers/array.h"
#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

// docgen diagnostics (tools/docgen/DESIGN.md section 7):
//   path:line: error|warning|note: message
namespace docgen {

using engine_placeholder::ArenaAllocator;
using engine_placeholder::Array;
using engine_placeholder::StringView;
using engine_placeholder::u32;
using engine_placeholder::usize;

// memcpy that tolerates (null, 0), which an empty StringView may be.
inline void CopyBytes(char* dst, const char* src, usize n) {
    if (n != 0) std::memcpy(dst, src, n);
}

// Concatenates up to four strings into one NUL-terminated copy in `arena`.
inline StringView ArenaCat(ArenaAllocator& arena, StringView a, StringView b, StringView c = StringView(), StringView d = StringView()) {
    const usize len = a.Length() + b.Length() + c.Length() + d.Length();
    char* buf = static_cast<char*>(arena.Alloc(len + 1, 1));
    ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
    usize at = 0;
    for (StringView s : {a, b, c, d}) {
        CopyBytes(buf + at, s.Data(), s.Length());
        at += s.Length();
    }
    buf[len] = '\0';
    return StringView(buf, len);
}

// Stable insertion sort; cheap for the nearly ordered lists docgen sorts.
template <typename T, typename Less>
void StableSort(Array<T, ArenaAllocator>& a, Less less) {
    for (usize i = 1; i < a.Size(); ++i) {
        T x = a[i];
        usize j = i;
        while (j > 0 && less(x, a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = x;
    }
}

enum class Severity : engine_placeholder::u8 { Note, Warning, Error };

struct Diagnostic {
    Severity severity;
    u32 line;
    StringView message; // storage outlives the Diagnostic (static literal or arena)
};

inline const char* SeverityName(Severity s) {
    switch (s) {
    case Severity::Note: return "note";
    case Severity::Warning: return "warning";
    case Severity::Error: return "error";
    }
    return "error";
}

} // namespace docgen
