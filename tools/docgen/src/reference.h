#pragma once

#include "markdown.h"

// Phase 3 Day 40: `docgen reference` and `docgen check`.
//
// Thread-safety: not-thread-safe per ReferenceBuild.
// Ownership: arena-allocated; the arena must outlive the ReferenceBuild.
namespace docgen {

struct OutFile {
    StringView name; // relative to the output directory: "jobs.md", "symbols.txt"
    StringView content;
};

struct ReferenceBuild {
    explicit ReferenceBuild(ArenaAllocator& arena) : map(arena), index(arena), files(arena) {}

    ModuleMap map;
    SymbolIndex index;
    Vector<OutFile, ArenaAllocator> files; // empty when the build failed; sorted by name
    bool io_failure = false;              // the module map could not be read (as opposed to documentation errors)
};

// Reads the module map at `map_path`, indexes the headers under `root`, and
// renders every page plus symbols.txt in memory. Returns false (with
// diagnostics in build.index) if the map cannot be read or any error was
// found; `files` is then empty.
bool BuildReference(StringView root, StringView map_path, ArenaAllocator& arena, ReferenceBuild& build);

// Writes the files into `out_dir` (created if missing) and removes
// generated pages that are no longer produced. Problems (I/O) are appended
// to `problems`; returns true on success.
bool WriteReference(const ReferenceBuild& build, StringView out_dir, ArenaAllocator& arena, Vector<FileDiag, ArenaAllocator>& problems);

// Compares the in-memory output with `out_dir`. Appends one problem per
// missing, differing or no-longer-generated file; true when up to date.
bool CheckReference(const ReferenceBuild& build, StringView out_dir, ArenaAllocator& arena, Vector<FileDiag, ArenaAllocator>& problems);

} // namespace docgen
