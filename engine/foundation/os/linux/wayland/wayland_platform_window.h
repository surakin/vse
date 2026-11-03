#pragma once

#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/wayland/wayland_input.h"
#include "os/linux/wayland/wayland_window.h"
#include "os/window.h"

// Phase 1 Day 26: the Wayland backend as an os::Window — owns the
// connection, xdg window and seat input together and moves the event
// pump that used to live in main.cpp behind PumpEvents(). Not movable
// (WaylandInput isn't).
namespace engine_placeholder::os::wayland {

class WaylandPlatformWindow final : public Window {
public:
    WaylandPlatformWindow() = default;

    // Connects, maps a window and hooks input into `input`, which must
    // outlive this object. `width`/`height` are hints the compositor may
    // override (the window takes its suggested size). False (logged) on
    // any failure, leaving the object invalid.
    bool Open(StringView title, u32 width, u32 height, InputState* input);

    // Tears everything down in the safe order (input proxies, window,
    // then the connection). Idempotent; Open() calls it on failure so a
    // failed open leaves no half-open connection or live listeners
    // pointing at the caller's InputState. The destructor calls it too.
    void Close();

    ~WaylandPlatformWindow() override { Close(); }

    bool Valid() const override { return window_.Valid() && !connection_lost_; }
    void PumpEvents() override;
    // Also true once the connection to the compositor has died (it
    // exited, crashed, or raised a protocol error): there is no window
    // left to wait on, so the frame loop must be able to exit.
    bool ShouldClose() const override { return window_.ShouldClose() || connection_lost_; }
    bool ConnectionLost() const { return connection_lost_; }
    wl_display* NativeDisplay() const { return connection_.Display(); }
    u32 Width() const override { return window_.Width(); }
    u32 Height() const override { return window_.Height(); }
    const char* BackendName() const override { return "wayland"; }

    const WaylandInput& Input() const { return input_; }

private:
    WaylandConnection connection_;
    WaylandWindow window_;
    WaylandInput input_;
    bool connection_lost_ = false;

    void MarkConnectionLost(const char* why);
};

} // namespace engine_placeholder::os::wayland
