#include "links.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <new>

#include "module_map.h"
#include "source_file.h"

namespace docgen {

namespace {

struct Page {
    StringView path; // repo-relative
    MdDoc* doc = nullptr;
    bool in_set = false; // checked (docs/** or top-level), not merely linked to
    bool reached = false;
    bool loaded = false;
};

bool EndsWith(StringView s, const char* suffix) {
    usize n = 0;
    while (suffix[n] != '\0') ++n;
    if (s.Length() < n) return false;
    for (usize i = 0; i < n; ++i) {
        if (s[s.Length() - n + i] != suffix[i]) return false;
    }
    return true;
}

bool StartsWith(StringView s, const char* prefix) {
    usize n = 0;
    while (prefix[n] != '\0') ++n;
    if (s.Length() < n) return false;
    for (usize i = 0; i < n; ++i) {
        if (s[i] != prefix[i]) return false;
    }
    return true;
}

// `scheme:` per RFC 3986 (letters, digits, + - .; at least two characters so Windows-style "c:" is not one).
bool HasScheme(StringView t) {
    if (t.Empty() || !((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= 'A' && t[0] <= 'Z'))) return false;
    for (usize i = 1; i < t.Length(); ++i) {
        const char c = t[i];
        if (c == ':') return i >= 2;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return false;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Edge {
    Page* from;
    Page* to;
};

class Checker {
public:
    Checker(StringView root, const SymbolIndex* index, ArenaAllocator& arena, const LinkOptions& opt, Array<FileDiag, ArenaAllocator>& out,
            LinkStats& stats)
        : root_(root), index_(index), arena_(arena), opt_(opt), out_(out), stats_(stats), pages_(arena) {}

    bool Run() {
        Array<StringView, ArenaAllocator> files(arena_);
        if (!CollectMarkdownFiles(root_, arena_, files)) return false;
        for (usize i = 0; i < files.Size(); ++i) {
            Page* p = Get(files[i]);
            if (p == nullptr) {
                Add(files[i], 1, Severity::Error, StringView("cannot read file"));
                continue;
            }
            p->in_set = true;
        }
        // Checking can load more pages (link targets outside the set), so index by position.
        const usize count = pages_.Size();
        for (usize i = 0; i < count; ++i) {
            if (!pages_[i]->in_set || pages_[i]->doc == nullptr) continue;
            ++stats_.files;
            CheckPage(*pages_[i]);
        }
        Orphans();
        StableSort(out_, [](const FileDiag& a, const FileDiag& b) {
            const int c = a.path.Compare(b.path);
            return c != 0 ? c < 0 : a.d.line < b.d.line;
        });
        return true;
    }

private:
    StringView Full(StringView rel) { return root_.Empty() ? ArenaCat(arena_, rel, StringView()) : ArenaCat(arena_, root_, StringView("/"), rel); }

    Page* Get(StringView path) {
        for (usize i = 0; i < pages_.Size(); ++i) {
            if (pages_[i]->path == path) return pages_[i]->loaded ? pages_[i] : nullptr;
        }
        Page* p = static_cast<Page*>(arena_.Alloc(sizeof(Page), alignof(Page)));
        new (p) Page();
        p->path = ArenaCat(arena_, path, StringView());
        pages_.Push(p);
        StringView text;
        if (!LoadSourceFile(Full(path), arena_, text)) return nullptr;
        MdDoc* doc = static_cast<MdDoc*>(arena_.Alloc(sizeof(MdDoc), alignof(MdDoc)));
        new (doc) MdDoc(arena_);
        ScanMarkdown(text, arena_, *doc);
        p->doc = doc;
        p->loaded = true;
        return p;
    }

    void Add(StringView path, u32 line, Severity sev, StringView msg) { out_.Push(FileDiag{path, Diagnostic{sev, line, msg}}); }

    void AddCat(StringView path, u32 line, Severity sev, StringView a, StringView b, StringView c = StringView()) {
        Add(path, line, sev, ArenaCat(arena_, a, b, c));
    }

    void CheckPage(Page& page) {
        const MdDoc& doc = *page.doc;
        for (usize i = 0; i < doc.diagnostics.Size(); ++i) Add(page.path, doc.diagnostics[i].line, doc.diagnostics[i].severity, doc.diagnostics[i].message);
        for (usize i = 0; i < doc.links.Size(); ++i) CheckLink(page, doc.links[i]);
        if (index_ != nullptr) {
            for (usize i = 0; i < doc.refs.Size(); ++i) {
                const MdRef& r = doc.refs[i];
                ++stats_.refs;
                const Symbol* hit = nullptr;
                Array<const Symbol*, ArenaAllocator> cands(arena_);
                ResolveStatus st = ResolveRef(*index_, r.target, hit, &cands);
                if (st != ResolveStatus::Ok) Add(page.path, r.line, Severity::Error, RefProblem(arena_, st, r.target, cands));
            }
        }
    }

    // Directory part of a repo-relative path ("docs/guide/jobs.md" -> "docs/guide"), "" at the top.
    static StringView DirOf(StringView path) {
        usize n = path.Length();
        while (n > 0 && path[n - 1] != '/') --n;
        return n == 0 ? StringView() : StringView(path.Data(), n - 1);
    }

    // Resolves `rel` against `dir`; false if it climbs out of the repository.
    bool Normalize(StringView dir, StringView rel, StringView& out) {
        Array<StringView, ArenaAllocator> segs(arena_);
        auto push_path = [&](StringView p) -> bool {
            usize i = 0;
            while (i <= p.Length()) {
                usize e = i;
                while (e < p.Length() && p[e] != '/') ++e;
                StringView seg(p.Data() + i, e - i);
                if (seg == StringView("..")) {
                    if (segs.Size() == 0) return false;
                    segs.Pop();
                } else if (!seg.Empty() && seg != StringView(".")) {
                    segs.Push(seg);
                }
                i = e + 1;
            }
            return true;
        };
        if (!rel.Empty() && rel[0] == '/') {
            if (!push_path(rel)) return false;
        } else {
            if (!push_path(dir) || !push_path(rel)) return false;
        }
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena_);
        for (usize i = 0; i < segs.Size(); ++i) {
            if (i > 0) sb.Append('/');
            sb.Append(segs[i]);
        }
        out = ArenaCat(arena_, sb.View(), StringView());
        return true;
    }

    StringView Decode(StringView s) {
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena_);
        for (usize i = 0; i < s.Length(); ++i) {
            // "%00" stays literal: a NUL would silently truncate the path handed to stat()
            if (s[i] == '%' && i + 2 < s.Length() && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0 &&
                HexVal(s[i + 1]) * 16 + HexVal(s[i + 2]) != 0) {
                sb.Append(static_cast<char>(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2])));
                i += 2;
            } else {
                sb.Append(s[i]);
            }
        }
        return ArenaCat(arena_, sb.View(), StringView());
    }

    void CheckLink(Page& page, const MdLink& link) {
        ++stats_.links;
        StringView t = link.target;
        if (t.Empty()) {
            Add(page.path, link.line, Severity::Error, StringView("empty link target"));
            return;
        }
        if (HasScheme(t)) {
            ++stats_.external;
            if (opt_.list_external) AddCat(page.path, link.line, Severity::Note, StringView("external link not checked: "), t);
            return;
        }
        usize hash = t.Find(StringView("#"));
        StringView path = StringView(t.Data(), hash);
        StringView frag = hash < t.Length() ? StringView(t.Data() + hash + 1, t.Length() - hash - 1) : StringView();
        usize q = path.IndexOf('?');
        path = StringView(path.Data(), q);
        path = Decode(path);
        frag = Decode(frag);

        const char* what = link.image ? "missing image: " : "broken link: ";
        if (path.Empty()) { // same-file anchor
            if (!frag.Empty() && !page.doc->HasAnchor(frag)) {
                AddCat(page.path, link.line, Severity::Error, StringView("broken anchor: no heading '#"), frag, StringView("' in this file"));
            }
            return;
        }
        StringView target;
        if (!Normalize(DirOf(page.path), path, target)) {
            AddCat(page.path, link.line, Severity::Error, StringView("link escapes the repository: "), path);
            return;
        }
        struct stat st;
        if (stat(Full(target.Empty() ? StringView(".") : target).Data(), &st) != 0) {
            AddCat(page.path, link.line, Severity::Error, StringView(what), path, StringView(" does not exist"));
            return;
        }
        if (!S_ISREG(st.st_mode)) return; // a directory link is fine
        if (!EndsWith(target, ".md")) return;
        Page* other = Get(target);
        if (other == nullptr) {
            AddCat(page.path, link.line, Severity::Error, StringView("cannot read "), target);
            return;
        }
        if (page.in_set) Mark(page, *other);
        if (!frag.Empty() && !other->doc->HasAnchor(frag)) {
            AddCat(page.path, link.line, Severity::Error, StringView("broken anchor: no heading '#"), frag, ArenaCat(arena_, StringView("' in "), target));
        }
    }

    // Reachability: edges are recorded as they are found; the walk happens in Orphans().
    void Mark(Page& from, Page& to) {
        Edge e{&from, &to};
        edges_.Push(e);
    }

    void Orphans() {
        Page* home = nullptr;
        for (usize i = 0; i < pages_.Size(); ++i) {
            if (pages_[i]->path == StringView("docs/README.md")) home = pages_[i];
        }
        if (home == nullptr) {
            // Without the root page every docs page is unreachable; say so once instead of passing silently.
            for (usize i = 0; i < pages_.Size(); ++i) {
                if (pages_[i]->in_set && StartsWith(pages_[i]->path, "docs/")) {
                    Add(StringView("docs/README.md"), 1, Severity::Error, StringView("missing: the root page every docs page must be reachable from"));
                    return;
                }
            }
            return;
        }
        home->reached = true;
        for (bool changed = true; changed;) {
            changed = false;
            for (usize i = 0; i < edges_.Size(); ++i) {
                if (edges_[i].from->reached && !edges_[i].to->reached) {
                    edges_[i].to->reached = true;
                    changed = true;
                }
            }
        }
        for (usize i = 0; i < pages_.Size(); ++i) {
            const Page& p = *pages_[i];
            if (p.in_set && !p.reached && StartsWith(p.path, "docs/")) {
                Add(p.path, 1, Severity::Error, StringView("orphan page: not reachable from docs/README.md"));
            }
        }
    }

    StringView root_;
    const SymbolIndex* index_;
    ArenaAllocator& arena_;
    const LinkOptions& opt_;
    Array<FileDiag, ArenaAllocator>& out_;
    LinkStats& stats_;
    Array<Page*, ArenaAllocator> pages_;
    Array<Edge, ArenaAllocator> edges_{arena_};
};

} // namespace

bool CollectMarkdownFiles(StringView root, ArenaAllocator& arena, Array<StringView, ArenaAllocator>& out) {
    Array<StringView, ArenaAllocator> files(arena);
    if (!CollectFiles(root, StringView("docs"), arena, files)) return false;
    // top-level *.md
    DIR* d = opendir(root.Empty() ? "." : ArenaCat(arena, root, StringView()).Data());
    if (d == nullptr) return false;
    while (const dirent* e = readdir(d)) {
        StringView name(e->d_name, std::strlen(e->d_name));
        if (name.Empty() || name[0] == '.' || !EndsWith(name, ".md")) continue;
        StringView path = ArenaCat(arena, name, StringView());
        struct stat st;
        StringView full = root.Empty() ? path : ArenaCat(arena, root, StringView("/"), path);
        if (stat(full.Data(), &st) == 0 && S_ISREG(st.st_mode)) files.Push(path);
    }
    closedir(d);
    StableSort(files, [](StringView a, StringView b) { return a.Compare(b) < 0; });
    for (usize i = 0; i < files.Size(); ++i) {
        if (EndsWith(files[i], ".md")) out.Push(files[i]);
    }
    return true;
}

bool CheckLinks(StringView root, const SymbolIndex* index, ArenaAllocator& arena, const LinkOptions& options,
                Array<FileDiag, ArenaAllocator>& out, LinkStats& stats) {
    Checker c(root, index, arena, options, out, stats);
    return c.Run();
}

} // namespace docgen
