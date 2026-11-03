#pragma once

#include "scanner.h"

// Phase 2.5 Day 38: the declaration extractor.
//
// Walks the scanner's token stream with a scope stack and builds a tree of
// the declarations the C++ subset in tools/docgen/DESIGN.md section 3
// supports, each with its attached doc comments. It is NOT a C++ parser: a
// statement it cannot classify is reported (`unrecognized declaration`,
// `macro-generated declaration`, `unbalanced braces`) and skipped, never
// guessed at.
//
// Thread-safety: not-thread-safe per ExtractResult; Extract() keeps no
//   global state.
// Ownership: everything is allocated from the arena, and views point into
//   the scanner's source; the arena, source and ScanResult must outlive
//   the ExtractResult.
namespace docgen {

using engine_placeholder::StringBuilder;

enum class DeclKind : engine_placeholder::u8 {
    Namespace,
    Class, // class / struct / union (see Decl::head_keyword)
    Enum,
    Enumerator,
    Function, // free function, method, constructor, destructor, operator, conversion
    Variable,
    Alias, // using X = ...; / typedef
    Macro, // a documented #define
};

enum class Access : engine_placeholder::u8 { Public, Protected, Private };

enum DeclFlags : engine_placeholder::u32 {
    kFlagStatic = 1u << 0,
    kFlagVirtual = 1u << 1,
    kFlagExplicit = 1u << 2,
    kFlagConstexpr = 1u << 3,
    kFlagInline = 1u << 4,
    kFlagOverride = 1u << 5,
    kFlagFinal = 1u << 6,
    kFlagPureVirtual = 1u << 7,
    kFlagDefaulted = 1u << 8,
    kFlagDeleted = 1u << 9,
    kFlagConstructor = 1u << 10,
    kFlagDestructor = 1u << 11,
    kFlagConversion = 1u << 12,
    kFlagOperator = 1u << 13,
    kFlagConst = 1u << 14,
    kFlagNoexcept = 1u << 15,
    kFlagConsteval = 1u << 16,
    kFlagInlineNamespace = 1u << 17,
    kFlagAnonymous = 1u << 18,
};

struct Decl {
    explicit Decl(ArenaAllocator& arena) : children(arena) {}

    DeclKind kind = DeclKind::Namespace;
    StringView head_keyword; // "class", "struct", "union", "enum", "enum class", "using", "typedef", "define"
    StringView name;         // unqualified ("operator+", "~Foo", "(anonymous)")
    StringView qualified;    // scope::name
    StringView signature;    // normalized declaration head, no body
    StringView template_header; // "template <...>" text, or empty
    StringView attributes;      // the declaration's `[[...]]` groups, space separated, or empty
    Access access = Access::Public;
    bool hidden = false; // inside an anonymous/detail namespace, or not public
    engine_placeholder::u32 flags = 0;
    engine_placeholder::i32 doc = -1;          // index into ScanResult::docs (`///`), or -1
    engine_placeholder::i32 trailing_doc = -1; // index into ScanResult::docs (`///<`), or -1
    u32 line = 0;
    Decl* parent = nullptr;
    Array<Decl*, ArenaAllocator> children;

    bool Is(engine_placeholder::u32 f) const { return (flags & f) != 0; }
    // Part of the documented public surface (DOC-CONVENTIONS "Visibility").
    bool IsPublic() const { return !hidden; }
};

struct ExtractResult {
    explicit ExtractResult(ArenaAllocator& arena) : diagnostics(arena) {}

    Decl* root = nullptr; // the (unnamed) global namespace
    Array<Diagnostic, ArenaAllocator> diagnostics;

    bool HasErrors() const {
        for (usize i = 0; i < diagnostics.Size(); ++i) {
            if (diagnostics[i].severity == Severity::Error) return true;
        }
        return false;
    }
};

// Builds the declaration tree for one scanned header. Always produces a
// root; problems are reported in `out.diagnostics` and parsing recovers at
// the next statement.
void Extract(const ScanResult& scan, ArenaAllocator& arena, ExtractResult& out);

// Appends the tree in the stable text form used by the golden tests
// (`*.tree`) and `docgen dump`. Only the public surface is printed unless
// `include_hidden` is set (hidden declarations are then tagged `[hidden]`).
void DumpTree(const ExtractResult& result, const ScanResult& scan, StringBuilder<ArenaAllocator>& out, bool include_hidden);

} // namespace docgen
