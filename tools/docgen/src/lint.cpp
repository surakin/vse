#include "lint.h"
#include "cstr/mem_ops.h"

namespace docgen
{

namespace
{

bool Contains(StringView s, const char* lit)
{
    usize n = 0;
    while (lit[n] != '\0')
    {
        ++n;
    }
    if (n == 0 || s.Length() < n)
    {
        return false;
    }
    for (usize i = 0; i + n <= s.Length(); ++i)
    {
        usize k = 0;
        while (k < n && s[i + k] == lit[k])
        {
            ++k;
        }
        if (k == n)
        {
            return true;
        }
    }
    return false;
}

bool IsIdent(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The declared return type of a function (text before its name), with attributes and
// specifier keywords removed, or empty for constructors, destructors, conversions and operators.
StringView ReturnType(const Decl& d, ArenaAllocator& arena)
{
    if (d.Is(kFlagConstructor) || d.Is(kFlagDestructor) || d.Is(kFlagConversion) || d.Is(kFlagOperator))
    {
        return StringView();
    }
    StringView sig = d.signature;
    usize at = sig.Length();
    bool found = false;
    for (usize i = 0; i + d.name.Length() < sig.Length(); ++i)
    {
        if (engine_placeholder::MemCompare(sig.Data() + i, d.name.Data(), d.name.Length()) != 0)
        {
            continue;
        }
        if (i > 0 && IsIdent(sig[i - 1]))
        {
            continue;
        }
        if (sig[i + d.name.Length()] != '(')
        {
            continue;
        }
        at = i;
        found = true;
        break;
    }
    if (!found)
    {
        return StringView();  // cannot tell where the name is: judge nothing rather than the whole signature
    }
    StringView head(sig.Data(), at);
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    usize i = 0;
    while (i < head.Length())
    {
        if (head[i] == '[' && i + 1 < head.Length() && head[i + 1] == '[')
        {  // skip an attribute group
            usize e = i + 2;
            while (e + 1 < head.Length() && !(head[e] == ']' && head[e + 1] == ']'))
            {
                ++e;
            }
            i = e + 2;
            continue;
        }
        if (IsIdent(head[i]))
        {
            usize e = i;
            while (e < head.Length() && IsIdent(head[e]))
            {
                ++e;
            }
            StringView w(head.Data() + i, e - i);
            const bool specifier = w == StringView("static") || w == StringView("virtual") ||
                                   w == StringView("inline") || w == StringView("constexpr") ||
                                   w == StringView("consteval") || w == StringView("explicit") ||
                                   w == StringView("friend");
            if (!specifier)
            {
                if (sb.View().Length() > 0)
                {
                    sb.Append(' ');
                }
                sb.Append(w);
            }
            i = e;
            continue;
        }
        if (head[i] != ' ')
        {
            sb.Append(head[i]);
        }
        ++i;
    }
    return sb.View();
}

bool EndsWithWord(StringView s, const char* w)
{
    usize n = 0;
    while (w[n] != '\0')
    {
        ++n;
    }
    if (s.Length() < n)
    {
        return false;
    }
    if (engine_placeholder::MemCompare(s.Data() + s.Length() - n, w, n) != 0)
    {
        return false;
    }
    return s.Length() == n || !IsIdent(s[s.Length() - n - 1]);
}

bool IsPointer(StringView r)
{
    return !r.Empty() && r[r.Length() - 1] == '*';
}
bool IsBoolOrPointer(StringView r)
{
    return !r.Empty() && (IsPointer(r) || EndsWithWord(r, "bool"));
}

// Does a parameter type look like a raw pointer to non-const (something an owner may be handed)?
bool TakesRawPointer(const Decl& d)
{
    StringView t = d.param_types;
    usize i = 0;
    int depth = 0;
    usize start = 0;
    auto check = [&](StringView p)
    {
        if (p.Length() > 7 && engine_placeholder::MemCompare(p.Data() + p.Length() - 7, "* const", 7) == 0)
        {
            p = StringView(p.Data(), p.Length() - 6);  // `T* const` is still a raw pointer
        }
        if (p.Empty() || p[p.Length() - 1] != '*')
        {
            return false;
        }
        if (Contains(p, "("))
        {
            return false;  // function pointer: a callback, not an owned object
        }
        return !(p.Length() >= 6 && engine_placeholder::MemCompare(p.Data(), "const ", 6) == 0);
    };
    for (; i <= t.Length(); ++i)
    {
        const char c = i < t.Length() ? t[i] : ',';
        if (c == '<' || c == '(')
        {
            ++depth;
        }
        if (c == '>' || c == ')')
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            if (check(StringView(t.Data() + start, i - start)))
            {
                return true;
            }
            start = i + 1;
        }
    }
    return false;
}

bool HasUserDestructor(const Decl& d)
{
    for (usize i = 0; i < d.children.Size(); ++i)
    {
        const Decl& c = *d.children[i];
        if (c.kind == DeclKind::Function && c.Is(kFlagDestructor) && !c.Is(kFlagDefaulted) && !c.Is(kFlagDeleted))
        {
            return true;
        }
    }
    return false;
}

bool Strict(const SymbolIndex& index, usize module, const char* rule)
{
    const ModuleDef& m = *index.map->modules[module];
    for (usize i = 0; i < m.strict.Size(); ++i)
    {
        if (m.strict[i] == StringView(rule))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

const char* LintRuleName(LintRule r)
{
    switch (r)
    {
    case LintRule::Undocumented: return "undocumented";
    case LintRule::ThreadSafety: return "thread-safety";
    case LintRule::Ownership: return "ownership";
    case LintRule::Failure: return "failure";
    }
    return "?";
}

bool IsLintExempt(const Decl& d)
{
    return d.kind == DeclKind::Function && (d.Is(kFlagDeleted) || d.Is(kFlagDefaulted));
}

void RunLint(const SymbolIndex& index, ArenaAllocator& arena, LintResult& out)
{
    for (usize m = 0; m < index.map->modules.Size(); ++m)
    {
        ModuleCoverage mc;
        mc.name = index.map->modules[m]->name;
        out.modules.Push(mc);
    }
    for (usize i = 0; i < index.symbols.Size(); ++i)
    {
        const Symbol& s = *index.symbols[i];
        const Decl& d = *s.decl;
        if (IsLintExempt(d))
        {
            continue;
        }
        ModuleCoverage& mc = out.modules[s.module];
        ++mc.total;
        ++out.total;
        const bool documented = (s.doc != nullptr && s.doc->has_summary) || s.inherited != nullptr;
        if (!documented)
        {
            out.findings.Push(LintFinding{LintRule::Undocumented, &s});
            continue;
        }
        ++mc.documented;
        ++out.documented;
        const ParsedDoc* doc = s.doc;
        if (doc == nullptr || !doc->has_summary)
        {
            continue;  // documented only by inheritance: the base carries the labels
        }
        if (d.kind == DeclKind::Class && Strict(index, s.module, "thread-safety") &&
            doc->Find(LabelKind::ThreadSafety) == nullptr)
        {
            out.findings.Push(LintFinding{LintRule::ThreadSafety, &s});
        }
        StringView ret = d.kind == DeclKind::Function ? ReturnType(d, arena) : StringView();
        if (Strict(index, s.module, "ownership") && doc->Find(LabelKind::Ownership) == nullptr)
        {
            const bool owns = d.kind == DeclKind::Class      ? HasUserDestructor(d)
                              : d.kind == DeclKind::Function ? (IsPointer(ret) || TakesRawPointer(d))
                                                             : false;
            if (owns)
            {
                out.findings.Push(LintFinding{LintRule::Ownership, &s});
            }
        }
        if (d.kind == DeclKind::Function && Strict(index, s.module, "failure") &&
            doc->Find(LabelKind::Returns) == nullptr && doc->Find(LabelKind::Fails) == nullptr &&
            doc->Find(LabelKind::Aborts) == nullptr)
        {
            if (IsBoolOrPointer(ret) ||
                (!d.Is(kFlagOperator) && !d.Is(kFlagConstructor) && Contains(d.attributes, "nodiscard")))
            {
                out.findings.Push(LintFinding{LintRule::Failure, &s});
            }
        }
    }
}

bool ParseBaseline(StringView text, ArenaAllocator& arena, Baseline& out, StringView& error)
{
    u32 line_no = 0;
    usize pos = 0;
    bool header = false;
    auto fail = [&](const char* msg)
    {
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
        sb.AppendUInt(line_no);
        sb.Append(StringView(": "));
        sb.Append(StringView(msg));
        error = sb.View();
        return false;
    };
    while (pos < text.Length())
    {
        usize eol = pos;
        while (eol < text.Length() && text[eol] != '\n')
        {
            ++eol;
        }
        StringView line(text.Data() + pos, eol - pos);
        pos = eol + 1;
        ++line_no;
        if (line.Empty())
        {
            continue;
        }
        if (line[0] == '#')
        {
            if (line == StringView("# docgen lint baseline v1"))
            {
                header = true;
            }
            continue;
        }
        if (!header)
        {
            return fail("not a docgen lint baseline (missing '# docgen lint baseline v1' header)");
        }
        StringView f[4];
        usize n = 0, s = 0;
        for (usize i = 0; i <= line.Length() && n < 4; ++i)
        {
            if (i == line.Length() || line[i] == '\t')
            {
                f[n++] = StringView(line.Data() + s, i - s);
                s = i + 1;
            }
        }
        if (f[0] == StringView("module") && n == 4)
        {
            ModuleCoverage mc;
            mc.name = f[1];
            for (int k = 2; k < 4; ++k)
            {
                usize v = 0;
                if (f[k].Empty())
                {
                    return fail("bad count");
                }
                for (usize i = 0; i < f[k].Length(); ++i)
                {
                    if (f[k][i] < '0' || f[k][i] > '9')
                    {
                        return fail("bad count");
                    }
                    v = v * 10 + static_cast<usize>(f[k][i] - '0');
                }
                (k == 2 ? mc.documented : mc.total) = v;
            }
            out.modules.Push(mc);
        }
        else if (f[0] == StringView("known") && n == 3 && !f[2].Empty())
        {
            Baseline::Known k;
            k.rule = f[1];
            k.id = f[2];
            out.known.Push(k);
        }
        else
        {
            return fail("unrecognized baseline line");
        }
    }
    if (!header)
    {
        return fail("not a docgen lint baseline (missing '# docgen lint baseline v1' header)");
    }
    return true;
}

void RenderBaseline(const LintResult& r, StringBuilder<ArenaAllocator>& out)
{
    out.Append(StringView(
        "# docgen lint baseline v1\n"
        "# Written by `docgen lint --baseline FILE --update-baseline`. The lint tolerates exactly these findings and fails on\n"
        "# any other, or if a module's documented/total ratio drops below its line here. Delete entries as symbols get documented.\n"));
    for (usize i = 0; i < r.modules.Size(); ++i)
    {
        out.Append(StringView("module\t"));
        out.Append(r.modules[i].name);
        out.Append('\t');
        out.AppendUInt(r.modules[i].documented);
        out.Append('\t');
        out.AppendUInt(r.modules[i].total);
        out.Append('\n');
    }
    for (usize i = 0; i < r.findings.Size(); ++i)
    {
        out.Append(StringView("known\t"));
        out.Append(StringView(LintRuleName(r.findings[i].rule)));
        out.Append('\t');
        out.Append(r.findings[i].symbol->id);
        out.Append('\n');
    }
}

void RenderCoverage(const LintResult& r, StringBuilder<ArenaAllocator>& out)
{
    auto row = [&](StringView name, usize doc, usize total)
    {
        out.Append(name);
        for (usize i = name.Length(); i < 28; ++i)
        {
            out.Append(' ');
        }
        out.AppendUInt(doc);
        out.Append('/');
        out.AppendUInt(total);
        out.Append(StringView("  "));
        out.AppendUInt(total == 0 ? 100 : doc * 100 / total);
        out.Append(StringView("%\n"));
    };
    for (usize i = 0; i < r.modules.Size(); ++i)
    {
        row(r.modules[i].name, r.modules[i].documented, r.modules[i].total);
    }
    row(StringView("TOTAL"), r.documented, r.total);
}

namespace
{

// documented/total >= base_documented/base_total, without division (an empty module counts as full).
bool RatioHolds(usize doc, usize total, usize base_doc, usize base_total)
{
    if (base_total == 0)
    {
        return true;
    }
    if (total == 0)
    {
        return true;
    }
    return doc * base_total >= base_doc * total;
}

StringView Ratio(ArenaAllocator& arena, usize d, usize t)
{
    engine_placeholder::StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendUInt(d);
    sb.Append('/');
    sb.AppendUInt(t);
    return sb.View();
}

}  // namespace

void CompareToBaseline(const LintResult& r, Baseline* baseline, ArenaAllocator& arena,
                       Vector<FileDiag, ArenaAllocator>& out)
{
    for (usize i = 0; i < r.findings.Size(); ++i)
    {
        const LintFinding& f = r.findings[i];
        StringView rule(LintRuleName(f.rule));
        bool known = false;
        if (baseline != nullptr)
        {
            for (usize k = 0; k < baseline->known.Size() && !known; ++k)
            {
                Baseline::Known& b = baseline->known[k];
                if (b.rule == rule && b.id == f.symbol->id)
                {
                    b.used = true;
                    known = true;
                }
            }
        }
        if (known)
        {
            continue;
        }
        StringView msg = f.rule == LintRule::Undocumented
                             ? ArenaCat(arena, StringView("undocumented public symbol: "), f.symbol->id)
                             : ArenaCat(arena, StringView("missing required label ("), rule,
                                        ArenaCat(arena, StringView("): "), f.symbol->id));
        out.Push(FileDiag{f.symbol->unit->path, Diagnostic{Severity::Error, f.symbol->decl->line, msg}});
    }
    if (baseline == nullptr)
    {
        return;
    }
    const char*
        refresh = " (refresh with `docgen lint --baseline FILE --update-baseline` and commit the smaller baseline)";
    for (usize k = 0; k < baseline->known.Size(); ++k)
    {
        const Baseline::Known& b = baseline->known[k];
        if (b.used)
        {
            continue;
        }
        out.Push(FileDiag{StringView("lint baseline"),
                          Diagnostic{Severity::Error, 0,
                                     ArenaCat(arena, StringView("baseline entry no longer applies: "), b.rule,
                                              StringView(" "), ArenaCat(arena, b.id, StringView(refresh)))}});
    }
    usize base_doc = 0, base_total = 0;
    for (usize m = 0; m < baseline->modules.Size(); ++m)
    {
        const ModuleCoverage& b = baseline->modules[m];
        base_doc += b.documented;
        base_total += b.total;
        const ModuleCoverage* c = nullptr;
        for (usize i = 0; i < r.modules.Size(); ++i)
        {
            if (r.modules[i].name == b.name)
            {
                c = &r.modules[i];
            }
        }
        if (c == nullptr)
        {
            out.Push(FileDiag{
                StringView("lint baseline"),
                Diagnostic{Severity::Error, 0,
                           ArenaCat(arena, StringView("baseline lists module '"), b.name,
                                    ArenaCat(arena, StringView("' which no longer exists"), StringView(refresh)))}});
        }
        else if (!RatioHolds(c->documented, c->total, b.documented, b.total))
        {
            out.Push(FileDiag{
                StringView("lint baseline"),
                Diagnostic{
                    Severity::Error, 0,
                    ArenaCat(arena, StringView("coverage of "), c->name,
                             ArenaCat(arena, StringView(" dropped from "), Ratio(arena, b.documented, b.total),
                                      ArenaCat(arena, StringView(" to "), Ratio(arena, c->documented, c->total))))}});
        }
    }
    for (usize i = 0; i < r.modules.Size(); ++i)
    {
        bool listed = false;
        for (usize m = 0; m < baseline->modules.Size() && !listed; ++m)
        {
            listed = baseline->modules[m].name == r.modules[i].name;
        }
        if (!listed)
        {
            out.Push(FileDiag{
                StringView("lint baseline"),
                Diagnostic{Severity::Error, 0,
                           ArenaCat(arena, StringView("module '"), r.modules[i].name,
                                    ArenaCat(arena, StringView("' has no baseline entry"), StringView(refresh)))}});
        }
    }
    if (!RatioHolds(r.documented, r.total, base_doc, base_total))
    {
        out.Push(FileDiag{
            StringView("lint baseline"),
            Diagnostic{Severity::Error, 0,
                       ArenaCat(arena, StringView("overall coverage dropped from "), Ratio(arena, base_doc, base_total),
                                ArenaCat(arena, StringView(" to "), Ratio(arena, r.documented, r.total)))}});
    }
}

}  // namespace docgen
