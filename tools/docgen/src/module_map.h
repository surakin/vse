#pragma once

#include "diagnostics.h"

// Phase 3 Day 40: the module map (docs/reference/modules.txt, DESIGN.md
// section 4) — which headers make up which reference page.
//
// Thread-safety: not-thread-safe per ModuleMap; the free functions keep no
//   global state.
// Ownership: names and patterns are views into the map text or the arena;
//   both must outlive the ModuleMap.
namespace docgen
{

struct ModuleDef
{
    explicit ModuleDef(ArenaAllocator& arena) : headers(arena), excludes(arena), strict(arena) {}

    StringView name;   // "jobs", "foundation/memory"
    StringView title;  // page title
    u32 line = 0;
    Vector<StringView, ArenaAllocator> headers;   // glob patterns relative to the repo root
    Vector<StringView, ArenaAllocator> excludes;  // glob patterns
    Vector<StringView, ArenaAllocator> strict;    // lint rule names
};

struct ModuleMap
{
    explicit ModuleMap(ArenaAllocator& arena) : modules(arena), diagnostics(arena) {}

    Vector<ModuleDef*, ArenaAllocator> modules;  // in file order
    Vector<Diagnostic, ArenaAllocator> diagnostics;

    bool HasErrors() const
    {
        for (usize i = 0; i < diagnostics.Size(); ++i)
        {
            if (diagnostics[i].severity == Severity::Error)
            {
                return true;
            }
        }
        return false;
    }
};

// Parses the map text. Problems (unknown directive, directive outside a
// module, bad module line, duplicate module, unknown `strict` rule, a
// pattern using more than `*` and `**`) become diagnostics.
void ParseModuleMap(StringView text, ArenaAllocator& arena, ModuleMap& out);

// `*` matches any run of characters within one path segment, a segment that
// is exactly `**` matches zero or more whole segments. Paths use '/'.
bool GlobMatch(StringView pattern, StringView path);

// The leading directory of `pattern` that contains no wildcard ("engine/jobs"
// for "engine/jobs/*.h"); empty if the first segment has one.
StringView GlobPrefix(StringView pattern);

// Appends to `out`, in lexicographic path order without duplicates, every
// path of `files` that matches one of the module's header patterns and none
// of its excludes. The result does not depend on the order of `files`.
void ExpandModule(const ModuleDef& module, const Vector<StringView, ArenaAllocator>& files, ArenaAllocator& arena,
                  Vector<StringView, ArenaAllocator>& out);

// Collects the repo-relative paths of regular files under `root`/`dir`
// (recursively; entries starting with '.' are skipped, symlinked directories
// are not followed, depth is capped). `dir` may be empty. Returns false if
// `root`/`dir` or any directory below it cannot be read.
bool CollectFiles(StringView root, StringView dir, ArenaAllocator& arena, Vector<StringView, ArenaAllocator>& out);

}  // namespace docgen
