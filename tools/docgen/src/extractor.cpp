#include "extractor.h"

#include "cstr/cstr.h"
#include "lang/new.h"

namespace docgen
{
namespace
{

using engine_placeholder::i32;

constexpr usize kNone = ~usize{0};

bool ExtractorIsIdentChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The declaration scope currently being filled in.
struct ScopeState
{
    Decl* decl;
    Access access;
    StringView class_name;  // unqualified name of the enclosing class, empty outside classes
    bool is_class;
};

class Extractor
{
public:
    Extractor(const ScanResult& scan, ArenaAllocator& arena, ExtractResult& out)
        : scan_(scan), arena_(arena), out_(out), toks_(scan.tokens), n_(scan.tokens.Size()), trailing_(arena)
    {
    }

    void Run()
    {
        out_.root = NewRaw();
        out_.root->kind = DeclKind::Namespace;
        for (usize i = 0; i < n_; ++i)
        {
            trailing_.Push(-1);
        }
        for (usize i = 0; i < scan_.docs.Size(); ++i)
        {
            const DocComment& d = scan_.docs[i];
            if (d.kind == DocKind::Trailing && d.token >= 0 && static_cast<usize>(d.token) < n_)
            {
                trailing_[static_cast<usize>(d.token)] = static_cast<i32>(i);
            }
        }
        for (usize i = 0; i < scan_.docs.Size(); ++i)
        {
            out_.doc_claimed.Push(0);
        }
        ScopeState top{out_.root, Access::Public, StringView(), false};
        while (p_ < n_)
        {
            if (Is(p_, "}"))
            {
                ReportUnbalanced(toks_[p_].line);
                ++p_;
                continue;
            }
            usize p0 = p_;
            ParseStatement(top);
            if (p_ == p0)
            {
                ++p_;
            }
        }
        Claim(out_.root);
    }

private:
    // A comment on a statement that is a declaration but adds nothing to the tree
    // (forward declaration, out-of-line definition, friend) is accepted and dropped.
    void ClaimDocOf(usize first)
    {
        if (first < n_ && toks_[first].doc >= 0)
        {
            out_.doc_claimed[static_cast<usize>(toks_[first].doc)] = 1;
        }
    }

    // Marks every doc comment a declaration owns.
    void Claim(const Decl* d)
    {
        if (d->doc >= 0)
        {
            out_.doc_claimed[static_cast<usize>(d->doc)] = 1;
        }
        if (d->trailing_doc >= 0)
        {
            out_.doc_claimed[static_cast<usize>(d->trailing_doc)] = 1;
        }
        for (usize i = 0; i < d->children.Size(); ++i)
        {
            Claim(d->children[i]);
        }
    }

    // ---- token helpers --------------------------------------------------

    bool Is(usize i, const char* s) const
    {
        if (i >= n_)
        {
            return false;
        }
        const Token& t = toks_[i];
        if (t.kind != TokenKind::Punct && t.kind != TokenKind::Identifier && t.kind != TokenKind::Number)
        {
            return false;
        }
        return t.text == StringView(s);
    }

    bool IsIdentTok(usize i) const { return i < n_ && toks_[i].kind == TokenKind::Identifier; }

    bool SameText(usize a, StringView b) const { return a < n_ && toks_[a].text == b; }

    void Error(u32 line, const char* msg) { out_.diagnostics.Push(Diagnostic{Severity::Error, line, StringView(msg)}); }

    void ReportUnbalanced(u32 line)
    {
        if (unbalanced_reported_)
        {
            return;
        }
        unbalanced_reported_ = true;
        Error(line, "unbalanced braces");
    }

    // ---- text helpers ---------------------------------------------------

    // Source text of tokens [from, to) with whitespace runs collapsed to one
    // space (and none where the source had none).
    StringView Join(usize from, usize to)
    {
        // Two passes so the result is allocated once, at its final size.
        char* buf = nullptr;
        usize len = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
                ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
                len = 0;
            }
            const char* prev_end = nullptr;
            for (usize k = from; k < to && k < n_; ++k)
            {
                const Token& t = toks_[k];
                if (t.kind == TokenKind::Define)
                {
                    continue;
                }
                if (prev_end != nullptr && t.text.Data() != prev_end)
                {
                    if (pass == 1)
                    {
                        buf[len] = ' ';
                    }
                    ++len;
                }
                if (pass == 1)
                {
                    CopyBytes(buf + len, t.text.Data(), t.text.Length());
                }
                len += t.text.Length();
                prev_end = t.text.Data() + t.text.Length();
            }
        }
        buf[len] = '\0';
        return StringView(buf, len);
    }

    StringView Cat(StringView a, const char* mid, StringView b)
    {
        const usize mid_len = engine_placeholder::CStrLength(mid);
        const usize len = a.Length() + mid_len + b.Length();
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
        CopyBytes(buf, a.Data(), a.Length());
        CopyBytes(buf + a.Length(), mid, mid_len);
        CopyBytes(buf + a.Length() + mid_len, b.Data(), b.Length());
        buf[len] = '\0';
        return StringView(buf, len);
    }

    StringView Qualify(const Decl* parent, StringView name)
    {
        if (parent == nullptr || parent->qualified.Empty())
        {
            return name;
        }
        return Cat(parent->qualified, "::", name);
    }

    // ---- structure helpers ----------------------------------------------

    // toks_[i] is ( [ or {. Returns the index after the matching closer.
    usize SkipBalanced(usize i)
    {
        const u32 open_line = toks_[i].line;
        int depth = 0;
        for (usize k = i; k < n_; ++k)
        {
            const Token& t = toks_[k];
            if (t.kind != TokenKind::Punct || t.text.Length() != 1)
            {
                continue;
            }
            char c = t.text[0];
            if (c == '(' || c == '[' || c == '{')
            {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}')
            {
                if (--depth == 0)
                {
                    return k + 1;
                }
            }
        }
        ReportUnbalanced(open_line);
        return n_;
    }

    // toks_[i] is '<'. Returns the index after the matching '>', or kNone.
    usize SkipAngles(usize i)
    {
        int depth = 0;
        for (usize k = i; k < n_; ++k)
        {
            if (Is(k, "<"))
            {
                ++depth;
            }
            else if (Is(k, ">"))
            {
                if (--depth == 0)
                {
                    return k + 1;
                }
            }
            else if (Is(k, "(") || Is(k, "[") || Is(k, "{"))
            {
                k = SkipBalanced(k) - 1;
            }
            else if (Is(k, ";") || Is(k, "}"))
            {
                return kNone;
            }
        }
        return kNone;
    }

    // Index of the ';' ending the statement that starts at `from`, skipping
    // bracketed groups; kNone when a '}' or the end of file comes first.
    usize FindStatementEnd(usize from)
    {
        for (usize k = from; k < n_;)
        {
            if (Is(k, ";"))
            {
                return k;
            }
            if (Is(k, "}"))
            {
                return kNone;
            }
            if (Is(k, "(") || Is(k, "[") || Is(k, "{"))
            {
                k = SkipBalanced(k);
                continue;
            }
            ++k;
        }
        return kNone;
    }

    // Recovery: consume the rest of the current statement. Leaves a closing
    // '}' of the enclosing scope unconsumed.
    void SkipStatement()
    {
        while (p_ < n_)
        {
            if (Is(p_, ";"))
            {
                ++p_;
                return;
            }
            if (Is(p_, "}"))
            {
                return;
            }
            if (Is(p_, "{"))
            {
                p_ = SkipBalanced(p_);
                if (Is(p_, ";"))
                {
                    ++p_;
                }
                return;
            }
            if (Is(p_, "(") || Is(p_, "["))
            {
                p_ = SkipBalanced(p_);
                continue;
            }
            ++p_;
        }
    }

    // After a '}' that closed a class/enum body: consume declarators up to
    // and including the ';'.
    void FinishTypeDecl(usize first)
    {
        if (Is(p_, ";"))
        {
            ++p_;
            return;
        }
        if (p_ < n_ && !Is(p_, "}"))
        {
            Error(
                toks_[first].line,
                "unrecognized declaration: declarators after a type definition are not supported; declare the variable separately");
        }
        SkipStatement();
    }

    // After the requires keyword at `i`: skips the constraint expression.
    usize SkipRequires(usize i)
    {
        ++i;
        for (;;)
        {
            if (Is(i, "requires"))
            {  // requires requires (...) { ... }
                ++i;
                if (Is(i, "("))
                {
                    i = SkipBalanced(i);
                }
                if (Is(i, "{"))
                {
                    i = SkipBalanced(i);
                }
            }
            else if (Is(i, "("))
            {
                i = SkipBalanced(i);
            }
            else
            {
                if (Is(i, "!"))
                {
                    ++i;
                }
                if (!IsIdentTok(i))
                {
                    return i;
                }
                ++i;
                while (Is(i, "::") && IsIdentTok(i + 1))
                {
                    i += 2;
                }
                if (Is(i, "<"))
                {
                    usize r = SkipAngles(i);
                    if (r == kNone)
                    {
                        return i;
                    }
                    i = r;
                }
                if (Is(i, "("))
                {
                    i = SkipBalanced(i);
                }
            }
            if (Is(i, "&&") || Is(i, "||"))
            {
                ++i;
                continue;
            }
            return i;
        }
    }

    // ---- declaration creation -------------------------------------------

    Decl* NewRaw()
    {
        void* mem = arena_.Alloc(sizeof(Decl), alignof(Decl));
        ENGINE_ASSERT(mem != nullptr, "docgen: arena exhausted");
        return new (mem) Decl(arena_);
    }

    Decl* NewDecl(Decl* parent, Access access, DeclKind kind, StringView name, usize first_tok, u32 line)
    {
        Decl* d = NewRaw();
        d->kind = kind;
        d->name = name;
        d->parent = parent;
        d->access = access;
        d->line = line;
        d->hidden = parent->hidden || access != Access::Public;
        d->qualified = Qualify(parent, name);
        d->doc = first_tok < n_ ? toks_[first_tok].doc : -1;
        parent->children.Push(d);
        return d;
    }

    i32 TrailingAt(usize tok) const { return tok < n_ ? trailing_[tok] : -1; }

    // ---- statements -----------------------------------------------------

    void ParseBlock(ScopeState& sc, u32 open_line)
    {
        while (p_ < n_)
        {
            if (Is(p_, "}"))
            {
                ++p_;
                return;
            }
            usize p0 = p_;
            ParseStatement(sc);
            if (p_ == p0)
            {
                ++p_;
            }
        }
        ReportUnbalanced(open_line);
    }

    void ParseStatement(ScopeState& sc)
    {
        const Token& tok = toks_[p_];
        if (tok.kind == TokenKind::Define)
        {
            if (tok.doc >= 0)
            {
                AddMacro(p_);
            }
            ++p_;
            return;
        }
        if (Is(p_, ";"))
        {
            ++p_;
            return;
        }
        if (sc.is_class && tok.kind == TokenKind::Identifier && Is(p_ + 1, ":") &&
            (Is(p_, "public") || Is(p_, "protected") || Is(p_, "private")))
        {
            sc.access = Is(p_, "public") ? Access::Public : (Is(p_, "protected") ? Access::Protected : Access::Private);
            p_ += 2;
            return;
        }

        const usize first = p_;
        StringView hdr;
        if (Is(p_, "template"))
        {
            ++p_;
            if (!Is(p_, "<"))
            {  // explicit instantiation
                SkipStatement();
                return;
            }
            usize q = SkipAngles(p_);
            if (q == kNone)
            {
                Error(toks_[first].line, "unrecognized declaration");
                SkipStatement();
                return;
            }
            p_ = q;
            hdr = Join(first, p_);
        }

        if (p_ >= n_)
        {
            return;
        }

        if (Is(p_, "extern") && p_ + 1 < n_ && toks_[p_ + 1].kind == TokenKind::StringLiteral)
        {
            if (Is(p_ + 2, "{"))
            {
                u32 open_line = toks_[p_ + 2].line;
                p_ += 3;
                ParseBlock(sc, open_line);
                return;
            }
            p_ += 2;
        }

        if (Is(p_, "namespace"))
        {
            ParseNamespace(sc, first, false);
        }
        else if (Is(p_, "inline") && Is(p_ + 1, "namespace"))
        {
            ++p_;
            ParseNamespace(sc, first, true);
        }
        else if (Is(p_, "using"))
        {
            ParseUsing(sc, first, hdr);
        }
        else if (Is(p_, "typedef"))
        {
            ParseTypedef(sc, first);
        }
        else if (Is(p_, "static_assert"))
        {
            SkipStatement();
        }
        else if (Is(p_, "friend"))
        {
            ClaimDocOf(first);  // a friend declaration may carry a comment; it documents nothing new
            SkipStatement();
        }
        else if (Is(p_, "enum") && ParseEnum(sc, first, hdr))
        {
            // parsed
        }
        else if ((Is(p_, "class") || Is(p_, "struct") || Is(p_, "union")) && ParseClass(sc, first, hdr))
        {
            // parsed
        }
        else
        {
            ParseGeneral(sc, first, hdr);
        }
    }

    void AddMacro(usize i)
    {
        StringView txt = toks_[i].text;
        usize len = 0;
        while (len < txt.Length() && ExtractorIsIdentChar(txt[len]))
        {
            ++len;
        }
        usize sig = len;
        if (len < txt.Length() && txt[len] == '(')
        {
            while (sig < txt.Length() && txt[sig] != ')')
            {
                ++sig;
            }
            if (sig < txt.Length())
            {
                ++sig;
            }
        }
        Decl* d = NewDecl(out_.root, Access::Public, DeclKind::Macro, StringView(txt.Data(), len), i, toks_[i].line);
        d->head_keyword = StringView("define");
        d->signature = StringView(txt.Data(), sig);
        d->trailing_doc = TrailingAt(i);
        if (sig > len)
        {  // function-like: split the parameter list at commas
            usize a = len + 1;
            const usize end = txt[sig - 1] == ')' ? sig - 1 : sig;  // the ')' (absent when the directive is cut off)
            while (a <= end)
            {
                usize b = a;
                while (b < end && txt[b] != ',')
                {
                    ++b;
                }
                usize x = a, y = b;
                while (x < y && (txt[x] == ' ' || txt[x] == '\t'))
                {
                    ++x;
                }
                while (y > x && (txt[y - 1] == ' ' || txt[y - 1] == '\t'))
                {
                    --y;
                }
                if (y > x)
                {
                    ++d->param_count;  // every non-empty entry counts, `...` included
                }
                if (y - x >= 3 && txt[y - 1] == '.' && txt[y - 2] == '.' && txt[y - 3] == '.')
                {
                    y -= 3;  // `args...` or `...`
                }
                while (y > x && (txt[y - 1] == ' ' || txt[y - 1] == '\t'))
                {
                    --y;
                }
                if (y > x)
                {
                    d->params.Push(StringView(txt.Data() + x, y - x));
                }
                a = b + 1;
            }
        }
    }

    Decl* GetNamespace(Decl* parent, StringView name, bool anonymous, usize first_tok, u32 line)
    {
        for (usize i = 0; i < parent->children.Size(); ++i)
        {
            Decl* c = parent->children[i];
            if (c->kind == DeclKind::Namespace && c->name == name)
            {
                return c;
            }
        }
        Decl* d = NewDecl(parent, Access::Public, DeclKind::Namespace, name, first_tok, line);
        d->head_keyword = StringView("namespace");
        d->signature = Cat(StringView("namespace "), "", name);
        if (anonymous)
        {
            d->flags |= kFlagAnonymous;
            d->hidden = true;
        }
        if (name == StringView("detail"))
        {
            d->hidden = true;
        }
        return d;
    }

    void ParseNamespace(ScopeState& sc, usize first, bool inline_ns)
    {
        const u32 line = toks_[p_].line;
        ++p_;  // namespace
        while (Is(p_, "[") && Is(p_ + 1, "["))
        {
            p_ = SkipBalanced(p_);
        }
        usize names[16];
        usize count = 0;
        for (;;)
        {
            if (Is(p_, "inline"))
            {  // namespace a::inline b
                ++p_;
                continue;
            }
            if (!IsIdentTok(p_) || count == 16)
            {
                break;
            }
            names[count++] = p_;
            ++p_;
            if (Is(p_, "::"))
            {
                ++p_;
                continue;
            }
            break;
        }
        if (Is(p_, "="))
        {  // namespace alias
            SkipStatement();
            return;
        }
        if (!Is(p_, "{"))
        {
            Error(toks_[first].line, "unrecognized declaration");
            SkipStatement();
            return;
        }
        const u32 open_line = toks_[p_].line;
        ++p_;
        Decl* cur = sc.decl;
        if (count == 0)
        {
            cur = GetNamespace(cur, StringView("(anonymous)"), true, first, line);
        }
        else
        {
            for (usize i = 0; i < count; ++i)
            {
                // Only the innermost segment of `a::b::c` carries the doc comment.
                cur = GetNamespace(cur, toks_[names[i]].text, false, i + 1 == count ? first : kNone, line);
            }
        }
        if (cur->doc < 0 && first < n_)
        {
            cur->doc = toks_[first].doc;
        }
        if (first < n_ && toks_[first].doc >= 0)
        {
            out_.doc_claimed[static_cast<usize>(toks_[first].doc)] = 1;  // reopened namespace
        }
        if (inline_ns)
        {
            cur->flags |= kFlagInlineNamespace;
        }
        ScopeState inner{cur, Access::Public, StringView(), false};
        ParseBlock(inner, open_line);
    }

    void ParseUsing(ScopeState& sc, usize first, StringView hdr)
    {
        // using namespace ...;  using Base::name;  using enum ...;
        if (!(IsIdentTok(p_ + 1) && Is(p_ + 2, "=")))
        {
            SkipStatement();
            return;
        }
        usize end = FindStatementEnd(p_);
        if (end == kNone)
        {
            Error(toks_[first].line, "unrecognized declaration");
            SkipStatement();
            return;
        }
        Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Alias, toks_[p_ + 1].text, first, toks_[first].line);
        d->head_keyword = StringView("using");
        d->template_header = hdr;
        d->signature = Join(p_, end);
        d->trailing_doc = TrailingAt(end);
        p_ = end + 1;
    }

    void ParseTypedef(ScopeState& sc, usize first)
    {
        usize end = FindStatementEnd(p_);
        if (end == kNone)
        {
            Error(toks_[first].line, "unrecognized declaration");
            SkipStatement();
            return;
        }
        usize name_tok = kNone;
        for (usize k = p_ + 1; k < end;)
        {
            if (Is(k, "("))
            {
                usize close = SkipBalanced(k);
                if (Is(k + 1, "*") || Is(k + 1, "&"))
                {
                    for (usize j = k + 1; j + 1 < close; ++j)
                    {
                        if (IsIdentTok(j))
                        {
                            name_tok = j;
                        }
                    }
                }
                k = close;
                continue;
            }
            if (Is(k, "[") || Is(k, "{"))
            {
                k = SkipBalanced(k);
                continue;
            }
            if (IsIdentTok(k))
            {
                name_tok = k;
            }
            ++k;
        }
        if (name_tok == kNone)
        {
            Error(toks_[first].line, "unrecognized declaration");
            p_ = end + 1;
            return;
        }
        Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Alias, toks_[name_tok].text, first, toks_[first].line);
        d->head_keyword = StringView("typedef");
        d->signature = Join(p_, end);
        d->trailing_doc = TrailingAt(end);
        p_ = end + 1;
    }

    // Returns false (with p_ untouched) when this is not an enum definition
    // or opaque declaration, e.g. `enum Color c = Red;`.
    bool ParseEnum(ScopeState& sc, usize first, StringView hdr)
    {
        usize q = p_ + 1;
        StringView kw("enum");
        bool scoped = false;
        if (Is(q, "class") || Is(q, "struct"))
        {
            scoped = true;
            kw = Is(q, "class") ? StringView("enum class") : StringView("enum struct");
            ++q;
        }
        while (Is(q, "[") && Is(q + 1, "["))
        {
            q = SkipBalanced(q);
        }
        usize name_b = kNone;
        if (IsIdentTok(q))
        {
            name_b = q;
            ++q;
            while (Is(q, "::") && IsIdentTok(q + 1))
            {
                q += 2;
            }
        }
        const usize name_e = q;
        if (Is(q, ":"))
        {
            ++q;
            while (q < n_ && !Is(q, "{") && !Is(q, ";"))
            {
                if (!IsIdentTok(q) && !Is(q, "::"))
                {
                    return false;
                }
                ++q;
            }
        }
        if (Is(q, ";"))
        {  // opaque declaration: nothing to document
            ClaimDocOf(first);
            p_ = q + 1;
            return true;
        }
        if (!Is(q, "{"))
        {
            return false;
        }

        const u32 open_line = toks_[q].line;
        const usize brace = q;
        StringView name = name_b == kNone ? StringView("(anonymous)") : Join(name_b, name_e);
        Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Enum, name, first, toks_[first].line);
        if (name_b == kNone)
        {
            d->flags |= kFlagAnonymous;
        }
        d->head_keyword = kw;
        d->template_header = hdr;
        d->signature = Join(p_, brace);

        p_ = brace + 1;
        while (p_ < n_ && !Is(p_, "}"))
        {
            if (Is(p_, ","))
            {
                ++p_;
                continue;
            }
            const usize es = p_;
            while (Is(p_, "[") && Is(p_ + 1, "["))
            {
                p_ = SkipBalanced(p_);
            }
            if (!IsIdentTok(p_))
            {
                Error(toks_[es].line, "unrecognized declaration");
                while (p_ < n_ && !Is(p_, ",") && !Is(p_, "}"))
                {
                    if (Is(p_, "(") || Is(p_, "[") || Is(p_, "{"))
                    {
                        p_ = SkipBalanced(p_);
                    }
                    else
                    {
                        ++p_;
                    }
                }
                continue;
            }
            const usize name_i = p_;
            ++p_;
            while (Is(p_, "[") && Is(p_ + 1, "["))
            {
                p_ = SkipBalanced(p_);
            }
            if (Is(p_, "="))
            {
                ++p_;
                while (p_ < n_ && !Is(p_, ",") && !Is(p_, "}"))
                {
                    if (Is(p_, "(") || Is(p_, "[") || Is(p_, "{"))
                    {
                        p_ = SkipBalanced(p_);
                    }
                    else
                    {
                        ++p_;
                    }
                }
            }
            const usize value_end = p_;
            Decl* e = NewDecl(d, sc.access, DeclKind::Enumerator, toks_[name_i].text, name_i, toks_[name_i].line);
            e->qualified = Qualify(scoped ? d : sc.decl, toks_[name_i].text);
            e->head_keyword = StringView();
            e->signature = Join(es, value_end);
            e->doc = toks_[name_i].doc >= 0 ? toks_[name_i].doc : toks_[es].doc;
            usize last = Is(p_, ",") ? p_ : (p_ > 0 ? p_ - 1 : 0);
            e->trailing_doc = TrailingAt(last);
            if (e->trailing_doc < 0 && value_end > name_i)
            {
                e->trailing_doc = TrailingAt(value_end - 1);
            }
            if (Is(p_, ","))
            {
                ++p_;
            }
        }
        if (p_ >= n_)
        {
            ReportUnbalanced(open_line);
            return true;
        }
        const usize close = p_;
        ++p_;  // }
        FinishTypeDecl(first);
        d->trailing_doc = TrailingAt(close);
        if (d->trailing_doc < 0 && p_ > 0 && Is(p_ - 1, ";"))
        {
            d->trailing_doc = TrailingAt(p_ - 1);
        }
        return true;
    }

    bool ParseClass(ScopeState& sc, usize first, StringView hdr)
    {
        usize q = p_;
        const StringView kw = toks_[q].text;
        ++q;
        for (;;)
        {
            if (Is(q, "[") && Is(q + 1, "["))
            {
                q = SkipBalanced(q);
            }
            else if (Is(q, "alignas"))
            {
                ++q;
                if (Is(q, "("))
                {
                    q = SkipBalanced(q);
                }
            }
            else
            {
                break;
            }
        }
        // `class ENGINE_API Foo`: skip an export/attribute macro (all capitals).
        while (IsIdentTok(q) && IsIdentTok(q + 1) && !Is(q + 1, "final") && LooksLikeMacroName(toks_[q].text))
        {
            ++q;
        }
        const usize name_b = q;
        usize simple = kNone;
        if (IsIdentTok(q) && !(Is(q, "final") && (Is(q + 1, "{") || Is(q + 1, ":"))))
        {
            simple = q;
            ++q;
            while (Is(q, "::") && IsIdentTok(q + 1))
            {
                simple = q + 1;
                q += 2;
            }
            if (Is(q, "<"))
            {
                usize r = SkipAngles(q);
                if (r == kNone)
                {
                    return false;
                }
                q = r;
            }
        }
        const usize name_e = q;
        bool is_final = false;
        if (Is(q, "final"))
        {
            is_final = true;
            ++q;
        }
        usize colon = kNone;
        if (Is(q, ":"))
        {
            colon = q;
            ++q;
            while (q < n_ && !Is(q, "{") && !Is(q, ";"))
            {
                if (Is(q, "(") || Is(q, "=") || Is(q, ")") || Is(q, "}"))
                {
                    return false;
                }
                if (Is(q, "<"))
                {
                    usize r = SkipAngles(q);
                    if (r == kNone)
                    {
                        return false;
                    }
                    q = r;
                    continue;
                }
                ++q;
            }
        }
        if (Is(q, ";"))
        {  // forward declaration
            ClaimDocOf(first);
            p_ = q + 1;
            return true;
        }
        if (!Is(q, "{"))
        {
            return false;
        }

        const u32 open_line = toks_[q].line;
        const bool is_class_kw = kw == StringView("class");
        const Access default_access = is_class_kw ? Access::Private : Access::Public;

        if (simple == kNone)
        {  // anonymous struct/union
            if (sc.is_class)
            {
                // Members of an anonymous struct/union are members of the enclosing class.
                ScopeState inner{sc.decl, default_access, sc.class_name, true};
                p_ = q + 1;
                ParseBlock(inner, open_line);
            }
            else
            {
                p_ = SkipBalanced(q);
            }
            FinishTypeDecl(first);
            return true;
        }

        StringView name = Join(name_b, name_e);
        Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Class, name, first, toks_[first].line);
        d->head_keyword = kw;
        d->template_header = hdr;
        d->signature = Join(p_, q);
        d->attributes = CollectAttributes(p_, q);
        if (is_final)
        {
            d->flags |= kFlagFinal;
        }
        if (colon != kNone)
        {
            ExtractBases(d, colon + 1, q);
        }

        ScopeState inner{d, default_access, toks_[simple].text, true};
        p_ = q + 1;
        ParseBlock(inner, open_line);
        const usize close = (p_ > 0 && Is(p_ - 1, "}")) ? p_ - 1 : kNone;
        FinishTypeDecl(first);
        d->trailing_doc = TrailingAt(close);
        if (d->trailing_doc < 0 && close != kNone && Is(p_ - 1, ";"))
        {
            d->trailing_doc = TrailingAt(p_ - 1);
        }
        return true;
    }

    void ExtractBases(Decl* d, usize from, usize to)
    {
        for (usize a = from; a < to;)
        {
            usize b = a;
            usize last = kNone;
            while (b < to && !Is(b, ","))
            {
                if (Is(b, "<") && b > a && IsIdentTok(b - 1))
                {
                    usize r = SkipAngles(b);
                    b = r == kNone ? b + 1 : r;
                    continue;
                }
                if (IsIdentTok(b) && !Is(b, "public") && !Is(b, "protected") && !Is(b, "private") && !Is(b, "virtual"))
                {
                    last = b;
                }
                ++b;
            }
            if (last != kNone)
            {
                d->bases.Push(toks_[last].text);
            }
            a = b + 1;
        }
    }

    // The '(' at `open` (closing at `after - 1`) holds a function/member-function pointer
    // declarator such as `(*name)`, `(&name)` or `(S::*name)`, followed by its parameter list.
    bool IsDeclaratorGroup(usize open, usize after) const
    {
        if (!Is(after, "(") && !Is(after, "["))
        {
            return false;
        }
        for (usize j = open + 1; j + 1 < after; ++j)
        {
            if (Is(j, "*") || Is(j, "&"))
            {
                return true;
            }
        }
        return false;
    }

    static bool IsBuiltinTypeWord(StringView s)
    {
        static const char* words[] = {"int",  "char", "short", "long",    "unsigned", "signed",   "float",   "double",
                                      "bool", "void", "auto",  "wchar_t", "char8_t",  "char16_t", "char32_t"};
        for (const char* w : words)
        {
            if (s == StringView(w))
            {
                return true;
            }
        }
        return false;
    }

    // Names the parameters of the group starting at the '(' at `open`.
    void ExtractParams(Decl* d, usize open) { d->param_types = ParamTypeList(open, d); }

    // Normalized parameter types of the group at `open`, joined by ','. Fills d's
    // names and arity when `d` is not null.
    StringView ParamTypeList(usize open, Decl* d)
    {
        Vector<StringView, ArenaAllocator> types(arena_);
        const usize close = SkipBalanced(open);
        const usize end = close > 0 ? close - 1 : close;  // the ')'
        for (usize a = open + 1; a < end;)
        {
            usize b = a;
            while (b < end && !Is(b, ","))
            {
                if (Is(b, "(") || Is(b, "[") || Is(b, "{"))
                {
                    b = SkipBalanced(b);
                }
                else if (Is(b, "<") && b > a && IsIdentTok(b - 1))
                {
                    usize r = SkipAngles(b);
                    b = r == kNone ? b + 1 : r;
                }
                else
                {
                    ++b;
                }
            }
            AddParam(d, a, b > end ? end : b, types);
            a = b + 1;
        }
        usize len = 0;
        for (usize i = 0; i < types.Size(); ++i)
        {
            len += types[i].Length() + (i > 0 ? 1 : 0);
        }
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
        usize at = 0;
        for (usize i = 0; i < types.Size(); ++i)
        {
            if (i > 0)
            {
                buf[at++] = ',';
            }
            CopyBytes(buf + at, types[i].Data(), types[i].Length());
            at += types[i].Length();
        }
        buf[len] = '\0';
        return StringView(buf, len);
    }

    static bool IsWordToken(const Token& t)
    {
        return t.kind == TokenKind::Identifier || t.kind == TokenKind::Number || t.kind == TokenKind::StringLiteral ||
               t.kind == TokenKind::CharLiteral;
    }

    struct TypePiece
    {
        StringView text;
        bool word;
        bool star;  // `*`, `&`, `&&` or `>`: a following word is separated by a space
        bool close_paren;
    };

    // Tokens [a, e) without `skip`, spaced only where two words (or `*`/`&`/`>` and a word)
    // meet. The parameter list that follows a declarator group, `(*)(void* user)`, is
    // normalized recursively so its parameter names are dropped too.
    StringView NormType(usize a, usize e, usize skip)
    {
        Vector<TypePiece, ArenaAllocator> pieces(arena_);
        for (usize k = a; k < e; ++k)
        {
            if (k == skip || toks_[k].kind == TokenKind::Define)
            {
                continue;
            }
            const Token& t = toks_[k];
            if (Is(k, "(") && pieces.Size() > 0 && pieces[pieces.Size() - 1].close_paren)
            {
                usize after = SkipBalanced(k);
                StringView inner = ParamTypeList(k, nullptr);
                char* buf = static_cast<char*>(arena_.Alloc(inner.Length() + 3, 1));
                ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
                buf[0] = '(';
                CopyBytes(buf + 1, inner.Data(), inner.Length());
                buf[inner.Length() + 1] = ')';
                buf[inner.Length() + 2] = '\0';
                pieces.Push(TypePiece{StringView(buf, inner.Length() + 2), false, false, false});
                k = after - 1;
                continue;
            }
            pieces.Push(TypePiece{t.text, IsWordToken(t),
                                  t.text == StringView("*") || t.text == StringView("&") ||
                                      t.text == StringView("&&") || t.text == StringView(">"),
                                  t.text == StringView(")")});
        }
        usize len = 0;
        for (usize i = 0; i < pieces.Size(); ++i)
        {
            if (i > 0 && pieces[i].word && (pieces[i - 1].word || pieces[i - 1].star))
            {
                ++len;
            }
            len += pieces[i].text.Length();
        }
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
        usize at = 0;
        for (usize i = 0; i < pieces.Size(); ++i)
        {
            if (i > 0 && pieces[i].word && (pieces[i - 1].word || pieces[i - 1].star))
            {
                buf[at++] = ' ';
            }
            CopyBytes(buf + at, pieces[i].text.Data(), pieces[i].text.Length());
            at += pieces[i].text.Length();
        }
        buf[len] = '\0';
        return StringView(buf, len);
    }

    void AddParam(Decl* d, usize a, usize b, Vector<StringView, ArenaAllocator>& types)
    {
        while (Is(a, "[") && Is(a + 1, "[") && a < b)
        {
            a = SkipBalanced(a);  // leading attributes
        }
        usize e = a;
        while (e < b && !Is(e, "="))
        {
            if (Is(e, "(") || Is(e, "[") || Is(e, "{"))
            {
                e = SkipBalanced(e);
            }
            else
            {
                ++e;
            }
        }
        if (e > b)
        {
            e = b;
        }
        if (e == a)
        {
            return;
        }
        if (e - a == 1 && Is(a, "void"))
        {
            return;
        }
        if (d != nullptr)
        {
            ++d->param_count;  // `...` counts toward the arity too
        }
        if (e - a == 1 && Is(a, "..."))
        {
            types.Push(StringView("..."));
            return;
        }
        usize last = kNone;
        usize fp_name = kNone;
        for (usize k = a; k < e;)
        {
            if (Is(k, "["))
            {
                k = SkipBalanced(k);
                continue;
            }
            if (Is(k, "("))
            {
                usize cl = SkipBalanced(k);
                if (IsDeclaratorGroup(k, cl))
                {
                    for (usize j = k + 1; j + 1 < cl; ++j)
                    {
                        if (IsIdentTok(j))
                        {
                            fp_name = j;
                        }
                    }
                }
                k = cl;
                continue;
            }
            if (Is(k, "<") && k > a && IsIdentTok(k - 1))
            {
                usize r = SkipAngles(k);
                if (r != kNone)
                {
                    k = r;
                    continue;
                }
            }
            if (IsIdentTok(k))
            {
                last = k;
            }
            ++k;
        }
        usize name = kNone;
        if (fp_name != kNone)
        {
            name = fp_name;
        }
        else if (last != kNone && last > a && !IsBuiltinTypeWord(toks_[last].text) && !Is(last, "const") &&
                 !Is(last, "volatile"))
        {
            // `const T` names a type, `int const x` names a parameter.
            bool cv_before = (Is(last - 1, "const") || Is(last - 1, "volatile")) &&
                             (last - 1 == a || Is(last - 2, "const") || Is(last - 2, "volatile"));
            bool type_ctx = cv_before || Is(last - 1, "struct") || Is(last - 1, "class") || Is(last - 1, "enum") ||
                            Is(last - 1, "typename") || Is(last - 1, "::") || Is(last - 1, "<");
            if (!type_ctx)
            {
                name = last;
            }
        }
        if (name != kNone && d != nullptr)
        {
            d->params.Push(toks_[name].text);
        }
        types.Push(NormType(a, e, name));
    }

    // True for functions declared `void`, and for constructors/destructors.
    bool DeclaredVoid(usize start, usize head_end, usize paren_open, usize sig_end, u32 flags)
    {
        if ((flags & (kFlagConstructor | kFlagDestructor)) != 0)
        {
            return true;
        }
        usize count = 0;
        usize only = kNone;
        for (usize k = start; k < head_end;)
        {
            if (Is(k, "[") && Is(k + 1, "["))
            {
                k = SkipBalanced(k);
                continue;
            }
            if (Is(k, "static") || Is(k, "virtual") || Is(k, "inline") || Is(k, "constexpr") || Is(k, "consteval") ||
                Is(k, "explicit") || Is(k, "friend") || Is(k, "extern"))
            {
                ++k;
                continue;
            }
            ++count;
            only = k;
            ++k;
        }
        if (count != 1)
        {
            return false;
        }
        if (Is(only, "void"))
        {
            return true;
        }
        if (Is(only, "auto"))
        {
            for (usize k = SkipBalanced(paren_open); k < sig_end; ++k)
            {
                if (Is(k, "->"))
                {
                    return Is(k + 1, "void") && !Is(k + 2, "*") && !Is(k + 2, "&");
                }
            }
        }
        return false;
    }

    StringView CollectAttributes(usize from, usize to)
    {
        StringBuilder<ArenaAllocator> sb(arena_);
        bool any = false;
        for (usize k = from; k < to && k < n_;)
        {
            if (Is(k, "(") || (Is(k, "[") && !Is(k + 1, "[")))
            {
                k = SkipBalanced(k);
                continue;
            }
            if (Is(k, "[") && Is(k + 1, "["))
            {
                usize r = SkipBalanced(k);
                if (any)
                {
                    sb.Append(' ');
                }
                sb.Append(Join(k, r));
                any = true;
                k = r;
                continue;
            }
            ++k;
        }
        return any ? sb.View() : StringView();
    }

    static bool LooksLikeMacroName(StringView s)
    {
        if (s.Length() < 2)
        {
            return false;
        }
        for (usize i = 0; i < s.Length(); ++i)
        {
            char c = s[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            {
                return false;
            }
        }
        return true;
    }

    // At a ',' inside an initializer: does a new declarator follow (`, y_;`)?
    bool LooksLikeDeclarator(usize comma) const
    {
        usize k = comma + 1;
        while (Is(k, "*") || Is(k, "&") || Is(k, "&&"))
        {
            ++k;
        }
        if (!IsIdentTok(k))
        {
            return false;
        }
        return Is(k + 1, ";") || Is(k + 1, "=") || Is(k + 1, ",") || Is(k + 1, "{") || Is(k + 1, "[") || Is(k + 1, ":");
    }

    static bool IsHeadKeywordBeforeParen(StringView s)
    {
        return s == StringView("decltype") || s == StringView("alignas") || s == StringView("__attribute__") ||
               s == StringView("__declspec") || s == StringView("sizeof") || s == StringView("typeof");
    }

    void ParseGeneral(ScopeState& sc, usize first, StringView hdr)
    {
        usize start = p_;
        usize i = p_;
        usize paren_open = kNone;
        usize op_idx = kNone;
        usize fp_name = kNone;
        usize last_ident = kNone;
        usize sig_end = kNone;
        usize body_open = kNone;
        bool in_init = false;
        bool trailing_ret = false;
        bool multi = false;
        u32 flags = 0;

        while (i < n_)
        {
            const Token& t = toks_[i];
            if (t.kind == TokenKind::Define)
            {
                ++i;
                continue;
            }
            if (t.kind == TokenKind::Punct)
            {
                if (Is(i, ";"))
                {
                    break;
                }
                if (Is(i, "}"))
                {
                    break;
                }
                if (Is(i, "("))
                {
                    if (in_init || paren_open != kNone)
                    {
                        i = SkipBalanced(i);
                        continue;
                    }
                    const usize after = SkipBalanced(i);
                    if (IsDeclaratorGroup(i, after))
                    {
                        // function-pointer variable: T (*name)(args)
                        for (usize j = i + 1; j + 1 < after; ++j)
                        {
                            if (IsIdentTok(j))
                            {
                                fp_name = j;
                            }
                        }
                        last_ident = fp_name;
                        i = SkipBalanced(after);
                        continue;
                    }
                    if (i > start && IsIdentTok(i - 1) && IsHeadKeywordBeforeParen(toks_[i - 1].text))
                    {
                        i = after;
                        continue;
                    }
                    if (i > start && i + 1 < n_ &&
                        (toks_[i + 1].kind == TokenKind::Number || toks_[i + 1].kind == TokenKind::StringLiteral ||
                         toks_[i + 1].kind == TokenKind::CharLiteral))
                    {
                        // `int v(5);` direct initialization, not a parameter list
                        if (sig_end == kNone)
                        {
                            sig_end = i;
                        }
                        in_init = true;
                        i = after;
                        continue;
                    }
                    paren_open = i;
                    i = after;
                    continue;
                }
                if (Is(i, "["))
                {
                    i = SkipBalanced(i);
                    continue;
                }
                if (Is(i, "<") && !in_init && i > start && IsIdentTok(i - 1))
                {
                    // template-id: its arguments may contain parentheses, braces and commas
                    usize r = SkipAngles(i);
                    if (r != kNone)
                    {
                        i = r;
                        continue;
                    }
                }
                if (Is(i, ",") && (in_init ? LooksLikeDeclarator(i) : true))
                {
                    multi = true;
                }
                if (Is(i, "->") && paren_open != kNone && !in_init)
                {
                    trailing_ret = true;
                }
                if (Is(i, "{"))
                {
                    if (paren_open != kNone && !in_init)
                    {
                        body_open = i;
                        if (sig_end == kNone)
                        {
                            sig_end = i;
                        }
                        break;
                    }
                    if (sig_end == kNone)
                    {
                        sig_end = i;
                    }
                    in_init = true;
                    i = SkipBalanced(i);
                    continue;
                }
                if (Is(i, "="))
                {
                    if (paren_open != kNone && !in_init)
                    {
                        if (sig_end == kNone)
                        {
                            sig_end = i;
                        }
                        if (Is(i + 1, "0"))
                        {
                            flags |= kFlagPureVirtual;
                            i += 2;
                            continue;
                        }
                        if (Is(i + 1, "default"))
                        {
                            flags |= kFlagDefaulted;
                            i += 2;
                            continue;
                        }
                        if (Is(i + 1, "delete"))
                        {
                            flags |= kFlagDeleted;
                            i += 2;
                            continue;
                        }
                    }
                    if (sig_end == kNone)
                    {
                        sig_end = i;
                    }
                    in_init = true;
                    ++i;
                    continue;
                }
                if (Is(i, ":") && paren_open != kNone && !in_init)
                {
                    // constructor initializer list: skip to the body brace
                    if (sig_end == kNone)
                    {
                        sig_end = i;
                    }
                    ++i;
                    while (i < n_)
                    {
                        if (Is(i, "{"))
                        {
                            if (IsIdentTok(i - 1) || Is(i - 1, ">"))
                            {
                                i = SkipBalanced(i);
                                continue;
                            }
                            body_open = i;
                            break;
                        }
                        if (Is(i, "("))
                        {
                            i = SkipBalanced(i);
                            continue;
                        }
                        if (Is(i, ";") || Is(i, "}"))
                        {
                            break;
                        }
                        ++i;
                    }
                    break;
                }
                ++i;
                continue;
            }
            if (t.kind == TokenKind::Identifier && !in_init)
            {
                if (Is(i, "operator"))
                {
                    op_idx = i;
                    ++i;
                    if (Is(i, "(") && Is(i + 1, ")"))
                    {
                        i += 2;
                    }
                    else
                    {
                        while (i < n_ && !Is(i, "(") && !Is(i, ";") && !Is(i, "{"))
                        {
                            ++i;
                        }
                    }
                    continue;
                }
                if (Is(i, "requires"))
                {
                    i = SkipRequires(i);
                    if (paren_open == kNone && i > start && !hdr.Empty() && toks_[start].text == StringView("requires"))
                    {
                        // `template <...> requires C<T> decl`: the clause belongs to the template header
                        hdr = Join(first, i);
                        start = i;
                    }
                    continue;
                }
                if (paren_open == kNone)
                {
                    if (Is(i, "static"))
                    {
                        flags |= kFlagStatic;
                    }
                    else if (Is(i, "virtual"))
                    {
                        flags |= kFlagVirtual;
                    }
                    else if (Is(i, "explicit"))
                    {
                        flags |= kFlagExplicit;
                    }
                    else if (Is(i, "constexpr"))
                    {
                        flags |= kFlagConstexpr;
                    }
                    else if (Is(i, "consteval"))
                    {
                        flags |= kFlagConsteval;
                    }
                    else if (Is(i, "inline"))
                    {
                        flags |= kFlagInline;
                    }
                    last_ident = i;
                }
                else
                {
                    if (Is(i, "const") && !trailing_ret)
                    {
                        flags |= kFlagConst;
                    }
                    else if (Is(i, "noexcept") && !trailing_ret)
                    {
                        flags |= kFlagNoexcept;
                    }
                    else if (Is(i, "override"))
                    {
                        flags |= kFlagOverride;
                    }
                    else if (Is(i, "final"))
                    {
                        flags |= kFlagFinal;
                    }
                }
            }
            ++i;
        }

        // Where the statement ends.
        usize resume;
        usize term_tok;  // token a trailing doc comment would follow
        if (body_open != kNone)
        {
            usize after_body = SkipBalanced(body_open);
            term_tok = after_body > 0 ? after_body - 1 : 0;
            resume = after_body;
            while (Is(resume, "catch"))
            {  // function-try-block handlers
                ++resume;
                if (Is(resume, "("))
                {
                    resume = SkipBalanced(resume);
                }
                if (Is(resume, "{"))
                {
                    resume = SkipBalanced(resume);
                }
            }
            if (Is(resume, ";"))
            {
                term_tok = resume;
                ++resume;
            }
        }
        else if (Is(i, ";"))
        {
            term_tok = i;
            resume = i + 1;
        }
        else
        {
            // '}' or end of file before the declaration ended
            Error(toks_[first].line, "unrecognized declaration");
            p_ = i;
            return;
        }
        if (sig_end == kNone)
        {
            sig_end = i;
        }

        if (multi)
        {
            Error(
                toks_[first].line,
                "unrecognized declaration: several declarators in one statement are not supported; declare each name separately");
            p_ = resume;
            return;
        }

        if (paren_open != kNone)
        {
            ParseFunctionTail(sc, first, hdr, start, paren_open, op_idx, sig_end, flags, term_tok);
        }
        else
        {
            usize name_tok = fp_name != kNone ? fp_name : last_ident;
            if (name_tok == kNone || name_tok <= start || name_tok >= sig_end)
            {
                Error(toks_[first].line, "unrecognized declaration");
            }
            else if (Is(name_tok - 1, "::"))
            {
                // definition of a previously declared static member/variable
                ClaimDocOf(first);
            }
            else
            {
                Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Variable, toks_[name_tok].text, first,
                                  toks_[first].line);
                d->flags = flags;
                d->template_header = hdr;
                d->signature = Join(start, sig_end);
                d->attributes = CollectAttributes(start, sig_end);
                d->trailing_doc = TrailingAt(term_tok);
            }
        }
        p_ = resume;
    }

    void ParseFunctionTail(ScopeState& sc, usize first, StringView hdr, usize start, usize paren_open, usize op_idx,
                           usize sig_end, u32 flags, usize term_tok)
    {
        StringView name;
        usize qual_check;  // token just before the (possibly qualified) name
        if (op_idx != kNone)
        {
            name = Join(op_idx, paren_open);
            flags |= kFlagOperator;
            if (IsIdentTok(op_idx + 1) && !Is(op_idx + 1, "new") && !Is(op_idx + 1, "delete") &&
                !Is(op_idx + 1, "co_await"))
            {
                flags |= kFlagConversion;
            }
            qual_check = op_idx;
        }
        else
        {
            if (paren_open == 0 || !IsIdentTok(paren_open - 1))
            {
                Error(toks_[first].line, "unrecognized declaration");
                return;
            }
            const usize name_tok = paren_open - 1;
            name = toks_[name_tok].text;
            qual_check = name_tok;
            if (name_tok > start && Is(name_tok - 1, "~"))
            {
                name = Cat(StringView("~"), "", name);
                flags |= kFlagDestructor;
                qual_check = name_tok - 1;
            }
        }

        // Out-of-line definition (`T Foo::Bar() {}`): the declaration lives in
        // the class, so there is nothing to add here.
        if (qual_check > start && Is(qual_check - 1, "::"))
        {
            ClaimDocOf(first);
            return;
        }

        if (op_idx == kNone && (flags & kFlagDestructor) == 0 && sc.is_class && name == sc.class_name)
        {
            flags |= kFlagConstructor;
        }
        else if ((flags & kFlagDestructor) != 0 && !sc.is_class)
        {
            // free "~Name()" without a class: leave as is
        }

        if (op_idx == kNone && (flags & (kFlagConstructor | kFlagDestructor)) == 0 && qual_check == start)
        {
            Error(toks_[first].line, "macro-generated declaration");
            return;
        }

        Decl* d = NewDecl(sc.decl, sc.access, DeclKind::Function, name, first, toks_[first].line);
        if (DeclaredVoid(start, qual_check, paren_open, sig_end, flags))
        {
            flags |= kFlagReturnsVoid;
        }
        ExtractParams(d, paren_open);
        d->flags = flags;
        d->template_header = hdr;
        d->signature = Join(start, sig_end);
        d->attributes = CollectAttributes(start, sig_end);
        d->trailing_doc = TrailingAt(term_tok);
    }

    const ScanResult& scan_;
    ArenaAllocator& arena_;
    ExtractResult& out_;
    const Vector<Token, ArenaAllocator>& toks_;
    const usize n_;
    Vector<i32, ArenaAllocator> trailing_;
    usize p_ = 0;
    bool unbalanced_reported_ = false;
};

// ---- dump -----------------------------------------------------------------

StringView KindWord(const Decl& d)
{
    switch (d.kind)
    {
    case DeclKind::Namespace: return StringView("namespace");
    case DeclKind::Class: return d.head_keyword.Empty() ? StringView("class") : d.head_keyword;
    case DeclKind::Enum: return d.head_keyword.Empty() ? StringView("enum") : d.head_keyword;
    case DeclKind::Enumerator: return StringView("enumerator");
    case DeclKind::Function: return StringView("function");
    case DeclKind::Variable: return StringView("variable");
    case DeclKind::Alias: return StringView("alias");
    case DeclKind::Macro: return StringView("macro");
    }
    return StringView("?");
}

void Indent(StringBuilder<ArenaAllocator>& sb, int depth)
{
    for (int i = 0; i < depth * 2; ++i)
    {
        sb.Append(' ');
    }
}

void AppendDocLine(StringBuilder<ArenaAllocator>& sb, const char* label, const ScanResult& scan,
                   engine_placeholder::i32 doc, int depth)
{
    if (doc < 0)
    {
        return;
    }
    StringView text = scan.docs[static_cast<usize>(doc)].text;
    usize len = 0;
    while (len < text.Length() && text[len] != '\n')
    {
        ++len;
    }
    Indent(sb, depth);
    sb.Append(StringView(label));
    sb.Append(StringView(": "));
    sb.Append(StringView(text.Data(), len));
    sb.Append('\n');
}

struct FlagName
{
    engine_placeholder::u32 bit;
    const char* name;
};

const FlagName kFlagNames[] = {
    {kFlagStatic, "static"},       {kFlagVirtual, "virtual"},       {kFlagExplicit, "explicit"},
    {kFlagConstexpr, "constexpr"}, {kFlagConsteval, "consteval"},   {kFlagInline, "inline"},
    {kFlagOverride, "override"},   {kFlagFinal, "final"},           {kFlagPureVirtual, "pure"},
    {kFlagDefaulted, "default"},   {kFlagDeleted, "delete"},        {kFlagConstructor, "ctor"},
    {kFlagDestructor, "dtor"},     {kFlagConversion, "conversion"}, {kFlagOperator, "operator"},
    {kFlagConst, "const"},         {kFlagNoexcept, "noexcept"},     {kFlagInlineNamespace, "inline-namespace"},
    {kFlagAnonymous, "anonymous"},
};

void DumpNode(const Decl& d, int depth, bool include_hidden, const ScanResult& scan, StringBuilder<ArenaAllocator>& sb)
{
    if (d.hidden && !include_hidden)
    {
        return;
    }
    Indent(sb, depth);
    sb.Append(KindWord(d));
    sb.Append(' ');
    sb.Append(d.qualified);
    sb.Append(StringView(" @"));
    sb.AppendUInt(d.line);
    for (const FlagName& f : kFlagNames)
    {
        if (d.Is(f.bit))
        {
            sb.Append(' ');
            sb.Append(StringView(f.name));
        }
    }
    if (d.access == Access::Protected)
    {
        sb.Append(StringView(" [protected]"));
    }
    if (d.access == Access::Private)
    {
        sb.Append(StringView(" [private]"));
    }
    if (d.hidden)
    {
        sb.Append(StringView(" [hidden]"));
    }
    sb.Append('\n');
    if (d.kind != DeclKind::Namespace)
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("sig: "));
        sb.Append(d.signature);
        sb.Append('\n');
    }
    if (d.params.Size() > 0)
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("params: "));
        for (usize i = 0; i < d.params.Size(); ++i)
        {
            if (i > 0)
            {
                sb.Append(StringView(", "));
            }
            sb.Append(d.params[i]);
        }
        sb.Append('\n');
    }
    if (!d.param_types.Empty())
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("ptypes: "));
        sb.Append(d.param_types);
        sb.Append('\n');
    }
    if (d.bases.Size() > 0)
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("bases: "));
        for (usize i = 0; i < d.bases.Size(); ++i)
        {
            if (i > 0)
            {
                sb.Append(StringView(", "));
            }
            sb.Append(d.bases[i]);
        }
        sb.Append('\n');
    }
    if (!d.template_header.Empty())
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("tmpl: "));
        sb.Append(d.template_header);
        sb.Append('\n');
    }
    if (!d.attributes.Empty())
    {
        Indent(sb, depth + 1);
        sb.Append(StringView("attrs: "));
        sb.Append(d.attributes);
        sb.Append('\n');
    }
    AppendDocLine(sb, "doc", scan, d.doc, depth + 1);
    AppendDocLine(sb, "trail", scan, d.trailing_doc, depth + 1);
    for (usize i = 0; i < d.children.Size(); ++i)
    {
        DumpNode(*d.children[i], depth + 1, include_hidden, scan, sb);
    }
}

}  // namespace

void Extract(const ScanResult& scan, ArenaAllocator& arena, ExtractResult& out)
{
    Extractor ex(scan, arena, out);
    ex.Run();
}

void DumpTree(const ExtractResult& result, const ScanResult& scan, StringBuilder<ArenaAllocator>& out,
              bool include_hidden)
{
    if (result.root == nullptr)
    {
        return;
    }
    for (usize i = 0; i < result.root->children.Size(); ++i)
    {
        DumpNode(*result.root->children[i], 0, include_hidden, scan, out);
    }
}

}  // namespace docgen
