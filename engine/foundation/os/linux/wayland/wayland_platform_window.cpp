#include "os/linux/wayland/wayland_platform_window.h"

#include <poll.h>

#include "log/log.h"

namespace engine_placeholder::os::wayland {

bool WaylandPlatformWindow::Open(StringView title, u32 width, u32 height, InputState* input) {
    (void)width;
    (void)height;
    if (input == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandPlatformWindow::Open needs an InputState");
        return false;
    }
    connection_ = WaylandConnection::Connect();
    if (!connection_.Valid()) {
        return false;
    }
    window_ = WaylandWindow::Create(connection_, title, "re_engine");
    if (!window_.Valid()) {
        connection_ = WaylandConnection();
        return false;
    }
    input_.Init(connection_, input);
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
    if (display == nullptr) {
        return;
    }
    while (wl_display_prepare_read(display) != 0) {
        wl_display_dispatch_pending(display);
    }
    wl_display_flush(display);

    pollfd pfd{};
    pfd.fd = wl_display_get_fd(display);
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0) {
        wl_display_read_events(display);
    } else {
        wl_display_cancel_read(display);
    }
    wl_display_dispatch_pending(display);
}

} // namespace engine_placeholder::os::wayland
