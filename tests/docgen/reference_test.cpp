#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reference.h"

using namespace docgen;

namespace {

std::string Str(StringView v) { return std::string(v.Data(), v.Length()); }

std::string ReadWhole(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

void WriteWhole(const std::string& path, const std::string& content) {
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

std::vector<std::string> ListDir(const std::string& dir) {
    std::vector<std::string> v;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return v;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] != '.') v.push_back(e->d_name);
    }
    closedir(d);
    std::sort(v.begin(), v.end());
    return v;
}

// A scratch directory removed with its (flat) contents.
struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/docgen_ref_XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() {
        for (auto& n : ListDir(path)) std::remove((path + "/" + n).c_str());
        rmdir(path.c_str());
    }
};

const std::string kRoot = std::string(DOCGEN_FIXTURE_DIR);
const std::string kMap = kRoot + "/ref/modules.txt";
const std::string kExpected = kRoot + "/ref/expected";

struct Built {
    ArenaAllocator arena{usize{1} << 28};
    ReferenceBuild build{arena};
    bool ok;
    explicit Built(const std::string& root = kRoot, const std::string& map = kMap)
        : ok(BuildReference(StringView(root.data(), root.size()), StringView(map.data(), map.size()), arena, build)) {}

    std::string File(const std::string& name) const {
        for (usize i = 0; i < build.files.Size(); ++i) {
            if (Str(build.files[i].name) == name) return Str(build.files[i].content);
        }
        return "<none>";
    }
    std::vector<std::string> Diags() const {
        std::vector<std::string> v;
        for (usize i = 0; i < build.index.diagnostics.Size(); ++i) {
            const FileDiag& f = build.index.diagnostics[i];
            v.push_back(Str(f.path) + ":" + std::to_string(f.d.line) + ": " + SeverityName(f.d.severity) + ": " + Str(f.d.message));
        }
        return v;
    }
};

bool HasDiag(const Built& b, const std::string& needle) {
    for (auto& d : b.Diags()) {
        if (d.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Builds a throwaway project: files = {relative path -> content}, plus the map text.
struct Project {
    TempDir dir;
    explicit Project(const std::vector<std::pair<std::string, std::string>>& files, const std::string& map) {
        for (auto& f : files) {
            size_t slash = f.first.rfind('/');
            if (slash != std::string::npos) mkdir((dir.path + "/" + f.first.substr(0, slash)).c_str(), 0777);
            WriteWhole(dir.path + "/" + f.first, f.second);
        }
        WriteWhole(dir.path + "/modules.txt", map);
    }
    ~Project() {
        // remove nested dirs created above
        for (auto& n : ListDir(dir.path)) {
            struct stat st;
            std::string p = dir.path + "/" + n;
            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                for (auto& m : ListDir(p)) std::remove((p + "/" + m).c_str());
                rmdir(p.c_str());
            }
        }
    }
    std::string Map() const { return dir.path + "/modules.txt"; }
};

} // namespace

// ---- module map and globbing ---------------------------------------------------------------

TEST_CASE("Glob matching: * stays in a segment, ** spans segments", "[docgen][reference][glob]") {
    auto m = [](const char* p, const char* s) { return GlobMatch(StringView(p), StringView(s)); };
    REQUIRE(m("engine/jobs/*.h", "engine/jobs/counter.h"));
    REQUIRE_FALSE(m("engine/jobs/*.h", "engine/jobs/sub/counter.h"));
    REQUIRE_FALSE(m("engine/jobs/*.h", "engine/jobs/counter.cpp"));
    REQUIRE(m("engine/**/*.h", "engine/a.h"));
    REQUIRE(m("engine/**/*.h", "engine/a/b/c.h"));
    REQUIRE_FALSE(m("engine/**/*.h", "other/a.h"));
    REQUIRE(m("a/**", "a/b/c"));
    REQUIRE(m("*.h", "x.h"));
    REQUIRE_FALSE(m("*.h", "d/x.h"));
    REQUIRE(m("a*b*c.h", "aXXbYYc.h"));
    REQUIRE_FALSE(m("a*b*c.h", "aXXbYY.h"));
    REQUIRE(m("engine/types.h", "engine/types.h"));
    REQUIRE_FALSE(m("engine/types.h", "engine/types.hh"));
    REQUIRE(Str(GlobPrefix(StringView("engine/jobs/*.h"))) == "engine/jobs");
    REQUIRE(Str(GlobPrefix(StringView("engine/**/*.h"))) == "engine");
    REQUIRE(Str(GlobPrefix(StringView("*.h"))) == "");
    REQUIRE(Str(GlobPrefix(StringView("engine/types.h"))) == "engine");
}

TEST_CASE("Module map parsing and its errors", "[docgen][reference][map]") {
    ArenaAllocator arena(usize{1} << 24);
    auto parse = [&](const char* text, ModuleMap& map) { ParseModuleMap(StringView(text), arena, map); };
    {
        ModuleMap mm(arena);
        auto* m = &mm;
        parse("# c\nmodule a \"Title A\"   # trailing\n  header x/*.h\n  exclude x/y.h\n  strict ownership\n\nmodule b/c \"B # C\"\n  header z.h\n", mm);
        REQUIRE_FALSE(m->HasErrors());
        REQUIRE(m->modules.Size() == 2);
        REQUIRE(Str(m->modules[0]->title) == "Title A");
        REQUIRE(m->modules[0]->headers.Size() == 1);
        REQUIRE(m->modules[0]->excludes.Size() == 1);
        REQUIRE(Str(m->modules[0]->strict[0]) == "ownership");
        REQUIRE(Str(m->modules[1]->title) == "B # C"); // '#' inside quotes is part of the title
    }
    auto err = [&](const char* text, const char* expect, u32 line) {
        ModuleMap mm(arena);
        auto* m = &mm;
        parse(text, mm);
        INFO(text);
        REQUIRE(m->diagnostics.Size() >= 1);
        REQUIRE(m->diagnostics[0].line == line);
        REQUIRE(Str(m->diagnostics[0].message).find(expect) != std::string::npos);
    };
    err("header x.h\n", "outside a module", 1);
    err("module a\n", "malformed module line", 1);
    err("module a Title\n", "malformed module line", 1);
    err("module a \"A\"\nmodule a \"B\"\n", "duplicate module", 2);
    err("module a \"A\"\n  strict speed\n", "unknown strict rule", 2);
    err("module a \"A\"\n  header a?.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  header a**.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  header /abs.h\n", "invalid pattern", 2);
    err("module a \"A\"\n  frobnicate x\n", "unknown directive", 2);
    err("module a \"A\"\n  header a.h b.h\n", "exactly one argument", 2);
}

TEST_CASE("Module expansion is sorted, deduplicated and independent of input order", "[docgen][reference][map]") {
    ArenaAllocator arena(usize{1} << 24);
    ModuleMap map(arena);
    ParseModuleMap(StringView("module m \"M\"\n header a/*.h\n header a/b.h\n exclude a/skip.h\n"), arena, map);
    REQUIRE_FALSE(map.HasErrors());
    std::vector<std::string> names = {"a/z.h", "a/b.h", "a/skip.h", "a/c.h", "q/other.h", "a/sub/deep.h", "a/b.h"};
    std::vector<std::string> first;
    for (int round = 0; round < 6; ++round) {
        Array<StringView, ArenaAllocator> files(arena), out(arena);
        for (auto& n : names) files.Push(StringView(n.data(), n.size()));
        ExpandModule(*map.modules[0], files, arena, out);
        std::vector<std::string> got;
        for (usize i = 0; i < out.Size(); ++i) got.push_back(Str(out[i]));
        if (round == 0) first = got;
        REQUIRE(got == first);
        std::next_permutation(names.begin(), names.end());
        std::rotate(names.begin(), names.begin() + 1, names.end());
    }
    REQUIRE(first == (std::vector<std::string>{"a/b.h", "a/c.h", "a/z.h"}));
}

TEST_CASE("File collection walks directories deterministically after sorting", "[docgen][reference][map]") {
    ArenaAllocator arena(usize{1} << 24);
    Array<StringView, ArenaAllocator> files(arena);
    REQUIRE(CollectFiles(StringView(kRoot.data(), kRoot.size()), StringView("ref"), arena, files));
    std::vector<std::string> got;
    for (usize i = 0; i < files.Size(); ++i) got.push_back(Str(files[i]));
    REQUIRE(std::find(got.begin(), got.end(), "ref/headers/counter.h") != got.end());
    REQUIRE(std::find(got.begin(), got.end(), "ref/modules.txt") != got.end());
    REQUIRE_FALSE(CollectFiles(StringView(kRoot.data(), kRoot.size()), StringView("nonexistent"), arena, files));
}

// ---- symbol IDs, anchors, resolution --------------------------------------------------------

TEST_CASE("Symbol IDs drop the outer namespace, normalize parameters and keep qualifiers", "[docgen][reference][index]") {
    Project p({{"h/a.h",
                "#pragma once\n"
                "namespace engine_placeholder {\nnamespace n {\n"
                "/// A class.\nclass K {\npublic:\n"
                "  /// Sum.\n  int Sum(const std::atomic<int>&  value,  unsigned long long n = 4, char* const names[], Foo<int, 2> f) const noexcept;\n"
                "  /// Plain.\n  void Plain(void);\n"
                "  /// Pointer.\n  void Fn(bool (*done)(void* user), int (&arr)[3]);\n"
                "  /// Op.\n  bool operator==(const K& other) const;\n"
                "};\n}\n}\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    std::vector<std::string> ids;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i) ids.push_back(Str(b.build.index.symbols[i]->id));
    REQUIRE(ids[0] == "n::K");
    REQUIRE(ids[1] == "n::K::Sum(const std::atomic<int>&,unsigned long long,char* const[],Foo<int,2>) const noexcept");
    REQUIRE(ids[2] == "n::K::Plain()");
    REQUIRE(ids[3] == "n::K::Fn(bool(*)(void*),int(&)[3])");
    REQUIRE(ids[4] == "n::K::operator==(const K&) const");
}

TEST_CASE("Anchors: lowercase, punctuation words for * & operators, remaining ties get -2", "[docgen][reference][index]") {
    Project p({{"h/a.h",
                "#pragma once\n"
                "/// A.\nvoid Do(int);\n"
                "/// B.\nvoid Do(int*);\n"      // different ID, same anchor "do-int"
                "/// C.\nvoid Do(int&);\n"      // and again
                "/// D.\nvoid Do_Int();\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    std::vector<std::string> anchors;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i) anchors.push_back(Str(b.build.index.symbols[i]->anchor));
    REQUIRE(anchors == (std::vector<std::string>{"do-int", "do-int-star", "do-int-amp", "do-int-2"}));
}

TEST_CASE("Hidden, Internal and namespace-less declarations are indexed correctly", "[docgen][reference][index]") {
    Built b;
    REQUIRE(b.ok);
    std::vector<std::string> ids;
    for (usize i = 0; i < b.build.index.symbols.Size(); ++i) ids.push_back(Str(b.build.index.symbols[i]->id));
    auto has = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    REQUIRE(has("demo::Counter::Increment(unsigned long)"));
    REQUIRE_FALSE(has("demo::Counter::count_"));      // private
    REQUIRE_FALSE(has("demo::HelperForTests()"));     // Internal:
    REQUIRE_FALSE(has("ForTests()"));                 // Internal: in the corpus
    REQUIRE(has("DEMO_SQUARE"));
    // Source order within a header: the macro sits between MakeCounter and Count.
    auto pos = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) - ids.begin(); };
    REQUIRE(pos("demo::MakeCounter(unsigned long)") < pos("DEMO_SQUARE"));
    REQUIRE(pos("DEMO_SQUARE") < pos("demo::Count"));
}

TEST_CASE("Reference resolution: qualified, partial, hinted, ambiguous, missing", "[docgen][reference][resolve]") {
    Project p({{"h/a.h",
                "#pragma once\n"
                "namespace engine_placeholder {\nnamespace jobs {\n"
                "/// Counter.\nclass Counter {\npublic:\n  /// Ctor.\n  Counter();\n"
                "  /// Inc.\n  void Increment(unsigned long n = 1);\n"
                "  /// Help.\n  void Help(int a);\n  /// Help.\n  void Help(Counter& c);\n};\n"
                "/// Other.\nclass Other {\npublic:\n  /// Inc.\n  void Increment();\n};\n"
                "}\n}\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    auto r = [&](const char* t, const char* expect_id) {
        const Symbol* s = nullptr;
        ResolveStatus st = ResolveRef(b.build.index, StringView(t), s);
        INFO(t);
        if (expect_id == nullptr) return st;
        REQUIRE(st == ResolveStatus::Ok);
        REQUIRE(Str(s->id) == expect_id);
        return st;
    };
    r("jobs::Counter", "jobs::Counter");
    r("Counter", "jobs::Counter");                       // the class, not its constructor
    r("engine_placeholder::jobs::Counter", "jobs::Counter");
    r("Counter::Counter", "jobs::Counter::Counter()");
    r("Counter::Increment", "jobs::Counter::Increment(unsigned long)");
    r("jobs::Counter::Help(int)", "jobs::Counter::Help(int)");
    r("Help(Counter&)", "jobs::Counter::Help(Counter&)");
    r("Help( Counter & )", "jobs::Counter::Help(Counter&)"); // whitespace-insensitive hint
    REQUIRE(r("Increment", nullptr) == ResolveStatus::Ambiguous);          // Counter::Increment vs Other::Increment
    REQUIRE(r("Counter::Help", nullptr) == ResolveStatus::Ambiguous);      // overloads without a hint
    REQUIRE(r("Help(long)", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("Nope", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("", nullptr) == ResolveStatus::NotFound);
    REQUIRE(r("jobs::Count", nullptr) == ResolveStatus::NotFound);          // a prefix of a name is not a match
}

// ---- build diagnostics ----------------------------------------------------------------------

TEST_CASE("Unresolved and ambiguous references in comments are errors at their line", "[docgen][reference][index]") {
    Project p({{"h/a.h",
                "#pragma once\n"
                "/// Fine, see [[Real]].\nvoid Real();\n"
                "/// Bad ref to [[Missing]].\nvoid A();\n"
                "/// Two of them.\n/// See: [[Dup]]\nvoid B();\n"
                "namespace x { /// Dup.\n void Dup(); }\n"
                "namespace y { /// Dup.\n void Dup(); }\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE_FALSE(b.ok);
    REQUIRE(b.build.files.Size() == 0);
    REQUIRE(HasDiag(b, "h/a.h:4: error: unresolved reference 'Missing'"));
    REQUIRE(HasDiag(b, "h/a.h:7: error: ambiguous reference 'Dup'"));
    REQUIRE_FALSE(HasDiag(b, "'Real'"));
}

TEST_CASE("Map problems: unmatched patterns, shared headers, duplicate IDs, bad headers", "[docgen][reference][index]") {
    {
        Project p({{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}}, "module m \"M\"\n header h/*.h\n header nothing/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "modules.txt:1: error: header pattern matches no files: nothing/*.h"));
    }
    {
        Project p({{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}}, "module m \"M\"\n header h/a.h\nmodule n \"N\"\n header h/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "header listed in two modules: h/a.h"));
    }
    {
        Project p({{"h/a.h", "#pragma once\n/// A.\nvoid A();\n"}, {"h/b.h", "#pragma once\n/// A again.\nvoid A();\n"}},
                  "module m \"M\"\n header h/*.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "h/b.h:3: error: duplicate symbol ID 'A()'"));
    }
    {
        // A declaration followed by its inline definition in one header is a single symbol.
        Project p({{"h/a.h", "#pragma once\nvoid A();\n/// Defined here.\ninline void A() {}\n"}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        REQUIRE(b.build.index.symbols.Size() == 1);
        REQUIRE(b.build.index.symbols[0]->doc != nullptr); // the documented one wins
    }
    {
        Project p({{"h/a.h", "#pragma once\nstruct S {\n"}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "h/a.h:2: error: unbalanced braces"));
    }
    {
        Built b(kRoot, kRoot + "/ref/does-not-exist.txt");
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "cannot read module map"));
    }
}

TEST_CASE("Excluded headers are never parsed", "[docgen][reference][index]") {
    Built b;
    REQUIRE(b.ok); // ref/headers/skipped.h is deliberately invalid C++
    for (usize i = 0; i < b.build.index.units.Size(); ++i) REQUIRE(Str(b.build.index.units[i]->path) != "ref/headers/skipped.h");
}

// ---- rendering and goldens ---------------------------------------------------------------------

TEST_CASE("Generated pages match the committed golden output", "[docgen][reference][golden]") {
    Built b;
    REQUIRE(b.ok);
    std::vector<std::string> names;
    for (usize i = 0; i < b.build.files.Size(); ++i) names.push_back(Str(b.build.files[i].name));
    REQUIRE(names == (std::vector<std::string>{"README.md", "demo.md", "extras.md", "symbols.txt"}));
    for (auto& n : names) {
        INFO(n);
        REQUIRE(b.File(n) == ReadWhole(kExpected + "/" + n));
    }
    // No stray files in the golden directory.
    REQUIRE(ListDir(kExpected) == names);
}

TEST_CASE("Rendering details", "[docgen][reference][render]") {
    Built b;
    REQUIRE(b.ok);
    const std::string demo = b.File("demo.md");
    auto has = [&](const char* s) { return demo.find(s) != std::string::npos; };
    REQUIRE(has("# Demo Module\n\n_Generated by docgen. Do not edit._\n"));
    REQUIRE(has("**Headers:** `ref/headers/counter.h`, `ref/headers/shapes.h`"));
    REQUIRE(has("The join primitive of the demo module."));                          // file overview
    REQUIRE(has("## `Counter` {#demo-counter}"));
    REQUIRE(has("### `Increment(unsigned long)` {#demo-counter-increment-unsigned-long}"));
    REQUIRE(has("#### `resumes` {#demo-counter-stats-resumes}"));                    // nested member level
    REQUIRE(has("| Parameter `n` | how many units to add. |"));
    REQUIRE(has("[`demo::Counter::Increment`](#demo-counter-increment-unsigned-long)")); // same-page link
    REQUIRE(has("<a id=\"demo-wakemode-requeue\"></a>`Requeue` | `3` |"));             // enumerator table
    REQUIRE(has("_Inherited from [`demo::Shape::Area() const`](#demo-shape-area-const)._"));
    REQUIRE(has("Draws the circle with its own words."));                              // own doc wins, no inheritance note
    REQUIRE(has("**Example** — counting two jobs."));
    REQUIRE(has("#define DEMO_SQUARE(x)"));
    REQUIRE_FALSE(has("count_"));
    REQUIRE_FALSE(has("HelperForTests"));

    const std::string extras = b.File("extras.md");
    REQUIRE(extras.find("Base(const Base&) = delete;") != std::string::npos);
    REQUIRE(extras.find("virtual void Hook() = 0;") != std::string::npos);
    REQUIRE(extras.find("template <>\nclass Buffer<char, 1> { ... }") != std::string::npos);
    REQUIRE(extras.find("| Deprecated | use [`Read`](#read-int-int-star) instead") != std::string::npos);
    REQUIRE(extras.find("_Undocumented._") != std::string::npos);

    const std::string syms = b.File("symbols.txt");
    REQUIRE(syms.compare(0, 20, "# docgen symbols v1\n") == 0);
    REQUIRE(syms.find("demo::Counter::Count() const\tfunction\tdemo\tdemo-counter-count-const\tdemo.md\tref/headers/counter.h:47\n") !=
            std::string::npos);
}

TEST_CASE("Cross-module links name the other page", "[docgen][reference][render]") {
    Project p({{"h/a.h", "#pragma once\n/// Uses [[B]].\nvoid A();\n"}, {"g/b.h", "#pragma once\n/// B.\nvoid B();\n"}},
              "module one \"One\"\n header h/a.h\nmodule two/x \"Two\"\n header g/b.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    REQUIRE(b.File("one.md").find("Uses [`B`](two-x.md#b).") != std::string::npos);
    REQUIRE(b.File("README.md").find("[Two](two-x.md)") != std::string::npos);
}

// ---- determinism, idempotence, staleness -------------------------------------------------------

TEST_CASE("Two builds are byte-identical", "[docgen][reference][determinism]") {
    Built a, b;
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.build.files.Size() == b.build.files.Size());
    for (usize i = 0; i < a.build.files.Size(); ++i) {
        REQUIRE(a.build.files[i].name == b.build.files[i].name);
        REQUIRE(a.build.files[i].content == b.build.files[i].content);
    }
}

TEST_CASE("reference writes, then check passes; stale output is detected", "[docgen][reference][check]") {
    TempDir out;
    Built b;
    REQUIRE(b.ok);
    {
        Array<FileDiag, ArenaAllocator> problems(b.arena);
        StringView dir(out.path.data(), out.path.size());
        REQUIRE(WriteReference(b.build, dir, b.arena, problems));
        REQUIRE(problems.Size() == 0);
        REQUIRE(ListDir(out.path) == (std::vector<std::string>{"README.md", "demo.md", "extras.md", "symbols.txt"}));

        // idempotent: a second write changes nothing, and check is clean
        std::string before = ReadWhole(out.path + "/demo.md");
        REQUIRE(WriteReference(b.build, dir, b.arena, problems));
        REQUIRE(ReadWhole(out.path + "/demo.md") == before);
        REQUIRE(CheckReference(b.build, dir, b.arena, problems));
        REQUIRE(problems.Size() == 0);
    }
    auto check = [&](std::vector<std::string>& msgs) {
        Array<FileDiag, ArenaAllocator> problems(b.arena);
        bool ok = CheckReference(b.build, StringView(out.path.data(), out.path.size()), b.arena, problems);
        for (usize i = 0; i < problems.Size(); ++i) msgs.push_back(Str(problems[i].path).substr(out.path.size() + 1) + ":" +
                                                                   std::to_string(problems[i].d.line) + ": " + Str(problems[i].d.message));
        return ok;
    };
    // 1. a changed page
    std::string demo = ReadWhole(out.path + "/demo.md");
    WriteWhole(out.path + "/demo.md", demo + "extra\n");
    {
        std::vector<std::string> msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.size() == 1);
        REQUIRE(msgs[0].compare(0, 8, "demo.md:") == 0);
        REQUIRE(msgs[0].find("out of date") != std::string::npos);
        // reports the first differing line
        size_t lines = std::count(demo.begin(), demo.end(), '\n');
        REQUIRE(msgs[0].find("line " + std::to_string(lines + 1)) != std::string::npos);
    }
    WriteWhole(out.path + "/demo.md", demo);
    // 2. a missing file
    std::remove((out.path + "/symbols.txt").c_str());
    {
        std::vector<std::string> msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.size() == 1);
        REQUIRE(msgs[0].find("missing") != std::string::npos);
    }
    // 3. a generated page that is no longer produced; a hand-written md is left alone
    Array<FileDiag, ArenaAllocator> problems(b.arena);
    REQUIRE(WriteReference(b.build, StringView(out.path.data(), out.path.size()), b.arena, problems));
    WriteWhole(out.path + "/old.md", std::string("# Old\n\n") + kGeneratedMarker + "\n");
    WriteWhole(out.path + "/notes.md", "# Hand written\n");
    {
        std::vector<std::string> msgs;
        REQUIRE_FALSE(check(msgs));
        REQUIRE(msgs.size() == 1);
        REQUIRE(msgs[0].compare(0, 7, "old.md:") == 0);
    }
    // reference removes the stale generated page and keeps the hand-written one
    REQUIRE(WriteReference(b.build, StringView(out.path.data(), out.path.size()), b.arena, problems));
    REQUIRE(ListDir(out.path) == (std::vector<std::string>{"README.md", "demo.md", "extras.md", "notes.md", "symbols.txt"}));
    std::vector<std::string> msgs;
    REQUIRE(check(msgs));
}

TEST_CASE("The repository's own module map builds (headers and patterns all resolve)", "[docgen][reference][engine]") {
    const std::string root = std::string(DOCGEN_ENGINE_DIR) + "/..";
    const std::string map = root + "/docs/reference/modules.txt";
    Built b(root, map);
    for (auto& d : b.Diags()) INFO(d);
    REQUIRE(b.ok);
    REQUIRE(b.build.index.symbols.Size() > 500);
    // The generated Wayland protocol headers are excluded.
    for (usize i = 0; i < b.build.index.units.Size(); ++i) {
        REQUIRE(Str(b.build.index.units[i]->path).find("/generated/") == std::string::npos);
    }
}

// ---- code-review regressions (Day 40) -----------------------------------------------------------

TEST_CASE("[review] operators, destructors and pointer overloads keep distinct, stable anchors", "[docgen][reference][review]") {
    auto anchors_for = [](const std::string& body) {
        Project p({{"h/a.h", "#pragma once\n" + body}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        std::vector<std::string> v;
        for (usize i = 0; i < b.build.index.symbols.Size(); ++i) {
            v.push_back(Str(b.build.index.symbols[i]->id) + " => " + Str(b.build.index.symbols[i]->anchor));
        }
        return v;
    };
    const std::string ops =
        "/// V.\nstruct V {\n"
        "  /// Default.\n  V();\n  /// Dtor.\n  ~V();\n"
        "  /// Minus.\n  V operator-(const V&) const;\n"
        "  /// Eq.\n  bool operator==(const V&) const;\n"
        "  /// Ne.\n  bool operator!=(const V&) const;\n"
        "  /// Lt.\n  bool operator<(const V&) const;\n"
        "  /// Call.\n  int operator()(int) const;\n"
        "  /// Index.\n  int operator[](int) const;\n"
        "};\n";
    auto first = anchors_for(ops);
    // No anchor needed a -2 tie-break suffix.
    for (auto& a : first) {
        INFO(a);
        REQUIRE(a.find("-2") == std::string::npos);
    }
    REQUIRE(first[1] == "V::V() => v-v");
    REQUIRE(first[2] == "V::~V() => v-tilde-v");
    REQUIRE(first[3] == "V::operator-(const V&) const => v-operator-minus-const-v-amp-const");
    REQUIRE(first[7] == "V::operator()(int) const => v-operator-call-int-const");
    REQUIRE(first[8] == "V::operator[](int) const => v-operator-index-int-const");
    // Reordering the operators does not change any anchor.
    const std::string swapped =
        "/// V.\nstruct V {\n"
        "  /// Dtor.\n  ~V();\n  /// Default.\n  V();\n"
        "  /// Ne.\n  bool operator!=(const V&) const;\n"
        "  /// Eq.\n  bool operator==(const V&) const;\n"
        "  /// Minus.\n  V operator-(const V&) const;\n"
        "  /// Index.\n  int operator[](int) const;\n"
        "  /// Call.\n  int operator()(int) const;\n"
        "  /// Lt.\n  bool operator<(const V&) const;\n"
        "};\n";
    auto second = anchors_for(swapped);
    std::sort(first.begin(), first.end());
    std::sort(second.begin(), second.end());
    REQUIRE(first == second);
}

TEST_CASE("[review] only a real marker line makes a page 'generated'", "[docgen][reference][review]") {
    TempDir out;
    Built b;
    REQUIRE(b.ok);
    Array<FileDiag, ArenaAllocator> problems(b.arena);
    StringView dir(out.path.data(), out.path.size());
    REQUIRE(WriteReference(b.build, dir, b.arena, problems));
    // Hand-written pages that quote the marker (mid-file, or as an indented line) are not touched.
    WriteWhole(out.path + "/design.md", std::string("# Design\n\nThe tool writes `") + kGeneratedMarker + "` at the top.\n");
    WriteWhole(out.path + "/quote.md", std::string("# Quote\n\n    ") + kGeneratedMarker + "\n");
    WriteWhole(out.path + "/nolines.md", kGeneratedMarker);
    REQUIRE(CheckReference(b.build, dir, b.arena, problems));
    REQUIRE(problems.Size() == 0);
    REQUIRE(WriteReference(b.build, dir, b.arena, problems));
    REQUIRE(ListDir(out.path) == (std::vector<std::string>{"README.md", "demo.md", "design.md", "extras.md", "nolines.md", "quote.md", "symbols.txt"}));
}

TEST_CASE("[review] a missing module map is an I/O failure; documentation errors are not", "[docgen][reference][review]") {
    Built missing(kRoot, kRoot + "/ref/does-not-exist.txt");
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.build.io_failure);
    Project p({{"h/a.h", "#pragma once\nstruct S {\n"}}, "module m \"M\"\n header h/a.h\n");
    Built broken(p.dir.path, p.Map());
    REQUIRE_FALSE(broken.ok);
    REQUIRE_FALSE(broken.build.io_failure);
}

TEST_CASE("[review] reference creates a nested output directory", "[docgen][reference][review]") {
    TempDir base;
    Built b;
    REQUIRE(b.ok);
    Array<FileDiag, ArenaAllocator> problems(b.arena);
    std::string nested = base.path + "/a/b/c";
    REQUIRE(WriteReference(b.build, StringView(nested.data(), nested.size()), b.arena, problems));
    REQUIRE(problems.Size() == 0);
    REQUIRE(ListDir(nested).size() == 4);
    // cleanup of the nested tree (TempDir only removes files)
    for (auto& n : ListDir(nested)) std::remove((nested + "/" + n).c_str());
    rmdir(nested.c_str());
    rmdir((base.path + "/a/b").c_str());
    rmdir((base.path + "/a").c_str());
    // a path that cannot be created is reported once, not once per file
    std::string bad = base.path + "/file";
    WriteWhole(bad, "x");
    std::string under_file = bad + "/sub";
    Array<FileDiag, ArenaAllocator> problems2(b.arena);
    REQUIRE_FALSE(WriteReference(b.build, StringView(under_file.data(), under_file.size()), b.arena, problems2));
    REQUIRE(problems2.Size() == 1);
}

TEST_CASE("[review] same-ID declarations: a function redeclaration merges, other kinds are duplicates", "[docgen][reference][review]") {
    {
        // The first declaration keeps its location; the documented one supplies the comment.
        Project p({{"h/a.h", "#pragma once\nvoid A();\n/// Defined here.\ninline void A() {}\n"}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        REQUIRE(b.ok);
        REQUIRE(b.build.index.symbols.Size() == 1);
        REQUIRE(b.build.index.symbols[0]->decl->line == 2);
        REQUIRE(b.build.index.symbols[0]->doc != nullptr);
    }
}

TEST_CASE("[review] duplicate classes, enums and variables in one header are errors", "[docgen][reference][review]") {
    for (const char* body : {"/// A.\nstruct S {};\n/// B.\nstruct S {};\n", "/// A.\nint v;\n/// B.\nint v;\n", "/// A.\nenum E { X };\n/// B.\nenum E { Y };\n"}) {
        Project p({{"h/a.h", std::string("#pragma once\n") + body}}, "module m \"M\"\n header h/a.h\n");
        Built b(p.dir.path, p.Map());
        INFO(body);
        REQUIRE_FALSE(b.ok);
        REQUIRE(HasDiag(b, "error: duplicate symbol ID"));
    }
}

TEST_CASE("[review] references to operator() and long overload hints resolve", "[docgen][reference][review]") {
    std::string params;
    for (int i = 0; i < 80; ++i) params += std::string(i ? ", " : "") + "SomeLongTypeName" + std::to_string(i) + " p" + std::to_string(i);
    Project p({{"h/a.h",
                "#pragma once\n"
                "/// Hash.\nstruct Hash {\n  /// Call.\n  unsigned long operator()(int v) const;\n  /// Index.\n  int operator[](int i) const;\n"
                "  /// Wide.\n  void Wide(" + params + ");\n};\n"}},
              "module m \"M\"\n header h/a.h\n");
    Built b(p.dir.path, p.Map());
    REQUIRE(b.ok);
    auto id = [&](const std::string& t) {
        const Symbol* s = nullptr;
        ResolveStatus st = ResolveRef(b.build.index, StringView(t.data(), t.size()), s);
        return st == ResolveStatus::Ok ? Str(s->id).substr(0, 40) : std::string("<") + std::to_string(static_cast<int>(st)) + ">";
    };
    REQUIRE(id("Hash::operator()") == "Hash::operator()(int) const");
    REQUIRE(id("Hash::operator()(int)") == "Hash::operator()(int) const");
    REQUIRE(id("operator()(int)") == "Hash::operator()(int) const");
    REQUIRE(id("Hash::operator[](int)") == "Hash::operator[](int) const");
    std::string hint = "Wide(";
    for (int i = 0; i < 80; ++i) hint += std::string(i ? "," : "") + "SomeLongTypeName" + std::to_string(i);
    hint += ")";
    REQUIRE(id(hint).compare(0, 10, "Hash::Wide") == 0);          // > 512 characters of hint
    REQUIRE(id(hint.substr(0, hint.size() - 2) + "x)") == "<1>");   // a long hint that differs at the end is NotFound
}

TEST_CASE("[review] file collection does not follow directory symlinks and reports unreadable subdirectories", "[docgen][reference][review]") {
    TempDir dir;
    mkdir((dir.path + "/real").c_str(), 0777);
    WriteWhole(dir.path + "/real/a.h", "x");
    REQUIRE(symlink(dir.path.c_str(), (dir.path + "/real/loop").c_str()) == 0); // a cycle
    REQUIRE(symlink("a.h", (dir.path + "/real/link.h").c_str()) == 0);         // a file symlink is still a file
    ArenaAllocator arena(usize{1} << 24);
    Array<StringView, ArenaAllocator> files(arena);
    REQUIRE(CollectFiles(StringView(dir.path.data(), dir.path.size()), StringView(), arena, files));
    std::vector<std::string> got;
    for (usize i = 0; i < files.Size(); ++i) got.push_back(Str(files[i]));
    std::sort(got.begin(), got.end());
    REQUIRE(got == (std::vector<std::string>{"real/a.h", "real/link.h"}));
    // an unreadable subdirectory makes the walk report failure (running as root ignores modes)
    if (geteuid() != 0) {
        mkdir((dir.path + "/locked").c_str(), 0000);
        Array<StringView, ArenaAllocator> files2(arena);
        REQUIRE_FALSE(CollectFiles(StringView(dir.path.data(), dir.path.size()), StringView(), arena, files2));
        chmod((dir.path + "/locked").c_str(), 0700);
        rmdir((dir.path + "/locked").c_str());
    }
    std::remove((dir.path + "/real/loop").c_str());
    std::remove((dir.path + "/real/link.h").c_str());
    std::remove((dir.path + "/real/a.h").c_str());
    rmdir((dir.path + "/real").c_str());
}

TEST_CASE("Reference: [review] deleted and defaulted members render as such, not as undocumented", "[docgen][reference]") {
    Project p({{"h/a.h", "#pragma once\nnamespace engine_placeholder {\n/// A thing.\nstruct T {\n  T() = default;\n  T(const T&) = delete;\n  /// Doc.\n  void F();\n  void G();\n};\n}\n"}},
              "module m \"M\"\n  header h/*.h\n");
    std::string map = p.dir.path + "/modules.txt";
    Built b(p.dir.path, map);
    REQUIRE(b.ok);
    std::string page = b.File("m.md");
    CHECK(page.find("_Defaulted._") != std::string::npos);
    CHECK(page.find("_Deleted._") != std::string::npos);
    CHECK(page.find("_Undocumented._") != std::string::npos); // G()
}
