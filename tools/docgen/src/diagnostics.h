#pragma once

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
