#pragma once

#include "symbol_index.h"

// Phase 2.5 Day 43: `docgen lint` — documentation coverage and the lint
// rules of DESIGN.md section 8, with a committed baseline that may only
// shrink.
//
// Thread-safety: thread-compatible; the functions keep no global state and
//   only read the index.
// Ownership: results live in the caller's arena.
namespace docgen {

enum class LintRule : engine_placeholder::u8 { Undocumented, ThreadSafety, Ownership, Failure };

const char* LintRuleName(LintRule r); // "undocumented", "thread-safety", "ownership", "failure"

struct LintFinding {
    LintRule rule;
    const Symbol* symbol;
};

struct ModuleCoverage {
    StringView name;
    usize total = 0;
    usize documented = 0;
};

struct LintResult {
    explicit LintResult(ArenaAllocator& arena) : findings(arena), modules(arena) {}

    Array<LintFinding, ArenaAllocator> findings; // symbol order, then rule order
    Array<ModuleCoverage, ArenaAllocator> modules; // module map order
    usize total = 0;
    usize documented = 0;
};

// True for declarations the lint does not ask to be documented: `= delete`
// and `= default` declarations (the owning class's comment and its
// Ownership/Thread-safety labels describe copyability).
bool IsLintExempt(const Decl& d);

// Computes coverage and findings for every symbol of `index`.
void RunLint(const SymbolIndex& index, ArenaAllocator& arena, LintResult& out);

// Baseline file (docs/reference/lint-baseline.txt):
//   # docgen lint baseline v1
//   module<TAB>NAME<TAB>DOCUMENTED<TAB>TOTAL
//   known<TAB>RULE<TAB>SYMBOL-ID
struct Baseline {
    explicit Baseline(ArenaAllocator& arena) : modules(arena), known(arena) {}

    struct Known {
        StringView rule;
        StringView id;
        bool used = false;
    };
    Array<ModuleCoverage, ArenaAllocator> modules;
    Array<Known, ArenaAllocator> known;
};

// Parses baseline text; returns false and sets `error` (with the line) on a malformed file.
bool ParseBaseline(StringView text, ArenaAllocator& arena, Baseline& out, StringView& error);

// The baseline text that records exactly the current state.
void RenderBaseline(const LintResult& result, StringBuilder<ArenaAllocator>& out);

// Human-readable coverage table.
void RenderCoverage(const LintResult& result, StringBuilder<ArenaAllocator>& out);

// Compares `result` with the baseline. Appends an error for: a finding the
// baseline does not list; a baseline entry that no longer applies (the
// baseline only shrinks, so fixed symbols must be removed from it); a module
// (or the total) whose documented/total ratio dropped; a module missing from
// or only in the baseline. With no baseline (`baseline` null) every finding is
// an error.
void CompareToBaseline(const LintResult& result, Baseline* baseline, ArenaAllocator& arena, Array<FileDiag, ArenaAllocator>& out);

} // namespace docgen
