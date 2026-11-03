#pragma once

#include "diagnostics.h"

// Phase 2.5 Day 41: a small Markdown scanner for the files under docs/ — just
// enough structure for the link checker (Day 41) and the sample compiler
// (Day 42): fenced code blocks, ATX headings and their anchors, inline and
// reference-style links, images, and `[[Symbol]]` references. It is not a
// CommonMark parser; DESIGN.md section 10 lists what is and is not understood.
//
// Thread-safety: thread-compatible; ScanMarkdown keeps no global state.
// Ownership: views point into the scanned text or the arena; both must
//   outlive the MdDoc.
namespace docgen {

struct MdFence {
    u32 line = 0;      // line of the opening fence (1-based)
    u32 body_line = 0; // first line of the body
    u32 end_line = 0;  // line of the closing fence (the last line of the file if unterminated)
    u32 indent = 0;    // blank characters before the opening fence (list items)
    StringView info;   // text after the backticks, trimmed ("cpp compile run")
    StringView body;   // the lines between the fences, '\n'-joined, no trailing newline; indent not stripped
    bool closed = false;
};

struct MdLink {
    u32 line = 0;
    StringView target; // as written, without <> or title
    bool image = false;
};

struct MdRef {
    u32 line = 0;
    StringView target; // inside [[ ]], trimmed
};

struct MdDoc {
    explicit MdDoc(ArenaAllocator& arena)
        : fences(arena), links(arena), refs(arena), anchors(arena), diagnostics(arena) {}

    Array<MdFence, ArenaAllocator> fences;
    Array<MdLink, ArenaAllocator> links;
    Array<MdRef, ArenaAllocator> refs;
    Array<StringView, ArenaAllocator> anchors; // heading slugs (GitHub style, -1/-2 for repeats), {#id}s, <a id/name>
    Array<Diagnostic, ArenaAllocator> diagnostics; // unterminated fence, malformed reference, undefined link label

    bool HasAnchor(StringView name) const;
};

void ScanMarkdown(StringView text, ArenaAllocator& arena, MdDoc& out);

// GitHub-style heading slug: lowercase, spaces to '-', everything but
// letters, digits, '_' and '-' dropped; inline markup (`code`, link syntax)
// contributes only its text. Non-ASCII bytes are kept.
StringView HeadingSlug(StringView heading, ArenaAllocator& arena);

} // namespace docgen
