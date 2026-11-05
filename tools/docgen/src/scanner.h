#pragma once

#include "diagnostics.h"

// Phase 2.5 Day 37: the docgen source scanner (lexer).
//
// Turns the text of a header into a token stream that is safe to walk:
// comments, string/char/raw-string literals and preprocessor lines can
// never be mistaken for declarations. Doc comments (`///`, `//!`, `///<`)
// are collected separately and attached to tokens per the rules in
// docs/DOC-CONVENTIONS.md. The scanner never guesses: lexical problems are
// reported as diagnostics and scanning resumes at the next line.
//
// Thread-safety: not-thread-safe per ScanResult; Scan() itself keeps no
//   global state, so independent scans may run concurrently.
// Ownership: the source text and the arena must outlive the ScanResult
//   (token texts are views into the source; doc text and messages are
//   allocated from the arena).
namespace docgen {

enum class TokenKind : engine_placeholder::u8 {
    Identifier,
    Number,
    StringLiteral, // includes prefixes and raw strings; text is the whole literal
    CharLiteral,
    Punct,
    Define, // a `#define` directive; text runs from the macro name to the end of the logical line
};

enum class DocKind : engine_placeholder::u8 {
    Declaration, // `///` lines directly above a declaration
    File,        // `//!` lines (file overview)
    Trailing,    // `///<` after a declaration on the same line
};

struct DocComment {
    DocKind kind;
    StringView text; // marker and one following space removed, lines joined by '\n'
    u32 first_line;
    u32 last_line;
    // Index of the token this comment documents; -1 for File comments.
    // Trailing: the last token before the comment on its line.
    engine_placeholder::i32 token = -1;
};

struct Token {
    TokenKind kind;
    StringView text;
    u32 line; // 1-based
    u32 column; // 1-based, in bytes
    // Index into ScanResult::docs of the `///` comment attached to this
    // token, or -1. (A `///<` comment points back at its token via
    // DocComment::token instead.)
    engine_placeholder::i32 doc = -1;
};

struct ScanResult {
    explicit ScanResult(ArenaAllocator& arena) : tokens(arena), docs(arena), diagnostics(arena) {}

    Vector<Token, ArenaAllocator> tokens;
    Vector<DocComment, ArenaAllocator> docs;
    Vector<Diagnostic, ArenaAllocator> diagnostics;

    bool HasErrors() const {
        for (usize i = 0; i < diagnostics.Size(); ++i) {
            if (diagnostics[i].severity == Severity::Error) return true;
        }
        return false;
    }
};

// Scans `source` (any bytes; never crashes or loops forever). Results go
// into `out`, which must be freshly constructed.
void Scan(StringView source, ArenaAllocator& arena, ScanResult& out);

} // namespace docgen
