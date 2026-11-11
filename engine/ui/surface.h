#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/callback.h"
#include "ui/clipboard.h"
#include "ui/drag_drop.h"
#include "ui/draw_list.h"
#include "ui/event.h"
#include "ui/pointer_lock.h"
#include "ui/popup.h"
#include "ui/theme.h"
#include "ui/widget.h"

//! The surface (Phase 15, Day 192): the root of a widget tree with its size, scale, clock, theme, font provider, focus,
//! pointer capture and popup layer. It knows no window.

namespace engine_placeholder::ui
{

class DrawListCanvas;

/// Turns a font family key of a theme into a font.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing by itself.
class FontProvider
{
public:
    virtual ~FontProvider() = default;
    /// Returns the font for `family` (a [[ui::FontRef::family]]), or null when there is none; text layout sets its
    /// pixel size, so it is not const.
    ///
    /// - Ownership: borrowed; valid while the provider lives.
    /// - Fails: returns null for an unknown family.
    virtual text::Font* Resolve(u32 family) = 0;
};

/// A tree of widgets and everything the widgets share.
///
/// A window, a test or an in-world screen feeds it events with `Post` and runs `Frame`; the surface never names a
/// window. The frame cycle is: apply the queued events, call `Update` of the widgets that want it, run layout if dirty,
/// and (when a draw list is given) rebuild it if anything looks different or the list is empty.
///
/// Pointer events go to the capture owner, else to the deepest widget under the pointer (popups first); a button press
/// captures the pointer to the widget it hit until the last button is released. Key and text events go to the focus
/// owner. An event the target does not handle bubbles to its parents. Enter and leave follow the hit widget.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the root and the popup layer; borrows the theme, the font provider and the widgets' references
/// (focus, capture, hover), which are cleared when a widget dies.
class Surface
{
public:
    /// Makes a surface of `width` x `height` logical units at `scale`, with an empty root called "root" covering it.
    /// - Aborts: `scale` is not above 0.
    Surface(f32 width, f32 height, f32 scale);
    /// Destroys the tree.
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    /// Returns the root widget; its bounds follow the surface size.
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* Root() const { return root_.Get(); }
    /// Returns the popup layer: a widget above the root, hit first and drawn last, that a menu or tooltip adds its
    /// popup to.
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* PopupLayer() const { return popups_.Get(); }
    /// How long a drag rests over a widget before `Widget::OnDragHoverDelay` runs, in seconds.
    static constexpr f64 kDragHoverSeconds = 0.6;

    /// Starts a drag from the widget that holds the pointer capture (the one whose press is in progress): the surface
    /// takes the pointer, hover goes to drop targets only, the source's press ends, and `visual` (if any) follows the
    /// pointer in the top layer. The drag ends with the release of the last button (a drop on the target under the
    /// pointer, else a cancel), with Escape, or with `CancelDrag`; the source then gets `OnDragEnded`.
    /// - Param payload: what is dragged; moved. Needs at least one entry and one allowed action.
    /// - Param visual: an optional widget tree shown at the pointer (made pass-through); its size is measured once.
    /// - Param visual_offset: where its top-left corner lies relative to the pointer.
    /// - Returns: whether the drag started.
    /// - Ownership: the surface owns `visual` until the drag ends.
    /// - Fails: returns false, changing nothing, when a drag is going on, no button is down or the payload is empty or
    /// allows nothing.
    bool BeginDrag(DragPayload payload, UniquePtr<Widget> visual = UniquePtr<Widget>(), Point visual_offset = Point());
    /// Returns whether a drag is going on (started here, handed to the OS, or coming from another program).
    ///
    /// - Fails: never.
    bool Dragging() const { return drag_.active; }
    /// Returns whether the drag in progress came from another program.
    ///
    /// - Fails: never.
    bool DragIsExternal() const { return drag_.active && drag_.external; }
    /// Returns whether the drag in progress was started here and now runs in the OS (the pointer left the window).
    ///
    /// - Fails: never.
    bool DragHandedOff() const { return drag_.active && drag_.handed_off; }
    /// Returns the payload of the drag in progress, or null.
    ///
    /// - Ownership: borrowed; valid until the drag ends.
    /// - Fails: returns null when there is none.
    const DragPayload* ActiveDrag() const { return drag_.active ? &drag_.payload : nullptr; }
    /// Returns the widget that started the drag in progress, or null (none, or it died).
    ///
    /// - Ownership: borrowed.
    /// - Fails: returns null when there is none.
    Widget* DragSource() const { return drag_.source; }
    /// Returns the widget a drop would go to now, or null.
    ///
    /// - Ownership: borrowed.
    /// - Fails: returns null when there is none.
    Widget* DragTarget() const { return drag_.target; }
    /// Returns the action a drop would take now (`Nothing` over no target).
    ///
    /// - Fails: never.
    DragAction CurrentDragAction() const { return drag_.active ? drag_.action : DragAction::Nothing; }
    /// Returns the theme's drag slop (`MetricId::DragSlop`) in logical units.
    f32 DragSlop() const;
    /// Returns whether `to` is farther than the drag slop from `from` on either axis.
    /// - Fails: never.
    bool PastDragSlop(Point from, Point to) const;
    /// Ends the drag in progress as cancelled (the target gets `OnDragLeave`, the source `OnDragEnded(Nothing)`); does
    /// nothing when none.
    void CancelDrag();
    /// Passes the drag started here to the OS: the target is left and the visual removed, the source stays until
    /// `EndHandedOffDrag`. Called by the window adapter when the pointer leaves the window.
    /// - Fails: does nothing unless a drag started here is going on.
    void HandOffDrag();
    /// Ends a handed-off drag: the source gets `OnDragEnded(taken)`.
    /// - Param taken: what the OS drop did (`Nothing` when cancelled).
    void EndHandedOffDrag(DragAction taken);
    /// A drag from another program entered: its kinds are on offer (`payload` holds pending entries) at `position`.
    /// Ignored while a drag started here is going on.
    /// - Param allowed: the actions the source allows.
    void ExternalDragEnter(DragPayload payload, Point position, u8 allowed);
    /// The external drag moved to `position`.
    void ExternalDragOver(Point position, u8 allowed);
    /// The external drag left without dropping.
    void ExternalDragLeave();
    /// The external drag was dropped with its data read.
    /// - Param complete: the payload with every entry read; moved.
    /// - Param action: the action the source chose.
    /// - Returns: what the drop did (`Nothing` when no target took it).
    DragAction ExternalDrop(DragPayload complete, Point position, DragAction action);
    /// The data of the external drop could not be read: the drag ends and the failure listeners run with `message`.
    void ExternalDropFailed(StringView message);
    /// Returns the listeners called with newline-separated absolute paths when a file list is dropped on nothing that
    /// accepts it (the root's file drop). While there is one, a file list is accepted anywhere.
    Callback<Surface*, StringView>& OnFilesDropped() { return on_files_dropped_; }
    /// Returns the listeners called with a message when a drop failed to read its data.
    Callback<Surface*, StringView>& OnDropFailed() { return on_drop_failed_; }
    /// Returns the widget under `point` as a drag would see it (modals, popups), or null.
    ///
    /// - Ownership: borrowed.
    /// - Fails: returns null when there is none.
    Widget* WidgetAt(Point point) { return HitTestAll(point); }

    /// Returns the tooltip layer: a widget above the popup layer that holds the tooltip bubble while one shows (it is
    /// never hit).
    /// - Ownership: borrowed; the surface keeps it.
    /// - Fails: returns null when there is none.
    Widget* TooltipLayer() const { return tips_.Get(); }
    /// Returns the width in logical units.
    f32 Width() const { return width_; }
    /// Returns the height in logical units.
    f32 Height() const { return height_; }
    /// Returns the scale: physical pixels per logical unit.
    f32 Scale() const { return scale_; }
    /// Changes the size and scale (the root and the popup layer follow); marks layout dirty. The same size and scale
    /// again does nothing; several calls between two frames cost one layout.
    /// - Aborts: `scale` is not above 0.
    void Resize(f32 width, f32 height, f32 scale);

    /// Uses `clipboard` for copy and paste (borrowed; null goes back to the surface's own in-process clipboard).
    /// - Ownership: the clipboard must outlive its use by the surface.
    void SetClipboard(Clipboard* clipboard) { clipboard_ = clipboard != nullptr ? clipboard : &local_clipboard_; }
    /// Returns the clipboard in use.
    Clipboard& GetClipboard() { return *clipboard_; }

    /// Uses `host` for pointer lock requests (borrowed; null means no host, and every request is refused).
    /// - Ownership: the host must outlive its use by the surface.
    void SetPointerLockHost(PointerLockHost* host);
    /// Asks for the pointer to be locked on behalf of `owner`, which then gets `RelativeMotion` events. Does nothing
    /// and returns false when there is no host, the host cannot lock the pointer, it refuses, or another widget already
    /// holds the lock.
    /// - Param owner: the widget; borrowed, it releases the lock itself or is cleared when destroyed.
    /// - Returns: whether `owner` now holds the lock request.
    /// - Ownership: borrows `owner`.
    bool RequestPointerLock(Widget* owner);
    /// Gives the lock up if `owner` holds it.
    /// - Ownership: borrows `owner`.
    void ReleasePointerLock(Widget* owner);
    /// Returns the widget that holds the lock request, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* PointerLockOwner() const { return lock_owner_; }
    /// Returns whether a lock host exists and can lock the pointer.
    ///
    /// - Fails: never.
    bool PointerLockSupported() const { return lock_host_ != nullptr && lock_host_->LockSupported(); }

    // ---- Popups ----

    /// Opens `widget` in the popup layer next to `anchor` (surface coordinates), sized by its own measure and placed by
    /// [[ui::PlacePopup]] inside the surface.
    ///
    /// Popups form a stack: a press outside all of them closes them all (and is consumed or passed on as `options`
    /// say), Escape that the focus owner did not handle closes the top one, and losing the window focus closes every
    /// one. Closing a popup closes the ones above it. The popup takes the keyboard focus when `options.take_focus` is
    /// set and it is focusable, and the previous owner gets it back when the last popup closes. Opening a popup hides
    /// the tooltip.
    ///
    /// - Param widget: the popup's content; the popup layer owns it until it closes.
    /// - Param anchor: where the popup attaches, in surface coordinates.
    /// - Param options: how it places, takes the focus and closes.
    /// - Returns: the widget, for `ClosePopup` and `IsPopupOpen`.
    /// - Ownership: ownership passes to the surface; `options.owner` is borrowed.
    /// - Aborts: `widget` is null.
    Widget* OpenPopup(UniquePtr<Widget> widget, const Rect& anchor, const PopupOptions& options);
    /// Opens `menu` as a context menu with its top-left corner at `position` (moved to stay inside the surface),
    /// closing on an outside click.
    /// - Param menu: the menu widget; ownership passes to the surface.
    /// - Param position: where the pointer is, in surface coordinates.
    /// - Returns: the widget.
    /// - Ownership: ownership of `menu` passes to the surface.
    Widget* ShowContextMenu(UniquePtr<Widget> menu, Point position);
    /// Closes `popup` and every popup opened after it (the deferral rule applies: during event delivery the widgets go
    /// when the delivery ends). Does nothing for a widget that is not a popup.
    /// - Param popup: a widget `OpenPopup` returned.
    /// - Ownership: borrowed for the call.
    /// - Fails: never.
    void ClosePopup(Widget* popup);
    /// Closes every popup.
    /// - Fails: never.
    void CloseAllPopups();
    /// Returns how many popups are open.
    /// - Fails: never.
    usize PopupCount() const { return popup_infos_.Size(); }
    /// Returns whether `popup` is an open popup.
    /// - Param popup: a widget; may be null or dead (it is only compared).
    /// - Ownership: borrowed for the call.
    /// - Fails: never.
    bool IsPopupOpen(const Widget* popup) const;
    /// Returns the newest popup, or null.
    /// - Ownership: borrowed; the surface owns it.
    /// - Fails: returns null when none is open.
    Widget* TopPopup() const { return popup_infos_.Empty() ? nullptr : popup_infos_[popup_infos_.Size() - 1].popup; }

    // ---- Accelerators ----

    /// Registers a chord with the function to call when it is pressed and the focus owner (and the Tab, Escape and
    /// popup handling) did not take the key.
    /// - Param chord: the key and modifiers.
    /// - Param function: what to call.
    /// - Param user: passed to it; borrowed.
    /// - Returns: an id for the other accelerator calls, or 0 when the chord is already registered.
    /// - Ownership: `user` is borrowed until the accelerator is removed.
    u32 RegisterAccelerator(const KeyChord& chord, void (*function)(void* user), void* user);
    /// Removes an accelerator; an unknown id is ignored.
    /// - Fails: never.
    void UnregisterAccelerator(u32 id);
    /// Turns an accelerator on or off (an off one is skipped, so the key goes on unhandled); an unknown id is ignored.
    /// - Fails: never.
    void SetAcceleratorEnabled(u32 id, bool enabled);
    /// Returns whether the accelerator exists and is on.
    /// - Fails: never.
    bool AcceleratorEnabled(u32 id) const;
    /// Returns the chord of an accelerator (an empty one for an unknown id).
    /// - Fails: never.
    KeyChord AcceleratorChord(u32 id) const;
    /// Sets the widget that gets the key presses nobody else took (F10, Alt+letter, a lone Alt released): the menu bar.
    /// Borrowed; null clears.
    /// - Ownership: borrowed; cleared when the widget dies.
    void SetMenuKeyTarget(Widget* widget) { menu_key_target_ = widget; }

    // ---- Modal dialogs ----

    /// Shows `dialog` as a modal dialog: a shade (the `modaldim` style) covers everything beneath, the pointer cannot
    /// reach what lies under it, and Tab stays inside it.
    ///
    /// The dialog is measured, centered in the surface and given the keyboard focus (its first Tab stop); the previous
    /// focus owner gets the focus back when the dialog closes. Dialogs stack: a dialog shown while another is open
    /// covers it. Keys go to the top dialog while the focus is outside it, and accelerators and the menu bar's keys do
    /// not run. Popups opened from the dialog work as usual (they are above it). Opening a dialog closes the popups and
    /// releases the pointer capture.
    ///
    /// - Param dialog: the dialog's root widget; it should be sized by its own `Measure`.
    /// - Returns: the dialog, for `CloseModal`.
    /// - Ownership: ownership passes to the surface.
    /// - Aborts: `dialog` is null.
    Widget* ShowModal(UniquePtr<Widget> dialog);
    /// Closes `dialog` and the dialogs opened after it, removes their shades, closes the popups and gives the focus
    /// back to the owner it had when `dialog` opened (the deferral rule applies during event delivery). Does nothing
    /// for a widget that is not a modal dialog.
    /// - Param dialog: a widget `ShowModal` returned.
    /// - Ownership: borrowed for the call.
    /// - Fails: never.
    void CloseModal(Widget* dialog);
    /// Returns how many modal dialogs are open.
    /// - Fails: never.
    usize ModalCount() const { return modal_infos_.Size(); }
    /// Returns the newest modal dialog, or null.
    /// - Ownership: borrowed; the surface owns it.
    /// - Fails: returns null when none is open.
    Widget* TopModal() const { return modal_infos_.Empty() ? nullptr : modal_infos_[modal_infos_.Size() - 1].dialog; }
    /// Returns whether `dialog` is an open modal dialog.
    /// - Param dialog: a widget; may be null or dead (it is only compared).
    /// - Ownership: borrowed for the call.
    /// - Fails: never.
    bool IsModalOpen(const Widget* dialog) const;
    /// Returns the modal layer: above the root and below the popups, it holds the shades and the dialogs.
    /// - Ownership: borrowed; the surface keeps it.
    /// - Fails: returns null when there is none.
    Widget* ModalLayer() const { return modals_.Get(); }

    // ---- Tooltips ----

    /// Seconds a widget with a tooltip text must stay under a still pointer before its tooltip shows.
    static constexpr f64 kTooltipDelay = 0.5;
    /// Returns whether a tooltip is showing.
    /// - Fails: never.
    bool TooltipShown() const { return tooltip_ != nullptr; }
    /// Hides the tooltip, if one shows, and keeps it hidden until the pointer moves (what a press, a key or a wheel
    /// turn does).
    /// - Fails: never.
    void HideTooltip();

    /// Returns the clock in seconds.
    f64 Time() const { return time_; }
    /// Sets the clock; `dt` of the next `Frame` is not affected (see `Frame`).
    void SetTime(f64 seconds) { time_ = seconds; }

    /// Uses `theme` (borrowed; null for the built-in default) and bumps the theme revision; marks the draw dirty, and
    /// layout too when a metric or font changed.
    void SetTheme(const Theme* theme);
    /// Returns the theme set and its cache; widgets resolve their styles through it.
    ThemeSet& Themes() { return themes_; }
    /// Sets the font provider (borrowed; may be null); marks layout dirty.
    ///
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetFontProvider(FontProvider* provider);
    /// Returns the font provider, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    FontProvider* GetFontProvider() const { return fonts_; }

    /// Queues an event for the next `Frame`. A pointer event's `position` is in surface coordinates.
    void Post(const Event& event);
    /// Delivers an event now (what `Frame` does for each queued event). Pointer events update hover and capture; see
    /// the class comment.
    void Dispatch(Event event);

    /// Gives the keyboard focus to `widget` (null clears it): the old owner gets `FocusLost`, the new one
    /// `FocusGained`.
    ///
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetFocus(Widget* widget);
    /// Remembers the current focus owner for `RestoreSavedFocus` (the menu bar takes the focus and gives it back);
    /// replaces an earlier one. The reference is cleared when the widget dies.
    /// - Fails: never.
    void SaveFocus() { menu_saved_focus_ = focus_; }
    /// Gives the focus to the owner `SaveFocus` remembered (to nobody when there is none, or it died), and forgets it.
    /// - Fails: never.
    void RestoreSavedFocus();
    /// Returns the widget Edit commands (Cut, Copy, Paste, Select All) act on: the focus owner, or, while a menu has
    /// the focus (a popup is open, or the menu bar holds it), the owner it borrowed the focus from.
    ///
    /// - Ownership: borrowed.
    /// - Fails: returns null when there is none.
    Widget* EditTarget() const
    {
        if (!popup_infos_.Empty() && saved_focus_ != nullptr)
        {
            return saved_focus_;
        }
        if (menu_saved_focus_ != nullptr && focus_ == menu_key_target_)
        {
            return menu_saved_focus_;
        }
        return focus_;
    }
    /// Returns the focus owner, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* Focus() const { return focus_; }
    /// Makes `widget` receive every pointer event until `ReleaseCapture` (null releases).
    ///
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void SetCapture(Widget* widget);
    /// Returns the capture owner, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* Capture() const { return capture_; }
    /// Moves the focus to the next (or previous) widget in Tab order and wraps at the ends; returns whether a widget
    /// took it. The order is: the widgets with a positive `TabIndex` ascending, then the ones with 0 in tree order;
    /// negative ones, disabled, hidden and collapsed widgets are skipped. While a popup is open only the popup layer is
    /// walked. With no focus, forward starts at the first and backward at the last. Tab and Shift+Tab call it when the
    /// focus owner does not handle the key.
    /// - Fails: returns false, changing nothing, when no widget can take the focus.
    bool FocusNext(bool backwards);
    /// Returns whether the window the surface shows in has the focus (true until a `WindowFocusLost` event; an
    /// offscreen surface stays focused).
    /// - Fails: never.
    bool WindowFocused() const { return window_focused_; }
    /// Returns the modifier mask of the last pointer, key or text event; 0 after `WindowFocusLost`, so a Ctrl released
    /// while the window was away cannot stay down.
    /// - Fails: never.
    u32 Modifiers() const { return modifiers_; }
    /// Returns the cursor shape to show now: the capture owner's, else the hovered widget's, else the nearest
    /// ancestor's that names one; Arrow when none.
    /// - Fails: never.
    os::CursorShape RequestedCursor() const;
    /// Returns the widget the pointer is over, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* Hover() const { return hover_; }

    /// Returns whether a frame run now would do anything (`NeedsRedraw` only says the picture differs from the last
    /// draw list): the tree is dirty, events are queued, a drag is going, a hovered widget is waiting to show its
    /// tooltip, or a shown widget asks for `Update` every frame (an animation, a caret blink, a viewport). An idle
    /// surface returns false, so an on-demand surface costs nothing while it is.
    ///
    /// - Fails: never.
    bool NeedsFrame() const;

    /// Runs one frame: advances the clock by `dt`, applies the queued events, updates, lays out if needed and rebuilds
    /// `list` when it must.
    ///
    /// - Param dt: seconds since the last frame, 0 or more.
    /// - Param list: the draw list to fill, or null to only run the logic.
    /// - Param atlas: the glyph atlas for text when `list` is given (null counts text as dropped).
    /// - Returns: whether the draw list was rebuilt.
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    bool Frame(f32 dt, DrawList* list = nullptr, text::GlyphAtlas* atlas = nullptr);
    /// Returns whether something looks different since the last rebuilt draw list (what an on-demand offscreen surface
    /// reads).
    /// - Fails: never.
    bool NeedsRedraw() const { return draw_dirty_; }
    /// Returns how many frames ran layout.
    u64 LayoutPasses() const { return layout_passes_; }
    /// Returns how many times layout was still invalidated after its second pass in a frame (a widget that keeps
    /// invalidating itself); the surface gives up and draws.
    u64 LayoutOverruns() const { return layout_overruns_; }
    /// Returns a counter that changes whenever layout is invalidated or run; cached measurements are valid for one
    /// value of it.
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
    struct PopupInfo
    {
        Widget* popup = nullptr;
        Widget* owner = nullptr;
        bool consume = true;
        bool take_focus = true;
        bool orphaned = false;  // the owner died: closed at the next boundary
    };
    struct ModalInfo
    {
        Widget* dialog = nullptr;  // null once it died: the entry is closed at the next boundary
        Widget* dim = nullptr;
        Widget* saved_focus = nullptr;
    };
    struct Accelerator
    {
        u32 id = 0;
        KeyChord chord;
        void (*function)(void*) = nullptr;
        void* user = nullptr;
        bool enabled = true;
    };
    bool InsidePopups(const Widget* widget) const;
    bool PressOutsidePopups(Widget* hit);
    bool RunKeyFallbacks(const Event& e);
    void UpdateTooltip();
    void ShowTooltip(Widget* target);
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
    PointerLockHost* lock_host_ = nullptr;
    Widget* lock_owner_ = nullptr;
    Widget* hover_ = nullptr;
    Widget* menu_saved_focus_ = nullptr;  // see SaveFocus
    Widget* saved_focus_ = nullptr;  // the focus owner when the first popup opened, given back when the last one closes
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
    LocalClipboard local_clipboard_;
    Clipboard* clipboard_ = &local_clipboard_;
    UniquePtr<DrawListCanvas> canvas_;  // kept between frames so that its clip and layer stacks keep their storage
    Vector<PopupInfo> popup_infos_;
    Vector<ModalInfo> modal_infos_;
    void CloseModalAt(usize index);
    struct DragState
    {
        bool active = false;
        bool external = false;
        bool handed_off = false;
        bool files_target = false;
        bool hover_fired = false;
        DragPayload payload;
        Widget* source = nullptr;
        Widget* visual = nullptr;
        Widget* target = nullptr;
        Widget* hover_widget = nullptr;
        Size visual_size;
        Point visual_offset;
        Point position;
        Point hover_position;
        f64 hover_since = 0;
        DragAction action = DragAction::Nothing;
        DragAction os_action = DragAction::Nothing;
    };
    void UpdateDragRequested();
    void UpdateDragTarget();
    void MoveDragVisual();
    void DragPointer(const Event& e);
    DragAction EndDrag(bool drop);
    void UpdateDrag(f32 dt);
    void ClearDragTargetState();
    DragState drag_;
    Callback<Surface*, StringView> on_files_dropped_;
    Callback<Surface*, StringView> on_drop_failed_;
    Widget* KeyTarget() const;
    Vector<Accelerator> accelerators_;
    u32 next_accelerator_ = 0;
    Widget* menu_key_target_ = nullptr;
    bool alt_alone_ = false;
    Widget* tooltip_ = nullptr;
    Widget* tooltip_target_ = nullptr;
    bool tooltip_blocked_ = false;  // a press, key or wheel hid the tooltip: none shows until the pointer moves
    void RemoveTooltip();
    f64 hover_since_ = 0;
    Vector<Widget*> removals_;
    UniquePtr<Widget> root_;
    UniquePtr<Widget> modals_;
    UniquePtr<Widget> popups_;
    UniquePtr<Widget> tips_;
};

}  // namespace engine_placeholder::ui
