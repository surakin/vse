#pragma once

#include "types.h"

//! The seam between a surface and the window's pointer lock (Phase 15, Day 201): `ui` names no window, so the window adapter implements this.

namespace engine_placeholder::ui {

/// What a surface asks the window for when a widget wants the pointer locked (mouse look).
///
/// The adapter that feeds the surface implements it over `os::Window::SetPointerLock`. While the pointer is locked the window reports motion as relative deltas, which the surface turns
/// into `RelativeMotion` events for the widget that asked.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing.
class PointerLockHost {
public:
    virtual ~PointerLockHost() = default;
    /// Returns whether the window can lock the pointer at all.
    ///
    /// - Fails: never.
    virtual bool LockSupported() const = 0;
    /// Asks the window to lock (true) or free (false) the pointer; returns whether the request was accepted (a lock may take effect a moment later).
    ///
    /// - Fails: returns false when the window refuses.
    virtual bool SetLocked(bool locked) = 0;
};

} // namespace engine_placeholder::ui
