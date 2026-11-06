#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "links.h"
#include "samples.h"
#include "source_file.h"

using namespace docgen;

namespace {

std::string SamplesTestStr(StringView v) { return std::string(v.Data(), v.Length()); }
StringView SamplesTestSv(const std::string& s) { return StringView(s.data(), s.size()); }

struct SamplesTestTree {
    std::string root;
    SamplesTestTree() {
        char tmpl[] = "/tmp/docgen_samples_XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        root = tmpl;
        mkdir((root + "/docs").c_str(), 0777);
        mkdir((root + "/scratch").c_str(), 0777);
        Put("prelude.h", "#pragma once\n#include <cstdio>\n#include <cstdlib>\nnamespace prelude { inline int Twice(int x) { return 2 * x; } }\nusing namespace prelude;\n");
    }
    ~SamplesTestTree() {
        nftw(root.c_str(), [](const char* p, const struct stat*, int, struct FTW*) { return std::remove(p); }, 16, FTW_DEPTH | FTW_PHYS);
    }
    void Put(const std::string& rel, const std::string& content) {
        FILE* f = std::fopen((root + "/" + rel).c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(content.data(), 1, content.size(), f);
        std::fclose(f);
    }
    size_t ScratchEntries() const {
        size_t n = 0;
        DIR* d = opendir((root + "/scratch").c_str());
        while (dirent* e = readdir(d)) n += e->d_name[0] != '.';
        closedir(d);
        return n;
    }
};

struct Result {
    ArenaAllocator arena{usize{1} << 28};
    Vector<FileDiag, ArenaAllocator> diags{arena};
    Vector<Sample, ArenaAllocator> samples{arena};
    SampleStats stats;
    bool set_up = false;
    std::vector<std::string> lines;

    // Runs every sample of docs/NAME.md of `t`.
    Result(const SamplesTestTree& t, const std::string& md, u32 jobs = 1, u32 run_timeout = 10, bool keep = false) {
        std::string text_path = t.root + "/" + md;
        StringView text;
        REQUIRE(LoadSourceFile(SamplesTestSv(text_path), arena, text));
        MdDoc doc(arena);
        ScanMarkdown(text, arena, doc);
        ExtractSamples(SamplesTestSv(md), text, doc, arena, samples, diags);
        SampleOptions opt(arena);
        opt.root = SamplesTestSv(t.root);
        opt.compiler = StringView(DOCGEN_CXX);
        opt.flags.Push(StringView("-std=c++20"));
        opt.prelude = ArenaCat(arena, SamplesTestSv(t.root), StringView("/prelude.h"));
        opt.scratch_base = ArenaCat(arena, SamplesTestSv(t.root), StringView("/scratch"));
        opt.jobs = jobs;
        opt.run_timeout_s = run_timeout;
        opt.keep_scratch = keep;
        set_up = RunSamples(opt, samples, arena, diags, stats);
        for (usize i = 0; i < diags.Size(); ++i) {
            lines.push_back(SamplesTestStr(diags[i].path) + ":" + std::to_string(diags[i].d.line) + ": " + SeverityName(diags[i].d.severity) + ": " +
                            SamplesTestStr(diags[i].d.message));
        }
    }
    bool Has(const std::string& needle) const {
        for (auto& l : lines) {
            if (l.find(needle) != std::string::npos) return true;
        }
        return false;
    }
    size_t Errors() const {
        size_t n = 0;
        for (usize i = 0; i < diags.Size(); ++i) n += diags[i].d.severity == Severity::Error;
        return n;
    }
};

} // namespace

TEST_CASE("Samples: only cpp blocks with compile/run/standalone tags are samples", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "# A\n```cpp\nthis is not even C++\n```\n```text compile\nnor this\n```\n```\nplain\n```\n"
                       "```cpp compile\nint x = Twice(2); (void)x;\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.set_up);
    CHECK(r.stats.found == 1);
    CHECK(r.Errors() == 0);
    CHECK(r.stats.compiled == 1);
    CHECK(r.stats.ran == 0); // compile-only
}

TEST_CASE("Samples: a compile error is reported at the Markdown line with the compiler's message", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "# A\n\ntext\n```cpp compile\nint ok = 1; (void)ok;\nint bad = undeclared_name;\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.set_up);
    REQUIRE(r.Errors() == 1);
    CHECK(r.Has("docs/a.md:4: error: sample failed to compile"));
    CHECK(r.Has("docs/a.md:6:")); // the compiler's own diagnostic, via #line
    CHECK(r.Has("undeclared_name"));
    CHECK(r.stats.failed == 1);
}

TEST_CASE("Samples: run samples are compared against the adjacent output block", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "# A\n```cpp compile run\nstd::printf(\"%d\\n\", Twice(21));\nstd::puts(\"done\");\n```\n\n```output\n42\ndone\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.set_up);
    CHECK(r.Errors() == 0);
    CHECK(r.stats.ran == 1);
}

TEST_CASE("Samples: wrong output names the first differing line", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "# A\n```cpp compile run\nstd::puts(\"one\");\nstd::puts(\"two\");\n```\n```output\none\n2\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.Errors() == 1);
    CHECK(r.Has("docs/a.md:2: error: sample output does not match the output block: first difference at output line 2: expected '2', got 'two'"));
}

TEST_CASE("Samples: a missing line at the end of the output is a mismatch too", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile run\nstd::puts(\"one\");\n```\n```output\none\ntwo\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.Errors() == 1);
    CHECK(r.Has("expected 'two', got end of output"));
}

TEST_CASE("Samples: a non-zero exit and a crash are failures", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile run\nstd::fputs(\"oops\\n\", stderr);\nreturn 3;\n```\n"
                       "```cpp compile run\nstd::abort();\n```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.Errors() == 2);
    CHECK(r.Has("docs/a.md:1: error: sample exited with status 3"));
    CHECK(r.Has("oops"));
    CHECK(r.Has("docs/a.md:5: error: sample was terminated by signal"));
}

TEST_CASE("Samples: a runaway sample is killed at the timeout", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile run\nvolatile int spin = 1;\nwhile (spin) {}\n```\n");
    Result r(t, "docs/a.md", 1, 1);
    REQUIRE(r.Errors() == 1);
    CHECK(r.Has("docs/a.md:1: error: sample timed out after 1 s"));
}

TEST_CASE("Samples: standalone samples are whole translation units; run ones need main", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile standalone\nint Free(int x) { return x + 1; }\n```\n"
                       "```cpp compile run standalone\n#include <cstdio>\nint main() { std::puts(\"hi\"); }\n```\n```output\nhi\n```\n");
    Result r(t, "docs/a.md");
    CHECK(r.Errors() == 0);
    CHECK(r.stats.compiled == 2);
    CHECK(r.stats.ran == 1);
}

TEST_CASE("Samples: indented fences (list items) are dedented and line-mapped", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "- item\n\n  ```cpp compile\n  int a = 1;\n  int b = nope;\n  ```\n");
    Result r(t, "docs/a.md");
    REQUIRE(r.Errors() == 1);
    CHECK(r.Has("docs/a.md:5:"));
}

TEST_CASE("Samples: malformed tags are errors, not silently skipped", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp run\nint x;\n```\n```cpp compile nonsense\nint y;\n```\n```cpp standalone\nint z;\n```\n```cpp complie\nint w = bad;\n```\n");
    Result r(t, "docs/a.md");
    CHECK(r.Errors() == 4);
    CHECK(r.Has("docs/a.md:10: error: unknown sample tag 'complie'"));
    CHECK(r.stats.found == 0);
    CHECK(r.Has("docs/a.md:1: error: sample tag 'run' or 'standalone' needs 'compile'"));
    CHECK(r.Has("docs/a.md:4: error: unknown sample tag 'nonsense'"));
}

TEST_CASE("Samples: an output block that is not adjacent is not used", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile run\nstd::puts(\"real\");\n```\n\nSome text in between.\n\n```output\nsomething else\n```\n");
    Result r(t, "docs/a.md");
    CHECK(r.Errors() == 0);
    CHECK_FALSE(r.samples[0].has_expected);
}

TEST_CASE("Samples: failures are collected together, in parallel, and the scratch directory is removed", "[docgen][samples]") {
    SamplesTestTree t;
    std::string md;
    for (int i = 0; i < 6; ++i) {
        if (i % 2 == 0) md += "```cpp compile run\nstd::printf(\"%d\\n\", " + std::to_string(i) + ");\n```\n```output\n" + std::to_string(i) + "\n```\n";
        else md += "```cpp compile\nint bad" + std::to_string(i) + " = missing;\n```\n";
    }
    t.Put("docs/a.md", md);
    Result serial(t, "docs/a.md", 1);
    Result parallel(t, "docs/a.md", 4);
    CHECK(serial.Errors() == 3);
    CHECK(parallel.lines == serial.lines);
    CHECK(parallel.stats.compiled == 3);
    CHECK(parallel.stats.ran == 3);
    CHECK(t.ScratchEntries() == 0);
}

TEST_CASE("Samples: keep_scratch leaves the generated sources for debugging", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile\nint x = 1; (void)x;\n```\n");
    Result r(t, "docs/a.md", 1, 10, true);
    CHECK(t.ScratchEntries() == 1);
    // the leftover directory is removed with the tree
}

TEST_CASE("Samples: a missing compiler is reported per sample, not a crash", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile\nint x;\n```\n");
    ArenaAllocator arena{usize{1} << 26};
    Vector<FileDiag, ArenaAllocator> diags(arena);
    Vector<Sample, ArenaAllocator> samples(arena);
    StringView text;
    std::string p = t.root + "/docs/a.md";
    REQUIRE(LoadSourceFile(SamplesTestSv(p), arena, text));
    MdDoc doc(arena);
    ScanMarkdown(text, arena, doc);
    ExtractSamples(StringView("docs/a.md"), text, doc, arena, samples, diags);
    SampleOptions opt(arena);
    opt.compiler = StringView("/nonexistent/compiler");
    opt.scratch_base = ArenaCat(arena, SamplesTestSv(t.root), StringView("/scratch"));
    SampleStats stats;
    REQUIRE(RunSamples(opt, samples, arena, diags, stats));
    REQUIRE(diags.Size() == 1);
    CHECK(SamplesTestStr(diags[0].d.message).find("cannot start the compiler") != std::string::npos);
}

TEST_CASE("Samples: the repository's own docs have no failing or malformed samples", "[docgen][samples][repo]") {
    const std::string repo = std::string(DOCGEN_ENGINE_DIR) + "/..";
    ArenaAllocator arena{usize{1} << 29};
    Vector<StringView, ArenaAllocator> files(arena);
    REQUIRE(CollectMarkdownFiles(SamplesTestSv(repo), arena, files));
    Vector<FileDiag, ArenaAllocator> problems(arena);
    Vector<Sample, ArenaAllocator> samples(arena);
    for (usize i = 0; i < files.Size(); ++i) {
        StringView text;
        std::string p = repo + "/" + SamplesTestStr(files[i]);
        REQUIRE(LoadSourceFile(SamplesTestSv(p), arena, text));
        MdDoc doc(arena);
        ScanMarkdown(text, arena, doc);
        ExtractSamples(files[i], text, doc, arena, samples, problems);
    }
    for (usize i = 0; i < problems.Size(); ++i) {
        INFO(SamplesTestStr(problems[i].path) << ":" << problems[i].d.line << ": " << SamplesTestStr(problems[i].d.message));
        CHECK(problems[i].d.severity != Severity::Error);
    }
}

TEST_CASE("Samples: [review] jobs = 0 does not hang", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile\nint x = 1; (void)x;\n```\n");
    Result r(t, "docs/a.md", 0);
    CHECK(r.Errors() == 0);
    CHECK(r.stats.compiled == 1);
}

TEST_CASE("Samples: [review] a stop request kills running samples and removes the scratch directory", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile run\nvolatile int spin = 1;\nwhile (spin) {}\n```\n");
    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        RequestStop();
    });
    const auto start = std::chrono::steady_clock::now();
    Result r(t, "docs/a.md", 1, 60);
    stopper.join();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(20));
    CHECK(r.Has("interrupted"));
    CHECK(t.ScratchEntries() == 0);
}

TEST_CASE("Samples: [review] a standalone sample finds headers through -I", "[docgen][samples]") {
    SamplesTestTree t;
    t.Put("docs/a.md", "```cpp compile standalone\n#include \"local.h\"\nint Use() { return kLocal; }\n```\n");
    t.Put("local.h", "constexpr int kLocal = 1;\n");
    ArenaAllocator arena{usize{1} << 26};
    Vector<FileDiag, ArenaAllocator> diags(arena);
    Vector<Sample, ArenaAllocator> samples(arena);
    StringView text;
    std::string p = t.root + "/docs/a.md";
    REQUIRE(LoadSourceFile(SamplesTestSv(p), arena, text));
    MdDoc doc(arena);
    ScanMarkdown(text, arena, doc);
    ExtractSamples(StringView("docs/a.md"), text, doc, arena, samples, diags);
    SampleOptions opt(arena);
    opt.compiler = StringView(DOCGEN_CXX);
    opt.flags.Push(StringView("-std=c++20"));
    opt.flags.Push(ArenaCat(arena, StringView("-I"), SamplesTestSv(t.root)));
    opt.scratch_base = ArenaCat(arena, SamplesTestSv(t.root), StringView("/scratch"));
    SampleStats stats;
    REQUIRE(RunSamples(opt, samples, arena, diags, stats));
    CHECK(diags.Size() == 0);
}

TEST_CASE("Samples: the compiler path keeps its name when made absolute (no symlink resolution)", "[docgen][samples]") {
    ArenaAllocator arena(1 << 16);
    SamplesTestTree t;
    // clang++ is usually a symlink to clang, itself a symlink to clang-NN; resolving it would change the driver mode.
    std::string link = t.root + "/fake-clang++";
    REQUIRE(symlink("/bin/true", link.c_str()) == 0);

    CHECK(SamplesTestStr(AbsoluteKeepingLinks(SamplesTestSv(link), arena)) == link);  // absolute: unchanged, still the link name

    char cwd[4096];
    REQUIRE(getcwd(cwd, sizeof(cwd)) != nullptr);
    CHECK(SamplesTestStr(AbsoluteKeepingLinks(StringView("tools/clang++"), arena)) == std::string(cwd) + "/tools/clang++");
    CHECK(SamplesTestStr(AbsoluteKeepingLinks(StringView("./clang++"), arena)) == std::string(cwd) + "/clang++");
    unlink(link.c_str());
}
