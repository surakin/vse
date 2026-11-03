#pragma once

#include "doc_parser.h"
#include "module_map.h"

// Phase 2.5 Day 40: the symbol index — every documented-surface declaration
// of every module header with a stable ID, anchor, page and source location
// (DESIGN.md section 5), plus reference resolution for `[[Name]]`.
//
// Thread-safety: not-thread-safe; build once, then read.
// Ownership: the index owns nothing beyond the arena; the arena must outlive
//   it. Source text is loaded into the arena.
namespace docgen {

struct SourceUnit {
    explicit SourceUnit(ArenaAllocator& arena) : scan(arena), ex(arena), docs(arena) {}

    StringView path; // repo-relative
    StringView source;
    usize module = 0;
    ScanResult scan;
    ExtractResult ex;
    DocResult docs;
};

struct Symbol {
    explicit Symbol(ArenaAllocator& arena) : members(arena) {}

    StringView id;        // jobs::Counter::Increment(u64)
    StringView qualified; // jobs::Counter::Increment (no leading engine_placeholder::)
    StringView types;     // functions: normalized parameter types, "u64"
    StringView types_key; // `types` without spaces, for matching overload hints
    StringView anchor;
    const Decl* decl = nullptr;
    const SourceUnit* unit = nullptr;
    usize module = 0;
    const ParsedDoc* doc = nullptr;           // the comment that documents it, if any
    const Symbol* inherited = nullptr;        // an undocumented override's documented base member
    Array<Symbol*, ArenaAllocator> members;   // public members, in source order (not enumerators' own members)
};

struct FileDiag {
    StringView path;
    Diagnostic d;
};

struct SymbolIndex {
    explicit SymbolIndex(ArenaAllocator& arena) : units(arena), symbols(arena), top(arena), page_names(arena), diagnostics(arena) {}

    const ModuleMap* map = nullptr;
    Array<SourceUnit*, ArenaAllocator> units;   // module order, then path order
    Array<Symbol*, ArenaAllocator> symbols;     // every symbol, in module/unit/source order
    Array<Symbol*, ArenaAllocator> top;         // namespace-scope symbols, same order
    Array<StringView, ArenaAllocator> page_names; // per module: "foundation-memory.md"
    Array<FileDiag, ArenaAllocator> diagnostics;  // sorted by path, then line

    bool HasErrors() const {
        for (usize i = 0; i < diagnostics.Size(); ++i) {
            if (diagnostics[i].d.severity == Severity::Error) return true;
        }
        return false;
    }
};

// Builds the index for the headers of `map` under `root`. `map_path` is only
// used to attribute map diagnostics. Always fills what it can; check
// HasErrors().
void BuildIndex(StringView root, StringView map_path, const ModuleMap& map, ArenaAllocator& arena, SymbolIndex& out);

enum class ResolveStatus : engine_placeholder::u8 { Ok, NotFound, Ambiguous };

// Resolves the text of a `[[reference]]`: a full or partially qualified name,
// optionally with an overload hint `Name(type,type)`. Among partially
// qualified matches the one with the fewest scope components wins.
ResolveStatus ResolveRef(const SymbolIndex& index, StringView target, const Symbol*& out);

// Page file name for a module ("foundation/memory" -> "foundation-memory.md").
StringView PageNameFor(StringView module_name, ArenaAllocator& arena);

// Short kind word used in symbols.txt and page text.
const char* KindName(const Decl& d);

} // namespace docgen
