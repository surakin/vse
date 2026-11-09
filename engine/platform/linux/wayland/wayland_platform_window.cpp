#include "platform/linux/wayland/wayland_platform_window.h"

#include "os/last_error.h"
#include <poll.h>

#include "log/log.h"
#include "os/os.h"

namespace engine_placeholder::os::wayland {

void WaylandPlatformWindow::Close() {
    // Order matters: the input proxies belong to the connection's
    // display and must be destroyed while it is still connected.
    input_.Shutdown();
    window_ = WaylandWindow();
    connection_ = WaylandConnection();
    connection_lost_ = false;
}

void WaylandPlatformWindow::MarkConnectionLost(const char* why) {
    if (!connection_lost_) {
        connection_lost_ = true;
        Log(LogPlatform, LogVerbosity::Error, why);
    }
}

bool WaylandPlatformWindow::Open(StringView title, u32 width, u32 height, InputState* input) {
    if (connection_.Valid()) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandPlatformWindow::Open called while already open");
        return false;
    }
    if (input == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandPlatformWindow::Open needs an InputState");
        return false;
    }
    connection_ = WaylandConnection::Connect();
    if (!connection_.Valid()) {
        return false;
    }
    // Input must hook the seat *before* any further roundtrip: wl_seat's
    // capabilities event is delivered once, shortly after the bind, and
    // an event arriving while the proxy has no listener is silently
    // discarded. WaylandWindow::Create() roundtrips, so creating the
    // window first lost the capabilities (no keyboard/pointer, no input
    // lines) — caught at the desk, 2026-11-03.
    input_.Init(connection_, input);
    window_ = WaylandWindow::Create(connection_, title, "re_engine", width, height);
    if (!window_.Valid()) {
        Close();
        return false;
    }
    input_.Cursor().SetSurface(window_.Surface());
    input_.Clipboard().SetSurface(window_.Surface());
    return true;
}

// Non-blocking pump of the display's events: reads whatever's already
// available on the socket (never blocks, per the poll(..., 0) timeout)
// and dispatches it. Uses the prepare_read/read_events pair rather than
// wl_display_dispatch_pending() alone, since dispatch_pending only
// drains what a *previous* read already buffered — without this, events
// the compositor sends after the initial round-trips (resize/close
// among them) would never actually be read off the socket. Blocking
// wl_display_dispatch() isn't used because the frame loop must keep
// meeting its budget (and terminate on its own in headless runs) even
// when the compositor sends nothing.
void WaylandPlatformWindow::PumpEvents() {
    wl_display* display = connection_.Display();
    if (display == nullptr || connection_lost_) {
        return;
    }
    // Every libwayland call below can report a dead connection or a
    // protocol error; none of them may be ignored, or a vanished
    // compositor would leave the frame loop running on a dead window.
    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
            MarkConnectionLost("Wayland: dispatch failed (protocol error or lost connection)");
            return;
        }
    }
    if (wl_display_flush(display) < 0 && os::LastError() != EAGAIN) {
        wl_display_cancel_read(display);
        MarkConnectionLost("Wayland: flush failed (compositor connection lost)");
        return;
    }

    pollfd pfd{};
    pfd.fd = wl_display_get_fd(display);
    pfd.events = POLLIN;
    int ready = ::poll(&pfd, 1, 0);
    if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        // POLLHUP/POLLERR: read_events reports the EOF/error as -1.
        if (wl_display_read_events(display) < 0) {
            MarkConnectionLost("Wayland: read failed (compositor connection lost)");
            return;
        }
    } else {
        wl_display_cancel_read(display);
    }
    if (wl_display_dispatch_pending(display) < 0) {
        MarkConnectionLost("Wayland: dispatch failed (protocol error or lost connection)");
        return;
    }
    if (wl_display_get_error(display) != 0) {
        MarkConnectionLost("Wayland: display reported an error");
        return;
    }
    window_.CommitPendingResize();
    input_.Tick();
    if (window_.ResizeSerial() != last_resize_serial_) {
        last_resize_serial_ = window_.ResizeSerial();
        InputEvent resize;
        resize.type = InputEventType::Resize;
        resize.width = window_.Width();
        resize.height = window_.Height();
        resize.time_ns = os::MonotonicTimeNanos();
        input_.Events().Push(resize);
    }
}

} // namespace engine_placeholder::os::wayland
