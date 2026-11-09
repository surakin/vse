#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"
#include "ui/canvas.h"
#include "ui/event.h"
#include "ui/geometry.h"

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
    // Inline on purpose: with no out-of-line virtual the class has no key function, so a test (built with RTTI) that derives from it emits its own typeinfo; the library is built without.
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

    /// Returns the state bits (`kStateHover`, `kStatePressed`, `kStateFocused`, `kStateChecked` of `ui/theme.h`, and `kStateDisabled` when not effectively enabled).
    u32 State() const;
    /// Sets or clears the `kStateChecked` bit (the surface sets the others); marks the draw dirty.
    void SetChecked(bool checked);

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
    /// Places the children; called by the surface, parent before children, when layout is dirty. The default does nothing (Day 193 adds the layout containers).
    virtual void Arrange() {}
    /// Handles an event delivered to this widget with its position in local space; set `event.handled` to stop it from bubbling. The default ignores it.
    virtual void OnEvent(Event&) {}
    /// Advances time by `dt` seconds; called each frame for the widgets whose `WantsUpdate` is true.
    virtual void Update(f32) {}
    /// Returns whether `Update` should be called each frame (a caret blink, a tooltip delay); default false.
    ///
    /// Fails: never.
    virtual bool WantsUpdate() const { return false; }

    /// Draws this widget and its subtree: the widget, then the children in order inside the clip when `ClipsChildren()`. Does nothing when hidden.
    void DrawTree(Canvas& canvas);

private:
    friend class Surface;
    void NotifyDestroyed();
    void AttachSurface(Surface* surface);
    void RecomputeIds(u64 parent_id, usize index);
    void ClearDirtyTree(u32 flags);

    String<HeapAllocator> name_;
    u64 id_ = 0;
    Widget* parent_ = nullptr;
    Surface* surface_ = nullptr;
    Vector<UniquePtr<Widget>> children_;
    Rect bounds_;
    u32 dirty_ = 0;
    u32 state_ = 0; // hover, pressed, focused, checked
    bool visible_ = true;
    bool enabled_ = true;
    bool pass_through_ = false;
    bool clip_children_ = true;
};

} // namespace engine_placeholder::ui
