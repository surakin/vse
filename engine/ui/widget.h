#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "platform/cursor.h"
#include "types.h"
#include "ui/canvas.h"
#include "ui/drag_drop.h"
#include "ui/event.h"
#include "ui/geometry.h"
#include "ui/layout_types.h"
#include "ui/theme.h"

//! The retained widget tree (Phase 15, Day 192): a [[ui::Widget]] owns its children, belongs to one [[ui::Surface]] and draws through the abstract [[ui::Canvas]].

namespace engine_placeholder::ui {

class Surface;

/// Dirty flag: the widget's size or place may change; the surface runs layout before the next draw.
constexpr u32 kLayoutDirty = 1;
/// Dirty flag: the widget looks different; the surface rebuilds its draw list.
constexpr u32 kDrawDirty = 2;

/// A node of the widget tree.
///
/// Children are owned (`UniquePtr`) and drawn in order, so the last child is on top and is hit-tested first. A widget's id is a hash of its parent's id and its name (its index among
/// the siblings when the name is empty), so it is the same every time a tree is built the same way. Bounds are in surface coordinates, logical units. The state bits (hover,
/// pressed, focused) are kept by the surface; `Disabled` follows [[ui::Widget::EffectivelyEnabled]].
///
/// Removal is safe from inside an event handler: while the surface dispatches, [[ui::Widget::RemoveChild]] and [[ui::Widget::RemoveFromParent]] queue the removal and the widget
/// dies when the dispatch ends. Destroying a widget clears the surface's references to it (focus, capture, hover).
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns its children; borrows the parent and the surface.
class Widget {
public:
    /// Makes a widget.
    /// Param name: the name used by `FindByName` and for the id; may be empty.
    explicit Widget(StringView name = StringView());
    /// Destroys the children, after clearing the surface's references to this widget.
    // Inline on purpose (like every virtual of this class): with no out-of-line virtual the class has no key function, so a test (built with RTTI) that derives from it emits its own typeinfo; the library is built without.
    virtual ~Widget() { NotifyDestroyed(); }
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    /// Returns the name.
    StringView Name() const { return name_.View(); }
    /// Returns the stable id (see the class comment); 0 never.
    u64 Id() const { return id_; }
    /// Returns the parent, or null for a root and for a widget not in a tree.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* Parent() const { return parent_; }
    /// Returns the surface whose tree contains this widget, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Surface* GetSurface() const { return surface_; }

    /// Appends `child` as the last (top-most) child.
    ///
    /// Param child: a widget with no parent.
    /// Returns: the child, now owned by this widget.
    /// Aborts: `child` is null or already has a parent.
    /// Ownership: takes the child.
    Widget* AddChild(UniquePtr<Widget> child);
    /// Removes and destroys `child`; deferred to the end of the dispatch when the surface is dispatching an event.
    ///
    /// Param child: a child of this widget.
    /// Aborts: `child` is not a child of this widget.
    /// Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void RemoveChild(Widget* child);
    /// Moves this widget to the end of its parent's children, so it is drawn last and hit first. Does nothing for a root.
    void BringToFront();
    /// Removes and destroys this widget from its parent (deferred during dispatch); does nothing for a root.
    void RemoveFromParent();
    /// Returns the number of children.
    usize ChildCount() const { return children_.Size(); }
    /// Returns child `index`.
    ///
    /// Aborts: `index` is not below `ChildCount()`.
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    Widget* ChildAt(usize index) const;

    /// Finds the widget with `id` in this subtree (this widget included), or null.
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* FindById(u64 id);
    /// Finds the first widget called `name` in this subtree in drawing order (this widget included), or null.
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* FindByName(StringView name);

    /// Returns the bounds in surface coordinates.
    const Rect& Bounds() const { return bounds_; }
    /// Sets the bounds; marks the draw dirty when they change.
    void SetBounds(const Rect& bounds);
    /// Returns whether the widget (and its subtree) is drawn and hit.
    ///
    /// Fails: never.
    bool Visible() const { return visible_; }
    /// Shows or hides the widget; marks layout and draw dirty.
    void SetVisible(bool visible);
    /// Returns whether the widget itself is enabled (it may still be disabled through an ancestor).
    ///
    /// Fails: never.
    bool Enabled() const { return enabled_; }
    /// Enables or disables the widget; marks the draw dirty.
    void SetEnabled(bool enabled);
    /// Returns whether the widget and every ancestor are enabled.
    ///
    /// Fails: never.
    bool EffectivelyEnabled() const;
    /// Returns whether hits pass through the widget itself to what is under it; its children are still hit.
    ///
    /// Fails: never.
    bool PassThrough() const { return pass_through_; }
    /// Sets pass-through.
    void SetPassThrough(bool pass_through) { pass_through_ = pass_through; }
    /// Returns whether the widget clips its children to its bounds, in drawing and in hit testing (default true).
    ///
    /// Fails: never.
    bool ClipsChildren() const { return clip_children_; }
    /// Sets whether the children are clipped; marks the draw dirty.
    void SetClipsChildren(bool clip);

    /// Styles this widget and everything under it from `theme` laid over the surface's theme (a value `theme` leaves unset falls through), for example a status bar that stays dark in a light window. An
    /// ancestor's override applies under this one's. Marks layout dirty. Null removes the override.
    ///
    /// Ownership: borrows `theme`, which must outlive its use here and must not change while the surface draws (set it again after editing).
    void SetThemeOverride(const Theme* theme);
    /// Returns the override set by `SetThemeOverride`, or null.
    ///
    /// Ownership: borrowed; the application owns it.
    /// Fails: returns null when there is none.
    const Theme* ThemeOverride() const { return theme_override_; }
    /// Returns the state bits (`kStateHover`, `kStatePressed`, `kStateFocused`, `kStateChecked` of `ui/theme.h`, and `kStateDisabled` when not effectively enabled).
    u32 State() const;
    /// Sets or clears the `kStateChecked` bit (the surface sets the others); marks the draw dirty.
    void SetChecked(bool checked);
    /// Returns whether the `kStateChecked` bit is set.
    ///
    /// Fails: never.
    bool Checked() const { return (state_ & kStateChecked) != 0; }
    /// Returns the description: a sentence for tests and a settings screen about what an interactive widget does (empty by default; the name is the short label).
    StringView Description() const { return description_.View(); }
    /// Sets the description.
    void SetDescription(StringView description) { description_ = String<HeapAllocator>(HeapAllocator::Default(), description); }
    /// Returns the role tag (0 for none): lets a container find the widgets it manages without run-time type information (a radio group finds its radio buttons).
    u32 Role() const { return role_; }
    /// Returns the tooltip text a tooltip popup (Day 198) shows after the pointer rests on the widget; empty for none.
    StringView Tooltip() const { return tooltip_.View(); }
    /// Sets the tooltip text.
    void SetTooltip(StringView tooltip) { tooltip_ = String<HeapAllocator>(HeapAllocator::Default(), tooltip); }

    /// Marks the widget and its ancestors dirty with `flags` (`kLayoutDirty`, `kDrawDirty`; layout implies draw) and tells the surface.
    void MarkDirty(u32 flags);
    /// Returns the dirty flags set on this widget since the last frame cleared them.
    u32 Dirty() const { return dirty_; }

    /// Returns the deepest visible widget under `point` (surface coordinates) in this subtree, honouring clipping and pass-through, or null.
    ///
    /// Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// Fails: returns null when there is none.
    Widget* HitTest(Point point);

    /// Draws this widget (not its children); the default draws nothing.
    virtual void Draw(Canvas&) {}
    /// Returns the size this widget wants from its content, within `constraints`: a label's text, an image's natural size, a container's children. The default is 0 x 0. Call
    /// [[ui::Widget::MeasureTotal]], not this, to ask a child: it adds the widget's own size overrides and limits and caches the answer for the layout pass.
    virtual Size Measure(const Constraints&) { return Size(); }
    /// Places the widget in `rect` (surface coordinates) and its children inside it; called by the surface when layout is dirty, parent before children.
    /// The default sets the bounds and arranges each shown child in its own current bounds, or in the whole content box if it has `fill_parent`.
    virtual void Arrange(const Rect& rect) {
        SetBounds(rect);
        for (usize i = 0; i < children_.Size(); ++i) {
            Widget* child = children_[i].Get();
            if (!child->collapsed_) child->Arrange(child->props_.fill_parent ? rect : child->bounds_);
        }
    }
    /// Asks for the size of this widget in `constraints`: `Measure` with the `width` and `height` of the layout properties applied, limited to their `min` and `max`, then to
    /// `constraints`. Cached until the next layout change.
    Size MeasureTotal(const Constraints& constraints);
    /// Returns the layout properties.
    const LayoutProps& Props() const { return props_; }
    /// Sets the layout properties; marks layout dirty.
    void SetProps(const LayoutProps& props);
    /// Returns whether the widget takes no space and is not drawn or hit (see `SetCollapsed`).
    ///
    /// Fails: never.
    bool Collapsed() const { return collapsed_; }
    /// Collapses the widget: it takes no space in a container and is not drawn or hit; `SetVisible(false)` instead hides it but keeps its space. Marks layout dirty.
    void SetCollapsed(bool collapsed);
    /// Returns whether the widget is drawn and hit: visible and not collapsed.
    ///
    /// Fails: never.
    bool Shown() const { return visible_ && !collapsed_; }
    /// Returns whether a click or Tab may give this widget the keyboard focus (default false).
    ///
    /// Fails: never.
    bool Focusable() const { return focusable_; }
    /// Sets whether the widget can take the keyboard focus.
    void SetFocusable(bool focusable) { focusable_ = focusable; }
    /// Returns whether `Text` events are delivered to this widget when it has the focus (default false; a text field sets it).
    ///
    /// Fails: never.
    bool AcceptsText() const { return accepts_text_; }
    /// Sets whether the widget takes text input.
    void SetAcceptsText(bool accepts) { accepts_text_ = accepts; }
    /// Returns the Tab order key: 0 follows the tree order, a positive value goes first in ascending order, a negative value is skipped by Tab (a click still focuses it).
    ///
    /// Fails: never.
    i32 TabIndex() const { return tab_index_; }
    /// Sets the Tab order key.
    void SetTabIndex(i32 index) { tab_index_ = index; }
    /// Returns whether the widget names a cursor shape for when the pointer is over it (or it holds the capture).
    ///
    /// Fails: never.
    bool HasCursor() const { return has_cursor_; }
    /// Returns the cursor shape set with `SetCursor` (Arrow when none).
    ///
    /// Fails: never.
    os::CursorShape Cursor() const { return cursor_; }
    /// Names the cursor shape over this widget; a widget without one uses its parent's.
    void SetCursor(os::CursorShape shape) {
        cursor_ = shape;
        has_cursor_ = true;
    }
    /// Removes the cursor shape set with `SetCursor`.
    void ClearCursor() { has_cursor_ = false; }
    /// Makes this widget receive every pointer event until `ReleaseCapture` or the buttons are up; for a widget that starts a drag. Does nothing outside a surface.
    void RequestCapture();
    /// Gives the capture up if this widget holds it.
    void ReleaseCapture();
    /// Takes the keyboard focus. Does nothing outside a surface or when the widget is not focusable, shown and enabled.
    void RequestFocus();

    /// Handles an event delivered to this widget with its position in local space; set `event.handled` to stop it from bubbling. The default ignores it.
    virtual void OnEvent(Event&) {}
    /// Called by the surface on every ancestor of a widget that just got the keyboard focus (the ancestor nearest to it first); a scroll view brings the widget into view. The default does nothing.
    ///
    /// Ownership: borrows the widget for the call.
    virtual void OnDescendantFocused(Widget*) {}
    /// Drop target: decides whether a drag at `position` (local) would be accepted here, and what it would do. The surface asks the widget under the pointer, then its ancestors, and the first
    /// that answers other than `Nothing` is the target. The default refuses. A payload from another program has `pending` entries without data. The answer must be one of `payload.Allowed()`.
    /// Returns: the action the drop would take (usually `payload.Requested()`), or `Nothing`.
    virtual DragAction CanAccept(const DragPayload&, Point) { return DragAction::Nothing; }
    /// Drop target: a drag it accepts came over it; the surface has set `kStateDragOver`. The default does nothing.
    virtual void OnDragEnter(const DragPayload&, Point) {}
    /// Drop target: the drag moved over it (also called after a scroll); the default does nothing.
    virtual void OnDragOver(const DragPayload&, Point, DragAction) {}
    /// Drop target: the drag left it, or ended without a drop; the default does nothing.
    virtual void OnDragLeave() {}
    /// Drop target: the drag was dropped on it at `position` (local) with `action`; the payload is complete. The default does nothing.
    virtual void OnDrop(const DragPayload&, Point, DragAction) {}
    /// Drag source: the drag this widget started ended. `Nothing` means cancelled or refused; `Move` asks the source to remove what it gave. The default does nothing.
    virtual void OnDragEnded(DragAction) {}
    /// Called each frame while a drag is over this widget or a descendant, with the pointer at `position` (local); a scrolling widget scrolls when the pointer rests near its edge, by an
    /// amount that grows with the depth into the edge and with `dt`. Returns whether it scrolled. The default does nothing.
    /// Fails: never.
    virtual bool DragAutoScroll(Point, f32) { return false; }
    /// Called once when a drag has rested over this widget (or a descendant that did not take it) for the hover delay; a tab view selects the tab, a menu opens its submenu. Returns whether it
    /// handled it. The default does nothing.
    /// Fails: never.
    virtual bool OnDragHoverDelay(Point) { return false; }
    /// Edit commands the surface and the Edit menu route to the focus owner.
    enum class EditCommand : u8 {
        Cut,       ///< Copy the selection and remove it.
        Copy,      ///< Copy the selection.
        Paste,     ///< Insert the clipboard's text.
        SelectAll, ///< Select everything.
    };
    /// Returns whether the widget can run `command` now (a menu enables its items from it). The default is false.
    /// Fails: never.
    virtual bool CanEdit(EditCommand) const { return false; }
    /// Runs `command`; does nothing when `CanEdit` is false. The default does nothing.
    virtual void RunEdit(EditCommand) {}
    /// Advances time by `dt` seconds; called each frame for the widgets whose `WantsUpdate` is true.
    virtual void Update(f32) {}
    /// What `SetContextHandler` takes: called with the user pointer, the widget and the pointer position in surface coordinates.
    using ContextFunction = void (*)(void* user, Widget* widget, Point position);
    /// Sets the function called when the right button goes down on this widget (or on a descendant that has none of its own), after the press was delivered as usual. Null removes it.
    /// Param function: what to call, typically to open a context menu with [[ui::Surface::ShowContextMenu]].
    /// Param user: passed to it; borrowed.
    /// Ownership: `user` is borrowed until the handler is replaced or the widget dies.
    void SetContextHandler(ContextFunction function, void* user) {
        context_function_ = function;
        context_user_ = user;
    }
    /// Returns whether `Update` should be called each frame (a caret blink, a tooltip delay); default false.
    ///
    /// Fails: never.
    virtual bool WantsUpdate() const { return false; }

    /// Draws this widget and its subtree: the widget, then the children in order inside the clip when `ClipsChildren()`. Does nothing when hidden.
    void DrawTree(Canvas& canvas);

protected:
    /// Sets the role tag; for the classes that define a role.
    void SetRole(u32 role) { role_ = role; }

private:
    friend class Surface;
    ContextFunction context_function_ = nullptr;
    void* context_user_ = nullptr;
    void NotifyDestroyed();
    void AttachSurface(Surface* surface);
    void RecomputeIds(u64 parent_id, usize index);
    void ClearDirtyTree(u32 flags);

    String<HeapAllocator> name_;
    String<HeapAllocator> description_;
    String<HeapAllocator> tooltip_;
    u64 id_ = 0;
    Widget* parent_ = nullptr;
    Surface* surface_ = nullptr;
    Vector<UniquePtr<Widget>> children_;
    Rect bounds_;
    u32 dirty_ = 0;
    u32 state_ = 0; // hover, pressed, focused, checked
    LayoutProps props_;
    u64 measure_generation_ = 0;
    Constraints measure_constraints_;
    Size measure_result_;
    os::CursorShape cursor_ = os::CursorShape::Arrow;
    i32 tab_index_ = 0;
    u32 role_ = 0;
    bool has_cursor_ = false;
    bool focusable_ = false;
    bool accepts_text_ = false;
    bool collapsed_ = false;
    const Theme* theme_override_ = nullptr;
    bool visible_ = true;
    bool enabled_ = true;
    bool pass_through_ = false;
    bool clip_children_ = true;
};

} // namespace engine_placeholder::ui
