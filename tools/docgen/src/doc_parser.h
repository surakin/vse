#pragma once

#include "extractor.h"

// Phase 2.5 Day 39: doc-comment parser and validator.
//
// Turns the text of each scanner DocComment into a ParsedDoc (summary,
// detail blocks, contract labels, [[references]]) and checks it against the
// declaration it documents (docs/DOC-CONVENTIONS.md "Doc-comment grammar").
//
// Thread-safety: not-thread-safe per DocResult; ParseDocs() keeps no global
//   state.
// Ownership: everything is allocated from the arena, and text views point
//   into the scanner's source or the arena; the arena, source, ScanResult
//   and ExtractResult must outlive the DocResult.
namespace docgen {

enum class LabelKind : engine_placeholder::u8 {
    Param,
    Returns,
    Fails,
    Aborts,
    ThreadSafety,
    Ownership,
    Example,
    See,
    Deprecated,
    Internal,
    Backend,
};

// The label as written in a comment ("Thread-safety").
const char* LabelName(LabelKind kind);

struct Label {
    LabelKind kind = LabelKind::Returns;
    StringView name;      // Param: the parameter name
    StringView text;      // continuation lines joined with single spaces
    StringView code;      // Example: the fenced block that follows, without the fences
    StringView code_lang; // Example: the fence's info string ("cpp compile")
    bool has_code = false;
    u32 line = 0;      // absolute source line of the label
    u32 ref_count = 0; // [[references]] inside the label's text
};

enum class BlockKind : engine_placeholder::u8 { Paragraph, List, Code };

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    StringView text; // Paragraph: lines joined with spaces; Code: the lines joined with '\n'
    StringView lang; // Code: info string
    usize first_item = 0; // List: range into ParsedDoc::items
    usize item_count = 0;
    u32 line = 0;
};

struct Ref {
    StringView target; // text between [[ and ]]
    u32 line = 0;
};

struct ParsedDoc {
    explicit ParsedDoc(ArenaAllocator& arena) : blocks(arena), items(arena), labels(arena), refs(arena) {}

    StringView summary; // first paragraph, lines joined with spaces
    bool has_summary = false;
    u32 summary_line = 0;
    bool internal = false; // carries an `Internal:` label
    Vector<Block, ArenaAllocator> blocks; // detail paragraphs, lists and code blocks, in order
    Vector<StringView, ArenaAllocator> items;
    Vector<Label, ArenaAllocator> labels; // in source order
    Vector<Ref, ArenaAllocator> refs;     // every [[reference]] in the comment, in order

    const Label* Find(LabelKind kind) const {
        for (usize i = 0; i < labels.Size(); ++i) {
            if (labels[i].kind == kind) return &labels[i];
        }
        return nullptr;
    }
};

struct DocResult {
    explicit DocResult(ArenaAllocator& arena) : parsed(arena), diagnostics(arena), undocumented(arena) {}

    // One entry per ScanResult::docs, same index; never null after ParseDocs.
    Vector<ParsedDoc*, ArenaAllocator> parsed;
    Vector<Diagnostic, ArenaAllocator> diagnostics; // sorted by line
    // Public declarations with no doc comment (and no inherited one). Not an
    // error here; `docgen lint` (Day 43) decides.
    Vector<const Decl*, ArenaAllocator> undocumented;

    bool HasErrors() const {
        for (usize i = 0; i < diagnostics.Size(); ++i) {
            if (diagnostics[i].severity == Severity::Error) return true;
        }
        return false;
    }
};

// Parses every doc comment of `scan` and validates the ones attached to a
// declaration of `ex`. `out` must be freshly constructed.
void ParseDocs(const ScanResult& scan, const ExtractResult& ex, ArenaAllocator& arena, DocResult& out);

// Appends the normalized comment text (no `///` markers): summary, details,
// then labels. Parsing the output yields the same output again.
void EmitDoc(const ParsedDoc& doc, StringBuilder<ArenaAllocator>& out);

// The declaration an undocumented `override` inherits its summary from: the
// same-named, same-arity, same-constness member of a base class, found by
// base name anywhere in `ex` (a file-local approximation; Day 40's symbol
// index resolves across files). Follows documented-or-not chains of bases.
// Returns null when `fn` is not an override or nothing documented is found.
const Decl* ResolveInherited(const Decl& fn, const ExtractResult& ex);

// Same, searching for base classes in several declaration trees (the roots of
// every parsed header), so a base declared in another header is found.
const Decl* ResolveInherited(const Decl& fn, const Decl* const* roots, usize root_count);

} // namespace docgen
