// The Win32 clipboard (Phase 15 Day 205): text, file lists and custom formats round trip through the real system clipboard, a hostile CF_HDROP
// keeps only local drive paths, another window's change queues ClipboardChanged, and the path rule is a table. The tests share the desktop's
// clipboard, so a guard saves its text first and puts it back afterwards (other formats it held are not restored).
#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"

#include "containers/vector.h"
#include "display_session.h"
#include "os/windows/win32.h"
#include "platform/clipboard.h"
#include "platform/input_event.h"
#include "platform/windows/win32_window.h"
#include "string_view.h"

#include "unity/windows_enable.h"
#include <shlobj.h> // DROPFILES
#include "unity/windows_disable.h"

using namespace engine_placeholder;
using os::ClipboardFormat;
using os::ClipboardKind;
using os::ClipboardStatus;
using os::InputEventType;
using os::win32::AcceptLocalPath;
using os::win32::Win32Window;

namespace {

// Saves the clipboard's text and puts it back when the test ends.
class ClipSaver {
public:
    ClipSaver() {
        if (!OpenClipboard(nullptr)) return;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                const usize units = GlobalSize(data) / sizeof(wchar_t);
                for (usize i = 0; i < units && text[i] != 0; ++i) saved_.Push(text[i]);
                saved_.Push(0);
                had_text_ = true;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    ~ClipSaver() {
        if (!OpenClipboard(nullptr)) return;
        EmptyClipboard();
        if (had_text_) {
            HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, saved_.Size() * sizeof(wchar_t));
            if (block != nullptr) {
                MemCopy(GlobalLock(block), saved_.Data(), saved_.Size() * sizeof(wchar_t));
                GlobalUnlock(block);
                if (SetClipboardData(CF_UNICODETEXT, block) == nullptr) GlobalFree(block);
            }
        }
        CloseClipboard();
    }
    ClipSaver(const ClipSaver&) = delete;
    ClipSaver& operator=(const ClipSaver&) = delete;

private:
    Vector<wchar_t> saved_;
    bool had_text_ = false;
};

struct ClipWindow {
    os::InputState input;
    Win32Window window;
    bool Open(const char* title = "clipboard test") { return window.Open(title, 200, 120, &input); }
    // Requests and waits for the event, then takes the result.
    os::ClipboardResult Read(ClipboardFormat format, StringView custom = StringView()) {
        const u32 id = window.RequestClipboard(ClipboardKind::Clipboard, format, custom);
        os::ClipboardResult result;
        if (id == 0) return result;
        bool announced = false;
        window.PumpEvents();
        os::InputEvent e;
        while (window.PollEvent(&e)) {
            if (e.type == InputEventType::ClipboardData && e.request_id == id) announced = true;
        }
        CHECK(announced);
        CHECK(window.TakeClipboardResult(id, &result));
        return result;
    }
    bool Set(ClipboardFormat format, StringView bytes, StringView custom = StringView()) {
        os::ClipboardOffer offer;
        offer.format = format;
        offer.custom_name = custom;
        offer.data = reinterpret_cast<const byte*>(bytes.Data());
        offer.size = bytes.Length();
        return window.SetClipboard(ClipboardKind::Clipboard, offer);
    }
    usize Changes() {
        window.PumpEvents();
        usize n = 0;
        os::InputEvent e;
        while (window.PollEvent(&e)) {
            if (e.type == InputEventType::ClipboardChanged) ++n;
        }
        return n;
    }
};

StringView BytesOf(const os::ClipboardResult& r) { return StringView(reinterpret_cast<const char*>(r.bytes.Data()), r.bytes.Size()); }

// Puts a raw CF_HDROP of these wide paths on the clipboard, as another program could.
void SetRawHdrop(const wchar_t* const* paths, usize count) {
    Vector<wchar_t> list;
    for (usize i = 0; i < count; ++i) {
        for (const wchar_t* p = paths[i]; *p != 0; ++p) list.Push(*p);
        list.Push(0);
    }
    list.Push(0);
    const usize size = sizeof(DROPFILES) + list.Size() * sizeof(wchar_t);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    byte* p = static_cast<byte*>(GlobalLock(block));
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    MemCopy(p, &header, sizeof(header));
    MemCopy(p + sizeof(DROPFILES), list.Data(), list.Size() * sizeof(wchar_t));
    GlobalUnlock(block);
    REQUIRE(OpenClipboard(nullptr));
    EmptyClipboard();
    if (SetClipboardData(CF_HDROP, block) == nullptr) GlobalFree(block);
    CloseClipboard();
}

} // namespace

TEST_CASE("Windows clipboard: only absolute local drive paths are accepted", "[win32][clipboard]") {
    CHECK(AcceptLocalPath("C:\\Users\\me\\file.txt"));
    CHECK(AcceptLocalPath("d:/data/a b/c.bin"));
    CHECK(AcceptLocalPath("C:\\"));
    CHECK(AcceptLocalPath("C:\\caf\xC3\xA9\\\xE2\x82\xAC.txt")); // UTF-8 names
    CHECK_FALSE(AcceptLocalPath("\\\\server\\share\\x"));    // UNC: a network share
    CHECK_FALSE(AcceptLocalPath("//server/share/x"));
    CHECK_FALSE(AcceptLocalPath("\\\\?\\C:\\x"));            // device and long-path forms
    CHECK_FALSE(AcceptLocalPath("\\\\.\\PhysicalDrive0"));
    CHECK_FALSE(AcceptLocalPath("relative\\x"));
    CHECK_FALSE(AcceptLocalPath("C:x"));                     // drive-relative
    CHECK_FALSE(AcceptLocalPath("C:\\a\\..\\b"));
    CHECK_FALSE(AcceptLocalPath("C:\\a\\.\\b"));
    CHECK_FALSE(AcceptLocalPath("C:\\a\\.."));
    CHECK_FALSE(AcceptLocalPath("C:\\a\tb"));               // control character
    CHECK_FALSE(AcceptLocalPath("C:\\a:stream"));           // an alternate data stream
    CHECK_FALSE(AcceptLocalPath(""));
    CHECK(AcceptLocalPath("C:\\a\\...dots\\b"));             // a name with dots is not ..
}

TEST_CASE("Windows clipboard: text round trips, invalid UTF-8 is cleaned, Primary is unsupported", "[window][win32][clipboard]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    ClipSaver saver;
    ClipWindow w;
    REQUIRE(w.Open());
    CHECK(w.window.SupportsClipboard(ClipboardKind::Clipboard));
    CHECK_FALSE(w.window.SupportsClipboard(ClipboardKind::Primary));
    CHECK(w.window.RequestClipboard(ClipboardKind::Primary, ClipboardFormat::Text, StringView()) == 0);
    const StringView text("h\xC3\xA9llo \xF0\x9F\x98\x80 line\r\nnext");
    REQUIRE(w.Set(ClipboardFormat::Text, text));
    CHECK(w.window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()));
    os::ClipboardResult r = w.Read(ClipboardFormat::Text);
    CHECK(r.status == ClipboardStatus::Ok);
    CHECK(BytesOf(r) == text); // CRLF kept as the system has it
    CHECK(r.replaced_sequences == 0);
    // An invalid byte is replaced before it is offered.
    REQUIRE(w.Set(ClipboardFormat::Text, StringView("a\xFF" "b", 3)));
    r = w.Read(ClipboardFormat::Text);
    CHECK(BytesOf(r) == StringView("a\xEF\xBF\xBD" "b"));
    // Bad offers change nothing.
    CHECK_FALSE(w.Set(ClipboardFormat::Text, StringView()));
    CHECK_FALSE(w.Set(ClipboardFormat::Custom, StringView("x"), StringView()));
    CHECK(BytesOf(w.Read(ClipboardFormat::Text)) == StringView("a\xEF\xBF\xBD" "b"));
}

TEST_CASE("Windows clipboard: a file list and a custom format round trip; a missing format is Missing", "[window][win32][clipboard]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    ClipSaver saver;
    ClipWindow w;
    REQUIRE(w.Open());
    const StringView files("C:\\Windows\\notepad.exe\nC:\\temp dir\\caf\xC3\xA9.txt");
    REQUIRE(w.Set(ClipboardFormat::FileList, files));
    CHECK(w.window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
    os::ClipboardResult r = w.Read(ClipboardFormat::FileList);
    CHECK(r.status == ClipboardStatus::Ok);
    CHECK(BytesOf(r) == files);
    CHECK(r.rejected_entries == 0);
    CHECK_FALSE(w.Set(ClipboardFormat::FileList, StringView("\\\\server\\share\\x"))); // a network path is never offered

    const StringView name("application/x-engine-test");
    const char payload[] = {'\x00', '\x01', '\x02', '\xFF', 'e', 'n', 'd'};
    REQUIRE(w.Set(ClipboardFormat::Custom, StringView(payload, sizeof(payload)), name));
    CHECK(w.window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Custom, name));
    r = w.Read(ClipboardFormat::Custom, name);
    CHECK(r.status == ClipboardStatus::Ok);
    REQUIRE(r.bytes.Size() >= sizeof(payload)); // the system may round a block up
    CHECK(MemCompare(r.bytes.Data(), payload, sizeof(payload)) == 0);
    CHECK_FALSE(w.window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()));
    CHECK(w.Read(ClipboardFormat::Text).status == ClipboardStatus::Missing);
    CHECK(w.Read(ClipboardFormat::Custom, StringView("application/x-engine-nobody")).status == ClipboardStatus::Missing);
}

TEST_CASE("Windows clipboard: a CF_HDROP from another program keeps only local drive paths and counts the rest", "[window][win32][clipboard]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    ClipSaver saver;
    ClipWindow w;
    REQUIRE(w.Open());
    const wchar_t* const paths[] = {L"C:\\good\\one.txt", L"\\\\evil\\share\\payload.exe", L"relative.txt", L"C:\\a\\..\\b", L"D:\\good two"};
    SetRawHdrop(paths, 5);
    const os::ClipboardResult r = w.Read(ClipboardFormat::FileList);
    CHECK(r.status == ClipboardStatus::Ok);
    CHECK(BytesOf(r) == StringView("C:\\good\\one.txt\nD:\\good two"));
    CHECK(r.rejected_entries == 3);
}

TEST_CASE("Windows clipboard: another window's change queues ClipboardChanged; a window's own does not", "[window][win32][clipboard]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    ClipSaver saver;
    ClipWindow a;
    ClipWindow b;
    REQUIRE(a.Open("clipboard a"));
    REQUIRE(b.Open("clipboard b"));
    a.Changes();
    b.Changes();
    REQUIRE(a.Set(ClipboardFormat::Text, StringView("from a")));
    CHECK(a.Changes() == 0);
    CHECK(b.Changes() == 1);
    REQUIRE(b.Set(ClipboardFormat::Text, StringView("from b")));
    CHECK(a.Changes() == 1);
    CHECK(b.Changes() == 0);
    CHECK(BytesOf(a.Read(ClipboardFormat::Text)) == StringView("from b"));
}
