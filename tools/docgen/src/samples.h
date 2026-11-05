#pragma once

#include "md_scan.h"
#include "symbol_index.h"

// Phase 2.5 Day 42: `docgen samples` — compiles (and optionally runs) the
// tagged C++ code blocks of the Markdown documentation (DESIGN.md section 11).
//
// Thread-safety: not-thread-safe per call; child processes are driven from
//   the calling thread.
// Ownership: arena-allocated; the arena must outlive the results. Scratch
//   files live in a private directory that is removed before returning
//   (unless `keep_scratch`).
namespace docgen {

struct Sample {
    StringView path;       // Markdown file, repo-relative
    u32 line = 0;          // line of the opening fence
    u32 body_line = 0;     // first line of the code
    u32 indent = 0;        // leading blank characters stripped from each code line
    bool standalone = false; // a whole translation unit instead of a body inside the prelude
    bool run = false;
    StringView code;
    bool has_expected = false;
    StringView expected;   // body of the adjacent ```output block
};

// Finds the samples of one Markdown file. A fence is a sample if its info
// string starts with `cpp` (or `c++`) and carries `compile`, `run` or
// `standalone`; all other fences are illustrative and ignored. Malformed
// tags (`run` or `standalone` without `compile`, any unknown word after `cpp`) are errors.
void ExtractSamples(StringView path, StringView text, const MdDoc& doc, ArenaAllocator& arena, Vector<Sample, ArenaAllocator>& samples,
                    Vector<FileDiag, ArenaAllocator>& problems);

struct SampleOptions {
    explicit SampleOptions(ArenaAllocator& arena) : includes(arena), flags(arena), links(arena) {}

    StringView root;                                 // repo root; Markdown paths are relative to it
    StringView compiler;                             // e.g. "clang++"; searched in PATH if it has no '/'
    Vector<StringView, ArenaAllocator> includes;      // -I directories
    Vector<StringView, ArenaAllocator> flags;         // verbatim compiler flags (-std=c++20, -fno-exceptions, ...)
    Vector<StringView, ArenaAllocator> links;         // objects/libraries/flags appended when linking `run` samples
    StringView prelude;                              // header included before every non-standalone sample (may be empty)
    StringView scratch_base;                         // directory under which the scratch directory is created ("/tmp" if empty)
    u32 jobs = 1;                                    // concurrent child processes
    u32 compile_timeout_s = 120;
    u32 run_timeout_s = 10;
    bool keep_scratch = false;
};

struct SampleStats {
    usize found = 0;
    usize compiled = 0; // compiled successfully
    usize ran = 0;      // run samples that ran to a verdict (pass or fail)
    usize failed = 0;
};

// An absolute form of `path` that does not resolve symlinks. The compiler path must keep the name it was given:
// a driver picks its mode from the name it is run as, so `/usr/bin/clang++` (a symlink chain ending in `clang-23`)
// has to stay `clang++` or it compiles C++ in C mode and does not link the C++ runtime. An absolute path is returned
// unchanged; a relative one gets the current directory prepended (a leading "./" is dropped).
StringView AbsoluteKeepingLinks(StringView path, ArenaAllocator& arena);

// Makes a RunSamples in progress kill its children and return (what SIGINT/SIGTERM do). Async-signal-safe.
void RequestStop();

// Builds and runs `samples`. Every failure becomes an error at the sample's
// Markdown line (compiler diagnostics already name the Markdown file and line
// via #line); failures are collected, not fatal. Returns false only if the
// scratch directory or the compiler cannot be set up.
bool RunSamples(const SampleOptions& options, const Vector<Sample, ArenaAllocator>& samples, ArenaAllocator& arena,
                Vector<FileDiag, ArenaAllocator>& out, SampleStats& stats);

} // namespace docgen
