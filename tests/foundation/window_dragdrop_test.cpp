#include "platform/input_event.h" // before the X11 headers: they define `None`
#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#undef CursorShape // <X11/X.h> defines it as a number; the engine has os::CursorShape

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "containers/vector.h"
#include "os/os.h"
#include "platform/drag_drop.h"
#include "platform/linux/wayland/wayland_clipboard.h"
#include "platform/linux/x11/x11_window.h"
#include "cstr/format.h"

// Day 188: the drag and drop value rules, and XDND between an X11Window and a scripted peer on a second connection.
namespace window_dragdrop_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;
using engine_placeholder::os::x11::X11Window;

namespace {

// The X11 macros are not usable in a unity build (an earlier file may have included the headers first), so the few constants are spelled here.
constexpr int kClientMessage = 33;
constexpr int kSelectionRequest = 30;
constexpr int kSelectionNotify = 31;
constexpr int kSuccess = 0;
constexpr int kPropModeReplace = 0;
constexpr int kAnyPropertyType = 0;
constexpr long kNoEventMask = 0;
constexpr long kPropertyChangeMask = 1L << 22;
constexpr unsigned long kXaAtom = 4;

Vector<byte> Bytes(const char* text) {
    Vector<byte> out;
    for (usize i = 0; text[i] != '\0'; ++i) out.Push(static_cast<byte>(text[i]));
    return out;
}

bool Equal(const Vector<byte>& a, const char* text) {
    const Vector<byte> b = Bytes(text);
    if (a.Size() != b.Size()) return false;
    for (usize i = 0; i < a.Size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// The peer: a second X connection with a window that acts as the other program of a drag (source or target).
struct Peer {
    Display* display = nullptr;
    unsigned long window = 0;
    unsigned long a_aware, a_enter, a_position, a_status, a_leave, a_drop, a_finished, a_selection, a_type_list;
    unsigned long a_copy, a_move, a_utf8, a_uri_list, a_prop;
    // What the peer saw (as target).
    int status_count = 0;
    bool last_status_accept = false;
    unsigned long last_status_action = 0;
    bool finished = false;
    bool finished_accept = false;
    unsigned long finished_action = 0;
    bool got_enter = false;
    bool got_leave = false;
    bool got_drop = false;
    unsigned long enter_types[3] = {0, 0, 0};
    unsigned long drop_time = 0;
    unsigned long source_window = 0;
    // What the peer serves (as source).
    Vector<byte> served;
    // What the peer received (as target).
    Vector<byte> received;
    bool received_done = false;

    bool Open(int x, int y) {
        display = XOpenDisplay(nullptr);
        if (display == nullptr) return false;
        window = XCreateSimpleWindow(display, XDefaultRootWindow(display), x, y, 100, 100, 0, 0, 0);
        a_aware = XInternAtom(display, "XdndAware", 0);
        a_enter = XInternAtom(display, "XdndEnter", 0);
        a_position = XInternAtom(display, "XdndPosition", 0);
        a_status = XInternAtom(display, "XdndStatus", 0);
        a_leave = XInternAtom(display, "XdndLeave", 0);
        a_drop = XInternAtom(display, "XdndDrop", 0);
        a_finished = XInternAtom(display, "XdndFinished", 0);
        a_selection = XInternAtom(display, "XdndSelection", 0);
        a_type_list = XInternAtom(display, "XdndTypeList", 0);
        a_copy = XInternAtom(display, "XdndActionCopy", 0);
        a_move = XInternAtom(display, "XdndActionMove", 0);
        a_utf8 = XInternAtom(display, "UTF8_STRING", 0);
        a_uri_list = XInternAtom(display, "text/uri-list", 0);
        a_prop = XInternAtom(display, "PEER_PROP", 0);
        XSelectInput(display, window, kPropertyChangeMask);
        XFlush(display);
        return true;
    }
    ~Peer() {
        if (display != nullptr) XCloseDisplay(display);
    }
    void Message(unsigned long to, unsigned long type, long d0, long d1, long d2, long d3, long d4) {
        XClientMessageEvent m = {};
        m.type = kClientMessage;
        m.display = display;
        m.window = to;
        m.message_type = type;
        m.format = 32;
        m.data.l[0] = d0;
        m.data.l[1] = d1;
        m.data.l[2] = d2;
        m.data.l[3] = d3;
        m.data.l[4] = d4;
        XSendEvent(display, to, 0, kNoEventMask, reinterpret_cast<XEvent*>(&m));
        XFlush(display);
    }
    // The peer owns XdndSelection, as the source of a drag does.
    void OwnSelection() {
        XSetSelectionOwner(display, a_selection, window, 0);
        XFlush(display);
    }
    void MakeAware() {
        long version = 5;
        XChangeProperty(display, window, a_aware, kXaAtom, 32, kPropModeReplace, reinterpret_cast<unsigned char*>(&version), 1);
        XMapWindow(display, window);
        XFlush(display);
    }
    // The peer acting as the source: Enter, Position (root coordinates), Drop and Leave toward `target`.
    void SendEnter(unsigned long target, unsigned long t0, unsigned long t1 = 0, unsigned long t2 = 0) {
        Message(target, a_enter, static_cast<long>(window), 5L << 24, static_cast<long>(t0), static_cast<long>(t1), static_cast<long>(t2));
    }
    void SendPosition(unsigned long target, int win_x, int win_y, unsigned long action) {
        int rx = 0, ry = 0;
        unsigned long child = 0;
        XTranslateCoordinates(display, target, XDefaultRootWindow(display), win_x, win_y, &rx, &ry, &child);
        Message(target, a_position, static_cast<long>(window), 0, (static_cast<long>(rx) << 16) | ry, 0, static_cast<long>(action));
    }
    // Handles whatever the server has for the peer.
    void Pump() {
        while (XPending(display) > 0) {
            XEvent e;
            XNextEvent(display, &e);
            if (e.type == kClientMessage) {
                const XClientMessageEvent& m = e.xclient;
                if (m.message_type == a_status) {
                    ++status_count;
                    last_status_accept = (m.data.l[1] & 1) != 0;
                    last_status_action = static_cast<unsigned long>(m.data.l[4]);
                } else if (m.message_type == a_finished) {
                    finished = true;
                    finished_accept = (m.data.l[1] & 1) != 0;
                    finished_action = static_cast<unsigned long>(m.data.l[2]);
                } else if (m.message_type == a_enter) {
                    got_enter = true;
                    source_window = static_cast<unsigned long>(m.data.l[0]);
                    for (int i = 0; i < 3; ++i) enter_types[i] = static_cast<unsigned long>(m.data.l[2 + i]);
                } else if (m.message_type == a_position) {
                    // As a target: accept with a copy.
                    Message(static_cast<unsigned long>(m.data.l[0]), a_status, static_cast<long>(window), 1, 0, 0, static_cast<long>(a_copy));
                } else if (m.message_type == a_leave) {
                    got_leave = true;
                } else if (m.message_type == a_drop) {
                    got_drop = true;
                    drop_time = static_cast<unsigned long>(m.data.l[2]);
                    XConvertSelection(display, a_selection, a_utf8, a_prop, window, drop_time);
                    XFlush(display);
                }
            } else if (e.type == kSelectionRequest) {
                const XSelectionRequestEvent& r = e.xselectionrequest;
                XSelectionEvent reply = {};
                reply.type = kSelectionNotify;
                reply.display = display;
                reply.requestor = r.requestor;
                reply.selection = r.selection;
                reply.target = r.target;
                reply.time = r.time;
                reply.property = 0;
                if (r.selection == a_selection && (r.target == a_utf8 || r.target == a_uri_list)) {
                    XChangeProperty(display, r.requestor, r.property, r.target, 8, kPropModeReplace, served.Data(), static_cast<int>(served.Size()));
                    reply.property = r.property;
                }
                XSendEvent(display, r.requestor, 0, kNoEventMask, reinterpret_cast<XEvent*>(&reply));
                XFlush(display);
            } else if (e.type == kSelectionNotify && e.xselection.property != 0) {
                Atom type = 0;
                int format = 0;
                unsigned long count = 0, after = 0;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(display, window, e.xselection.property, 0, 1 << 20, 1, kAnyPropertyType, &type, &format, &count, &after, &data) == kSuccess && data != nullptr) {
                    for (unsigned long i = 0; i < count; ++i) received.Push(static_cast<byte>(data[i]));
                    XFree(data);
                    received_done = true;
                }
            }
        }
    }
};

// Pumps the window and the peer until `done()` or ~3 s pass.
template <typename Pred>
bool PumpAll(X11Window& window, Peer& peer, Pred done) {
    for (int i = 0; i < 300; ++i) {
        window.PumpEvents(nullptr);
        peer.Pump();
        if (done()) return true;
        SleepNanos(10'000'000);
    }
    return false;
}

// Lets the window manager finish placing a new window, so root coordinates computed now still hold when the window translates them back.
inline void Settle(X11Window& window, Peer& peer) {
    for (int i = 0; i < 20; ++i) {
        window.PumpEvents(nullptr);
        peer.Pump();
        SleepNanos(10'000'000);
    }
}

// Collects window events into a list until `done`.
struct Events {
    Vector<InputEvent> list;
    void Drain(X11Window& window) {
        InputEvent e;
        while (window.PollEvent(&e)) list.Push(e);
    }
    const InputEvent* Find(InputEventType type, usize from = 0) const {
        for (usize i = from; i < list.Size(); ++i) {
            if (list[i].type == type) return &list[i];
        }
        return nullptr;
    }
    usize Count(InputEventType type) const {
        usize n = 0;
        for (usize i = 0; i < list.Size(); ++i) n += list[i].type == type ? 1 : 0;
        return n;
    }
};

} // namespace

TEST_CASE("DragAction bits and the path list limit", "[dragdrop][os]") {
    CHECK(DragActionBit(DragAction::Nothing) == 0);
    CHECK(DragActionBit(DragAction::Copy) == 1);
    CHECK(DragActionBit(DragAction::Move) == 2);
    CHECK(DragActionBit(DragAction::Link) == 4);
    CHECK((DragActionBit(DragAction::Copy) | DragActionBit(DragAction::Move) | DragActionBit(DragAction::Link)) == kAllDragActions);

    Vector<byte> list = Bytes("/a\n/b\n/c\n/d");
    CHECK(LimitPathList(list, 10) == 0);
    CHECK(Equal(list, "/a\n/b\n/c\n/d"));
    CHECK(LimitPathList(list, 2) == 2);
    CHECK(Equal(list, "/a\n/b"));
    CHECK(LimitPathList(list, 0) == 2);
    CHECK(list.Empty());
    Vector<byte> single = Bytes("/only");
    CHECK(LimitPathList(single, 1) == 0);
    CHECK(Equal(single, "/only"));
}

TEST_CASE("Drag and drop is unsupported by default", "[dragdrop][os]") {
    struct Plain : os::Window {
        bool Valid() const override { return true; }
        void PumpEvents() override {}
        bool ShouldClose() const override { return false; }
        u32 Width() const override { return 1; }
        u32 Height() const override { return 1; }
        const char* BackendName() const override { return "plain"; }
        NativeWindowInfo GetNativeInfo() const override { return NativeWindowInfo(); }
        bool IsMinimized() const override { return false; }
        u64 ResizeSerial() const override { return 0; }
        bool PollEvent(InputEvent*) override { return false; }
        u64 DroppedEvents() const override { return 0; }
    };
    Plain plain;
    CHECK_FALSE(plain.SupportsDragAndDrop());
    CHECK(plain.RequestDropData(ClipboardFormat::Text, StringView()) == 0);
    DragSource source;
    CHECK_FALSE(plain.BeginDrag(source));
    CHECK_FALSE(plain.Dragging());
}

TEST_CASE("Drag and drop: every Wayland data device listener slot is filled", "[dragdrop][wayland][os]") {
    CHECK(wayland::WaylandClipboard::ListenersComplete());
}

// ---- X11: a scripted source drags onto an X11Window -------------------------------------------------------------------

TEST_CASE("XDND: text dropped on a window arrives with its events in order", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    CHECK(window.SupportsDragAndDrop());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    peer.OwnSelection();
    peer.served = Bytes("caf\xC3\xA9 \xF0\x9F\x8E\x89 dropped");

    const unsigned long target = window.NativeWindow();
    Settle(window, peer);
    peer.SendEnter(target, peer.a_utf8);
    peer.SendPosition(target, 10, 20, peer.a_copy);
    Events events;
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragEnter) != nullptr && peer.status_count >= 1;
    }));
    const InputEvent* enter = events.Find(InputEventType::DragEnter);
    CHECK(enter->x == 10);
    CHECK(enter->y == 20);
    CHECK((enter->drag_formats & kDropText) != 0);
    CHECK(enter->drag_action == static_cast<u8>(DragAction::Copy));
    CHECK(peer.last_status_accept);
    CHECK(peer.last_status_action == peer.a_copy);

    peer.SendPosition(target, 30, 40, peer.a_copy);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragOver) != nullptr;
    }));
    CHECK(events.Find(InputEventType::DragOver)->x == 30);

    peer.Message(target, peer.a_drop, static_cast<long>(peer.window), 0, 1234, 0, 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::Drop) != nullptr;
    }));
    CHECK(events.Find(InputEventType::Drop)->drag_action == static_cast<u8>(DragAction::Copy));
    // The order is Enter, Over, Drop.
    usize enter_at = 0, over_at = 0, drop_at = 0;
    for (usize i = 0; i < events.list.Size(); ++i) {
        if (events.list[i].type == InputEventType::DragEnter) enter_at = i;
        if (events.list[i].type == InputEventType::DragOver) over_at = i;
        if (events.list[i].type == InputEventType::Drop) drop_at = i;
    }
    CHECK(enter_at < over_at);
    CHECK(over_at < drop_at);

    const u32 id = window.RequestDropData(ClipboardFormat::Text, StringView());
    REQUIRE(id != 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DropData) != nullptr;
    }));
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(Equal(result.bytes, "caf\xC3\xA9 \xF0\x9F\x8E\x89 dropped"));

    window.FinishDrop(DragAction::Copy);
    REQUIRE(PumpAll(window, peer, [&] { return peer.finished; }));
    CHECK(peer.finished_accept);
    CHECK(peer.finished_action == peer.a_copy);
}

TEST_CASE("XDND: leave, a refused format and a window that accepts nothing", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    const unsigned long target = window.NativeWindow();

    // A window that accepts nothing is not XdndAware, and a rogue peer's drag is refused without a word to the program.
    peer.SendEnter(target, peer.a_utf8);
    peer.SendPosition(target, 5, 5, peer.a_copy);
    REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
    CHECK_FALSE(peer.last_status_accept);
    Events events;
    events.Drain(window);
    CHECK(events.Find(InputEventType::DragEnter) == nullptr);

    // Accepting only files: a text drag is refused too.
    DropAccept files;
    files.formats = kDropFiles;
    window.SetDropAccept(files);
    peer.status_count = 0;
    peer.SendEnter(target, peer.a_utf8);
    peer.SendPosition(target, 5, 5, peer.a_copy);
    REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
    CHECK_FALSE(peer.last_status_accept);

    // A leave after an entered drag is reported once.
    peer.SendEnter(target, peer.a_uri_list);
    peer.SendPosition(target, 5, 5, peer.a_copy);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragEnter) != nullptr;
    }));
    peer.Message(target, peer.a_leave, static_cast<long>(peer.window), 0, 0, 0, 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragLeave) != nullptr;
    }));
    CHECK(events.Count(InputEventType::DragLeave) == 1);
}

TEST_CASE("XDND: the program's answer reaches the source at once, and a refusal is final", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    const unsigned long target = window.NativeWindow();
    peer.SendEnter(target, peer.a_utf8);
    peer.SendPosition(target, 5, 5, peer.a_move);
    REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
    CHECK(peer.last_status_accept);
    CHECK(peer.last_status_action == peer.a_move); // the requested action is taken by default
    peer.status_count = 0;
    window.SetDropAction(DragAction::Nothing);
    REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
    CHECK_FALSE(peer.last_status_accept);
    // A drop after the refusal is finished without events for the program.
    peer.Message(target, peer.a_drop, static_cast<long>(peer.window), 0, 1, 0, 0);
    REQUIRE(PumpAll(window, peer, [&] { return peer.finished; }));
    CHECK_FALSE(peer.finished_accept);
    Events events;
    events.Drain(window);
    CHECK(events.Find(InputEventType::Drop) == nullptr);
}

TEST_CASE("XDND: an offer of 1000 types is cut at the first 64", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    const unsigned long target = window.NativeWindow();
    for (int variant = 0; variant < 2; ++variant) {
        // The text type is at index 10 (seen) or index 900 (beyond the cut).
        Vector<unsigned long> types;
        for (int i = 0; i < 1000; ++i) {
            char name[32];
            FormatTo(name, sizeof(name), "application/x-junk-%d", i);
            types.Push(XInternAtom(peer.display, name, 0));
        }
        types[variant == 0 ? 10 : 900] = peer.a_utf8;
        XChangeProperty(peer.display, peer.window, peer.a_type_list, kXaAtom, 32, kPropModeReplace, reinterpret_cast<unsigned char*>(types.Data()), 1000);
        peer.status_count = 0;
        peer.Message(target, peer.a_enter, static_cast<long>(peer.window), (5L << 24) | 1, 0, 0, 0);
        peer.SendPosition(target, 5, 5, peer.a_copy);
        REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
        CHECK(peer.last_status_accept == (variant == 0));
        peer.Message(target, peer.a_leave, static_cast<long>(peer.window), 0, 0, 0, 0);
        PumpAll(window, peer, [&] { return false; });
    }
}

TEST_CASE("XDND: a dropped file list is checked and cut at 10,000 entries", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    DropAccept accept;
    accept.formats = kDropFiles;
    window.SetDropAccept(accept);
    // 12,000 valid entries, plus hostile ones first.
    Vector<byte> list = Bytes("http://example.com/x\r\nfile:///a/../etc/passwd\r\nrelative\r\n");
    for (int i = 0; i < 12000; ++i) {
        char line[48];
        FormatTo(line, sizeof(line), "file:///tmp/f%d\r\n", i);
        for (const char* c = line; *c != '\0'; ++c) list.Push(static_cast<byte>(*c));
    }
    peer.OwnSelection();
    peer.served = static_cast<Vector<byte>&&>(list);
    const unsigned long target = window.NativeWindow();
    // The peer's drop handler asks for UTF8_STRING; the served data is returned for uri-list only, so script the exchange by hand: the window asks for text/uri-list.
    peer.SendEnter(target, peer.a_uri_list);
    peer.SendPosition(target, 5, 5, peer.a_copy);
    Events events;
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragEnter) != nullptr;
    }));
    peer.Message(target, peer.a_drop, static_cast<long>(peer.window), 0, 1, 0, 0);
    // The scripted peer's own Drop handling would convert for UTF8_STRING; it receives no XdndDrop, because the message went to the window.
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::Drop) != nullptr;
    }));
    const u32 id = window.RequestDropData(ClipboardFormat::FileList, StringView());
    REQUIRE(id != 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DropData) != nullptr;
    }));
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(result.rejected_entries == 3 + 2000); // the three hostile entries and the 2,000 beyond the limit
    usize lines = 1;
    for (usize i = 0; i < result.bytes.Size(); ++i) lines += result.bytes[i] == '\n' ? 1 : 0;
    CHECK(lines == kMaxDropFileEntries);
    window.FinishDrop(DragAction::Copy);
}

TEST_CASE("XDND: a source that vanishes mid-drag does not take the program down", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    const unsigned long target = window.NativeWindow();
    {
        Peer peer;
        REQUIRE(peer.Open(0, 0));
        peer.SendEnter(target, peer.a_utf8);
        peer.SendPosition(target, 5, 5, peer.a_copy);
        REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
        XDestroyWindow(peer.display, peer.window);
        XFlush(peer.display);
    } // the peer's connection is closed
    // Answers to a window that no longer exists raise BadWindow; Xlib's default handler would exit the process.
    window.SetDropAction(DragAction::Move);
    window.PumpEvents(nullptr);
    window.SetDropAction(DragAction::Nothing);
    window.PumpEvents(nullptr);
    CHECK(window.Valid());
}

TEST_CASE("XDND: a new drag while the last never left reports the leave", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    const unsigned long target = window.NativeWindow();
    Events events;
    for (int round = 0; round < 2; ++round) {
        peer.SendEnter(target, peer.a_utf8);
        peer.SendPosition(target, 5, 5, peer.a_copy);
        REQUIRE(PumpAll(window, peer, [&] {
            events.Drain(window);
            return events.Count(InputEventType::DragEnter) == static_cast<usize>(round + 1);
        }));
    }
    CHECK(events.Count(InputEventType::DragLeave) == 1);
    // A position after a drop is ignored.
    peer.Message(target, peer.a_drop, static_cast<long>(peer.window), 0, 1, 0, 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::Drop) != nullptr;
    }));
    const usize overs = events.Count(InputEventType::DragOver);
    peer.SendPosition(target, 9, 9, peer.a_copy);
    PumpAll(window, peer, [&] { return false; });
    events.Drain(window);
    CHECK(events.Count(InputEventType::DragOver) == overs);
    window.FinishDrop(DragAction::Nothing);
}

TEST_CASE("XDND: an XdndTypeList of the wrong format is ignored", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drop target", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(0, 0));
    DropAccept accept;
    accept.formats = kDropText;
    window.SetDropAccept(accept);
    const unsigned long target = window.NativeWindow();
    unsigned char bytes[64] = {};
    XChangeProperty(peer.display, peer.window, peer.a_type_list, kXaAtom, 8, kPropModeReplace, bytes, 64);
    peer.Message(target, peer.a_enter, static_cast<long>(peer.window), (5L << 24) | 1, 0, 0, 0);
    peer.SendPosition(target, 5, 5, peer.a_copy);
    REQUIRE(PumpAll(window, peer, [&] { return peer.status_count >= 1; }));
    CHECK_FALSE(peer.last_status_accept);
    CHECK(window.Valid());
}

// ---- X11: an X11Window drags onto a scripted target --------------------------------------------------------------------

TEST_CASE("XDND: a drag started by a window reaches an XdndAware peer and ends with its action", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drag source", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(400, 300));
    peer.MakeAware();
    for (int i = 0; i < 20; ++i) peer.Pump();

    const Vector<byte> text = Bytes("dragged out \xF0\x9F\x8E\x89");
    ClipboardOffer offer;
    offer.data = text.Data();
    offer.size = text.Size();
    DragSource source;
    source.offers = &offer;
    source.offer_count = 1;
    if (!window.BeginDrag(source)) SKIP("the pointer could not be grabbed (another client holds a grab, as a screen locker does)");
    CHECK(window.Dragging());
    CHECK_FALSE(window.BeginDrag(source)); // one at a time

    int rx = 0, ry = 0;
    unsigned long child = 0;
    XTranslateCoordinates(peer.display, peer.window, XDefaultRootWindow(peer.display), 20, 20, &rx, &ry, &child);
    window.DragPointerMoved(rx, ry, 0);
    Events events;
    if (!PumpAll(window, peer, [&] { return peer.got_enter && peer.status_count >= 1; })) {
        window.CancelDrag();
        SKIP("the peer window was not found under the pointer (the screen is not showing it)");
    }
    CHECK(peer.enter_types[0] == peer.a_utf8);
    CHECK(peer.last_status_accept);
    window.DragButtonReleased();
    REQUIRE(PumpAll(window, peer, [&] { return peer.got_drop; }));
    REQUIRE(PumpAll(window, peer, [&] { return peer.received_done; }));
    CHECK(Equal(peer.received, "dragged out \xF0\x9F\x8E\x89"));
    peer.Message(window.NativeWindow(), peer.a_finished, static_cast<long>(peer.window), 1, static_cast<long>(peer.a_copy), 0, 0);
    REQUIRE(PumpAll(window, peer, [&] {
        events.Drain(window);
        return events.Find(InputEventType::DragEnded) != nullptr;
    }));
    CHECK(events.Find(InputEventType::DragEnded)->drag_action == static_cast<u8>(DragAction::Copy));
    CHECK_FALSE(window.Dragging());
}

TEST_CASE("XDND: a drag cancelled, or dropped on a peer that never finishes, ends with Nothing", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drag source", 300, 200);
    REQUIRE(window.Valid());
    Peer peer;
    REQUIRE(peer.Open(400, 300));
    peer.MakeAware();
    for (int i = 0; i < 20; ++i) peer.Pump();
    const Vector<byte> text = Bytes("x");
    ClipboardOffer offer;
    offer.data = text.Data();
    offer.size = text.Size();
    DragSource source;
    source.offers = &offer;
    source.offer_count = 1;
    if (!window.BeginDrag(source)) SKIP("the pointer could not be grabbed (another client holds a grab, as a screen locker does)");
    window.CancelDrag();
    Events events;
    events.Drain(window);
    REQUIRE(events.Find(InputEventType::DragEnded) != nullptr);
    CHECK(events.Find(InputEventType::DragEnded)->drag_action == static_cast<u8>(DragAction::Nothing));
    CHECK_FALSE(window.Dragging());

    // A second drag works after the first, and a release over nothing ends it without a drop.
    REQUIRE(window.BeginDrag(source));
    window.DragPointerMoved(0, 0, 0);
    window.DragButtonReleased();
    events.list.Clear();
    events.Drain(window);
    REQUIRE(events.Find(InputEventType::DragEnded) != nullptr);
    CHECK(events.Find(InputEventType::DragEnded)->drag_action == static_cast<u8>(DragAction::Nothing));
}

TEST_CASE("XDND: BeginDrag refuses bad sources", "[dragdrop][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("drag source", 300, 200);
    REQUIRE(window.Valid());
    DragSource none;
    CHECK_FALSE(window.BeginDrag(none));
    ClipboardOffer custom;
    custom.format = ClipboardFormat::Custom; // no name
    DragSource bad;
    bad.offers = &custom;
    bad.offer_count = 1;
    CHECK_FALSE(window.BeginDrag(bad));
    ClipboardOffer four[4];
    DragSource many;
    many.offers = four;
    many.offer_count = 4;
    CHECK_FALSE(window.BeginDrag(many));
    CHECK_FALSE(window.Dragging());
}

} // namespace window_dragdrop_test
