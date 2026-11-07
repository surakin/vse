#include "settings/config_text.h"

#include <cstdio>
#include <cstring>

#include "string/utf8.h"

namespace engine_placeholder::settings {

namespace {

enum class Kind : u8 { Blank, Comment, Section, Entry, Raw };

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

void AddProblem(ConfigProblem* list, u32* count, u32 line, u32 column, const char* message) {
    if (*count < kMaxConfigProblems) {
        ConfigProblem& p = list[*count];
        p.line = line;
        p.column = column;
        std::snprintf(p.message, sizeof(p.message), "%s", message);
    }
    ++*count;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Reads the value that starts at s[0]: sets *end to its length and returns an error message, or null. The text after the value must be
// blank or a comment.
const char* LexValue(StringView s, usize* end, usize* error_at) {
    *error_at = 0;
    if (s.Length() == 0) {
        *end = 0;
        return nullptr;
    }
    usize value_end = 0;
    if (s[0] == '#' || s[0] == ';') { // `a = # note`: an empty value and a comment (a value that starts with # or ; must be quoted)
        *end = 0;
        return nullptr;
    }
    if (s[0] == '"') {
        usize i = 1;
        bool closed = false;
        while (i < s.Length()) {
            if (s[i] == '\\') {
                if (i + 1 >= s.Length()) break;
                if (s[i + 1] == 'x') {
                    if (i + 3 >= s.Length() || HexVal(s[i + 2]) < 0 || HexVal(s[i + 3]) < 0) {
                        *error_at = i;
                        return "bad \\x escape in quoted text";
                    }
                    i += 4;
                    continue;
                }
                if (s[i + 1] != '"' && s[i + 1] != '\\' && s[i + 1] != 'n' && s[i + 1] != 't') {
                    *error_at = i;
                    return "unknown escape in quoted text";
                }
                i += 2;
                continue;
            }
            if (s[i] == '"') {
                closed = true;
                ++i;
                break;
            }
            ++i;
        }
        if (!closed) {
            *error_at = 0;
            return "unterminated quoted text";
        }
        value_end = i;
        usize j = value_end;
        while (j < s.Length() && IsBlank(s[j])) ++j;
        if (j < s.Length() && s[j] != '#' && s[j] != ';') {
            *error_at = j;
            return "unexpected text after the value";
        }
    } else {
        usize last = 0;
        usize i = 0;
        while (i < s.Length()) {
            if (IsBlank(s[i])) {
                usize j = i;
                while (j < s.Length() && IsBlank(s[j])) ++j;
                if (j < s.Length() && (s[j] == '#' || s[j] == ';')) break; // a trailing comment
                i = j;
                continue;
            }
            ++i;
            last = i;
        }
        value_end = last;
    }
    *end = value_end;
    return nullptr;
}

// A set of keys with the line each was last seen on, hashed so a large file parses in linear time.
class SeenKeys {
public:
    // Returns the index of `key`, or -1.
    i64 Find(const Key& key) const {
        if (table_.Empty()) return -1;
        const usize mask = table_.Size() - 1;
        for (usize i = key.Hash() & mask;; i = (i + 1) & mask) {
            if (table_[i] < 0) return -1;
            if (keys_[static_cast<usize>(table_[i])] == key) return table_[i];
        }
    }
    void Add(const Key& key, u32 line) {
        if ((keys_.Size() + 1) * 2 > table_.Size()) Grow();
        keys_.Push(key);
        lines_.Push(line);
        Place(keys_.Size() - 1);
    }
    u32& Line(i64 index) { return lines_[static_cast<usize>(index)]; }

private:
    void Place(usize index) {
        const usize mask = table_.Size() - 1;
        usize i = keys_[index].Hash() & mask;
        while (table_[i] >= 0) i = (i + 1) & mask;
        table_[i] = static_cast<i64>(index);
    }
    void Grow() {
        usize size = table_.Size() == 0 ? 16 : table_.Size() * 2;
        table_.Clear();
        table_.Resize(size);
        for (usize i = 0; i < size; ++i) table_[i] = -1;
        for (usize i = 0; i < keys_.Size(); ++i) Place(i);
    }
    Vector<Key> keys_;
    Vector<u32> lines_;
    Vector<i64> table_;
};

} // namespace

struct ConfigDocument::Line {
    String<HeapAllocator> raw = String<HeapAllocator>(HeapAllocator::Default());
    Kind kind = Kind::Blank;
    u8 eol = 0;       // 0 none (last line), 1 LF, 2 CRLF
    Key key;          // Entry: the full key; Section: the section name
    u32 value_begin = 0;
    u32 value_end = 0;
    bool in_section = false; // an Entry written below a [header] (not a top-level dotted key)
};

ConfigDocument::ConfigDocument() = default;
ConfigDocument::ConfigDocument(ConfigDocument&&) noexcept = default;
ConfigDocument& ConfigDocument::operator=(ConfigDocument&&) noexcept = default;
ConfigDocument::~ConfigDocument() = default;

void ConfigDocument::Parse(StringView text, ConfigReport* report) {
    ConfigReport scratch;
    ConfigReport* rep = report != nullptr ? report : &scratch;
    *rep = ConfigReport();
    lines_.Clear();
    bom_ = false;
    eol_ = 1;
    bool eol_seen = false;
    if (text.Length() >= 3 && static_cast<u8>(text[0]) == 0xEF && static_cast<u8>(text[1]) == 0xBB && static_cast<u8>(text[2]) == 0xBF) {
        bom_ = true;
        text = text.Substring(3);
    }
    Key section; // the current section (invalid = none)
    bool bad_section = false; // the last [header] could not be read: the entries below it cannot be placed
    SeenKeys seen;             // for the repeated-key warning
    usize pos = 0;
    u32 line_number = 0;
    while (pos < text.Length()) {
        ++line_number;
        usize eol_at = pos;
        while (eol_at < text.Length() && text[eol_at] != '\n') ++eol_at;
        const bool has_lf = eol_at < text.Length();
        usize content_end = eol_at;
        u8 eol = 0;
        if (has_lf) {
            eol = 1;
            if (content_end > pos && text[content_end - 1] == '\r') {
                --content_end;
                eol = 2;
            }
        }
        if (has_lf && !eol_seen) {
            eol_ = eol;
            eol_seen = true;
        }
        const StringView raw = text.Substring(pos, content_end - pos);
        pos = has_lf ? eol_at + 1 : eol_at;

        Line line;
        line.raw = String<HeapAllocator>(HeapAllocator::Default(), raw);
        line.eol = eol;
        auto fail = [&](u32 column, const char* message) {
            line.kind = Kind::Raw;
            AddProblem(rep->errors, &rep->error_count, line_number, column, message);
        };
        usize i = 0;
        while (i < raw.Length() && IsBlank(raw[i])) ++i;
        bool nul = false;
        for (usize k = 0; k < raw.Length(); ++k) nul = nul || raw[k] == '\0';
        if (raw.Length() > kMaxConfigLineBytes) {
            fail(static_cast<u32>(kMaxConfigLineBytes + 1), "line is longer than 4096 bytes");
        } else if (nul) {
            fail(1, "a NUL byte in the line");
        } else if (!IsValidUtf8(raw)) {
            fail(1, "the line is not valid UTF-8");
        } else if (i == raw.Length()) {
            line.kind = Kind::Blank;
        } else if (raw[i] == '#' || raw[i] == ';') {
            line.kind = Kind::Comment;
        } else if (raw[i] == '[') {
            usize close = i + 1;
            while (close < raw.Length() && raw[close] != ']') ++close;
            if (close >= raw.Length()) {
                fail(static_cast<u32>(i + 1), "unterminated section header");
                bad_section = true;
            } else {
                const StringView name = raw.Substring(i + 1, close - i - 1);
                Key parsed;
                usize j = close + 1;
                while (j < raw.Length() && IsBlank(raw[j])) ++j;
                if (Key::Parse(name, &parsed) != KeyError::None || parsed.Segments() > 2) {
                    fail(static_cast<u32>(i + 2), "invalid section name");
                    bad_section = true;
                } else if (j < raw.Length() && raw[j] != '#' && raw[j] != ';') {
                    fail(static_cast<u32>(j + 1), "unexpected text after the section header");
                    bad_section = true;
                } else {
                    line.kind = Kind::Section;
                    line.key = parsed;
                    section = parsed;
                    bad_section = false;
                }
            }
        } else {
            usize eq = i;
            while (eq < raw.Length() && raw[eq] != '=') ++eq;
            if (bad_section) {
                fail(static_cast<u32>(i + 1), "entry below a section header that could not be read");
            } else if (eq >= raw.Length()) {
                fail(static_cast<u32>(i + 1), "expected key = value");
            } else {
                usize name_end = eq;
                while (name_end > i && IsBlank(raw[name_end - 1])) --name_end;
                const StringView name = raw.Substring(i, name_end - i);
                usize vs = eq + 1;
                while (vs < raw.Length() && IsBlank(raw[vs])) ++vs;
                usize value_len = 0, error_at = 0;
                const char* value_error = LexValue(raw.Substring(vs), &value_len, &error_at);
                String<HeapAllocator> full = String<HeapAllocator>(HeapAllocator::Default());
                if (section.IsValid()) {
                    full.Append(section.View());
                    full.Append('.');
                }
                full.Append(name);
                Key key;
                if (name.Length() == 0 || (section.IsValid() && name.IndexOf('.') != name.Length()) || Key::Parse(full.View(), &key) != KeyError::None) {
                    fail(static_cast<u32>(i + 1), "invalid key");
                } else if (value_error != nullptr) {
                    fail(static_cast<u32>(vs + error_at + 1), value_error);
                } else {
                    line.kind = Kind::Entry;
                    line.key = key;
                    line.value_begin = static_cast<u32>(vs);
                    line.value_end = static_cast<u32>(vs + value_len);
                    line.in_section = section.IsValid();
                    const i64 earlier = seen.Find(key);
                    if (earlier >= 0) {
                        char message[kMaxProblemMessageBytes + 1];
                        std::snprintf(message, sizeof(message), "repeated key; the last one (this line) wins; first on line %u", static_cast<unsigned>(seen.Line(earlier)));
                        AddProblem(rep->warnings, &rep->warning_count, line_number, static_cast<u32>(i + 1), message);
                        seen.Line(earlier) = line_number;
                    } else {
                        seen.Add(key, line_number);
                    }
                }
            }
        }
        lines_.Push(static_cast<Line&&>(line));
    }
}

usize ConfigDocument::FindLast(const Key& key) const {
    for (usize i = lines_.Size(); i-- > 0;) {
        if (lines_[i].kind == Kind::Entry && lines_[i].key == key) return i;
    }
    return lines_.Size();
}

bool ConfigDocument::Find(StringView key_text, StringView* value) const {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return false;
    const usize at = FindLast(key);
    if (at == lines_.Size()) return false;
    const Line& l = lines_[at];
    *value = l.raw.View().Substring(l.value_begin, l.value_end - l.value_begin);
    return true;
}

u32 ConfigDocument::LineOf(StringView key_text) const {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return 0;
    const usize at = FindLast(key);
    return at == lines_.Size() ? 0 : static_cast<u32>(at + 1);
}

bool ConfigDocument::IsValidValue(StringView value) {
    if (value.Length() > 4000) return false;
    if (value.Length() != 0 && (IsBlank(value[0]) || IsBlank(value[value.Length() - 1]))) return false;
    for (usize i = 0; i < value.Length(); ++i) {
        if (value[i] == '\n' || value[i] == '\r' || value[i] == '\0') return false;
    }
    if (!IsValidUtf8(value)) return false;
    usize end = 0, error_at = 0;
    if (LexValue(value, &end, &error_at) != nullptr) return false;
    return end == value.Length();
}

void ConfigDocument::AppendLine(StringView text) {
    if (!lines_.Empty() && lines_[lines_.Size() - 1].eol == 0) lines_[lines_.Size() - 1].eol = eol_;
    Line l;
    l.raw = String<HeapAllocator>(HeapAllocator::Default(), text);
    l.eol = eol_;
    lines_.Push(static_cast<Line&&>(l));
}

bool ConfigDocument::Set(StringView key_text, StringView value) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None || !IsValidValue(value)) return false;
    const usize at = FindLast(key);
    if (at != lines_.Size()) {
        Line& l = lines_[at];
        String<HeapAllocator> raw(HeapAllocator::Default(), l.raw.View().Substring(0, l.value_begin));
        raw.Append(value);
        const StringView tail = l.raw.View().Substring(l.value_end);
        // A comment that followed an empty value (`a = # note`) needs a blank in front of it, or it would read as part of the value.
        if (l.value_begin == l.value_end && tail.Length() != 0 && (tail[0] == '#' || tail[0] == ';')) raw.Append(' ');
        raw.Append(tail);
        if (raw.Length() > kMaxConfigLineBytes) return false; // the parser would call the line too long on the next load
        l.value_end = static_cast<u32>(l.value_begin + value.Length());
        l.raw = static_cast<String<HeapAllocator>&&>(raw);
        return true;
    }
    const StringView wanted = key.Section();
    // The new line: the name alone inside a section, the whole key at the top.
    String<HeapAllocator> entry(HeapAllocator::Default(), wanted.Length() == 0 ? key.View() : key.Name());
    const u32 vb = static_cast<u32>(entry.Length() + 3);
    entry.Append(" = ");
    entry.Append(value);
    auto make = [&]() {
        Line l;
        l.raw = String<HeapAllocator>(HeapAllocator::Default(), entry.View());
        l.kind = Kind::Entry;
        l.in_section = wanted.Length() != 0;
        l.eol = eol_;
        l.key = key;
        l.value_begin = vb;
        l.value_end = static_cast<u32>(vb + value.Length());
        return l;
    };
    // After the last entry of the same section anywhere in the file.
    usize after = lines_.Size();
    for (usize i = 0; i < lines_.Size(); ++i) {
        // Only an entry written below a header of that section is a place for the new line: a top-level `renderer.width = 1` is not.
        if (lines_[i].kind == Kind::Entry && lines_[i].key.Section() == wanted && (lines_[i].in_section || wanted.Length() == 0)) after = i;
    }
    if (after == lines_.Size() && wanted.Length() != 0) {
        for (usize i = 0; i < lines_.Size(); ++i) {
            if (lines_[i].kind == Kind::Section && lines_[i].key.View() == wanted) {
                after = i;
                break;
            }
        }
    }
    if (after != lines_.Size()) {
        if (lines_[after].eol == 0) lines_[after].eol = eol_;
        lines_.Insert(after + 1, make());
        return true;
    }
    if (wanted.Length() == 0) { // a top-level key with none yet goes before the first section header
        usize first_section = lines_.Size();
        for (usize i = 0; i < lines_.Size(); ++i) {
            if (lines_[i].kind == Kind::Section) {
                first_section = i;
                break;
            }
        }
        if (first_section == lines_.Size()) {
            if (!lines_.Empty() && lines_[lines_.Size() - 1].eol == 0) lines_[lines_.Size() - 1].eol = eol_;
            lines_.Push(make());
        } else {
            usize at_index = first_section;
            // A comment block directly above the header describes the section: keep them together (unless the block starts the file,
            // where it is the file's own heading), then go before the blank lines that precede them.
            usize block = at_index;
            while (block > 0 && lines_[block - 1].kind == Kind::Comment) --block;
            if (block > 0) at_index = block;
            while (at_index > 0 && lines_[at_index - 1].kind == Kind::Blank) --at_index;
            lines_.Insert(at_index, make());
            if (at_index == first_section) { // nothing separated the header from the new line: add a blank line
                Line blank;
                blank.kind = Kind::Blank;
                blank.eol = eol_;
                lines_.Insert(at_index + 1, static_cast<Line&&>(blank));
            }
        }
        return true;
    }
    // A new section at the end, after a blank line.
    if (!lines_.Empty() && lines_[lines_.Size() - 1].kind != Kind::Blank) AppendLine(StringView());
    String<HeapAllocator> header(HeapAllocator::Default());
    header.Append('[');
    header.Append(wanted);
    header.Append(']');
    AppendLine(header.View());
    Key section_key;
    Key::Parse(wanted, &section_key);
    lines_[lines_.Size() - 1].kind = Kind::Section;
    lines_[lines_.Size() - 1].key = section_key;
    lines_.Push(make());
    return true;
}

bool ConfigDocument::Remove(StringView key_text) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return false;
    bool removed = false;
    for (usize i = lines_.Size(); i-- > 0;) {
        if (lines_[i].kind == Kind::Entry && lines_[i].key == key) {
            lines_.Remove(i);
            removed = true;
        }
    }
    return removed;
}

void ConfigDocument::RemoveUnreadable() {
    for (usize i = lines_.Size(); i-- > 0;) {
        if (lines_[i].kind == Kind::Raw) lines_.Remove(i);
    }
}

void ConfigDocument::Keys(StringView prefix, Vector<Key>* out) const {
    SeenKeys seen;
    for (usize i = 0; i < lines_.Size(); ++i) {
        if (lines_[i].kind != Kind::Entry) continue;
        const Key& k = lines_[i].key;
        const StringView v = k.View();
        const bool under = prefix.Length() == 0 || v == prefix || (v.Length() > prefix.Length() && v.StartsWith(prefix) && v[prefix.Length()] == '.');
        if (!under) continue;
        if (seen.Find(k) >= 0) continue;
        seen.Add(k, 0);
        out->Push(k);
    }
}

void ConfigDocument::Serialize(Vector<char>* out, bool skip_unreadable) const {
    usize total = bom_ ? 3 : 0;
    for (usize i = 0; i < lines_.Size(); ++i) total += lines_[i].raw.Length() + lines_[i].eol;
    out->Clear();
    out->Reserve(total);
    if (bom_) {
        out->Push(static_cast<char>(0xEF));
        out->Push(static_cast<char>(0xBB));
        out->Push(static_cast<char>(0xBF));
    }
    for (usize i = 0; i < lines_.Size(); ++i) {
        if (skip_unreadable && lines_[i].kind == Kind::Raw) continue;
        const StringView raw = lines_[i].raw.View();
        for (usize k = 0; k < raw.Length(); ++k) out->Push(raw[k]);
        if (lines_[i].eol == 2) out->Push('\r');
        if (lines_[i].eol != 0) out->Push('\n');
    }
}

} // namespace engine_placeholder::settings
