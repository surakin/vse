#include "module_map.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <new>

namespace docgen {
namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

StringView Trim(StringView s) {
    usize a = 0, b = s.Length();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return StringView(s.Data() + a, b - a);
}

// Splits "word rest" at the first run of blanks.
bool NextWord(StringView& line, StringView& word) {
    line = Trim(line);
    if (line.Empty()) return false;
    usize i = 0;
    while (i < line.Length() && !IsSpace(line[i])) ++i;
    word = StringView(line.Data(), i);
    line = Trim(StringView(line.Data() + i, line.Length() - i));
    return true;
}

bool ValidPattern(StringView p) {
    if (p.Empty() || p[0] == '/') return false;
    usize i = 0;
    while (i <= p.Length()) {
        usize e = i;
        while (e < p.Length() && p[e] != '/') ++e;
        StringView seg(p.Data() + i, e - i);
        if (seg.Empty()) return false;
        if (seg == StringView("**")) {
            // fine
        } else {
            for (usize k = 0; k < seg.Length(); ++k) {
                char c = seg[k];
                if (c == '?' || c == '[' || c == ']' || c == '{' || c == '}' || c == '\\') return false;
                if (c == '*' && k + 1 < seg.Length() && seg[k + 1] == '*') return false; // `**` only as a whole segment
            }
        }
        if (e >= p.Length()) break;
        i = e + 1;
    }
    return true;
}

bool MatchSegment(const char* p, usize pn, const char* s, usize sn) {
    // Iterative '*' matching within one segment.
    usize pi = 0, si = 0, star = ~usize{0}, mark = 0;
    while (si < sn) {
        if (pi < pn && p[pi] == '*') {
            star = pi++;
            mark = si;
        } else if (pi < pn && p[pi] == s[si]) {
            ++pi;
            ++si;
        } else if (star != ~usize{0}) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < pn && p[pi] == '*') ++pi;
    return pi == pn;
}

bool MatchSegments(StringView pat, usize pi, StringView path, usize si) {
    for (;;) {
        if (pi > pat.Length()) return si > path.Length();
        usize pe = pi;
        while (pe < pat.Length() && pat[pe] != '/') ++pe;
        StringView pseg(pat.Data() + pi, pe - pi);
        const bool last_pat = pe >= pat.Length();
        if (pseg == StringView("**")) {
            if (last_pat) return si <= path.Length(); // trailing ** matches the rest (but see below)
            // Try consuming 0..n whole path segments.
            usize k = si;
            for (;;) {
                if (MatchSegments(pat, pe + 1, path, k)) return true;
                if (k > path.Length()) return false;
                usize e = k;
                while (e < path.Length() && path[e] != '/') ++e;
                if (e >= path.Length()) return false;
                k = e + 1;
            }
        }
        if (si > path.Length()) return false;
        usize se = si;
        while (se < path.Length() && path[se] != '/') ++se;
        if (!MatchSegment(pseg.Data(), pseg.Length(), path.Data() + si, se - si)) return false;
        const bool last_path = se >= path.Length();
        if (last_pat || last_path) return last_pat && last_path;
        pi = pe + 1;
        si = se + 1;
    }
}

int CompareViews(StringView a, StringView b) { return a.Compare(b); }

} // namespace

bool GlobMatch(StringView pattern, StringView path) { return MatchSegments(pattern, 0, path, 0); }

StringView GlobPrefix(StringView pattern) {
    usize last_slash = 0;
    bool any = false;
    usize i = 0;
    while (i < pattern.Length()) {
        usize e = i;
        bool wild = false;
        while (e < pattern.Length() && pattern[e] != '/') {
            if (pattern[e] == '*') wild = true;
            ++e;
        }
        if (wild || e >= pattern.Length()) break;
        last_slash = e;
        any = true;
        i = e + 1;
    }
    return any ? StringView(pattern.Data(), last_slash) : StringView();
}

void ParseModuleMap(StringView text, ArenaAllocator& arena, ModuleMap& out) {
    ModuleDef* cur = nullptr;
    u32 no = 0;
    usize pos = 0;
    while (pos <= text.Length()) {
        usize e = pos;
        while (e < text.Length() && text[e] != '\n') ++e;
        ++no;
        StringView line(text.Data() + pos, e - pos);
        pos = e + 1;
        // strip a '#' comment (the map has no quoting rules beyond the title)
        bool in_quote = false;
        for (usize i = 0; i < line.Length(); ++i) {
            if (line[i] == '"') in_quote = !in_quote;
            if (line[i] == '#' && !in_quote) {
                line = StringView(line.Data(), i);
                break;
            }
        }
        StringView word;
        if (!NextWord(line, word)) continue;
        auto err = [&](const char* m) { out.diagnostics.Push(Diagnostic{Severity::Error, no, StringView(m)}); };
        if (word == StringView("module")) {
            StringView name;
            if (!NextWord(line, name) || line.Length() < 2 || line[0] != '"' || line[line.Length() - 1] != '"') {
                err("malformed module line: expected `module NAME \"Title\"`");
                cur = nullptr;
                continue;
            }
            for (usize i = 0; i < out.modules.Size(); ++i) {
                if (out.modules[i]->name == name) {
                    err("duplicate module name");
                    break;
                }
            }
            void* mem = arena.Alloc(sizeof(ModuleDef), alignof(ModuleDef));
            ENGINE_ASSERT(mem != nullptr, "docgen: arena exhausted");
            cur = new (mem) ModuleDef(arena);
            cur->name = name;
            cur->title = StringView(line.Data() + 1, line.Length() - 2);
            cur->line = no;
            out.modules.Push(cur);
            continue;
        }
        if (word == StringView("header") || word == StringView("exclude") || word == StringView("strict")) {
            if (cur == nullptr) {
                err("directive outside a module");
                continue;
            }
            StringView arg;
            if (!NextWord(line, arg) || !line.Empty()) {
                err("expected exactly one argument");
                continue;
            }
            if (word == StringView("strict")) {
                if (arg != StringView("thread-safety") && arg != StringView("ownership") && arg != StringView("failure")) {
                    err("unknown strict rule (expected thread-safety, ownership or failure)");
                    continue;
                }
                cur->strict.Push(arg);
            } else {
                if (!ValidPattern(arg)) {
                    err("invalid pattern (only `*` within a segment and whole-segment `**` are supported)");
                    continue;
                }
                (word == StringView("header") ? cur->headers : cur->excludes).Push(arg);
            }
            continue;
        }
        err("unknown directive");
    }
}

void ExpandModule(const ModuleDef& module, const Array<StringView, ArenaAllocator>& files, ArenaAllocator& arena,
                  Array<StringView, ArenaAllocator>& out) {
    Array<StringView, ArenaAllocator> hits(arena);
    for (usize f = 0; f < files.Size(); ++f) {
        bool in = false;
        for (usize h = 0; h < module.headers.Size() && !in; ++h) in = GlobMatch(module.headers[h], files[f]);
        for (usize x = 0; x < module.excludes.Size() && in; ++x) {
            if (GlobMatch(module.excludes[x], files[f])) in = false;
        }
        if (in) hits.Push(files[f]);
    }
    // Sort + dedupe: lexicographic, independent of the input order.
    StableSort(hits, [](StringView x, StringView y) { return CompareViews(x, y) < 0; });
    for (usize i = 0; i < hits.Size(); ++i) {
        if (i > 0 && hits[i] == hits[i - 1]) continue;
        out.Push(hits[i]);
    }
}

namespace {

constexpr int kMaxDepth = 32;

bool CollectFilesRec(StringView root, StringView dir, int depth, ArenaAllocator& arena, Array<StringView, ArenaAllocator>& out) {
    StringView abs_dir = dir.Empty() ? ArenaCat(arena, root, StringView()) : ArenaCat(arena, root, StringView("/"), dir);
    DIR* d = opendir(abs_dir.Data());
    if (d == nullptr) return false;
    bool ok = true;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        StringView name(e->d_name, std::strlen(e->d_name));
        StringView rel = dir.Empty() ? ArenaCat(arena, name, StringView()) : ArenaCat(arena, dir, StringView("/"), name);
        StringView abs = ArenaCat(arena, root, StringView("/"), rel);
        struct stat lst;
        if (lstat(abs.Data(), &lst) != 0) continue;
        if (S_ISDIR(lst.st_mode)) { // symlinked directories are not followed (no cycles)
            if (depth >= kMaxDepth || !CollectFilesRec(root, rel, depth + 1, arena, out)) ok = false;
        } else if (S_ISREG(lst.st_mode)) {
            out.Push(rel);
        } else if (S_ISLNK(lst.st_mode)) {
            struct stat st;
            if (stat(abs.Data(), &st) == 0 && S_ISREG(st.st_mode)) out.Push(rel);
        }
    }
    closedir(d);
    return ok;
}

} // namespace

bool CollectFiles(StringView root, StringView dir, ArenaAllocator& arena, Array<StringView, ArenaAllocator>& out) {
    return CollectFilesRec(root, dir, 0, arena, out);
}

} // namespace docgen
