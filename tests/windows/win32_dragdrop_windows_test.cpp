// OLE drag and drop of the Win32 window (Phase 15 Day 205). A real OLE drag needs the user's mouse, so the tests play OLE's part: they call the
// window's drop target (DragEnter, DragOver, DragLeave, Drop) with a data object, the window's own outgoing Win32DataObject (so its GetData is
// tested too) or the clipboard's (OleGetClipboard, for a hostile CF_HDROP that our object would never make). BeginDrag is checked for what it
// refuses; a drag to Explorer and back is on the Day 205 by-eye list.
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "containers/vector.h"
#include "display_session.h"
#include "os/windows/win32.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "platform/windows/win32_dragdrop.h"
#include "platform/windows/win32_window.h"
#include "string_view.h"

#include "unity/windows_enable.h"
#include <shlobj.h> // DROPFILES
#include "unity/windows_disable.h"

using namespace engine_placeholder;
using os::ClipboardFormat;
using os::ClipboardStatus;
using os::DragAction;
using os::InputEvent;
using os::InputEventType;
using os::win32::Win32DataObject;
using os::win32::Win32Window;

namespace {

struct DndWindow {
    os::InputState input;
    Win32Window window;
    IDropTarget* target = nullptr;
    bool Open() {
        if (!window.Open("drag and drop test", 240, 160, &input)) return false;
        target = static_cast<IDropTarget*>(window.DropTargetForTest());
        return target != nullptr;
    }
    // A screen point at client (x, y).
    POINTL At(i32 x, i32 y) const {
        POINT p{x, y};
        ClientToScreen(window.NativeHandle(), &p);
        return POINTL{p.x, p.y};
    }
    Vector<InputEvent> Drain() {
        window.PumpEvents();
        Vector<InputEvent> out;
        InputEvent e;
        while (window.PollEvent(&e)) {
            if (e.type == InputEventType::DragEnter || e.type == InputEventType::DragOver || e.type == InputEventType::DragLeave ||
                e.type == InputEventType::Drop || e.type == InputEventType::DropData || e.type == InputEventType::DragEnded) {
                out.Push(e);
            }
        }
        return out;
    }
    os::ClipboardResult ReadDrop(ClipboardFormat format, StringView name = StringView()) {
        os::ClipboardResult result;
        const u32 id = window.RequestDropData(format, name);
        if (id == 0) return result;
        const Vector<InputEvent> events = Drain();
        bool announced = false;
        for (usize i = 0; i < events.Size(); ++i) announced = announced || (events[i].type == InputEventType::DropData && events[i].request_id == id);
        CHECK(announced);
        CHECK(window.TakeClipboardResult(id, &result));
        return result;
    }
};

os::ClipboardOffer DndTextOffer(StringView text) {
    os::ClipboardOffer o;
    o.format = ClipboardFormat::Text;
    o.data = reinterpret_cast<const byte*>(text.Data());
    o.size = text.Length();
    return o;
}

StringView DndBytesOf(const os::ClipboardResult& r) { return StringView(reinterpret_cast<const char*>(r.bytes.Data()), r.bytes.Size()); }

constexpr DWORD kAllEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

} // namespace

TEST_CASE("Windows drag and drop: the data object hands out what it holds and lists its formats", "[window][win32][dragdrop]") {
    Win32DataObject data;
    const os::ClipboardOffer offers[] = {DndTextOffer("h\xC3\xA9llo")};
    REQUIRE(data.Set(offers, 1));
    FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    CHECK(data.QueryGetData(&text) == S_OK);
    CHECK(data.QueryGetData(&files) == DV_E_FORMATETC);
    STGMEDIUM medium{};
    REQUIRE(data.GetData(&text, &medium) == S_OK);
    const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal));
    CHECK(w[0] == L'h');
    CHECK(w[1] == 0x00E9);
    CHECK(w[5] == 0);
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    IEnumFORMATETC* list = nullptr;
    REQUIRE(data.EnumFormatEtc(DATADIR_GET, &list) == S_OK);
    FORMATETC got{};
    ULONG fetched = 0;
    CHECK(list->Next(1, &got, &fetched) == S_OK);
    CHECK(got.cfFormat == CF_UNICODETEXT);
    CHECK(list->Next(1, &got, &fetched) == S_FALSE); // only one
    list->Release();
    // Bad offers are refused and nothing is kept.
    const os::ClipboardOffer empty[] = {DndTextOffer(StringView())};
    CHECK_FALSE(data.Set(empty, 1));
    CHECK(data.QueryGetData(&text) == DV_E_FORMATETC);
    os::ClipboardOffer unc;
    unc.format = ClipboardFormat::FileList;
    const char* network = "\\\\server\\share\\x";
    unc.data = reinterpret_cast<const byte*>(network);
    unc.size = std::strlen(network);
    CHECK_FALSE(data.Set(&unc, 1));
    CHECK(data.References() == 0);
}

TEST_CASE("Windows drag and drop: a window that accepts nothing refuses without events", "[window][win32][dragdrop]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    DndWindow w;
    REQUIRE(w.Open());
    CHECK(w.window.SupportsDragAndDrop());
    w.Drain();
    Win32DataObject data;
    const os::ClipboardOffer offers[] = {DndTextOffer("text")};
    REQUIRE(data.Set(offers, 1));
    DWORD effect = kAllEffects;
    REQUIRE(w.target->DragEnter(&data, 0, w.At(10, 10), &effect) == S_OK);
    CHECK(effect == DROPEFFECT_NONE);
    effect = kAllEffects;
    w.target->DragOver(0, w.At(12, 12), &effect);
    CHECK(effect == DROPEFFECT_NONE);
    effect = kAllEffects;
    w.target->Drop(&data, 0, w.At(12, 12), &effect);
    CHECK(effect == DROPEFFECT_NONE);
    CHECK(w.Drain().Size() == 0);
    CHECK(w.window.RequestDropData(ClipboardFormat::Text, StringView()) == 0);
}

TEST_CASE("Windows drag and drop: text enters, moves, takes the program's answer and drops with its data", "[window][win32][dragdrop]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    DndWindow w;
    REQUIRE(w.Open());
    os::DropAccept accept;
    accept.formats = os::kDropText | os::kDropFiles;
    w.window.SetDropAccept(accept);
    w.Drain();
    Win32DataObject data;
    const os::ClipboardOffer offers[] = {DndTextOffer("dropped \xE2\x82\xAC")};
    REQUIRE(data.Set(offers, 1));
    DWORD effect = kAllEffects;
    REQUIRE(w.target->DragEnter(&data, MK_LBUTTON, w.At(20, 30), &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY); // the default answer
    Vector<InputEvent> e = w.Drain();
    REQUIRE(e.Size() == 1);
    CHECK(e[0].type == InputEventType::DragEnter);
    CHECK(e[0].x == 20.0);
    CHECK(e[0].y == 30.0);
    CHECK(e[0].drag_formats == os::kDropText);
    CHECK(e[0].drag_actions == os::kAllDragActions);
    // The keys: Shift moves, Ctrl and Shift link (while the program has not answered).
    effect = kAllEffects;
    w.target->DragOver(MK_LBUTTON | MK_SHIFT, w.At(21, 31), &effect);
    CHECK(effect == DROPEFFECT_MOVE);
    effect = kAllEffects;
    w.target->DragOver(MK_LBUTTON | MK_SHIFT | MK_CONTROL, w.At(22, 32), &effect);
    CHECK(effect == DROPEFFECT_LINK);
    effect = DROPEFFECT_COPY; // a source that only copies: Shift cannot ask for a move
    w.target->DragOver(MK_LBUTTON | MK_SHIFT, w.At(22, 32), &effect);
    CHECK(effect == DROPEFFECT_NONE);
    e = w.Drain();
    REQUIRE(e.Size() == 3);
    CHECK(e[0].type == InputEventType::DragOver);
    CHECK(e[0].x == 21.0);
    // The program answers Move; OLE hears it on the next move.
    w.window.SetDropAction(DragAction::Move);
    effect = kAllEffects;
    w.target->DragOver(MK_LBUTTON, w.At(25, 35), &effect);
    CHECK(effect == DROPEFFECT_MOVE);
    effect = kAllEffects;
    REQUIRE(w.target->Drop(&data, 0, w.At(26, 36), &effect) == S_OK);
    CHECK(effect == DROPEFFECT_MOVE);
    e = w.Drain();
    REQUIRE(e.Size() == 2);
    CHECK(e[1].type == InputEventType::Drop);
    CHECK(e[1].x == 26.0);
    CHECK(e[1].drag_action == static_cast<u8>(DragAction::Move));
    CHECK(e[1].drag_formats == os::kDropText);
    // The data was read during Drop, so it is there although the data object goes away.
    data.Clear();
    const os::ClipboardResult r = w.ReadDrop(ClipboardFormat::Text);
    CHECK(r.status == ClipboardStatus::Ok);
    CHECK(DndBytesOf(r) == StringView("dropped \xE2\x82\xAC"));
    CHECK(w.ReadDrop(ClipboardFormat::FileList).status == ClipboardStatus::Missing); // not offered
    w.window.FinishDrop(DragAction::Move);
    CHECK(w.window.RequestDropData(ClipboardFormat::Text, StringView()) == 0); // finished
}

TEST_CASE("Windows drag and drop: a leave and a refusal at the drop give DragLeave and no Drop", "[window][win32][dragdrop]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    DndWindow w;
    REQUIRE(w.Open());
    os::DropAccept accept;
    accept.formats = os::kDropText;
    w.window.SetDropAccept(accept);
    w.Drain();
    Win32DataObject data;
    const os::ClipboardOffer offers[] = {DndTextOffer("x")};
    REQUIRE(data.Set(offers, 1));
    DWORD effect = kAllEffects;
    w.target->DragEnter(&data, MK_LBUTTON, w.At(5, 5), &effect);
    w.target->DragLeave();
    Vector<InputEvent> e = w.Drain();
    REQUIRE(e.Size() == 2);
    CHECK(e[1].type == InputEventType::DragLeave);
    effect = kAllEffects;
    w.target->DragEnter(&data, MK_LBUTTON, w.At(5, 5), &effect);
    w.window.SetDropAction(DragAction::Nothing); // the program refuses at this position
    effect = kAllEffects;
    w.target->Drop(&data, 0, w.At(6, 6), &effect);
    CHECK(effect == DROPEFFECT_NONE);
    e = w.Drain();
    REQUIRE(e.Size() == 2);
    CHECK(e[0].type == InputEventType::DragEnter);
    CHECK(e[1].type == InputEventType::DragLeave);
    CHECK(w.window.RequestDropData(ClipboardFormat::Text, StringView()) == 0);
}

TEST_CASE("Windows drag and drop: a dropped CF_HDROP keeps only local drive paths, and a custom format arrives", "[window][win32][dragdrop]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    DndWindow w;
    REQUIRE(w.Open());
    os::DropAccept accept;
    accept.formats = os::kDropFiles | os::kDropCustom;
    accept.custom_name = "application/x-engine-dnd-test";
    w.window.SetDropAccept(accept);
    w.Drain();
    // A hostile file list, as another program could offer it: through the clipboard's data object (our own would never build one).
    Vector<wchar_t> list;
    for (const wchar_t* path : {L"C:\\good.txt", L"\\\\evil\\share\\x.exe", L"..\\up.txt", L"D:\\dir\\fine"}) {
        for (const wchar_t* p = path; *p != 0; ++p) list.Push(*p);
        list.Push(0);
    }
    list.Push(0);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + list.Size() * sizeof(wchar_t));
    byte* p = static_cast<byte*>(GlobalLock(block));
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(p, &header, sizeof(header));
    std::memcpy(p + sizeof(DROPFILES), list.Data(), list.Size() * sizeof(wchar_t));
    GlobalUnlock(block);
    REQUIRE(OpenClipboard(nullptr));
    EmptyClipboard();
    if (SetClipboardData(CF_HDROP, block) == nullptr) GlobalFree(block);
    CloseClipboard();
    IDataObject* clipboard = nullptr;
    HRESULT got = E_FAIL;
    for (int attempt = 0; attempt < 50 && got != S_OK; ++attempt) { // the system's clipboard history may hold it open for a moment
        got = OleGetClipboard(&clipboard);
        if (got != S_OK) Sleep(10);
    }
    REQUIRE(got == S_OK);
    DWORD effect = kAllEffects;
    REQUIRE(w.target->DragEnter(clipboard, MK_LBUTTON, w.At(3, 3), &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY);
    effect = kAllEffects;
    REQUIRE(w.target->Drop(clipboard, 0, w.At(3, 3), &effect) == S_OK);
    clipboard->Release();
    os::ClipboardResult r = w.ReadDrop(ClipboardFormat::FileList);
    CHECK(r.status == ClipboardStatus::Ok);
    CHECK(DndBytesOf(r) == StringView("C:\\good.txt\nD:\\dir\\fine"));
    CHECK(r.rejected_entries == 2);
    w.window.FinishDrop(DragAction::Copy);
    OpenClipboard(nullptr);
    EmptyClipboard();
    CloseClipboard();

    // A custom format from our own data object.
    Win32DataObject data;
    os::ClipboardOffer custom;
    custom.format = ClipboardFormat::Custom;
    custom.custom_name = "application/x-engine-dnd-test";
    const char payload[] = {'\x01', '\x02', '\x03'};
    custom.data = reinterpret_cast<const byte*>(payload);
    custom.size = sizeof(payload);
    REQUIRE(data.Set(&custom, 1));
    effect = kAllEffects;
    w.target->DragEnter(&data, MK_LBUTTON, w.At(4, 4), &effect);
    CHECK(effect == DROPEFFECT_COPY);
    effect = kAllEffects;
    w.target->Drop(&data, 0, w.At(4, 4), &effect);
    r = w.ReadDrop(ClipboardFormat::Custom, "application/x-engine-dnd-test");
    CHECK(r.status == ClipboardStatus::Ok);
    REQUIRE(r.bytes.Size() >= sizeof(payload));
    CHECK(std::memcmp(r.bytes.Data(), payload, sizeof(payload)) == 0);
    CHECK(w.ReadDrop(ClipboardFormat::Custom, "application/x-other").status == ClipboardStatus::Missing);
}

TEST_CASE("Windows drag and drop: BeginDrag refuses bad sources and a drag without a mouse button down", "[window][win32][dragdrop]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    DndWindow w;
    REQUIRE(w.Open());
    os::DragSource none;
    CHECK_FALSE(w.window.BeginDrag(none));
    const os::ClipboardOffer offers[] = {DndTextOffer("a"), DndTextOffer("b"), DndTextOffer("c"), DndTextOffer("d")};
    os::DragSource many;
    many.offers = offers;
    many.offer_count = 4; // above kMaxDragOffers
    CHECK_FALSE(w.window.BeginDrag(many));
    // With no button physically down OLE would drop at once on whatever window is under the cursor, so the drag does not start.
    const bool button_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (!button_down) {
        os::DragSource one;
        one.offers = offers;
        one.offer_count = 1;
        CHECK_FALSE(w.window.BeginDrag(one));
    }
    CHECK_FALSE(w.window.Dragging());
    w.window.CancelDrag(); // no drag: nothing happens
    CHECK(w.Drain().Size() == 0);
}
