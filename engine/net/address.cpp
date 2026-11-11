#include "net/address.h"

namespace engine_placeholder::net {

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// The value of a hex digit, or -1.
int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsScopeChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit(c) || c == '_' || c == '-' || c == '.';
}

// Parses exactly "d.d.d.d" (each one to three digits, no leading zero, at most 255) from the whole of `text`.
bool ParseDottedV4(StringView text, u8 out[4]) {
    usize pos = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (pos >= text.Length() || text[pos] != '.') return false;
            ++pos;
        }
        const usize start = pos;
        u32 value = 0;
        while (pos < text.Length() && IsDigit(text[pos]) && pos - start < 3) {
            value = value * 10 + static_cast<u32>(text[pos] - '0');
            ++pos;
        }
        const usize digits = pos - start;
        if (digits == 0) return false;
        if (digits > 1 && text[start] == '0') return false;  // a leading zero would read as octal
        if (value > 255) return false;
        out[part] = static_cast<u8>(value);
    }
    return pos == text.Length();
}

// Parses the IPv6 text (no brackets, no scope) from the whole of `text` into sixteen bytes.
bool ParseV6Body(StringView text, u8 out[16]) {
    if (text.Empty() || text.Length() > 45) return false;
    u16 groups[8] = {};
    int count = 0;           // groups written
    int gap_at = -1;         // index in groups where "::" was seen
    usize pos = 0;
    if (text[0] == ':') {
        if (text.Length() < 2 || text[1] != ':') return false;  // a lone leading colon
        gap_at = 0;
        pos = 2;
    }
    while (pos < text.Length()) {
        // A dotted IPv4 tail: only as the last element, takes two groups.
        usize end = pos;
        while (end < text.Length() && text[end] != ':') ++end;
        StringView token = text.Substring(pos, end - pos);
        if (token.IndexOf('.') != token.Length()) {
            if (end != text.Length() || count > 6) return false;
            u8 v4[4];
            if (!ParseDottedV4(token, v4)) return false;
            groups[count++] = static_cast<u16>((v4[0] << 8) | v4[1]);
            groups[count++] = static_cast<u16>((v4[2] << 8) | v4[3]);
            pos = end;
            break;
        }
        if (token.Empty() || token.Length() > 4) return false;
        u32 value = 0;
        for (usize i = 0; i < token.Length(); ++i) {
            const int h = HexValue(token[i]);
            if (h < 0) return false;
            value = value * 16 + static_cast<u32>(h);
        }
        if (count >= 8) return false;
        groups[count++] = static_cast<u16>(value);
        pos = end;
        if (pos < text.Length()) {
            // text[pos] is ':'
            if (pos + 1 < text.Length() && text[pos + 1] == ':') {
                if (gap_at >= 0) return false;  // two "::"
                gap_at = count;
                pos += 2;
            } else {
                ++pos;
                if (pos >= text.Length()) return false;  // a trailing single colon
            }
        }
    }
    if (pos != text.Length()) return false;
    if (gap_at < 0) {
        if (count != 8) return false;
    } else {
        // "::" stands for at least one group of zeros.
        if (count > 7) return false;
        const int zeros = 8 - count;
        for (int i = count - 1; i >= gap_at; --i) groups[i + zeros] = groups[i];
        for (int i = gap_at; i < gap_at + zeros; ++i) groups[i] = 0;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 2] = static_cast<u8>(groups[i] >> 8);
        out[i * 2 + 1] = static_cast<u8>(groups[i] & 0xff);
    }
    return true;
}

void AppendChar(AddressText& t, char c) {
    if (t.length < AddressText::kCapacity) {
        t.chars[t.length++] = c;
    }
}

void AppendDecimal(AddressText& t, u32 v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0) AppendChar(t, digits[--n]);
}

void AppendHexGroup(AddressText& t, u32 v) {
    static const char kHex[] = "0123456789abcdef";
    bool started = false;
    for (int shift = 12; shift >= 0; shift -= 4) {
        const u32 nibble = (v >> shift) & 0xf;
        if (nibble != 0 || started || shift == 0) {
            AppendChar(t, kHex[nibble]);
            started = true;
        }
    }
}

}  // namespace

Address Address::V4(u8 a, u8 b, u8 c, u8 d) {
    Address r;
    r.family_ = AddressFamily::V4;
    r.bytes_[0] = a;
    r.bytes_[1] = b;
    r.bytes_[2] = c;
    r.bytes_[3] = d;
    return r;
}

Address Address::V6(const Array<u8, 16>& bytes, u32 scope_id) {
    Address r;
    r.family_ = AddressFamily::V6;
    r.bytes_ = bytes;
    r.scope_id_ = scope_id;
    return r;
}

Address Address::Any(AddressFamily family) {
    Address r;
    r.family_ = family;
    return r;
}

Address Address::Loopback(AddressFamily family) {
    if (family == AddressFamily::V4) return V4(127, 0, 0, 1);
    Address r;
    r.family_ = family;
    if (family == AddressFamily::V6) r.bytes_[15] = 1;
    return r;
}

Status Address::Parse(StringView text, Address* out) {
    if (text.Empty() || text.Length() > 64) return Status::Of(ErrorCode::Invalid);
    // One bracket pair around the whole address.
    if (text[0] == '[') {
        if (text[text.Length() - 1] != ']' || text.Length() < 3) return Status::Of(ErrorCode::Invalid);
        text = text.Substring(1, text.Length() - 2);
        if (text.IndexOf(':') == text.Length()) return Status::Of(ErrorCode::Invalid);  // brackets hold IPv6 only
    }
    Address r;
    if (text.IndexOf(':') == text.Length()) {
        u8 v4[4];
        if (!ParseDottedV4(text, v4)) return Status::Of(ErrorCode::Invalid);
        r = V4(v4[0], v4[1], v4[2], v4[3]);
    } else {
        StringView body = text;
        StringView scope;
        const usize pct = text.IndexOf('%');
        const bool has_scope = pct != text.Length();
        if (has_scope) {
            body = text.Substring(0, pct);
            scope = text.Substring(pct + 1);
        }
        u8 raw[16];
        if (!ParseV6Body(body, raw)) return Status::Of(ErrorCode::Invalid);
        Array<u8, 16> bytes{};
        for (int i = 0; i < 16; ++i) bytes[static_cast<usize>(i)] = raw[i];
        r = V6(bytes);
        if (has_scope) {
            // All digits and within u32 (at most 15 digits, so u64 cannot overflow): an interface index; anything else a name.
            bool numeric = !scope.Empty() && scope.Length() <= kMaxScopeNameLength;
            u64 id = 0;
            for (usize i = 0; i < scope.Length() && numeric; ++i) {
                numeric = IsDigit(scope[i]);
                id = id * 10 + static_cast<u64>(scope[i] - '0');
            }
            if (numeric && id <= 0xffffffffULL) {
                r.scope_id_ = static_cast<u32>(id);
            } else {
                if (!r.WithScopeName(scope, &r)) return Status::Of(ErrorCode::Invalid);
            }
        }
    }
    *out = r;
    return Status::Success();
}

Address Address::WithScopeId(u32 scope_id) const {
    Address r = *this;
    if (r.family_ != AddressFamily::V6) return r;
    r.scope_id_ = scope_id;
    r.scope_name_length_ = 0;
    r.scope_name_ = Array<char, kMaxScopeNameLength + 1>{};
    return r;
}

bool Address::WithScopeName(StringView name, Address* out) const {
    Address r = *this;
    r.scope_id_ = 0;
    r.scope_name_length_ = 0;
    r.scope_name_ = Array<char, kMaxScopeNameLength + 1>{};
    bool ok = family_ == AddressFamily::V6 && !name.Empty() && name.Length() <= kMaxScopeNameLength;
    for (usize i = 0; ok && i < name.Length(); ++i) ok = IsScopeChar(name[i]);
    if (ok) {
        for (usize i = 0; i < name.Length(); ++i) r.scope_name_[i] = name[i];
        r.scope_name_length_ = static_cast<u8>(name.Length());
    }
    *out = r;
    return ok;
}

bool Address::IsV4Mapped() const {
    if (family_ != AddressFamily::V6) return false;
    for (usize i = 0; i < 10; ++i) {
        if (bytes_[i] != 0) return false;
    }
    return bytes_[10] == 0xff && bytes_[11] == 0xff;
}

Address Address::Unmapped() const {
    if (!IsV4Mapped()) return *this;
    return V4(bytes_[12], bytes_[13], bytes_[14], bytes_[15]);
}

Address Address::Mapped() const {
    if (family_ != AddressFamily::V4) return *this;
    Array<u8, 16> b{};
    b[10] = 0xff;
    b[11] = 0xff;
    for (usize i = 0; i < 4; ++i) b[12 + i] = bytes_[i];
    return V6(b);
}

bool Address::IsAny() const {
    const usize n = family_ == AddressFamily::V4 ? 4 : 16;
    if (family_ == AddressFamily::None) return false;
    for (usize i = 0; i < n; ++i) {
        if (bytes_[i] != 0) return false;
    }
    return true;
}

bool Address::IsLoopback() const {
    if (family_ == AddressFamily::V4) return bytes_[0] == 127;
    if (family_ != AddressFamily::V6) return false;
    if (IsV4Mapped()) return Unmapped().IsLoopback();
    for (usize i = 0; i < 15; ++i) {
        if (bytes_[i] != 0) return false;
    }
    return bytes_[15] == 1;
}

bool Address::IsLinkLocal() const {
    if (family_ == AddressFamily::V4) return bytes_[0] == 169 && bytes_[1] == 254;
    if (family_ != AddressFamily::V6) return false;
    if (IsV4Mapped()) return Unmapped().IsLinkLocal();
    return bytes_[0] == 0xfe && (bytes_[1] & 0xc0) == 0x80;
}

bool Address::IsPrivate() const {
    if (family_ == AddressFamily::V4) {
        return bytes_[0] == 10 || (bytes_[0] == 172 && (bytes_[1] & 0xf0) == 16) || (bytes_[0] == 192 && bytes_[1] == 168);
    }
    if (family_ != AddressFamily::V6) return false;
    if (IsV4Mapped()) return Unmapped().IsPrivate();
    return (bytes_[0] & 0xfe) == 0xfc;
}

bool Address::IsMulticast() const {
    if (family_ == AddressFamily::V4) return (bytes_[0] & 0xf0) == 224;
    if (family_ != AddressFamily::V6) return false;
    if (IsV4Mapped()) return Unmapped().IsMulticast();
    return bytes_[0] == 0xff;
}

AddressText Address::Format() const {
    AddressText t;
    if (family_ == AddressFamily::V4) {
        for (usize i = 0; i < 4; ++i) {
            if (i > 0) AppendChar(t, '.');
            AppendDecimal(t, bytes_[i]);
        }
    } else if (family_ == AddressFamily::V6) {
        if (IsV4Mapped()) {
            const char prefix[] = "::ffff:";
            for (usize i = 0; prefix[i] != 0; ++i) AppendChar(t, prefix[i]);
            for (usize i = 12; i < 16; ++i) {
                if (i > 12) AppendChar(t, '.');
                AppendDecimal(t, bytes_[i]);
            }
        } else {
            u32 groups[8];
            for (usize i = 0; i < 8; ++i) groups[i] = (static_cast<u32>(bytes_[i * 2]) << 8) | bytes_[i * 2 + 1];
            // The longest run of two or more zero groups; the first on a tie.
            int best_start = -1, best_len = 0;
            for (int i = 0; i < 8;) {
                if (groups[i] != 0) {
                    ++i;
                    continue;
                }
                int j = i;
                while (j < 8 && groups[j] == 0) ++j;
                if (j - i > best_len) {
                    best_len = j - i;
                    best_start = i;
                }
                i = j;
            }
            if (best_len < 2) best_start = -1;
            for (int i = 0; i < 8; ++i) {
                if (i == best_start) {
                    AppendChar(t, ':');
                    AppendChar(t, ':');
                    i += best_len - 1;
                    continue;
                }
                if (i > 0 && i != best_start + best_len) AppendChar(t, ':');
                AppendHexGroup(t, groups[i]);
            }
        }
        if (scope_name_length_ != 0) {
            AppendChar(t, '%');
            for (usize i = 0; i < scope_name_length_; ++i) AppendChar(t, scope_name_[i]);
        } else if (scope_id_ != 0) {
            AppendChar(t, '%');
            AppendDecimal(t, scope_id_);
        }
    }
    t.chars[t.length] = 0;
    return t;
}

usize Address::HashValue() const {
    u64 h = static_cast<u64>(family_) + 1;
    for (usize i = 0; i < 16; ++i) h = h * 0x100000001b3ULL + bytes_[i];
    h = h * 0x100000001b3ULL + scope_id_;
    for (usize i = 0; i < scope_name_length_; ++i) h = h * 0x100000001b3ULL + static_cast<u8>(scope_name_[i]);
    return detail::MixHash64(h);
}

bool Address::operator==(const Address& other) const {
    return (*this <=> other) == Ordering::equal;
}

Ordering Address::operator<=>(const Address& other) const {
    if (family_ != other.family_) return family_ < other.family_ ? Ordering::less : Ordering::greater;
    for (usize i = 0; i < 16; ++i) {
        if (bytes_[i] != other.bytes_[i]) return bytes_[i] < other.bytes_[i] ? Ordering::less : Ordering::greater;
    }
    if (scope_id_ != other.scope_id_) return scope_id_ < other.scope_id_ ? Ordering::less : Ordering::greater;
    return ScopeName().Compare(other.ScopeName()) <=> 0;
}

}  // namespace engine_placeholder::net
