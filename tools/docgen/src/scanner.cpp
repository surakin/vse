#include "scanner.h"

#include <cstring>

namespace docgen {

namespace {

using engine_placeholder::i32;

bool IsIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (static_cast<engine_placeholder::byte>(c) >= 0x80);
}
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsIdentChar(char c) { return IsIdentStart(c) || IsDigit(c); }
bool IsHSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

class Scanner {
public:
    Scanner(StringView source, ArenaAllocator& arena, ScanResult& out)
        : src_(source.Data()), n_(source.Length()), arena_(arena), out_(out) {}

    void Run() {
        // Skip a UTF-8 byte-order mark.
        if (n_ >= 3 && static_cast<engine_placeholder::byte>(src_[0]) == 0xEF && static_cast<engine_placeholder::byte>(src_[1]) == 0xBB &&
            static_cast<engine_placeholder::byte>(src_[2]) == 0xBF) {
            pos_ = 3;
            line_start_ = 3;
        }
        while (pos_ < n_) {
            const char c = src_[pos_];
            if (c == '\n') {
                EndLine();
                ++pos_;
                continue;
            }
            if (IsHSpace(c)) {
                ++pos_;
                continue;
            }
            if (c == '\\' && pos_ + 1 < n_ && (src_[pos_ + 1] == '\n' ||
                                               (src_[pos_ + 1] == '\r' && pos_ + 2 < n_ && src_[pos_ + 2] == '\n'))) {
                // Line continuation outside a directive: whitespace.
                pos_ += src_[pos_ + 1] == '\n' ? 2 : 3;
                NewlineInside(pos_ - 1);
                continue;
            }
            if (c == '#' && !line_has_content_) {
                line_has_content_ = true;
                Directive();
                continue;
            }
            if (c == '/' && pos_ + 1 < n_ && src_[pos_ + 1] == '/') {
                LineComment();
                continue;
            }
            if (c == '/' && pos_ + 1 < n_ && src_[pos_ + 1] == '*') {
                BlockComment();
                continue;
            }
            line_has_content_ = true;
            if (IsIdentStart(c)) {
                IdentifierOrLiteral();
            } else if (IsDigit(c) || (c == '.' && pos_ + 1 < n_ && IsDigit(src_[pos_ + 1]))) {
                Number();
            } else if (c == '"') {
                StringLiteral(pos_, 0);
            } else if (c == '\'') {
                CharLiteral(pos_, 0);
            } else {
                Punct();
            }
        }
        // End of input: an unattached doc comment is detached.
        if (pending_ >= 0) {
            Report(Severity::Warning, out_.docs[static_cast<usize>(pending_)].first_line, "detached doc comment");
            pending_ = -1;
        }
    }

private:
    // ---- helpers ---------------------------------------------------------

    void Report(Severity sev, u32 line, const char* message) {
        Diagnostic d;
        d.severity = sev;
        d.line = line;
        d.message = StringView(message, std::strlen(message));
        out_.diagnostics.Push(d);
    }

    char At(usize i) const { return i < n_ ? src_[i] : '\0'; }

    void EndLine() {
        if (!line_has_content_ && pending_ >= 0) blank_since_doc_ = true;
        ++line_;
        line_start_ = pos_ + 1;
        line_has_content_ = false;
        last_token_on_line_ = -1;
    }

    // A newline consumed inside a construct (block comment, raw string,
    // continuation): `nl` is the index of the '\n'.
    void NewlineInside(usize nl) {
        ++line_;
        line_start_ = nl + 1;
        last_token_on_line_ = -1;
    }

    u32 Column(usize p) const { return static_cast<u32>(p - line_start_ + 1); }

    void Emit(TokenKind kind, usize begin, usize end, u32 line, u32 column) {
        Token t;
        t.kind = kind;
        t.text = StringView(src_ + begin, end - begin);
        t.line = line;
        t.column = column;
        t.doc = -1;
        if (pending_ >= 0) {
            if (blank_since_doc_) {
                Report(Severity::Warning, out_.docs[static_cast<usize>(pending_)].first_line, "detached doc comment");
            } else {
                t.doc = pending_;
                out_.docs[static_cast<usize>(pending_)].token = static_cast<i32>(out_.tokens.Size());
            }
            pending_ = -1;
            blank_since_doc_ = false;
        }
        last_token_on_line_ = static_cast<i32>(out_.tokens.Size());
        seen_token_ = true;
        out_.tokens.Push(t);
    }

    // ---- comments -------------------------------------------------------------

    // Returns the end (exclusive, before the '\n') of the line starting at p,
    // following backslash-newline continuations is NOT done here.
    usize LineEnd(usize p) const {
        while (p < n_ && src_[p] != '\n') ++p;
        return p;
    }

    // Copies [begin, end) to the arena minus a trailing '\r' / spaces.
    void TrimmedRange(usize& begin, usize& end) const {
        while (end > begin && (src_[end - 1] == '\r' || src_[end - 1] == ' ' || src_[end - 1] == '\t')) --end;
        (void)begin;
    }

    void LineComment() {
        const usize start = pos_;
        const u32 start_line = line_;
        const bool code_before = last_token_on_line_ >= 0;
        // Marker classification: `///` (not `////`), `///<`, `//!`.
        enum { kPlain, kDecl, kTrailing, kFile } kind = kPlain;
        if (At(start + 2) == '/' && At(start + 3) != '/') {
            kind = At(start + 3) == '<' ? kTrailing : kDecl;
        } else if (At(start + 2) == '!') {
            kind = kFile;
        }

        if (kind == kPlain) {
            // Ordinary comment; a trailing backslash continues it.
            pos_ = SkipLineCommentBody(start);
            line_has_content_ = true;
            return;
        }

        if (kind == kTrailing) {
            if (!code_before) {
                Report(Severity::Warning, start_line, "trailing doc comment without a declaration on the same line");
                pos_ = LineEnd(start);
                line_has_content_ = true;
                return;
            }
            usize b = start + 4;
            if (At(b) == ' ') ++b;
            usize e = LineEnd(start);
            TrimmedRange(b, e);
            DocComment d;
            d.kind = DocKind::Trailing;
            d.text = Copy(src_ + b, e - b);
            d.first_line = d.last_line = start_line;
            d.token = last_token_on_line_;
            out_.docs.Push(d);
            pos_ = LineEnd(start);
            return;
        }

        if (code_before && kind == kDecl) {
            Report(Severity::Warning, start_line, "trailing doc comment must use ///<");
            pos_ = LineEnd(start);
            return;
        }

        // A run of consecutive `///` (or `//!`) lines forms one comment.
        const char marker = kind == kFile ? '!' : '/';
        // Orphan check: a previous unattached comment followed by a new
        // run is detached.
        if (kind == kDecl && pending_ >= 0) {
            Report(Severity::Warning, out_.docs[static_cast<usize>(pending_)].first_line, "detached doc comment");
            pending_ = -1;
        }
        if (kind == kFile && seen_token_) {
            Report(Severity::Warning, start_line, "misplaced file overview");
        }

        // Pass 1: size. Pass 2: copy.
        usize total = 0;
        u32 lines = 0;
        usize p = start;
        u32 last_line = start_line;
        for (;;) {
            usize b = p + 3;
            if (At(b) == ' ') ++b;
            usize e = LineEnd(p);
            TrimmedRange(b, e);
            total += (e - b) + 1;
            ++lines;
            // Next line continues the run if (after indentation) it starts
            // with the same marker.
            usize q = LineEnd(p);
            if (q >= n_) {
                p = q;
                break;
            }
            ++q; // past '\n'
            usize r = q;
            while (r < n_ && (src_[r] == ' ' || src_[r] == '\t')) ++r;
            bool continues = At(r) == '/' && At(r + 1) == '/' &&
                             (marker == '!' ? At(r + 2) == '!' : (At(r + 2) == '/' && At(r + 3) != '/' && At(r + 3) != '<'));
            if (!continues) {
                p = q - 1; // the '\n' of the last doc line
                break;
            }
            p = r;
            ++last_line;
        }
        char* buf = static_cast<char*>(arena_.Alloc(total + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen scanner: arena exhausted");
        usize w = 0;
        usize pp = start;
        for (u32 i = 0; i < lines; ++i) {
            usize b = pp + 3;
            if (At(b) == ' ') ++b;
            usize e = LineEnd(pp);
            TrimmedRange(b, e);
            CopyBytes(buf + w, src_ + b, e - b);
            w += e - b;
            if (i + 1 < lines) {
                buf[w++] = '\n';
                usize q = LineEnd(pp) + 1;
                while (q < n_ && (src_[q] == ' ' || src_[q] == '\t')) ++q;
                pp = q;
            }
        }
        DocComment d;
        d.kind = kind == kFile ? DocKind::File : DocKind::Declaration;
        d.text = StringView(buf, w);
        d.first_line = start_line;
        d.last_line = last_line;
        d.token = -1;
        out_.docs.Push(d);
        if (kind == kDecl) {
            pending_ = static_cast<i32>(out_.docs.Size()) - 1;
            blank_since_doc_ = false;
        }
        // Advance to the '\n' ending the run; the main loop handles it.
        pos_ = p;
        line_ = last_line;
        // line_start_ for column bookkeeping: find the start of the last line.
        usize ls = pos_;
        while (ls > 0 && src_[ls - 1] != '\n') --ls;
        line_start_ = ls;
        line_has_content_ = true;
        last_token_on_line_ = -1;
    }

    char* CopyBuf(usize len) {
        char* buf = static_cast<char*>(arena_.Alloc(len + 1, 1));
        ENGINE_ASSERT(buf != nullptr, "docgen scanner: arena exhausted");
        return buf;
    }

    StringView Copy(const char* s, usize len) {
        char* buf = CopyBuf(len);
        CopyBytes(buf, s, len);
        return StringView(buf, len);
    }

    // Skips a plain `//` comment starting at `start`, honoring backslash
    // continuations (counted into line_); returns the position of the
    // terminating '\n' (or end of input).
    usize SkipLineCommentBody(usize start) {
        usize p = start;
        for (;;) {
            p = LineEnd(p);
            usize back = p;
            if (back > start && src_[back - 1] == '\r') --back;
            if (p < n_ && back > start && src_[back - 1] == '\\') {
                NewlineInside(p);
                ++p; // continue on the next line
                continue;
            }
            return p;
        }
    }

    void BlockComment() {
        const u32 start_line = line_;
        usize p = pos_ + 2;
        for (;;) {
            if (p >= n_) {
                Report(Severity::Error, start_line, "unterminated comment");
                pos_ = n_;
                return;
            }
            if (src_[p] == '*' && At(p + 1) == '/') {
                pos_ = p + 2;
                line_has_content_ = true;
                return;
            }
            if (src_[p] == '\n') NewlineInside(p);
            ++p;
        }
    }

    // ---- tokens ------------------------------------------------------------------

    void IdentifierOrLiteral() {
        const usize b = pos_;
        const u32 line = line_, col = Column(b);
        usize e = b;
        while (e < n_ && IsIdentChar(src_[e])) ++e;
        const usize len = e - b;
        // String/char literal prefixes: u8 u U L R u8R uR UR LR.
        auto is = [&](const char* s) { return std::strlen(s) == len && std::memcmp(src_ + b, s, len) == 0; };
        const bool str_prefix = is("u8") || is("u") || is("U") || is("L");
        const bool raw_prefix = is("R") || is("u8R") || is("uR") || is("UR") || is("LR");
        if (e < n_ && src_[e] == '"' && (str_prefix || raw_prefix)) {
            if (raw_prefix) {
                RawString(b, e, line, col);
            } else {
                StringLiteral(b, e - b);
            }
            return;
        }
        if (e < n_ && src_[e] == '\'' && str_prefix) {
            CharLiteral(b, e - b);
            return;
        }
        pos_ = e;
        Emit(TokenKind::Identifier, b, e, line, col);
    }

    void Number() {
        const usize b = pos_;
        const u32 line = line_, col = Column(b);
        usize e = b;
        const bool hex = b + 1 < n_ && src_[b] == '0' && (src_[b + 1] == 'x' || src_[b + 1] == 'X');
        while (e < n_) {
            const char c = src_[e];
            if (IsIdentChar(c) || c == '.') {
                ++e;
            } else if (c == '\'' && e > b && IsIdentChar(At(e + 1)) && IsIdentChar(src_[e - 1])) {
                ++e; // digit separator
            } else if ((c == '+' || c == '-') && e > b &&
                       (hex ? (src_[e - 1] == 'p' || src_[e - 1] == 'P') : (src_[e - 1] == 'e' || src_[e - 1] == 'E'))) {
                ++e;
            } else {
                break;
            }
        }
        pos_ = e;
        Emit(TokenKind::Number, b, e, line, col);
    }

    // Opening quote at pos_; `prefix_len` characters of encoding prefix
    // precede it (starting at `begin`).
    void StringLiteral(usize begin, usize prefix_len) {
        const u32 line = line_, col = Column(begin);
        usize p = begin + prefix_len + 1;
        for (;;) {
            if (p >= n_ || src_[p] == '\n') {
                Report(Severity::Error, line, "unterminated string literal");
                pos_ = p; // resume at the end of the line
                return;
            }
            const char c = src_[p];
            if (c == '\\') {
                if (At(p + 1) == '\n') {
                    NewlineInside(p + 1);
                    p += 2;
                } else if (At(p + 1) == '\r' && At(p + 2) == '\n') {
                    NewlineInside(p + 2);
                    p += 3;
                } else {
                    p += 2;
                }
                continue;
            }
            if (c == '"') {
                pos_ = p + 1;
                break;
            }
            ++p;
        }
        Emit(TokenKind::StringLiteral, begin, pos_, line, col);
    }

    void CharLiteral(usize begin, usize prefix_len) {
        const u32 line = line_, col = Column(begin);
        usize p = begin + prefix_len + 1;
        for (;;) {
            if (p >= n_ || src_[p] == '\n') {
                Report(Severity::Error, line, "unterminated character literal");
                pos_ = p;
                return;
            }
            const char c = src_[p];
            if (c == '\\') {
                if (At(p + 1) == '\n') {
                    NewlineInside(p + 1);
                    p += 2;
                } else if (At(p + 1) == '\r' && At(p + 2) == '\n') {
                    NewlineInside(p + 2);
                    p += 3;
                } else {
                    p += 2;
                }
                continue;
            }
            if (c == '\'') {
                pos_ = p + 1;
                break;
            }
            ++p;
        }
        Emit(TokenKind::CharLiteral, begin, pos_, line, col);
    }

    // `begin` .. `quote` is the prefix (R, u8R, ...); src_[quote] == '"'.
    void RawString(usize begin, usize quote, u32 line, u32 col) {
        usize p = quote + 1;
        const usize delim_begin = p;
        while (p < n_ && src_[p] != '(' && p - delim_begin < 16 && src_[p] != '\n' && src_[p] != ')' &&
               src_[p] != '\\' && src_[p] != ' ' && src_[p] != '"') {
            ++p;
        }
        if (p >= n_ || src_[p] != '(') {
            Report(Severity::Error, line, "unterminated raw string literal");
            pos_ = LineEnd(quote);
            return;
        }
        const usize delim_len = p - delim_begin;
        ++p; // past '('
        for (;;) {
            if (p >= n_) {
                Report(Severity::Error, line, "unterminated raw string literal");
                pos_ = n_;
                return;
            }
            if (src_[p] == ')' && p + 1 + delim_len < n_ &&
                std::memcmp(src_ + p + 1, src_ + delim_begin, delim_len) == 0 && src_[p + 1 + delim_len] == '"') {
                p += delim_len + 2;
                break;
            }
            if (src_[p] == '\n') NewlineInside(p);
            ++p;
        }
        pos_ = p;
        Emit(TokenKind::StringLiteral, begin, pos_, line, col);
    }

    void Punct() {
        const usize b = pos_;
        const u32 line = line_, col = Column(b);
        const char c = src_[b], d = At(b + 1), e3 = At(b + 2);
        usize len = 1;
        if (c == '<' && d == '=' && e3 == '>') len = 3;
        else if (c == '.' && d == '.' && e3 == '.') len = 3;
        else if (c == ':' && d == ':') len = 2;
        else if (c == '-' && d == '>') len = 2;
        else if (c == '&' && d == '&') len = 2;
        else if (c == '|' && d == '|') len = 2;
        else if ((c == '=' || c == '!' || c == '<' || c == '>') && d == '=') len = 2;
        else if (c == '+' && d == '+') len = 2;
        else if (c == '-' && d == '-') len = 2;
        else if (c == '<' && d == '<') len = 2;
        pos_ = b + len;
        Emit(TokenKind::Punct, b, pos_, line, col);
    }

    // ---- preprocessor ------------------------------------------------------------------

    // Scans the remainder of a directive starting at `p`. Understands
    // backslash-newline continuations, string/char literals and both comment
    // forms, so a `//` or `/* */` inside a directive never leaks into the
    // code part. Returns the end of the logical line (the '\n' or n_).
    //   code_end     end of the directive text before any comment (trailing
    //                whitespace excluded)
    //   line_comment start of a `//` comment, or n_ if none
    //   extra_lines  newlines consumed beyond the first line
    //   last_ls      start of the last physical line consumed (for columns)
    usize DirectiveBody(usize p, u32 base_line, u32& extra_lines, usize& code_end, usize& line_comment,
                        usize& last_ls) {
        extra_lines = 0;
        code_end = p;
        line_comment = n_;
        last_ls = line_start_;
        bool in_line_comment = false;
        usize i = p;
        while (i < n_) {
            const char c = src_[i];
            if (c == '\n') {
                usize back = i;
                if (back > p && src_[back - 1] == '\r') --back;
                if (back > p && src_[back - 1] == '\\') { // continuation
                    ++extra_lines;
                    last_ls = i + 1;
                    ++i;
                    continue;
                }
                return i;
            }
            if (in_line_comment) {
                ++i;
                continue;
            }
            if (c == '/' && At(i + 1) == '/') {
                in_line_comment = true;
                line_comment = i;
                i += 2;
                continue;
            }
            if (c == '/' && At(i + 1) == '*') {
                const u32 open_line = base_line + extra_lines;
                i += 2;
                for (;;) {
                    if (i >= n_) {
                        Report(Severity::Error, open_line, "unterminated comment");
                        return n_;
                    }
                    if (src_[i] == '*' && At(i + 1) == '/') {
                        i += 2;
                        break;
                    }
                    if (src_[i] == '\n') {
                        ++extra_lines;
                        last_ls = i + 1;
                    }
                    ++i;
                }
                continue;
            }
            if (c == '"' || (c == '\'' && !(i > p && IsIdentChar(src_[i - 1])))) {
                const char q = c;
                ++i;
                while (i < n_ && src_[i] != q && src_[i] != '\n') {
                    if (src_[i] == '\\' && i + 1 < n_ && src_[i + 1] != '\n' && src_[i + 1] != '\r') {
                        i += 2;
                    } else if (src_[i] == '\\') {
                        break; // backslash-newline: let the outer loop count the continuation
                    } else {
                        ++i;
                    }
                }
                if (i < n_ && src_[i] == q) ++i;
                code_end = i;
                continue;
            }
            if (!IsHSpace(c)) code_end = i + 1;
            ++i;
        }
        return n_;
    }

    void Directive() {
        const u32 dir_line = line_;
        usize p = pos_ + 1;
        while (p < n_ && IsHSpace(src_[p])) ++p;
        const usize name_b = p;
        while (p < n_ && IsIdentChar(src_[p])) ++p;
        const usize name_len = p - name_b;
        auto is = [&](const char* s) { return std::strlen(s) == name_len && std::memcmp(src_ + name_b, s, name_len) == 0; };

        u32 extra_lines = 0;
        usize code_end = p, line_comment = n_, last_ls = line_start_;
        const usize end = DirectiveBody(p, dir_line, extra_lines, code_end, line_comment, last_ls);

        usize resume = end;
        if (is("define")) {
            usize q = p;
            while (q < code_end && IsHSpace(src_[q])) ++q;
            if (q < code_end && IsIdentStart(src_[q])) {
                Emit(TokenKind::Define, q, code_end, dir_line, Column(q));
                // A single-line `#define X 1 ///< doc`: let the main loop
                // lex the comment so a trailing doc attaches to the macro.
                if (extra_lines == 0 && line_comment < end) resume = line_comment;
            }
        } else if (is("if")) {
            // `#if 0` skips to the matching #else/#elif/#endif.
            usize q = p;
            while (q < code_end && IsHSpace(src_[q])) ++q;
            if (code_end - q == 1 && src_[q] == '0') {
                pos_ = end;
                line_ += extra_lines;
                if (extra_lines > 0) line_start_ = last_ls;
                SkipInactive();
                return;
            }
        }
        pos_ = resume;
        if (resume == end) {
            line_ += extra_lines;
            if (extra_lines > 0) {
                line_start_ = last_ls;
                last_token_on_line_ = -1;
            }
        }
        // (the directive line itself counts as content so a doc comment
        // directly above a directive is not "blank-separated")
    }

    // Called with pos_ at the end of an `#if 0` logical line. Skips lines
    // until the matching #else/#elif (resume with that branch) or #endif.
    void SkipInactive() {
        int depth = 1;
        while (pos_ < n_) {
            if (src_[pos_] != '\n') {
                ++pos_;
                continue;
            }
            ++pos_;
            ++line_;
            line_start_ = pos_;
            usize p = pos_;
            while (p < n_ && IsHSpace(src_[p])) ++p;
            if (At(p) == '#') {
                const u32 dir_line = line_;
                ++p;
                while (p < n_ && IsHSpace(src_[p])) ++p;
                const usize nb = p;
                while (p < n_ && IsIdentChar(src_[p])) ++p;
                const usize nl = p - nb;
                auto is = [&](const char* s) { return std::strlen(s) == nl && std::memcmp(src_ + nb, s, nl) == 0; };
                u32 extra = 0;
                usize code_end = p, lc = n_, ls = line_start_;
                const usize end = DirectiveBody(p, dir_line, extra, code_end, lc, ls);
                pos_ = end;
                line_ += extra;
                if (extra > 0) line_start_ = ls;
                if (is("if") || is("ifdef") || is("ifndef")) {
                    ++depth;
                } else if (is("endif")) {
                    if (--depth == 0) {
                        line_has_content_ = true;
                        return;
                    }
                } else if ((is("else") || is("elif") || is("elifdef") || is("elifndef")) && depth == 1) {
                    line_has_content_ = true;
                    return;
                }
            } else {
                pos_ = LineEnd(pos_);
            }
        }
    }

    // ---- state ---------------------------------------------------------------------------

    const char* src_;
    usize n_;
    ArenaAllocator& arena_;
    ScanResult& out_;

    usize pos_ = 0;
    u32 line_ = 1;
    usize line_start_ = 0;
    bool line_has_content_ = false;
    i32 last_token_on_line_ = -1;
    bool seen_token_ = false;

    i32 pending_ = -1;          // index of an unattached `///` comment
    bool blank_since_doc_ = false;
};

} // namespace

void Scan(StringView source, ArenaAllocator& arena, ScanResult& out) {
    Scanner s(source, arena, out);
    s.Run();
}

} // namespace docgen
