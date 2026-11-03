#pragma once

#include "symbol_index.h"

// Phase 2.5 Day 40: Markdown emitters for the reference (DESIGN.md section 6).
// Output is deterministic and carries no timestamps.
//
// Thread-safety: thread-compatible; the functions only read the index.
// Ownership: results are appended to caller-provided builders.
namespace docgen {

// First line after the title of every generated page; also how `docgen
// reference` recognizes files it may delete.
extern const char kGeneratedMarker[];

void RenderModulePage(const SymbolIndex& index, usize module, StringBuilder<ArenaAllocator>& out, ArenaAllocator& arena);
void RenderIndexPage(const SymbolIndex& index, StringBuilder<ArenaAllocator>& out);
// docs/reference/symbols.txt: `# docgen symbols v1`, then one tab-separated
// line per symbol: ID, kind, module, anchor, page, header:line.
void RenderSymbolsFile(const SymbolIndex& index, StringBuilder<ArenaAllocator>& out);

} // namespace docgen
