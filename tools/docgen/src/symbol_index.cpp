#include "symbol_index.h"

#include <sys/stat.h>

#include <cstring>
#include <new>

#include "source_file.h"

namespace docgen {
namespace {

const char kStripPrefix[] = "engine_placeholder::";

using ::docgen::ArenaCat;

StringView StripOuter(StringView q) {
    const usize n = sizeof(kStripPrefix) - 1;
    if (q.Length() >= n && std::memcmp(q.Data(), kStripPrefix, n) == 0) return StringView(q.Data() + n, q.Length() - n);
    return q;
}

StringView NoSpaces(StringView s, ArenaAllocator& arena) {
    char* buf = static_cast<char*>(arena.Alloc(s.Length() + 1, 1));
    ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
    usize n = 0;
    for (usize i = 0; i < s.Length(); ++i) {
        if (s[i] != ' ' && s[i] != '\t') buf[n++] = s[i];
    }
    buf[n] = '\0';
    return StringView(buf, n);
}

// Words for the punctuation that distinguishes operators, destructors, pointers and
// references, so that IDs differing only in those characters keep different anchors.
const char* PunctWord(char c) {
    switch (c) {
    case '~': return "tilde";
    case '=': return "eq";
    case '!': return "not";
    case '<': return "lt";
    case '>': return "gt";
    case '+': return "plus";
    case '-': return "minus";
    case '*': return "star";
    case '/': return "slash";
    case '%': return "pct";
    case '&': return "amp";
    case '|': return "pipe";
    case '^': return "caret";
    default: return nullptr;
    }
}

StringView MakeAnchor(StringView id, ArenaAllocator& arena) {
    StringBuilder<ArenaAllocator> sb(arena);
    bool pending = false; // a separator is owed before the next alphanumeric run
    bool any = false;
    auto token = [&](const char* w, usize n) { // a whole word, set apart from its neighbours
        if (any) sb.Append('-');
        sb.Append(StringView(w, n));
        any = true;
        pending = true;
    };
    auto is_operator_before = [&](usize i) { return i >= 8 && std::memcmp(id.Data() + i - 8, "operator", 8) == 0; };
    for (usize i = 0; i < id.Length(); ++i) {
        char c = id[i];
        if (c == '(' && i + 1 < id.Length() && id[i + 1] == ')' && is_operator_before(i)) { // operator()
            token("call", 4);
            ++i;
            continue;
        }
        if (c == '[' && i + 1 < id.Length() && id[i + 1] == ']' && is_operator_before(i)) { // operator[]
            token("index", 5);
            ++i;
            continue;
        }
        if (const char* w = PunctWord(c)) {
            token(w, std::strlen(w));
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (pending && any) sb.Append('-');
            pending = false;
            sb.Append(c);
            any = true;
        } else {
            pending = true;
        }
    }
    if (!any) return StringView("s");
    return sb.View();
}

void SortDiags(Vector<FileDiag, ArenaAllocator>& d) {
    StableSort(d, [](const FileDiag& x, const FileDiag& y) {
        const int c = x.path.Compare(y.path);
        return c < 0 || (c == 0 && x.d.line < y.d.line);
    });
}

class Builder {
public:
    Builder(StringView root, StringView map_path, const ModuleMap& map, ArenaAllocator& arena, SymbolIndex& out)
        : root_(root), map_path_(map_path), map_(map), arena_(arena), out_(out) {}

    void Run() {
        out_.map = &map_;
        for (usize m = 0; m < map_.modules.Size(); ++m) out_.page_names.Push(PageNameFor(map_.modules[m]->name, arena_));
        for (usize i = 0; i < map_.diagnostics.Size(); ++i) out_.diagnostics.Push(FileDiag{map_path_, map_.diagnostics[i]});

        // 1. headers per module
        Vector<StringView, ArenaAllocator> all(arena_);
        for (usize m = 0; m < map_.modules.Size(); ++m) {
            const ModuleDef& mod = *map_.modules[m];
            for (usize h = 0; h < mod.headers.Size(); ++h) {
                StringView prefix = GlobPrefix(mod.headers[h]);
                if (!CollectFiles(root_, prefix, arena_, all)) {
                    // A missing prefix just means the pattern matches nothing (reported below);
                    // a directory that exists but cannot be fully read would silently drop headers.
                    struct stat st;
                    StringView dir = prefix.Empty() ? ArenaCat(arena_, root_, StringView()) : ArenaCat(arena_, root_, StringView("/"), prefix);
                    if (stat(dir.Data(), &st) == 0 && S_ISDIR(st.st_mode)) {
                        out_.diagnostics.Push(FileDiag{map_path_, Diagnostic{Severity::Error, mod.line,
                                                                             ArenaCat(arena_, StringView("cannot read all of directory "), prefix.Empty() ? StringView(".") : prefix)}});
                    }
                }
            }
        }
        Vector<StringView, ArenaAllocator> assigned(arena_); // path per unit, to detect two-module headers
        for (usize m = 0; m < map_.modules.Size(); ++m) {
            const ModuleDef& mod = *map_.modules[m];
            for (usize h = 0; h < mod.headers.Size(); ++h) {
                bool any = false;
                for (usize f = 0; f < all.Size() && !any; ++f) any = GlobMatch(mod.headers[h], all[f]);
                if (!any) {
                    out_.diagnostics.Push(FileDiag{map_path_, Diagnostic{Severity::Error, mod.line,
                                                                         ArenaCat(arena_, StringView("header pattern matches no files: "), mod.headers[h])}});
                }
            }
            Vector<StringView, ArenaAllocator> files(arena_);
            ExpandModule(mod, all, arena_, files);
            for (usize f = 0; f < files.Size(); ++f) {
                bool dup = false;
                for (usize a = 0; a < assigned.Size(); ++a) {
                    if (assigned[a] == files[f]) dup = true;
                }
                if (dup) {
                    out_.diagnostics.Push(FileDiag{map_path_, Diagnostic{Severity::Error, mod.line,
                                                                         ArenaCat(arena_, StringView("header listed in two modules: "), files[f])}});
                    continue;
                }
                assigned.Push(files[f]);
                LoadUnit(m, files[f]);
            }
        }

        // 2. symbols
        for (usize u = 0; u < out_.units.Size(); ++u) {
            SourceUnit& unit = *out_.units[u];
            VisitUnit(unit);
        }
        AssignAnchors();
        ResolveInheritance();
        CheckRefs();
        SortDiags(out_.diagnostics);
    }

private:
    void AddDiags(StringView path, const Vector<Diagnostic, ArenaAllocator>& ds) {
        for (usize i = 0; i < ds.Size(); ++i) out_.diagnostics.Push(FileDiag{path, ds[i]});
    }

    void LoadUnit(usize module, StringView rel) {
        void* mem = arena_.Alloc(sizeof(SourceUnit), alignof(SourceUnit));
        ENGINE_ASSERT(mem != nullptr, "docgen: arena exhausted");
        SourceUnit* u = new (mem) SourceUnit(arena_);
        u->path = rel;
        u->module = module;
        out_.units.Push(u);
        StringView abs = ArenaCat(arena_, root_, StringView("/"), rel);
        if (!LoadSourceFile(abs, arena_, u->source)) {
            out_.diagnostics.Push(FileDiag{rel, Diagnostic{Severity::Error, 1, StringView("cannot read file")}});
            u->source = StringView();
        }
        Scan(u->source, arena_, u->scan);
        Extract(u->scan, arena_, u->ex);
        ParseDocs(u->scan, u->ex, arena_, u->docs);
        AddDiags(rel, u->scan.diagnostics);
        AddDiags(rel, u->ex.diagnostics);
        AddDiags(rel, u->docs.diagnostics);
    }

    const ParsedDoc* DocOf(const SourceUnit& unit, const Decl& d) const {
        if (d.doc >= 0) return unit.docs.parsed[static_cast<usize>(d.doc)];
        if (d.trailing_doc >= 0) return unit.docs.parsed[static_cast<usize>(d.trailing_doc)];
        return nullptr;
    }

    StringView MakeId(const Decl& d, StringView& types, StringView& qualified) {
        qualified = StripOuter(d.qualified);
        types = StringView();
        if (d.kind != DeclKind::Function) return qualified;
        types = d.param_types;
        StringView tail = d.Is(kFlagConst) ? (d.Is(kFlagNoexcept) ? StringView(" const noexcept") : StringView(" const"))
                                           : (d.Is(kFlagNoexcept) ? StringView(" noexcept") : StringView());
        return ArenaCat(arena_, qualified, StringView("("), types, ArenaCat(arena_, StringView(")"), tail));
    }

    // Namespace-scope declarations of a unit in source order (macros live at the
    // root of the tree, so the tree order alone is not source order).
    void CollectTop(const Decl& scope, Vector<const Decl*, ArenaAllocator>& out) {
        for (usize i = 0; i < scope.children.Size(); ++i) {
            const Decl& d = *scope.children[i];
            if (d.hidden) continue;
            if (d.kind == DeclKind::Namespace) {
                CollectTop(d, out);
            } else {
                out.Push(&d);
            }
        }
    }

    void VisitUnit(SourceUnit& unit) {
        Vector<const Decl*, ArenaAllocator> tops(arena_);
        CollectTop(*unit.ex.root, tops);
        for (usize i = 1; i < tops.Size(); ++i) { // stable insertion sort by line
            const Decl* x = tops[i];
            usize j = i;
            while (j > 0 && tops[j - 1]->line > x->line) {
                tops[j] = tops[j - 1];
                --j;
            }
            tops[j] = x;
        }
        for (usize i = 0; i < tops.Size(); ++i) MakeSymbol(unit, *tops[i], nullptr);
    }

    void MakeSymbol(SourceUnit& unit, const Decl& d, Symbol* parent) {
        const ParsedDoc* doc = DocOf(unit, d);
        if (doc != nullptr && doc->internal) return;
        if (d.kind == DeclKind::Function) {
            // A function declared and then defined inline in the same header is one
            // symbol: the first declaration stays (location, signature), but the
            // documented one supplies the comment. Anything else with a repeated ID
            // is reported as a duplicate in AssignAnchors().
            StringView types, qualified;
            StringView id = MakeId(d, types, qualified);
            Vector<Symbol*, ArenaAllocator>& siblings = parent == nullptr ? out_.top : parent->members;
            for (usize i = 0; i < siblings.Size(); ++i) {
                Symbol* e = siblings[i];
                if (e->unit != &unit || !(e->id == id) || e->decl->kind != DeclKind::Function) continue;
                if (e->doc == nullptr && doc != nullptr) e->doc = doc;
                return;
            }
        }
        void* mem = arena_.Alloc(sizeof(Symbol), alignof(Symbol));
        ENGINE_ASSERT(mem != nullptr, "docgen: arena exhausted");
        Symbol* s = new (mem) Symbol(arena_);
        s->decl = &d;
        s->unit = &unit;
        s->module = unit.module;
        s->doc = doc;
        s->id = MakeId(d, s->types, s->qualified);
        s->types_key = NoSpaces(s->types, arena_);
        out_.symbols.Push(s);
        if (parent == nullptr) {
            out_.top.Push(s);
        } else {
            parent->members.Push(s);
        }
        if (d.kind == DeclKind::Class || d.kind == DeclKind::Enum) {
            for (usize i = 0; i < d.children.Size(); ++i) {
                if (!d.children[i]->hidden) MakeSymbol(unit, *d.children[i], s);
            }
        }
    }

    void AssignAnchors() {
        for (usize i = 0; i < out_.symbols.Size(); ++i) {
            Symbol& s = *out_.symbols[i];
            StringView base = MakeAnchor(s.id, arena_);
            StringView cand = base;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (usize j = 0; j < i && !taken; ++j) taken = out_.symbols[j]->anchor == cand;
                if (!taken) break;
                char num[16];
                int len = 0;
                for (int v = n; v > 0; v /= 10) num[len++] = static_cast<char>('0' + v % 10);
                char rev[16];
                for (int k = 0; k < len; ++k) rev[k] = num[len - 1 - k];
                cand = ArenaCat(arena_, base, StringView("-"), StringView(rev, static_cast<usize>(len)));
            }
            s.anchor = cand;
            // duplicate IDs are errors (the anchor suffix above only keeps output well-formed)
            for (usize j = 0; j < i; ++j) {
                if (out_.symbols[j]->id == s.id) {
                    out_.diagnostics.Push(FileDiag{s.unit->path, Diagnostic{Severity::Error, s.decl->line,
                                                                            ArenaCat(arena_, StringView("duplicate symbol ID '"), s.id, StringView("'"))}});
                    break;
                }
            }
        }
    }

    void ResolveInheritance() {
        Vector<const Decl*, ArenaAllocator> roots(arena_);
        for (usize u = 0; u < out_.units.Size(); ++u) roots.Push(out_.units[u]->ex.root);
        for (usize i = 0; i < out_.symbols.Size(); ++i) {
            Symbol& s = *out_.symbols[i];
            if (s.doc != nullptr || !s.decl->Is(kFlagOverride)) continue;
            const Decl* base = ResolveInherited(*s.decl, roots.Size() ? &roots[0] : nullptr, roots.Size());
            if (base == nullptr) continue;
            for (usize j = 0; j < out_.symbols.Size(); ++j) {
                if (out_.symbols[j]->decl == base) {
                    s.inherited = out_.symbols[j];
                    break;
                }
            }
        }
    }

    // Every [[reference]] in every symbol's comment must resolve.
    void CheckRefs() {
        for (usize i = 0; i < out_.symbols.Size(); ++i) {
            const Symbol& s = *out_.symbols[i];
            if (s.doc == nullptr) continue;
            for (usize r = 0; r < s.doc->refs.Size(); ++r) {
                const Ref& ref = s.doc->refs[r];
                const Symbol* hit = nullptr;
                Vector<const Symbol*, ArenaAllocator> cands(arena_);
                ResolveStatus st = ResolveRef(out_, ref.target, hit, &cands);
                if (st == ResolveStatus::Ok) continue;
                out_.diagnostics.Push(FileDiag{s.unit->path, Diagnostic{Severity::Error, ref.line, RefProblem(arena_, st, ref.target, cands)}});
            }
        }
    }

    StringView root_;
    StringView map_path_;
    const ModuleMap& map_;
    ArenaAllocator& arena_;
    SymbolIndex& out_;
};

// `key` has no spaces; `text` may.
bool EqualsIgnoringSpaces(StringView key, StringView text) {
    usize k = 0;
    for (usize i = 0; i < text.Length(); ++i) {
        if (text[i] == ' ' || text[i] == '\t') continue;
        if (k >= key.Length() || key[k] != text[i]) return false;
        ++k;
    }
    return k == key.Length();
}

usize Components(StringView q) {
    usize n = 1;
    for (usize i = 0; i + 1 < q.Length(); ++i) {
        if (q[i] == ':' && q[i + 1] == ':') {
            ++n;
            ++i;
        }
    }
    return n;
}

bool EndsWithScoped(StringView q, StringView base) {
    if (q.Length() < base.Length() + 2) return false;
    const usize off = q.Length() - base.Length();
    return std::memcmp(q.Data() + off, base.Data(), base.Length()) == 0 && q[off - 1] == ':' && q[off - 2] == ':';
}

} // namespace

StringView PageNameFor(StringView module_name, ArenaAllocator& arena) {
    char* buf = static_cast<char*>(arena.Alloc(module_name.Length() + 4, 1));
    ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
    for (usize i = 0; i < module_name.Length(); ++i) buf[i] = module_name[i] == '/' ? '-' : module_name[i];
    CopyBytes(buf + module_name.Length(), ".md", 4);
    return StringView(buf, module_name.Length() + 3);
}

const char* KindName(const Decl& d) {
    switch (d.kind) {
    case DeclKind::Namespace: return "namespace";
    case DeclKind::Class: return d.head_keyword.Empty() ? "class" : (d.head_keyword == StringView("struct") ? "struct" : (d.head_keyword == StringView("union") ? "union" : "class"));
    case DeclKind::Enum: return "enum";
    case DeclKind::Enumerator: return "enumerator";
    case DeclKind::Function: return "function";
    case DeclKind::Variable: return "variable";
    case DeclKind::Alias: return "alias";
    case DeclKind::Macro: return "macro";
    }
    return "?";
}

void BuildIndex(StringView root, StringView map_path, const ModuleMap& map, ArenaAllocator& arena, SymbolIndex& out) {
    Builder b(root, map_path, map, arena, out);
    b.Run();
}

StringView RefProblem(ArenaAllocator& arena, ResolveStatus st, StringView target, const Vector<const Symbol*, ArenaAllocator>& cands) {
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    sb.Append(StringView(st == ResolveStatus::NotFound ? "unresolved reference '" : "ambiguous reference '"));
    sb.Append(target);
    sb.Append('\'');
    if (st == ResolveStatus::Ambiguous) {
        sb.Append(StringView(" (candidates: "));
        for (usize i = 0; i < cands.Size(); ++i) {
            if (i > 0) sb.Append(StringView(", "));
            sb.Append(cands[i]->id);
        }
        sb.Append(')');
    }
    return sb.View(); // the builder's storage lives in `arena`
}

namespace {
void DropAll(Vector<const Symbol*, ArenaAllocator>* c) {
    if (c == nullptr) return;
    while (c->Size() > 0) c->Pop();
}
} // namespace

ResolveStatus ResolveRef(const SymbolIndex& index, StringView target, const Symbol*& out, Vector<const Symbol*, ArenaAllocator>* candidates) {
    out = nullptr;
    DropAll(candidates);
    // trim
    usize a = 0, e = target.Length();
    while (a < e && (target[a] == ' ' || target[a] == '\t')) ++a;
    while (e > a && (target[e - 1] == ' ' || target[e - 1] == '\t')) --e;
    StringView t = StripOuter(StringView(target.Data() + a, e - a));

    StringView base = t;
    bool has_hint = false;
    StringView hint_text;
    for (usize i = 0; i < t.Length(); ++i) {
        if (t[i] != '(') continue;
        // `operator()` / `operator[]` carry their own parentheses: skip over them
        if (i >= 8 && std::memcmp(t.Data() + i - 8, "operator", 8) == 0 && i + 1 < t.Length() && t[i + 1] == ')') {
            ++i;
            continue;
        }
        usize close = t.Length();
        while (close > i && t[close - 1] != ')') --close;
        base = StringView(t.Data(), i);
        while (base.Length() > 0 && base[base.Length() - 1] == ' ') base = StringView(base.Data(), base.Length() - 1);
        if (close >= i + 2) { // there is a closing ')' after the opening one
            has_hint = true;
            hint_text = StringView(t.Data() + i + 1, close - i - 2);
        }
        break;
    }
    if (base.Empty()) return ResolveStatus::NotFound;

    // exact full-name matches win; otherwise the shortest partially qualified matches
    const Symbol* best = nullptr;
    usize best_count = 0;
    bool exact = false;
    usize best_comp = ~usize{0};
    for (usize i = 0; i < index.symbols.Size(); ++i) {
        const Symbol& s = *index.symbols[i];
        const bool is_exact = s.qualified == base;
        if (!is_exact && !EndsWithScoped(s.qualified, base)) continue;
        if (has_hint && (s.decl->kind != DeclKind::Function || !EqualsIgnoringSpaces(s.types_key, hint_text))) continue;
        const usize comp = Components(s.qualified);
        if (is_exact && !exact) {
            exact = true;
            best = nullptr;
            best_count = 0;
            DropAll(candidates);
        }
        if (exact && !is_exact) continue;
        if (!exact) {
            if (comp > best_comp) continue;
            if (comp < best_comp) {
                best_comp = comp;
                best = nullptr;
                best_count = 0;
                DropAll(candidates);
            }
        }
        if (best == nullptr) best = &s;
        ++best_count;
        if (candidates != nullptr) candidates->Push(&s);
    }
    if (best_count == 0) return ResolveStatus::NotFound;
    if (best_count > 1) return ResolveStatus::Ambiguous;
    DropAll(candidates);
    out = best;
    return ResolveStatus::Ok;
}

} // namespace docgen
