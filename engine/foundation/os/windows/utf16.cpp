#include "os/windows/utf16.h"

// No Windows header: the conversion is plain code over wchar_t, which is 16 bits on Windows.
static_assert(sizeof(wchar_t) == 2, "the Windows backends assume UTF-16 wchar_t");

namespace engine_placeholder::os::windows
{

namespace
{

constexpr u32 kReplacement = 0xFFFD;
// Without the \\?\ prefix, CreateDirectoryW allows MAX_PATH - 12 = 248 characters (room for an 8.3 file name) and the
// file functions MAX_PATH - 1; the prefix is added from the lower limit, so a path either works everywhere or gets the
// prefix.
constexpr usize kLongPathFrom = 248;

// Decodes one code point from `s` at `*i`, advancing `*i`; invalid input yields U+FFFD and consumes one maximal subpart
// (Unicode 15, section 3.9: the longest prefix of a well-formed sequence, at least one byte), so `ED A0 80` (a
// surrogate) is three errors and `E2 9C` followed by `A` is one. The second byte's allowed range depends on the first
// (Table 3-7), which is what rules out overlong forms, surrogates and values above U+10FFFF without decoding them.
u32 DecodeUtf8(const char* s, usize n, usize* i, bool* invalid)
{
    const u8 b0 = static_cast<u8>(s[*i]);
    *invalid = false;
    if (b0 < 0x80)
    {
        ++*i;
        return b0;
    }
    usize need = 0;
    u32 cp = 0;
    u8 lo = 0x80, hi = 0xBF;  // the allowed range of the second byte
    if (b0 >= 0xC2 && b0 <= 0xDF)
    {
        need = 1, cp = b0 & 0x1F;
    }
    else if (b0 >= 0xE0 && b0 <= 0xEF)
    {
        need = 2, cp = b0 & 0x0F;
        lo = b0 == 0xE0 ? 0xA0 : 0x80;  // E0 80..9F would be overlong
        hi = b0 == 0xED ? 0x9F : 0xBF;  // ED A0..BF would be a surrogate
    }
    else if (b0 >= 0xF0 && b0 <= 0xF4)
    {
        need = 3, cp = b0 & 0x07;
        lo = b0 == 0xF0 ? 0x90 : 0x80;  // F0 80..8F would be overlong
        hi = b0 == 0xF4 ? 0x8F : 0xBF;  // F4 90.. would be above U+10FFFF
    }
    else
    {
        ++*i;  // C0, C1, F5..FF and a continuation byte on its own
        *invalid = true;
        return kReplacement;
    }
    usize j = *i + 1;
    for (usize k = 0; k < need; ++k, ++j)
    {
        const u8 b = j < n ? static_cast<u8>(s[j]) : 0;
        const bool ok = j < n && (k == 0 ? (b >= lo && b <= hi) : (b & 0xC0) == 0x80);
        if (!ok)
        {
            *i = j;  // the bytes so far are one maximal subpart
            *invalid = true;
            return kReplacement;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    *i = j;
    return cp;
}

usize EncodedUtf8Length(u32 cp)
{
    return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}

void EncodeUtf8(u32 cp, char* out)
{
    if (cp < 0x80)
    {
        out[0] = static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out[0] = static_cast<char>(0xF0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

usize Utf8ToUtf16(StringView utf8, wchar_t* out, usize capacity, usize* replaced)
{
    const char* s = utf8.Data();
    const usize n = utf8.Length();
    usize needed = 0;
    usize written = 0;
    usize bad = 0;
    bool full = capacity == 0;
    for (usize i = 0; i < n;)
    {
        bool invalid = false;
        const u32 cp = DecodeUtf8(s, n, &i, &invalid);
        bad += invalid ? 1 : 0;
        const usize units = cp >= 0x10000 ? 2 : 1;
        if (!full && written + units < capacity)
        {  // whole code points only, and room for the terminator
            if (units == 2)
            {
                const u32 v = cp - 0x10000;
                out[written] = static_cast<wchar_t>(0xD800 + (v >> 10));
                out[written + 1] = static_cast<wchar_t>(0xDC00 + (v & 0x3FF));
            }
            else
            {
                out[written] = static_cast<wchar_t>(cp);
            }
            written += units;
        }
        else
        {
            full = true;  // keep counting, write nothing more
        }
        needed += units;
    }
    if (capacity > 0)
    {
        out[written] = 0;
    }
    if (replaced != nullptr)
    {
        *replaced = bad;
    }
    return needed;
}

usize Utf16ToUtf8(const wchar_t* utf16, usize length, char* out, usize capacity, usize* replaced)
{
    usize needed = 0;
    usize written = 0;
    usize bad = 0;
    bool full = capacity == 0;
    for (usize i = 0; i < length;)
    {
        u32 unit = static_cast<u16>(utf16[i]);
        u32 cp = unit;
        ++i;
        if (unit >= 0xD800 && unit <= 0xDBFF)
        {
            if (i < length && static_cast<u16>(utf16[i]) >= 0xDC00 && static_cast<u16>(utf16[i]) <= 0xDFFF)
            {
                cp = 0x10000 + ((unit - 0xD800) << 10) + (static_cast<u16>(utf16[i]) - 0xDC00);
                ++i;
            }
            else
            {
                cp = kReplacement;
                ++bad;
            }
        }
        else if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            cp = kReplacement;
            ++bad;
        }
        const usize bytes = EncodedUtf8Length(cp);
        if (!full && written + bytes < capacity)
        {
            EncodeUtf8(cp, out + written);
            written += bytes;
        }
        else
        {
            full = true;
        }
        needed += bytes;
    }
    if (capacity > 0)
    {
        out[written] = '\0';
    }
    if (replaced != nullptr)
    {
        *replaced = bad;
    }
    return needed;
}

namespace
{

// Resolves `.` and `..` components and drops trailing dots and spaces from components, in place, in the part of `path`
// after `root` characters: what Win32 does to a path itself, but not to one with the \\?\ prefix, so a prefixed path
// means what the same path without it would. `..` never goes above the root. Returns the new length.
usize NormalizeLikeWin32(wchar_t* path, usize length, usize root)
{
    usize out = root;
    usize i = root;
    while (i < length)
    {
        usize end = i;
        while (end < length && path[end] != L'\\')
        {
            ++end;
        }
        usize comp_end = end;
        const bool dot = comp_end - i == 1 && path[i] == L'.';
        const bool dotdot = comp_end - i == 2 && path[i] == L'.' && path[i + 1] == L'.';
        if (dotdot)
        {
            // Back to the separator before the last kept component (never before the root).
            if (out > root)
            {
                --out;  // the separator after it
                while (out > root && path[out - 1] != L'\\')
                {
                    --out;
                }
            }
        }
        else if (!dot && comp_end > i)
        {
            while (comp_end > i && (path[comp_end - 1] == L'.' || path[comp_end - 1] == L' '))
            {
                --comp_end;
            }
            for (usize k = i; k < comp_end; ++k)
            {
                path[out++] = path[k];
            }
            if (end < length)
            {
                path[out++] = L'\\';
            }
        }
        i = end + 1;
    }
    // `C:\a\b\..` is `C:\a`, not `C:\a\`: a separator left behind by a final `.` or `..` goes, one the caller wrote
    // stays.
    if (out > root && path[out - 1] == L'\\' && length > 0 && path[length - 1] != L'\\')
    {
        --out;
    }
    path[out] = 0;
    return out;
}

}  // namespace

usize PathToWin32(StringView utf8_path, wchar_t* out, usize capacity, usize* replaced)
{
    const char* p = utf8_path.Data();
    const usize n = utf8_path.Length();
    auto sep = [](char c) { return c == '/' || c == '\\'; };
    const bool already_prefixed = n >= 4 && sep(p[0]) && sep(p[1]) && (p[2] == '?' || p[2] == '.') && sep(p[3]);
    const bool unc = !already_prefixed && n >= 2 && sep(p[0]) && sep(p[1]);
    const bool drive = n >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' &&
                       sep(p[2]);

    usize bad = 0;
    const usize plain = Utf8ToUtf16(utf8_path, nullptr, 0, &bad);
    if (replaced != nullptr)
    {
        *replaced = bad;
    }
    const bool prefix = !already_prefixed && (drive || unc) && plain >= kLongPathFrom;
    if (!prefix)
    {
        if (plain >= capacity)
        {
            if (capacity > 0)
            {
                out[0] = 0;
            }
            return plain;
        }
        Utf8ToUtf16(utf8_path, out, capacity, nullptr);
        for (usize k = 0; k < plain; ++k)
        {
            if (out[k] == L'/')
            {
                out[k] = L'\\';
            }
        }
        return plain;
    }
    // `\\?\` + the path for a drive path; `\\?\UNC\` + the path without its leading two separators for a UNC path.
    // Converted, then normalized like Win32 would (the prefix turns that off). The root that `..` cannot climb above:
    // `C:\` or `server\share\`.
    const wchar_t* head = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    const usize head_len = unc ? 8 : 4;
    const usize skip = unc ? 2 : 0;
    const usize upper = head_len + plain - skip;  // the length before normalization, which only shortens the path
    if (upper >= capacity)
    {
        if (capacity > 0)
        {
            out[0] = 0;
        }
        return upper;
    }
    for (usize k = 0; k < head_len; ++k)
    {
        out[k] = head[k];
    }
    Utf8ToUtf16(StringView(p + skip, n - skip), out + head_len, capacity - head_len, nullptr);
    for (usize k = head_len; k < upper; ++k)
    {
        if (out[k] == L'/')
        {
            out[k] = L'\\';
        }
    }
    usize root = head_len + 3;  // after "C:\"
    if (unc)
    {
        // After "server\share\": two components.
        root = head_len;
        for (int separators = 0; root < upper && separators < 2; ++root)
        {
            if (out[root] == L'\\')
            {
                ++separators;
            }
        }
    }
    return NormalizeLikeWin32(out, upper, root < upper ? root : upper);
}

}  // namespace engine_placeholder::os::windows
