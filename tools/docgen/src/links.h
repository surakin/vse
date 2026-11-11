#pragma once

#include "md_scan.h"
#include "symbol_index.h"

// Phase 3 Day 41: `docgen links` — the Markdown link checker (DESIGN.md
// section 10). Checks every `docs/**/*.md` and top-level `*.md`: relative
// file links, `#anchor` links (heading slugs, `{#id}`s, `<a id>`s), image
// paths, `[[Symbol]]` references against the symbol index, and that every
// page under docs/ is reachable from docs/README.md. External http(s) links
// are counted (and listed on request) but never fetched.
//
// Thread-safety: not-thread-safe; one call at a time per arena.
// Ownership: results are allocated in the caller's arena.
namespace docgen
{

struct LinkStats
{
    usize files = 0;     // Markdown files checked
    usize links = 0;     // links and images checked (internal and external)
    usize external = 0;  // http(s)/mailto links, not fetched
    usize refs = 0;      // [[Symbol]] references resolved
};

struct LinkOptions
{
    bool list_external = false;  // add a note per external link
};

// Repo-relative paths of the Markdown files docgen checks: `docs/**/*.md`
// and the top-level `*.md`, sorted. False if the tree cannot be read.
bool CollectMarkdownFiles(StringView root, ArenaAllocator& arena, Vector<StringView, ArenaAllocator>& out);

// `index` may be null (then `[[refs]]` are not checked). `root` is the repo
// root. Appends diagnostics (sorted by path, then line) to `out`. Returns
// false only if the file tree cannot be read; check `out` for errors.
bool CheckLinks(StringView root, const SymbolIndex* index, ArenaAllocator& arena, const LinkOptions& options,
                Vector<FileDiag, ArenaAllocator>& out, LinkStats& stats);

}  // namespace docgen
