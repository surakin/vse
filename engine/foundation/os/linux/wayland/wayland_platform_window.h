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

    bool Valid() const override { return window_.Valid(); }
    void PumpEvents() override;
    bool ShouldClose() const override { return window_.ShouldClose(); }
    u32 Width() const override { return window_.Width(); }
    u32 Height() const override { return window_.Height(); }
    const char* BackendName() const override { return "wayland"; }

private:
    WaylandConnection connection_;
    WaylandWindow window_;
    WaylandInput input_;
};

} // namespace engine_placeholder::os::wayland
