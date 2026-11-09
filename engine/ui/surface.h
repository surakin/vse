#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/draw_list.h"
#include "ui/event.h"
#include "ui/theme.h"
#include "ui/widget.h"

//! The surface (Phase 15, Day 192): the root of a widget tree with its size, scale, clock, theme, font provider, focus, pointer capture and popup layer. It knows no window.

namespace engine_placeholder::ui {

class DrawListCanvas;

/// Turns a font family key of a theme into a font.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns nothing by itself.
class FontProvider {
public:
    virtual ~FontProvider() = default;
    /// Returns the font for `family` (a [[ui::FontRef::family]]), or null when there is none; text layout sets its pixel size, so it is not const.
    ///
    /// Ownership: borrowed; valid while the provider lives.
    /// Fails: returns null for an unknown family.
    virtual text::Font* Resolve(u32 family) = 0;
};

/// A tree of widgets and everything the widgets share.
///
/// A window, a test or an in-world screen feeds it events with `Post` and runs `Frame`; the surface never names a window. The frame cycle is: apply the queued events, call `Update` of
/// the widgets that want it, run layout if dirty, and (when a draw list is given) rebuild it if anything looks different or the list is empty.
///
/// Pointer events go to the capture owner, else to the deepest widget under the pointer (popups first); a button press captures the pointer to the widget it hit until the
/// last button is released. Key and text events go to the focus owner. An event the target does not handle bubbles to its parents. Enter and leave follow the hit widget.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns the root and the popup layer; borrows the theme, the font provider and the widgets' references (focus, capture, hover), which are cleared when a widget dies.
class Surface {
public:
    /// Makes a surface of `width` x `height` logical units at `scale`, with an empty root called "root" covering it.
    /// Aborts: `scale` is not above 0.
    Surface(f32 width, f32 height, f32 scale);
    /// Destroys the tree.
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    /// Returns the root widget; its bounds follow the surface size.
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* Root() const { return root_.Get(); }
    /// Returns the popup layer: a widget above the root, hit first and drawn last, that a menu or tooltip adds its popup to.
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* PopupLayer() const { return popups_.Get(); }
    /// Returns the width in logical units.
    f32 Width() const { return width_; }
    /// Returns the height in logical units.
    f32 Height() const { return height_; }
    /// Returns the scale: physical pixels per logical unit.
    f32 Scale() const { return scale_; }
    /// Changes the size and scale (the root and the popup layer follow); marks layout dirty. The same size and scale again does nothing; several calls between two frames cost one layout.
    /// Aborts: `scale` is not above 0.
    void Resize(f32 width, f32 height, f32 scale);

    /// Returns the clock in seconds.
    f64 Time() const { return time_; }
    /// Sets the clock; `dt` of the next `Frame` is not affected (see `Frame`).
    void SetTime(f64 seconds) { time_ = seconds; }

    /// Uses `theme` (borrowed; null for the built-in default) and bumps the theme revision; marks the draw dirty, and layout too when a metric or font changed.
    void SetTheme(const Theme* theme);
    /// Returns the theme set and its cache; widgets resolve their styles through it.
    ThemeSet& Themes() { return themes_; }
    /// Sets the font provider (borrowed; may be null); marks layout dirty.
    ///
    /// Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetFontProvider(FontProvider* provider);
    /// Returns the font provider, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    FontProvider* GetFontProvider() const { return fonts_; }

    /// Queues an event for the next `Frame`. A pointer event's `position` is in surface coordinates.
    void Post(const Event& event);
    /// Delivers an event now (what `Frame` does for each queued event). Pointer events update hover and capture; see the class comment.
    void Dispatch(Event event);

    /// Gives the keyboard focus to `widget` (null clears it): the old owner gets `FocusLost`, the new one `FocusGained`.
    ///
    /// Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetFocus(Widget* widget);
    /// Returns the focus owner, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* Focus() const { return focus_; }
    /// Makes `widget` receive every pointer event until `ReleaseCapture` (null releases).
    ///
    /// Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetCapture(Widget* widget);
    /// Returns the capture owner, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* Capture() const { return capture_; }
    /// Moves the focus to the next (or previous) widget in Tab order and wraps at the ends; returns whether a widget took it. The order is: the widgets with a positive `TabIndex`
    /// ascending, then the ones with 0 in tree order; negative ones, disabled, hidden and collapsed widgets are skipped. While a popup is open only the popup layer is walked.
    /// With no focus, forward starts at the first and backward at the last. Tab and Shift+Tab call it when the focus owner does not handle the key.
    /// Fails: returns false, changing nothing, when no widget can take the focus.
    bool FocusNext(bool backwards);
    /// Returns whether the window the surface shows in has the focus (true until a `WindowFocusLost` event; an offscreen surface stays focused).
    /// Fails: never.
    bool WindowFocused() const { return window_focused_; }
    /// Returns the modifier mask of the last pointer, key or text event; 0 after `WindowFocusLost`, so a Ctrl released while the window was away cannot stay down.
    /// Fails: never.
    u32 Modifiers() const { return modifiers_; }
    /// Returns the cursor shape to show now: the capture owner's, else the hovered widget's, else the nearest ancestor's that names one; Arrow when none.
    /// Fails: never.
    os::CursorShape RequestedCursor() const;
    /// Returns the widget the pointer is over, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* Hover() const { return hover_; }

    /// Runs one frame: advances the clock by `dt`, applies the queued events, updates, lays out if needed and rebuilds `list` when it must.
    ///
    /// Param dt: seconds since the last frame, 0 or more.
    /// Param list: the draw list to fill, or null to only run the logic.
    /// Param atlas: the glyph atlas for text when `list` is given (null counts text as dropped).
    /// Returns: whether the draw list was rebuilt.
    /// Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    bool Frame(f32 dt, DrawList* list = nullptr, text::GlyphAtlas* atlas = nullptr);
    /// Returns whether something looks different since the last rebuilt draw list (what an on-demand offscreen surface reads).
    /// Fails: never.
    bool NeedsRedraw() const { return draw_dirty_; }
    /// Returns how many frames ran layout.
    u64 LayoutPasses() const { return layout_passes_; }
    /// Returns how many times layout was still invalidated after its second pass in a frame (a widget that keeps invalidating itself); the surface gives up and draws.
    u64 LayoutOverruns() const { return layout_overruns_; }
    /// Returns a counter that changes whenever layout is invalidated or run; cached measurements are valid for one value of it.
    u64 LayoutGeneration() const { return layout_generation_; }
    /// Returns how many events are queued.
    usize QueuedEvents() const { return queue_.Size(); }

private:
    friend class Widget;
    void NoteDirty(u32 flags);
    void WidgetDestroyed(Widget* widget);
    void QueueRemoval(Widget* widget);
    bool Deliver(Widget* target, Event event, bool bubble);
    void RefreshHover();
    void TrackPopups();
    void SetWindowFocus(bool focused);
    void ReleasePointer();
    void SetHover(Widget* widget, const Event& cause);
    Widget* HitTestAll(Point point);
    void FlushRemovals();
    void UpdateWidgets(Widget* widget, f32 dt);
    bool Dispatching() const { return depth_ > 0; }

    f32 width_;
    f32 height_;
    f32 scale_;
    f64 time_ = 0;
    ThemeSet themes_;
    FontProvider* fonts_ = nullptr;
    Widget* focus_ = nullptr;
    Widget* capture_ = nullptr;
    Widget* hover_ = nullptr;
    Widget* saved_focus_ = nullptr; // the focus owner when the first popup opened, given back when the last one closes
    usize popup_count_ = 0;
    Point pointer_position_;
    bool pointer_inside_ = false;
    bool window_focused_ = true;
    u32 modifiers_ = 0;
    u32 buttons_down_ = 0;
    bool layout_dirty_ = true;
    bool draw_dirty_ = true;
    u64 layout_passes_ = 0;
    u64 layout_overruns_ = 0;
    u64 layout_generation_ = 1;
    u32 depth_ = 0;
    Vector<Event> queue_;
    Vector<Event> processing_;
    UniquePtr<DrawListCanvas> canvas_; // kept between frames so that its clip and layer stacks keep their storage
    Vector<Widget*> removals_;
    UniquePtr<Widget> root_;
    UniquePtr<Widget> popups_;
};

} // namespace engine_placeholder::ui
