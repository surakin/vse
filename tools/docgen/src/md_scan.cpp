#include "md_scan.h"

namespace docgen {

namespace {

bool MdScanIsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

StringView MdScanTrim(StringView s) {
    usize a = 0, e = s.Length();
    while (a < e && MdScanIsSpace(s[a])) ++a;
    while (e > a && (MdScanIsSpace(s[e - 1]) || s[e - 1] == '\r')) --e;
    return StringView(s.Data() + a, e - a);
}

bool EqualsLower(StringView a, StringView b) {
    if (a.Length() != b.Length()) return false;
    for (usize i = 0; i < a.Length(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

class MdScanScanner {
public:
    MdScanScanner(ArenaAllocator& arena, MdDoc& out) : arena_(arena), out_(out), slugs_(arena) {}

    void Run(StringView text) {
        u32 line_no = 0;
        usize pos = 0;
        bool in_fence = false;
        char fence_char = 0;
        usize fence_len = 0;
        usize fence_index = 0;
        usize body_start = 0;
        usize last_body_end = 0;
        // Consecutive prose lines form a paragraph that is scanned as one piece, so
        // code spans and [[references]] may wrap across lines.
        constexpr usize kNone = ~usize{0};
        usize para_start = kNone, para_end = 0;
        u32 para_line = 0;
        auto flush = [&]() {
            if (para_start != kNone) Inline(StringView(text.Data() + para_start, para_end - para_start), para_line);
            para_start = kNone;
        };
        while (pos < text.Length()) {
            usize eol = pos;
            while (eol < text.Length() && text[eol] != '\n') ++eol;
            StringView line(text.Data() + pos, eol - pos);
            ++line_no;
            if (in_fence) {
                if (IsClosingFence(line, fence_char, fence_len)) {
                    MdFence& f = out_.fences[fence_index];
                    f.closed = true;
                    f.end_line = line_no;
                    f.body = BodyView(text, body_start, last_body_end, f.body_line, line_no);
                    in_fence = false;
                } else {
                    last_body_end = eol;
                }
            } else {
                u32 indent = 0;
                char c = 0;
                usize n = 0;
                StringView info;
                if (IsOpeningFence(line, indent, c, n, info)) {
                    flush();
                    MdFence f;
                    f.line = line_no;
                    f.body_line = line_no + 1;
                    f.indent = indent;
                    f.info = info;
                    out_.fences.Push(f);
                    fence_index = out_.fences.Size() - 1;
                    in_fence = true;
                    fence_char = c;
                    fence_len = n;
                    body_start = eol < text.Length() ? eol + 1 : eol;
                    last_body_end = body_start > 0 ? body_start - 1 : 0;
                } else if (MdScanTrim(line).Empty()) {
                    flush();
                } else if (Structural(line, line_no)) {
                    flush();
                } else {
                    if (para_start == kNone) {
                        para_start = pos;
                        para_line = line_no;
                    }
                    para_end = eol;
                }
            }
            pos = eol + 1;
        }
        flush();
        if (in_fence) {
            MdFence& f = out_.fences[fence_index];
            f.end_line = line_no;
            f.body = BodyView(text, body_start, last_body_end, f.body_line, line_no + 1);
            out_.diagnostics.Push(Diagnostic{Severity::Error, f.line, StringView("unterminated code fence")});
        }
    }

private:
    static StringView BodyView(StringView text, usize start, usize end, u32 body_line, u32 close_line) {
        if (close_line <= body_line || end < start) return StringView(text.Data() + start, 0);
        return StringView(text.Data() + start, end - start);
    }

    static bool IsOpeningFence(StringView line, u32& indent, char& c, usize& n, StringView& info) {
        usize i = 0;
        while (i < line.Length() && MdScanIsSpace(line[i])) ++i;
        if (i >= line.Length() || (line[i] != '`' && line[i] != '~')) return false;
        c = line[i];
        usize j = i;
        while (j < line.Length() && line[j] == c) ++j;
        if (j - i < 3) return false;
        StringView rest = MdScanTrim(StringView(line.Data() + j, line.Length() - j));
        if (c == '`') { // an info string may not contain backticks (that is an inline code span)
            for (usize k = 0; k < rest.Length(); ++k) {
                if (rest[k] == '`') return false;
            }
        }
        indent = static_cast<u32>(i);
        n = j - i;
        info = rest;
        return true;
    }

    static bool IsClosingFence(StringView line, char c, usize len) {
        usize i = 0;
        while (i < line.Length() && MdScanIsSpace(line[i])) ++i;
        usize j = i;
        while (j < line.Length() && line[j] == c) ++j;
        if (j - i < len) return false;
        return MdScanTrim(StringView(line.Data() + j, line.Length() - j)).Empty();
    }

    // Headings and link reference definitions are single-line constructs. Returns true if `line` is one
    // (and has been handled), false for ordinary prose.
    bool Structural(StringView line, u32 line_no) {
        StringView t = MdScanTrim(line);
        // ATX heading
        usize hashes = 0;
        while (hashes < t.Length() && t[hashes] == '#') ++hashes;
        if (hashes >= 1 && hashes <= 6 && (hashes == t.Length() || MdScanIsSpace(t[hashes])) && LeadingSpaces(line) <= 3) {
            Heading(MdScanTrim(StringView(t.Data() + hashes, t.Length() - hashes)));
            Inline(t, line_no);
            return true;
        }
        // link reference definition: [label]: target
        if (LeadingSpaces(line) <= 3 && t[0] == '[' && t.Length() > 3) {
            usize close = 1;
            while (close < t.Length() && t[close] != ']') ++close;
            if (close + 1 < t.Length() && t[close + 1] == ':' && close > 1 && t[1] != '[' && t[1] != '^') {
                usize k = close + 2;
                while (k < t.Length() && MdScanIsSpace(t[k])) ++k;
                usize e = k;
                if (k < t.Length() && t[k] == '<') {
                    ++k;
                    e = k;
                    while (e < t.Length() && t[e] != '>') ++e;
                } else {
                    while (e < t.Length() && !MdScanIsSpace(t[e])) ++e;
                }
                if (e > k) out_.links.Push(MdLink{line_no, StringView(t.Data() + k, e - k), false});
                return true;
            }
        }
        return false;
    }

    static usize LeadingSpaces(StringView line) {
        usize n = 0;
        while (n < line.Length() && MdScanIsSpace(line[n])) ++n;
        return n;
    }

    void Heading(StringView text) {
        // trailing closing hashes
        usize e = text.Length();
        while (e > 0 && text[e - 1] == '#') --e;
        if (e < text.Length() && (e == 0 || MdScanIsSpace(text[e - 1]))) text = MdScanTrim(StringView(text.Data(), e));
        // explicit {#id}
        if (text.Length() >= 4 && text[text.Length() - 1] == '}') {
            usize open = text.Length();
            while (open > 0 && text[open - 1] != '{') --open;
            if (open > 0 && open < text.Length() - 1 && text[open] == '#') {
                out_.anchors.Push(StringView(text.Data() + open + 1, text.Length() - open - 2));
                text = MdScanTrim(StringView(text.Data(), open - 1));
            }
        }
        StringView slug = HeadingSlug(text, arena_);
        usize seen = 0;
        for (usize i = 0; i < slugs_.Size(); ++i) {
            if (slugs_[i] == slug) ++seen;
        }
        slugs_.Push(slug);
        if (seen == 0) {
            out_.anchors.Push(slug);
        } else {
            engine_placeholder::StringBuilder<ArenaAllocator> sb(arena_);
            sb.Append(slug);
            sb.Append('-');
            sb.AppendUInt(seen);
            out_.anchors.Push(ArenaCat(arena_, sb.View(), StringView()));
        }
    }

    // Skips an inline code span starting at `i`; returns the index after it, or `i` if it is not a span.
    static usize CodeSpanEnd(StringView t, usize i) {
        usize n = 0;
        while (i + n < t.Length() && t[i + n] == '`') ++n;
        usize j = i + n;
        while (j < t.Length()) {
            if (t[j] != '`') {
                ++j;
                continue;
            }
            usize m = 0;
            while (j + m < t.Length() && t[j + m] == '`') ++m;
            if (m == n) return j + m;
            j += m;
        }
        return i;
    }

    void Inline(StringView t, u32 first_line) {
        usize counted = 0;
        u32 cur = first_line;
        auto LineAt = [&](usize at) {
            for (; counted < at && counted < t.Length(); ++counted) cur += t[counted] == '\n';
            return cur;
        };
        usize i = 0;
        while (i < t.Length()) {
            const char c = t[i];
            if (c == '\\') {
                i += 2;
                continue;
            }
            if (c == '`') {
                usize end = CodeSpanEnd(t, i);
                if (end != i) {
                    i = end;
                } else {
                    while (i < t.Length() && t[i] == '`') ++i;
                }
                continue;
            }
            if (c == '<' && Starts(t, i + 1, "a") && i + 2 < t.Length() && MdScanIsSpace(t[i + 2])) { // <a id="x"> / <a name="x">
                usize e = i;
                while (e < t.Length() && t[e] != '>') ++e;
                AnchorAttrs(StringView(t.Data() + i, e - i));
                i = e;
                continue;
            }
            if (c == '<' && (Starts(t, i + 1, "http://") || Starts(t, i + 1, "https://"))) {
                usize e = i + 1;
                while (e < t.Length() && t[e] != '>' && !MdScanIsSpace(t[e])) ++e;
                out_.links.Push(MdLink{LineAt(i), StringView(t.Data() + i + 1, e - i - 1), false});
                i = e;
                continue;
            }
            if (c == '[' && i + 1 < t.Length() && t[i + 1] == '[') {
                usize j = i + 2;
                while (j + 1 < t.Length() && !(t[j] == ']' && t[j + 1] == ']')) ++j;
                if (j + 1 < t.Length()) {
                    StringView target = MdScanTrim(StringView(t.Data() + i + 2, j - i - 2));
                    if (target.Empty()) {
                        out_.diagnostics.Push(Diagnostic{Severity::Error, LineAt(i), StringView("malformed reference: empty [[]]")});
                    } else {
                        out_.refs.Push(MdRef{LineAt(i), OneLine(target)});
                    }
                    i = j + 2;
                } else {
                    out_.diagnostics.Push(Diagnostic{Severity::Error, LineAt(i), StringView("malformed reference: [[ without ]]")});
                    i += 2;
                }
                continue;
            }
            if (c == ']' && i + 1 < t.Length() && t[i + 1] == '(') {
                const bool image = IsImage(t, i);
                usize k = i + 2;
                while (k < t.Length() && MdScanIsSpace(t[k])) ++k;
                usize s = k, e = k;
                if (k < t.Length() && t[k] == '<') {
                    s = ++k;
                    while (k < t.Length() && t[k] != '>') ++k;
                    e = k;
                } else {
                    int depth = 0;
                    while (k < t.Length() && !MdScanIsSpace(t[k])) {
                        if (t[k] == '(') ++depth;
                        if (t[k] == ')') {
                            if (depth == 0) break;
                            --depth;
                        }
                        ++k;
                    }
                    e = k;
                }
                out_.links.Push(MdLink{LineAt(i), StringView(t.Data() + s, e - s), image});
                i = k;
                continue;
            }
            ++i;
        }
    }

    // id="..." / name="..." attributes of an <a> tag (matched on a word boundary, so data-id is not one).
    void AnchorAttrs(StringView tag) {
        for (const char* key : {"id=\"", "name=\""}) {
            usize kl = 0;
            while (key[kl] != '\0') ++kl;
            for (usize k = 1; k + kl < tag.Length(); ++k) {
                if (!MdScanIsSpace(tag[k - 1]) || !Starts(tag, k, key)) continue;
                usize v = k + kl, ve = v;
                while (ve < tag.Length() && tag[ve] != '"') ++ve;
                out_.anchors.Push(StringView(tag.Data() + v, ve - v));
                break;
            }
        }
    }

    // A reference that wraps across lines: a line break (with its blanks) disappears next to `::`, `(`, `)` or
    // `,` and becomes one space elsewhere.
    StringView OneLine(StringView t) {
        bool plain = true;
        for (usize i = 0; i < t.Length(); ++i) plain = plain && t[i] != '\n' && t[i] != '\r';
        if (plain) return t;
        auto glue = [](char c) { return c == ':' || c == '(' || c == ')' || c == ',' || c == '<' || c == '>'; };
        engine_placeholder::StringBuilder<ArenaAllocator> sb(arena_);
        char prev = 0;
        for (usize i = 0; i < t.Length();) {
            if (!MdScanIsSpace(t[i])) {
                sb.Append(t[i]);
                prev = t[i++];
                continue;
            }
            bool newline = false;
            usize j = i;
            while (j < t.Length() && MdScanIsSpace(t[j])) newline = newline || t[j++] == '\n';
            if (!newline) {
                sb.Append(StringView(t.Data() + i, j - i));
            } else if (j < t.Length() && prev != 0 && !glue(prev) && !glue(t[j])) {
                sb.Append(' ');
            }
            i = j;
        }
        return sb.View();
    }

    static bool Starts(StringView t, usize at, const char* lit) {
        usize n = 0;
        while (lit[n] != '\0') ++n;
        if (at + n > t.Length()) return false;
        for (usize k = 0; k < n; ++k) {
            if (t[at + k] != lit[k]) return false;
        }
        return true;
    }

    // Is the `]` at `close` the end of `![alt]`?
    static bool IsImage(StringView t, usize close) {
        int depth = 0;
        for (usize k = close; k > 0; --k) {
            const char c = t[k - 1];
            if (c == ']') ++depth;
            if (c == '[') {
                if (depth == 0) return k >= 2 && t[k - 2] == '!';
                --depth;
            }
        }
        return false;
    }

    ArenaAllocator& arena_;
    MdDoc& out_;
    Vector<StringView, ArenaAllocator> slugs_;
};

} // namespace

bool MdDoc::HasAnchor(StringView name) const {
    for (usize i = 0; i < anchors.Size(); ++i) {
        if (anchors[i] == name) return true;
    }
    return false;
}

StringView HeadingSlug(StringView h, ArenaAllocator& arena) {
    char* buf = static_cast<char*>(arena.Alloc(h.Length() + 1, 1));
    ENGINE_ASSERT(buf != nullptr, "docgen: arena exhausted");
    usize n = 0;
    for (usize i = 0; i < h.Length(); ++i) {
        char c = h[i];
        if (c == '`' || c == '*') continue;
        if (c == '!' && i + 1 < h.Length() && h[i + 1] == '[') continue;
        if (c == '[') continue;
        if (c == ']' && i + 1 < h.Length() && h[i + 1] == '(') { // drop the link target
            usize e = i + 2;
            while (e < h.Length() && h[e] != ')') ++e;
            i = e;
            continue;
        }
        if (c == ']') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == ' ' || c == '\t') {
            buf[n++] = '-';
            continue;
        }
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || static_cast<engine_placeholder::byte>(c) >= 0x80;
        if (keep) buf[n++] = c;
    }
    buf[n] = '\0';
    return StringView(buf, n);
}

void ScanMarkdown(StringView text, ArenaAllocator& arena, MdDoc& out) {
    MdScanScanner s(arena, out);
    s.Run(text);
}

} // namespace docgen
