#pragma once

#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"

//! The tab view (Phase 15, Day 199): a row of tab headers over a stack of pages, of which one shows.

namespace engine_placeholder::ui {

/// Tab headers over pages.
///
/// Each tab has a title, an optional image and an optional close button, and owns one page (a widget). Only the selected page is shown (the others are collapsed, not destroyed, so their
/// state is kept); a lazy tab (`AddLazyTab`) builds its page, once, the first time it is selected. A click on a header selects its tab. With the view focused, Left, Right, Home and End
/// move the selection; Ctrl+Tab and Ctrl+PageDown select the next tab, Ctrl+Shift+Tab and Ctrl+PageUp the previous one (wrapping), also when the focus is inside a page. When the headers do
/// not fit, two arrows appear at the right end and the wheel over the headers scrolls them; selecting a tab scrolls it into view. A close button asks `OnCloseRequested`; the program decides and
/// calls `CloseTab`.
///
/// `Select` and the other setters run no listener; the user's actions (mouse and keys) run `OnSelectionChanged`.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its pages (as children) and its header layouts.
class TabView : public Widget {
public:
    /// Builds a page when a lazy tab is first selected; return null for no page.
    using PageFactory = UniquePtr<Widget> (*)(void* user);
    /// No tab.
    static constexpr usize kNone = static_cast<usize>(-1);
    /// The thickness of the selected tab's underline, in logical units.
    static constexpr f32 kUnderline = 2.0f;
    /// The size of the close glyph as a fraction of the theme's icon size.
    static constexpr f32 kCloseGlyph = 0.5f;
    /// The overflow arrows' half size as a fraction of the header height.
    static constexpr f32 kArrowReach = 0.25f;
    /// The wheel moves the headers this many tab heights per notch.
    static constexpr f32 kWheelHeights = 2.0f;

    /// Makes an empty view.
    explicit TabView(StringView name = StringView());
    /// Adds a tab with a ready page.
    /// - Param title: the header text.
    /// - Param page: the page; may be null for an empty tab.
    /// - Returns: the tab's index. The first tab added is selected.
    /// - Ownership: the view owns the page.
    usize AddTab(StringView title, UniquePtr<Widget> page);
    /// Adds a tab whose page is built by `factory` the first time the tab is selected (once).
    /// - Param factory: what builds the page.
    /// - Param user: passed to it; borrowed.
    /// - Returns: the tab's index.
    /// - Ownership: `user` is borrowed.
    usize AddLazyTab(StringView title, PageFactory factory, void* user);
    /// Shows `image` (an icon of the theme's icon size) left of the title; 0 for none.
    /// - Aborts: `index` is out of range.
    void SetTabImage(usize index, ImageId image);
    /// Gives the tab a close button (or takes it away); new tabs follow `SetClosable`.
    /// - Aborts: `index` is out of range.
    void SetTabClosable(usize index, bool closable);
    /// Makes the tabs added from now on closable (default off).
    void SetClosable(bool closable) { closable_ = closable; }
    /// Changes a header text.
    /// - Aborts: `index` is out of range.
    void SetTabTitle(usize index, StringView title);
    /// Returns how many tabs there are.
    ///
    /// - Fails: never.
    usize TabCount() const { return tabs_.Size(); }
    /// Returns the selected tab, or `kNone` when there are no tabs.
    ///
    /// - Fails: never.
    usize Selected() const { return selected_; }
    /// Selects tab `index` (building its lazy page) and scrolls it into view. Runs no listener.
    /// - Param index: a tab; an index out of range is ignored.
    void Select(usize index);
    /// Returns the page of a tab.
    /// - Ownership: borrowed; the view owns it.
    /// - Fails: returns null for an index out of range, an empty tab and a lazy page not built yet.
    Widget* Page(usize index) const;
    /// Removes tab `index` and destroys its page (the removal is deferred while an event is delivered). The selection moves to the neighbour when the selected tab goes. Runs no listener.
    /// - Param index: a tab; an index out of range is ignored.
    void CloseTab(usize index);
    /// Returns whether the headers are wider than the view (the arrows show).
    ///
    /// - Fails: never.
    bool HeaderOverflows();
    /// Returns how far the headers are scrolled, in logical units.
    ///
    /// - Fails: never.
    f32 HeaderScroll() const { return scroll_; }
    /// Returns the rectangle of header `index` in surface coordinates (it may lie partly or wholly outside the visible span when the headers are scrolled).
    /// - Fails: returns an empty rectangle for an index out of range.
    Rect TabRect(usize index);
    /// Returns the rectangle the selected page gets, in surface coordinates.
    Rect ContentRect();
    /// The tag of the pointer entry a drag started by a tab header carries.
    static constexpr char kDragTag[] = "ui.tabview.tab";
    /// Lets the user reorder the tabs by dragging a header (default off).
    void SetReorderable(bool reorderable) { reorderable_ = reorderable; }
    /// Returns the insertion index the drag shows now (`kNone` when none), for a test.
    usize DropIndex() const { return drop_index_; }
    /// Returns the listeners called after the user moved a tab by dragging (with the view, the old and the new index).
    Callback<TabView*, usize, usize>& OnTabMoved() { return on_moved_; }
    /// Accepts the drag of one of this view's headers (a move) when the view is reorderable.
    DragAction CanAccept(const DragPayload& payload, Point position) override;
    /// Shows the insertion line.
    void OnDragOver(const DragPayload& payload, Point position, DragAction action) override;
    /// Hides the insertion line.
    void OnDragLeave() override;
    /// Moves the dragged tab to the insertion index.
    void OnDrop(const DragPayload& payload, Point position, DragAction action) override;
    /// Ends the press that started the drag.
    void OnDragEnded(DragAction action) override;
    /// Scrolls the headers while the pointer rests near an end of them.
    /// - Fails: never.
    bool DragAutoScroll(Point position, f32 dt) override;
    /// Selects the tab under the pointer after a drag has rested over it (as a click would, so `OnSelectionChanged` runs).
    /// - Fails: never.
    bool OnDragHoverDelay(Point position) override;
    /// Returns the listeners called when the user changes the selection (with the view).
    Callback<TabView*>& OnSelectionChanged() { return on_selection_; }
    /// Returns the listeners called when the user clicks a close button (with the view and the tab's index).
    Callback<TabView*, usize>& OnCloseRequested() { return on_close_; }
    /// Returns the headers' height plus the largest ready page.
    Size Measure(const Constraints& constraints) override;
    /// Places the selected page under the headers and collapses the others.
    void Arrange(const Rect& rect) override;
    /// Draws the headers and the arrows.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer, the wheel and the keys.
    void OnEvent(Event& event) override;

private:
    struct Tab {
        TextRun title;
        Widget* page = nullptr;
        PageFactory factory = nullptr;
        void* user = nullptr;
        ImageId image = 0;
        bool closable = false;
        f32 x = 0;
        f32 width = 0;
    };
    enum class Hit : u8 { None, Tab, Close, ArrowLeft, ArrowRight };
    struct HitInfo {
        Hit kind = Hit::None;
        usize tab = kNone;
    };
    void Layout();
    HitInfo HitAt(Point local);
    f32 Span() const;
    f32 MaxScroll() const;
    void SetScroll(f32 scroll);
    void ScrollIntoView(usize index);
    void ShowPage(usize index);
    void UserSelect(usize index);
    f32 CloseSize() const;

    usize InsertionIndex(f32 local_x);

    bool reorderable_ = false;
    usize press_tab_ = kNone;
    Point press_position_;
    usize drop_index_ = kNone;
    Callback<TabView*, usize, usize> on_moved_;
    Vector<Tab> tabs_;
    usize selected_ = kNone;
    usize hover_ = kNone;
    bool hover_close_ = false;
    bool closable_ = false;
    f32 header_h_ = 0;
    f32 total_w_ = 0;
    f32 scroll_ = 0;
    Callback<TabView*> on_selection_;
    Callback<TabView*, usize> on_close_;
};

} // namespace engine_placeholder::ui
