#include "doc_parser.h"

#include <cstring>
#include <new>

namespace docgen {
namespace {

constexpr int kNoLabel = -1;
constexpr int kSwallow = -2; // inside an unknown/duplicate label: its continuation lines are ignored

struct LabelInfo {
    const char* name;
    LabelKind kind;
};

const LabelInfo kLabels[] = {
    {"Param", LabelKind::Param},
    {"Returns", LabelKind::Returns},
    {"Fails", LabelKind::Fails},
    {"Aborts", LabelKind::Aborts},
    {"Thread-safety", LabelKind::ThreadSafety},
    {"Ownership", LabelKind::Ownership},
    {"Example", LabelKind::Example},
    {"See", LabelKind::See},
    {"Deprecated", LabelKind::Deprecated},
    {"Internal", LabelKind::Internal},
};

const char* const kCategories[] = {"immutable",      "thread-safe",  "thread-compatible",
                                   "owner-thread-only", "single-consumer", "not-thread-safe"};

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

StringView Trim(StringView s) {
    usize a = 0, b = s.Length();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return StringView(s.Data() + a, b - a);
}

bool StartsWith(StringView s, const char* p) {
    usize n = std::strlen(p);
    if (s.Length() < n) return false;
    for (usize i = 0; i < n; ++i) {
        if (s[i] != p[i]) return false;
    }
    return true;
}

bool IsIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsIdentChar(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }

struct LabelMatch {
    bool matched = false;
    bool known = false;
    LabelKind kind = LabelKind::Returns;
    StringView word;
    StringView name; // Param
    StringView rest; // text after the colon, trimmed
};

// A label starts at column 0: a capitalized word (letters and hyphens)
// directly followed by ':' and a space or end of line. `Param` is followed
// by a parameter name before the colon.
bool MatchLabel(StringView t, LabelMatch& m) {
    if (t.Empty() || !(t[0] >= 'A' && t[0] <= 'Z')) return false;
    usize i = 1;
    while (i < t.Length() && ((t[i] >= 'a' && t[i] <= 'z') || (t[i] >= 'A' && t[i] <= 'Z') || t[i] == '-')) ++i;
    m.word = StringView(t.Data(), i);
    const bool is_param = m.word == StringView("Param");
    if (is_param && i < t.Length() && t[i] == ' ') {
        usize j = i;
        while (j < t.Length() && t[j] == ' ') ++j;
        usize k = j;
        if (k < t.Length() && IsIdentStart(t[k])) {
            while (k < t.Length() && IsIdentChar(t[k])) ++k;
            if (k < t.Length() && t[k] == ':' && (k + 1 == t.Length() || t[k + 1] == ' ')) {
                m.matched = true;
                m.known = true;
                m.kind = LabelKind::Param;
                m.name = StringView(t.Data() + j, k - j);
                m.rest = Trim(StringView(t.Data() + k + 1, t.Length() - k - 1));
                return true;
            }
        }
        return false;
    }
    if (i >= t.Length() || t[i] != ':') return false;
    if (i + 1 < t.Length() && t[i + 1] != ' ') return false; // `Foo::Bar`
    m.matched = true;
    m.rest = Trim(StringView(t.Data() + i + 1, t.Length() - i - 1));
    for (const LabelInfo& l : kLabels) {
        if (m.word == StringView(l.name)) {
            m.known = true;
            m.kind = l.kind;
            break;
        }
    }
    return true;
}

class DocParser {
public:
    DocParser(ArenaAllocator& arena, Vector<Diagnostic, ArenaAllocator>& diags) : arena_(arena), diags_(diags), pieces_(arena) {}

    ParsedDoc* Parse(const DocComment& dc) {
        void* mem = arena_.Alloc(sizeof(ParsedDoc), alignof(ParsedDoc));
        ENGINE_ASSERT(mem != nullptr, "docgen: arena exhausted");
        doc_ = new (mem) ParsedDoc(arena_);
        first_line_ = dc.first_line;
        Reset();

        const StringView text = dc.text;
        usize pos = 0;
        u32 no = dc.first_line;
        for (;;) {
            usize e = pos;
            while (e < text.Length() && text[e] != '\n') ++e;
            ProcessLine(StringView(text.Data() + pos, e - pos), no);
            if (e >= text.Length()) break;
            pos = e + 1;
            ++no;
        }
        Finish();
        return doc_;
    }

private:
    void Reset() {
        in_para_ = in_list_ = item_open_ = in_fence_ = false;
        content_seen_ = false;
        label_text_open_ = false;
        cur_label_ = kNoLabel;
    }

    // ---- diagnostics ----------------------------------------------------

    void Diag(Severity sev, u32 line, StringView msg) { diags_.Push(Diagnostic{sev, line, msg}); }
    void Diag(Severity sev, u32 line, const char* msg) { Diag(sev, line, StringView(msg)); }

    StringView Msg(const char* prefix, StringView detail) {
        const usize pl = std::strlen(prefix);
        const usize len = pl + 2 + detail.Length() + 1;
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
        CopyBytes(buf, prefix, pl);
        buf[pl] = ' ';
        buf[pl + 1] = '\'';
        CopyBytes(buf + pl + 2, detail.Data(), detail.Length());
        buf[pl + 2 + detail.Length()] = '\'';
        buf[len] = '\0';
        return StringView(buf, len);
    }

    // ---- text helpers ---------------------------------------------------

    StringView JoinPieces(usize from, usize to, char sep) {
        usize len = 0;
        for (usize i = from; i < to; ++i) len += pieces_[i].Length() + (i > from ? 1 : 0);
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
        usize at = 0;
        for (usize i = from; i < to; ++i) {
            if (i > from) buf[at++] = sep;
            CopyBytes(buf + at, pieces_[i].Data(), pieces_[i].Length());
            at += pieces_[i].Length();
        }
        buf[len] = '\0';
        return StringView(buf, len);
    }

    // Records every [[reference]] in the joined text of pieces_[from, to) (outside
    // `code spans`), so a reference may wrap across comment lines; returns how many.
    // Piece k sits on source line first_line + k (blocks never span a blank line).
    u32 ScanRefs(usize from, usize to, u32 first_line) {
        if (to <= from) return 0;
        const StringView t = JoinPieces(from, to, ' ');
        auto line_at = [&](usize off) {
            usize k = from;
            usize end = pieces_[k].Length();
            while (off > end && k + 1 < to) {
                ++k;
                end += 1 + pieces_[k].Length();
            }
            return first_line + static_cast<u32>(k - from);
        };
        u32 count = 0;
        for (usize i = 0; i < t.Length(); ++i) {
            if (t[i] == '`') {
                usize j = i + 1;
                while (j < t.Length() && t[j] != '`') ++j;
                if (j < t.Length()) i = j;
                continue;
            }
            if (t[i] == '[' && i + 1 < t.Length() && t[i + 1] == '[') {
                usize j = i + 2;
                while (j + 1 < t.Length() && !(t[j] == ']' && t[j + 1] == ']')) ++j;
                if (j + 1 >= t.Length()) {
                    Diag(Severity::Error, line_at(i), "malformed reference: missing ]]");
                    break;
                }
                StringView target = Trim(StringView(t.Data() + i + 2, j - i - 2));
                if (target.Empty()) {
                    Diag(Severity::Error, line_at(i), "malformed reference: empty [[]]");
                } else {
                    doc_->refs.Push(Ref{target, line_at(i)});
                    ++count;
                }
                i = j + 1;
            }
        }
        return count;
    }

    // ---- block state machine --------------------------------------------

    void ProcessLine(StringView raw, u32 no) {
        if (in_fence_) {
            if (StartsWith(Trim(raw), "```")) {
                EndFence();
            } else {
                pieces_.Push(raw);
            }
            return;
        }
        StringView t = Trim(raw);
        if (t.Empty()) {
            FlushPara();
            FlushList();
            EndLabel();
            return;
        }
        const bool indented = IsSpace(raw[0]);

        if (StartsWith(t, "```")) {
            FlushPara();
            FlushList();
            const bool attach = cur_label_ >= 0 && doc_->labels[static_cast<usize>(cur_label_)].kind == LabelKind::Example &&
                                !doc_->labels[static_cast<usize>(cur_label_)].has_code;
            FinishLabelText();
            if (!attach) cur_label_ = kNoLabel;
            StartFence(t, no, attach);
            return;
        }

        if (!indented) {
            LabelMatch m;
            if (MatchLabel(t, m)) {
                FlushPara();
                FlushList();
                EndLabel();
                StartLabel(m, no);
                return;
            }
        }

        if (indented) {
            if (cur_label_ == kSwallow) return;
            if (cur_label_ >= 0 && label_text_open_) {
                pieces_.Push(t);
                return;
            }
            if (in_list_ && item_open_) {
                pieces_.Push(t);
                return;
            }
            if (!in_para_) {
                EndLabel();
                FlushList();
            }
            AddParaLine(t, no);
            return;
        }

        if (t[0] == '-' && (t.Length() == 1 || t[1] == ' ')) {
            FlushPara();
            EndLabel();
            StartItem(t, no);
            return;
        }
        EndLabel();
        FlushList();
        AddParaLine(t, no);
    }

    void AddParaLine(StringView t, u32 no) {
        if (!in_para_) {
            in_para_ = true;
            para_line_ = no;
            para_start_ = pieces_.Size();
        }
        pieces_.Push(t);
    }

    void FlushPara() {
        if (!in_para_) return;
        in_para_ = false;
        ScanRefs(para_start_, pieces_.Size(), para_line_);
        StringView text = JoinPieces(para_start_, pieces_.Size(), ' ');
        if (!content_seen_) {
            doc_->summary = text;
            doc_->has_summary = true;
            doc_->summary_line = para_line_;
        } else {
            Block b;
            b.kind = BlockKind::Paragraph;
            b.text = text;
            b.line = para_line_;
            doc_->blocks.Push(b);
        }
        content_seen_ = true;
    }

    void StartItem(StringView t, u32 no) {
        if (!in_list_) {
            in_list_ = true;
            list_line_ = no;
            list_first_ = doc_->items.Size();
        } else {
            CloseItem();
        }
        item_open_ = true;
        item_start_ = pieces_.Size();
        item_line_ = no;
        pieces_.Push(Trim(StringView(t.Data() + 1, t.Length() - 1)));
    }

    void CloseItem() {
        if (!item_open_) return;
        item_open_ = false;
        ScanRefs(item_start_, pieces_.Size(), item_line_);
        doc_->items.Push(JoinPieces(item_start_, pieces_.Size(), ' '));
    }

    void FlushList() {
        if (!in_list_) return;
        CloseItem();
        in_list_ = false;
        Block b;
        b.kind = BlockKind::List;
        b.first_item = list_first_;
        b.item_count = doc_->items.Size() - list_first_;
        b.line = list_line_;
        doc_->blocks.Push(b);
        content_seen_ = true;
    }

    void StartLabel(const LabelMatch& m, u32 no) {
        content_seen_ = true;
        if (!m.known) {
            Diag(Severity::Error, no, Msg("unknown label", m.word));
            cur_label_ = kSwallow;
            return;
        }
        for (usize i = 0; i < doc_->labels.Size(); ++i) {
            const Label& l = doc_->labels[i];
            if (l.kind == m.kind && (m.kind != LabelKind::Param || l.name == m.name)) {
                Diag(Severity::Error, no, Msg("duplicate label", m.kind == LabelKind::Param ? m.name : m.word));
                cur_label_ = kSwallow;
                return;
            }
        }
        if (m.kind == LabelKind::Param && m.name.Empty()) {
            Diag(Severity::Error, no, "malformed label: Param needs a parameter name (`Param name: text`)");
            cur_label_ = kSwallow;
            return;
        }
        Label l;
        l.kind = m.kind;
        l.name = m.name;
        l.line = no;
        doc_->labels.Push(l);
        cur_label_ = static_cast<int>(doc_->labels.Size()) - 1;
        label_text_open_ = true;
        label_start_ = pieces_.Size();
        label_first_line_ = m.rest.Empty() ? no + 1 : no;
        if (!m.rest.Empty()) pieces_.Push(m.rest);
        if (m.kind == LabelKind::Internal) doc_->internal = true;
    }

    void FinishLabelText() {
        if (cur_label_ >= 0 && label_text_open_) {
            Label& l = doc_->labels[static_cast<usize>(cur_label_)];
            l.ref_count = ScanRefs(label_start_, pieces_.Size(), label_first_line_);
            l.text = JoinPieces(label_start_, pieces_.Size(), ' ');
        }
        label_text_open_ = false;
    }

    void EndLabel() {
        FinishLabelText();
        cur_label_ = kNoLabel;
    }

    void StartFence(StringView t, u32 no, bool attach) {
        in_fence_ = true;
        fence_line_ = no;
        fence_lang_ = Trim(StringView(t.Data() + 3, t.Length() - 3));
        fence_attach_ = attach;
        code_start_ = pieces_.Size();
        content_seen_ = true;
    }

    void EndFence() {
        in_fence_ = false;
        StringView code = JoinPieces(code_start_, pieces_.Size(), '\n');
        if (fence_attach_) {
            Label& l = doc_->labels[static_cast<usize>(cur_label_)];
            l.code = code;
            l.code_lang = fence_lang_;
            l.has_code = true;
        } else {
            Block b;
            b.kind = BlockKind::Code;
            b.text = code;
            b.lang = fence_lang_;
            b.line = fence_line_;
            doc_->blocks.Push(b);
        }
    }

    void Finish() {
        if (in_fence_) {
            Diag(Severity::Error, fence_line_, "unterminated code fence");
            EndFence();
        }
        FlushPara();
        FlushList();
        EndLabel();

        if (!doc_->has_summary) {
            Diag(Severity::Error, first_line_, "missing summary");
        } else {
            if (doc_->summary.Length() > 160) Diag(Severity::Warning, doc_->summary_line, "summary too long (over 160 characters)");
            char last = doc_->summary[doc_->summary.Length() - 1];
            if (last != '.') Diag(Severity::Warning, doc_->summary_line, "summary does not end in a period");
        }

        for (usize i = 0; i < doc_->labels.Size(); ++i) {
            const Label& l = doc_->labels[i];
            const bool empty = l.text.Empty() && !l.has_code;
            if (empty) {
                Diag(Severity::Error, l.line, Msg("empty label", StringView(LabelName(l.kind))));
                continue;
            }
            if (l.kind == LabelKind::ThreadSafety) {
                usize n = 0;
                while (n < l.text.Length() && ((l.text[n] >= 'a' && l.text[n] <= 'z') || l.text[n] == '-')) ++n;
                StringView word(l.text.Data(), n);
                bool ok = false;
                for (const char* c : kCategories) {
                    if (word == StringView(c)) ok = true;
                }
                if (!ok) Diag(Severity::Error, l.line, Msg("invalid thread-safety category", word.Empty() ? l.text : word));
            }
            if (l.kind == LabelKind::See && l.ref_count == 0) {
                Diag(Severity::Error, l.line, "malformed label: See needs at least one [[reference]]");
            }
        }
    }

    ArenaAllocator& arena_;
    Vector<Diagnostic, ArenaAllocator>& diags_;
    Vector<StringView, ArenaAllocator> pieces_; // text fragments of whatever block is open (never shrinks)
    ParsedDoc* doc_ = nullptr;
    u32 first_line_ = 0;

    bool content_seen_ = false;
    bool in_para_ = false;
    usize para_start_ = 0;
    u32 para_line_ = 0;
    bool in_list_ = false;
    bool item_open_ = false;
    usize item_start_ = 0;
    u32 item_line_ = 0;
    u32 label_first_line_ = 0;
    usize list_first_ = 0;
    u32 list_line_ = 0;
    int cur_label_ = kNoLabel;
    bool label_text_open_ = false;
    usize label_start_ = 0;
    bool in_fence_ = false;
    bool fence_attach_ = false;
    u32 fence_line_ = 0;
    usize code_start_ = 0;
    StringView fence_lang_;
};

// ---- validation against declarations ---------------------------------------

bool IsParam(const Decl& d, StringView name) {
    for (usize i = 0; i < d.params.Size(); ++i) {
        if (d.params[i] == name) return true;
    }
    return false;
}

void Validate(const Decl& d, const ParsedDoc& doc, Vector<Diagnostic, ArenaAllocator>& diags, ArenaAllocator& arena) {
    for (usize i = 0; i < doc.labels.Size(); ++i) {
        const Label& l = doc.labels[i];
        if (l.kind == LabelKind::Param && !IsParam(d, l.name)) {
            const char* prefix = "Param name does not match a parameter:";
            const usize pl = std::strlen(prefix);
            char* buf = static_cast<char*>(arena.Alloc(pl + l.name.Length() + 4, 1));
            ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
            CopyBytes(buf, prefix, pl);
            buf[pl] = ' ';
            buf[pl + 1] = '\'';
            CopyBytes(buf + pl + 2, l.name.Data(), l.name.Length());
            buf[pl + 2 + l.name.Length()] = '\'';
            diags.Push(Diagnostic{Severity::Error, l.line, StringView(buf, pl + 3 + l.name.Length())});
        }
        if (l.kind == LabelKind::Returns && d.kind == DeclKind::Function && d.Is(kFlagReturnsVoid)) {
            diags.Push(Diagnostic{Severity::Warning, l.line, StringView("Returns on a void function")});
        }
    }
}

class Walker {
public:
    Walker(const ScanResult& scan, const ExtractResult& ex, DocResult& out, ArenaAllocator& arena)
        : scan_(scan), ex_(ex), out_(out), arena_(arena) {}

    void Visit(const Decl& d) {
        if (d.doc >= 0) Validate(d, *out_.parsed[static_cast<usize>(d.doc)], out_.diagnostics, arena_);
        if (d.trailing_doc >= 0) Validate(d, *out_.parsed[static_cast<usize>(d.trailing_doc)], out_.diagnostics, arena_);
        if (d.parent != nullptr && d.IsPublic() && NeedsDoc(d) && d.doc < 0 && d.trailing_doc < 0) {
            if (!(d.kind == DeclKind::Function && d.Is(kFlagOverride) && ResolveInherited(d, ex_) != nullptr)) {
                out_.undocumented.Push(&d);
            }
        }
        for (usize i = 0; i < d.children.Size(); ++i) Visit(*d.children[i]);
    }

private:
    static bool NeedsDoc(const Decl& d) { return d.kind != DeclKind::Namespace; }

    const ScanResult& scan_;
    const ExtractResult& ex_;
    DocResult& out_;
    ArenaAllocator& arena_;
};

// ---- inheritance -------------------------------------------------------------

const Decl* FindClass(const Decl* scope, StringView name) {
    for (usize i = 0; i < scope->children.Size(); ++i) {
        const Decl* c = scope->children[i];
        if (c->kind == DeclKind::Class) {
            usize n = 0;
            while (n < c->name.Length() && c->name[n] != '<') ++n;
            if (StringView(c->name.Data(), n) == name) return c;
        }
        if (c->kind == DeclKind::Namespace || c->kind == DeclKind::Class) {
            if (const Decl* r = FindClass(c, name)) return r;
        }
    }
    return nullptr;
}

const Decl* FindClassIn(const Decl* const* roots, usize count, StringView name) {
    for (usize i = 0; i < count; ++i) {
        if (const Decl* r = FindClass(roots[i], name)) return r;
    }
    return nullptr;
}

const Decl* ResolveIn(const Decl& fn, const Decl& cls, const Decl* const* roots, usize count, int depth) {
    if (depth > 16) return nullptr;
    for (usize b = 0; b < cls.bases.Size(); ++b) {
        const Decl* base = FindClassIn(roots, count, cls.bases[b]);
        if (base == nullptr || base == &cls) continue;
        for (usize i = 0; i < base->children.Size(); ++i) {
            const Decl* m = base->children[i];
            if (m->kind != DeclKind::Function || m->name != fn.name || m->param_count != fn.param_count ||
                m->Is(kFlagConst) != fn.Is(kFlagConst)) {
                continue;
            }
            if (m->doc >= 0) return m;
        }
        if (const Decl* up = ResolveIn(fn, *base, roots, count, depth + 1)) return up;
    }
    return nullptr;
}

void SortByLine(Vector<Diagnostic, ArenaAllocator>& d) {
    StableSort(d, [](const Diagnostic& x, const Diagnostic& y) { return x.line < y.line; });
}

} // namespace

const char* LabelName(LabelKind kind) {
    for (const LabelInfo& l : kLabels) {
        if (l.kind == kind) return l.name;
    }
    return "?";
}

const Decl* ResolveInherited(const Decl& fn, const ExtractResult& ex) {
    if (fn.kind != DeclKind::Function || !fn.Is(kFlagOverride) || fn.parent == nullptr || fn.parent->kind != DeclKind::Class ||
        ex.root == nullptr) {
        return nullptr;
    }
    const Decl* roots[] = {ex.root};
    return ResolveIn(fn, *fn.parent, roots, 1, 0);
}

const Decl* ResolveInherited(const Decl& fn, const Decl* const* roots, usize root_count) {
    if (fn.kind != DeclKind::Function || !fn.Is(kFlagOverride) || fn.parent == nullptr || fn.parent->kind != DeclKind::Class) {
        return nullptr;
    }
    return ResolveIn(fn, *fn.parent, roots, root_count, 0);
}

void ParseDocs(const ScanResult& scan, const ExtractResult& ex, ArenaAllocator& arena, DocResult& out) {
    DocParser parser(arena, out.diagnostics);
    for (usize i = 0; i < scan.docs.Size(); ++i) out.parsed.Push(parser.Parse(scan.docs[i]));

    if (ex.root != nullptr) {
        Walker w(scan, ex, out, arena);
        w.Visit(*ex.root);
    }
    if (!ex.HasErrors()) {
        for (usize i = 0; i < scan.docs.Size(); ++i) {
            const DocComment& dc = scan.docs[i];
            if (dc.kind != DocKind::File && dc.token >= 0 && i < ex.doc_claimed.Size() && ex.doc_claimed[i] == 0) {
                out.diagnostics.Push(
                    Diagnostic{Severity::Error, dc.first_line, StringView("doc comment is not attached to a declaration")});
            }
        }
    }
    SortByLine(out.diagnostics);
}

void EmitDoc(const ParsedDoc& doc, StringBuilder<ArenaAllocator>& out) {
    bool first = true;
    auto gap = [&]() {
        if (!first) out.Append('\n');
        first = false;
    };
    if (doc.has_summary) {
        gap();
        out.Append(doc.summary);
        out.Append('\n');
    }
    for (usize i = 0; i < doc.blocks.Size(); ++i) {
        const Block& b = doc.blocks[i];
        gap();
        switch (b.kind) {
        case BlockKind::Paragraph:
            out.Append(b.text);
            out.Append('\n');
            break;
        case BlockKind::List:
            for (usize k = 0; k < b.item_count; ++k) {
                out.Append(StringView("- "));
                out.Append(doc.items[b.first_item + k]);
                out.Append('\n');
            }
            break;
        case BlockKind::Code:
            out.Append(StringView("```"));
            out.Append(b.lang);
            out.Append('\n');
            if (!b.text.Empty()) {
                out.Append(b.text);
                out.Append('\n');
            }
            out.Append(StringView("```\n"));
            break;
        }
    }
    if (doc.labels.Size() > 0) gap();
    for (usize i = 0; i < doc.labels.Size(); ++i) {
        const Label& l = doc.labels[i];
        out.Append(StringView(LabelName(l.kind)));
        if (l.kind == LabelKind::Param) {
            out.Append(' ');
            out.Append(l.name);
        }
        out.Append(':');
        if (!l.text.Empty()) {
            out.Append(' ');
            out.Append(l.text);
        }
        out.Append('\n');
        if (l.has_code) {
            out.Append(StringView("```"));
            out.Append(l.code_lang);
            out.Append('\n');
            if (!l.code.Empty()) {
                out.Append(l.code);
                out.Append('\n');
            }
            out.Append(StringView("```\n"));
        }
    }
}

} // namespace docgen
