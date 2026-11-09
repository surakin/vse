// The Win32 clipboard of Win32Window (Phase 15 Day 205): CF_UNICODETEXT, CF_HDROP and registered custom formats, change notification by
// WM_CLIPBOARDUPDATE. The Win32 calls are synchronous; a request is answered at once and announced by the ClipboardData event, the same
// shape the Linux backends give their asynchronous transfers.
#include "platform/windows/win32_window.h"

#include <cstring>

#include "containers/vector.h"
#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <shellapi.h> // DragQueryFileW
#include <shlobj.h>   // DROPFILES
#include "unity/windows_disable.h"

namespace engine_placeholder::os::win32 {

namespace {

// Another program may hold the clipboard open for a moment (a clipboard manager copying it): try a few times before giving up.
bool OpenClipboardRetrying(HWND hwnd) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(hwnd)) return true;
        Sleep(1);
    }
    return false;
}

// The registered format of a custom name, or 0 for an empty or unusable name.
UINT CustomFormat(StringView name) {
    if (name.Length() == 0 || name.Length() > 255) return 0;
    Array<wchar_t, 256> wide = {};
    windows::Utf8ToUtf16(name, wide.Data(), wide.Size());
    return RegisterClipboardFormatW(wide.Data());
}

// Copies bytes into a movable global block for SetClipboardData; null on failure.
HGLOBAL GlobalCopy(const void* data, usize size) {
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, size == 0 ? 1 : size);
    if (block == nullptr) return nullptr;
    void* p = GlobalLock(block);
    if (p == nullptr) {
        GlobalFree(block);
        return nullptr;
    }
    if (size != 0) std::memcpy(p, data, size);
    GlobalUnlock(block);
    return block;
}

// UTF-8 to a NUL-terminated UTF-16 buffer.
void ToWide(StringView utf8, Vector<wchar_t>* out) {
    out->Resize(utf8.Length() + 1); // a UTF-16 text never has more code units than its UTF-8 has bytes
    const usize n = windows::Utf8ToUtf16(utf8, out->Data(), out->Size());
    out->Resize(n + 1);
    (*out)[n] = 0;
}

// UTF-16 code units to UTF-8 bytes.
void ToUtf8(const wchar_t* text, usize length, Vector<byte>* out) {
    out->Resize(length * 3 + 1);
    const usize n = windows::Utf16ToUtf8(text, length, reinterpret_cast<char*>(out->Data()), out->Size());
    out->Resize(n);
}

// A CF_HDROP block from paths separated by '\n' (DROPFILES, then the wide paths, each NUL-terminated, then one more NUL). Null when a path is
// refused or the block cannot be made.
HGLOBAL MakeHdrop(const byte* paths, usize size) {
    Vector<wchar_t> list;
    usize start = 0;
    while (start < size) {
        usize end = start;
        while (end < size && paths[end] != '\n') ++end;
        if (end > start) {
            const StringView path(reinterpret_cast<const char*>(paths + start), end - start);
            if (!AcceptLocalPath(path)) return nullptr;
            Vector<wchar_t> wide;
            ToWide(path, &wide);
            for (usize i = 0; i < wide.Size(); ++i) list.Push(wide[i] == L'/' ? L'\\' : wide[i]); // includes the NUL
        }
        start = end + 1;
    }
    if (list.Size() == 0) return nullptr;
    list.Push(0);
    Vector<byte> block;
    block.Resize(sizeof(DROPFILES) + list.Size() * sizeof(wchar_t));
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(block.Data(), &header, sizeof(header));
    std::memcpy(block.Data() + sizeof(DROPFILES), list.Data(), list.Size() * sizeof(wchar_t));
    return GlobalCopy(block.Data(), block.Size());
}

} // namespace

bool AcceptLocalPath(StringView path) {
    // A drive letter, a colon, a separator: `C:\` or `C:/`. Everything else (UNC, `\\?\`, `\\.\`, relative, a drive-relative `C:x`) is refused.
    const usize n = path.Length();
    if (n < 3) return false;
    const char drive = path[0];
    if (!((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) || path[1] != ':' || (path[2] != '\\' && path[2] != '/')) return false;
    usize segment = 3;
    for (usize i = 3; i <= n; ++i) {
        const char c = i < n ? path[i] : '\\';
        if (i < n && (static_cast<u8>(c) < 0x20 || c == 0x7F || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')) return false;
        if (c == '\\' || c == '/') {
            const usize len = i - segment;
            if (len == 1 && path[segment] == '.') return false;
            if (len == 2 && path[segment] == '.' && path[segment + 1] == '.') return false;
            segment = i + 1;
        }
    }
    return true;
}

bool Win32Window::SetClipboard(ClipboardKind kind, const ClipboardOffer& offer) {
    if (!SupportsClipboard(kind) || offer.data == nullptr || offer.size == 0 || offer.size > kMaxClipboardBytes) return false;
    HGLOBAL block = nullptr;
    UINT format = 0;
    switch (offer.format) {
    case ClipboardFormat::Text: {
        Vector<byte> clean;
        SanitizeUtf8(offer.data, offer.size, clean, nullptr);
        Vector<wchar_t> wide;
        ToWide(StringView(reinterpret_cast<const char*>(clean.Data()), clean.Size()), &wide);
        block = GlobalCopy(wide.Data(), wide.Size() * sizeof(wchar_t));
        format = CF_UNICODETEXT;
        break;
    }
    case ClipboardFormat::FileList:
        block = MakeHdrop(offer.data, offer.size);
        format = CF_HDROP;
        break;
    case ClipboardFormat::Custom:
        format = CustomFormat(offer.custom_name);
        if (format == 0) return false;
        block = GlobalCopy(offer.data, offer.size);
        break;
    }
    if (block == nullptr) return false;
    if (!OpenClipboardRetrying(hwnd_)) {
        GlobalFree(block);
        return false;
    }
    EmptyClipboard(); // this window owns the clipboard now
    const bool set = SetClipboardData(format, block) != nullptr; // the system owns the block once it is set
    if (!set) GlobalFree(block);
    CloseClipboard();
    own_clipboard_sequence_ = GetClipboardSequenceNumber();
    return set;
}

u32 Win32Window::RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name) {
    if (!SupportsClipboard(kind)) return 0;
    const u64 now = MonotonicTimeNanos();
    const u32 id = clipboard_requests_.Begin(kind, format, now);
    if (id == 0) return 0;
    UINT native = format == ClipboardFormat::Text ? CF_UNICODETEXT : format == ClipboardFormat::FileList ? CF_HDROP : CustomFormat(custom_name);
    if (native == 0 || !IsClipboardFormatAvailable(native)) {
        clipboard_requests_.Fail(id, ClipboardStatus::Missing, now);
        return id;
    }
    if (!OpenClipboardRetrying(hwnd_)) {
        clipboard_requests_.Fail(id, ClipboardStatus::Failed, now);
        return id;
    }
    ClipboardStatus status = ClipboardStatus::Ok;
    Vector<byte> bytes;
    u32 replaced = 0;
    u32 rejected = 0;
    HANDLE data = GetClipboardData(native);
    const void* p = data != nullptr ? GlobalLock(data) : nullptr;
    if (p == nullptr) {
        status = ClipboardStatus::Missing;
    } else {
        const usize size = GlobalSize(data);
        if (format == ClipboardFormat::Text) {
            const wchar_t* text = static_cast<const wchar_t*>(p);
            usize length = 0;
            const usize max_units = size / sizeof(wchar_t);
            while (length < max_units && text[length] != 0) ++length; // up to the NUL, never past the block
            if (length * 3 > kMaxClipboardBytes) {
                status = ClipboardStatus::TooLarge;
            } else {
                Vector<byte> utf8;
                ToUtf8(text, length, &utf8);
                SanitizeUtf8(utf8.Data(), utf8.Size(), bytes, &replaced);
            }
        } else if (format == ClipboardFormat::FileList) {
            HDROP drop = static_cast<HDROP>(data);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                Vector<wchar_t> wide;
                wide.Resize(static_cast<usize>(length) + 1);
                DragQueryFileW(drop, i, wide.Data(), length + 1);
                Vector<byte> path;
                ToUtf8(wide.Data(), length, &path);
                if (!AcceptLocalPath(StringView(reinterpret_cast<const char*>(path.Data()), path.Size()))) {
                    ++rejected;
                    continue;
                }
                if (bytes.Size() + path.Size() + 1 > kMaxClipboardBytes) {
                    status = ClipboardStatus::TooLarge;
                    break;
                }
                if (bytes.Size() != 0) bytes.Push('\n');
                for (usize k = 0; k < path.Size(); ++k) bytes.Push(path[k]);
            }
        } else if (size > kMaxClipboardBytes) {
            status = ClipboardStatus::TooLarge;
        } else {
            bytes.Resize(size); // a custom block may be rounded up by the system; the program's own format carries its length if it needs one
            std::memcpy(bytes.Data(), p, size);
        }
        GlobalUnlock(data);
    }
    CloseClipboard();
    if (status != ClipboardStatus::Ok) bytes.Clear();
    clipboard_requests_.Complete(id, status, static_cast<Vector<byte>&&>(bytes), replaced, rejected, now);
    return id;
}

bool Win32Window::ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const {
    if (!SupportsClipboard(kind)) return false;
    const UINT native = format == ClipboardFormat::Text ? CF_UNICODETEXT : format == ClipboardFormat::FileList ? CF_HDROP : CustomFormat(custom_name);
    return native != 0 && IsClipboardFormatAvailable(native) != FALSE;
}

void Win32Window::ClipboardUpdated() {
    if (GetClipboardSequenceNumber() == own_clipboard_sequence_) return; // this window's own SetClipboard
    InputEvent changed;
    changed.type = InputEventType::ClipboardChanged;
    changed.clipboard_kind = static_cast<u8>(ClipboardKind::Clipboard);
    QueueEvent(changed);
}

} // namespace engine_placeholder::os::win32
