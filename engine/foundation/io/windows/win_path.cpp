// The Windows file backend's path boundary (see win_path.h). One of the places allowed to call the Win32 file API
// directly.
#include "io/windows/win_path.h"

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

namespace engine_placeholder::io::windows
{

namespace
{

// Win32 functions that create directories stop at MAX_PATH - 12 (248) characters, the others at MAX_PATH - 1; prefixing
// from the lower bound covers both.
constexpr usize kShortPathLimit = 248;

bool EqualsAsciiNoCase(StringView a, StringView b)
{
    if (a.Length() != b.Length())
    {
        return false;
    }
    for (usize i = 0; i < a.Length(); ++i)
    {
        char x = a[i];
        char y = b[i];
        if (x >= 'a' && x <= 'z')
        {
            x = static_cast<char>(x - 'a' + 'A');
        }
        if (y >= 'a' && y <= 'z')
        {
            y = static_cast<char>(y - 'a' + 'A');
        }
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

bool StartsWithText(StringView text, StringView prefix)
{
    return text.Length() >= prefix.Length() && text.Substring(0, prefix.Length()) == prefix;
}

bool IsSeparator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

}  // namespace

bool IsReservedDeviceName(StringView name)
{
    StringView base = name.Substring(0, name.IndexOf('.'));
    while (!base.Empty() && base[base.Length() - 1] == ' ')
    {
        base = base.Substring(0, base.Length() - 1);
    }
    static const char* const kNames[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    for (const char* reserved : kNames)
    {
        if (EqualsAsciiNoCase(base, StringView(reserved)))
        {
            return true;
        }
    }
    // COM0-COM9 and LPT0-LPT9, and the superscript digits Win32 also treats as devices (U+00B9, U+00B2, U+00B3, UTF-8
    // C2 B9/B2/B3).
    if (base.Length() < 4)
    {
        return false;
    }
    const StringView stem = base.Substring(0, 3);
    if (!EqualsAsciiNoCase(stem, "COM") && !EqualsAsciiNoCase(stem, "LPT"))
    {
        return false;
    }
    const StringView digit = base.Substring(3);
    if (digit.Length() == 1)
    {
        return digit[0] >= '0' && digit[0] <= '9';  // COM0 and LPT0 too (Win32's naming rules list them)
    }
    if (digit.Length() == 2 && static_cast<u8>(digit[0]) == 0xC2)
    {
        const u8 d = static_cast<u8>(digit[1]);
        return d == 0xB9 || d == 0xB2 || d == 0xB3;
    }
    return false;
}

Status ToWin32(const Path& path, WidePath* out, bool extended)
{
    out->length = 0;
    out->text[0] = 0;
    char native[kMaxPathBytes + 1];
    if (!path.ToNative(native, sizeof(native)))
    {
        return Status::Of(Error::TooLong);
    }
    const StringView text(native);
    const bool verbatim = StartsWithText(text, "\\\\?\\") || StartsWithText(text, "\\\\.\\");
    if (!verbatim)
    {
        Path::Components components = path.GetComponents();
        StringView name;
        while (components.Next(&name))
        {
            if (name == StringView(".") || name == StringView(".."))
            {
                continue;
            }
            const char last = name[name.Length() - 1];
            if (last == '.' || last == ' ' || name.IndexOf(':') != name.Length() || IsReservedDeviceName(name))
            {
                return Status::Of(Error::Invalid);
            }
            // NTFS keeps names of up to 255 UTF-16 units; Win32 reports a longer one as an invalid name, POSIX as
            // ENAMETOOLONG.
            if (name.Length() > 255 && os::windows::Utf8ToUtf16(name, nullptr, 0) > 255)
            {
                return Status::Of(Error::TooLong);
            }
        }
    }
    usize replaced = 0;
    const usize n = os::windows::Utf8ToUtf16(text, out->text, kWidePathCapacity, &replaced);
    if (replaced != 0)
    {
        return Status::Of(Error::Invalid);
    }
    if (n >= kWidePathCapacity)
    {
        return Status::Of(Error::TooLong);
    }
    if (verbatim || (n < kShortPathLimit && !extended))
    {
        out->length = n;
        return Status::Success();
    }
    // Long: resolve to an absolute path the way Win32 would, then prefix it.
    constexpr usize kPrefixRoom = 8;  // `\\?\UNC\` replaces the two leading separators of a UNC path
    wchar_t full[kWidePathCapacity];
    const DWORD got = GetFullPathNameW(out->text, static_cast<DWORD>(kWidePathCapacity), full, nullptr);
    if (got == 0)
    {
        return Status::FromOs(static_cast<i32>(GetLastError()));
    }
    if (got >= kWidePathCapacity - kPrefixRoom)
    {
        return Status::Of(Error::TooLong);
    }
    const bool unc = IsSeparator(full[0]) && IsSeparator(full[1]);
    const bool prefixed = unc && (full[2] == L'?' || full[2] == L'.') && IsSeparator(full[3]);
    usize length = 0;
    auto put = [&](const wchar_t* s, usize count)
    {
        for (usize i = 0; i < count; ++i)
        {
            out->text[length++] = s[i];
        }
    };
    if (prefixed)
    {
        put(full, got);
    }
    else if (unc)
    {
        put(L"\\\\?\\UNC\\", 8);
        put(full + 2, got - 2);
    }
    else
    {
        put(L"\\\\?\\", 4);
        put(full, got);
    }
    out->text[length] = 0;
    out->length = length;
    return Status::Success();
}

Status RefineNotFound(const WidePath& wide, u32 code)
{
    if (code != ERROR_PATH_NOT_FOUND && code != ERROR_FILE_NOT_FOUND)
    {
        return Status::FromOs(static_cast<i32>(code));
    }
    wchar_t parent[kWidePathCapacity];
    usize length = wide.length;
    for (usize i = 0; i <= length; ++i)
    {
        parent[i] = wide.text[i];
    }
    for (;;)
    {
        // Drop the last component (and the separators before it).
        while (length > 0 && !IsSeparator(parent[length - 1]))
        {
            --length;
        }
        while (length > 0 && IsSeparator(parent[length - 1]))
        {
            --length;
        }
        if (length < 3)
        {
            break;  // nothing left above a root (`C:`) or a prefix
        }
        parent[length] = 0;
        const DWORD attributes = GetFileAttributesW(parent);
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = GetLastError();
            if (e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND)
            {
                continue;
            }
            break;
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            return Status{Error::NotADirectory, static_cast<i32>(code)};
        }
        break;
    }
    return Status::FromOs(static_cast<i32>(code));
}

Status LastError(const WidePath& wide)
{
    return RefineNotFound(wide, GetLastError());
}

HandleGuard::~HandleGuard()
{
    if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h_);
    }
}

}  // namespace engine_placeholder::io::windows
