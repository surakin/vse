#pragma once

#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"
#include "ui/widgets/scroll.h"

//! The list box (Phase 15, Day 196): a virtualized list of text rows with single or multiple selection.

namespace engine_placeholder::ui {

/// The rows of a [[ui::ListBox]].
///
/// Thread-safety: owner-thread-only.
/// Ownership: the model is owned by the program; the list box borrows it.
class ListModel {
public:
    virtual ~ListModel() = default;
    /// Returns how many items there are.
    virtual usize ItemCount() const = 0;
    /// Returns the text of item `index`. The view must stay valid until the next call of this function or of `ItemCount`.
    /// Aborts: `index` is not below `ItemCount()`.
    virtual StringView ItemText(usize index) = 0;
};

/// How many items a [[ui::ListBox]] can select.
enum class SelectionMode : u8 {
    Single,   ///< At most one.
    Multiple, ///< Any number: Ctrl+click toggles, Shift+click and Shift+arrows select a range from the anchor.
};

/// A scrolling list of text items from a model.
///
/// The list is virtual: rows are all one height, so the item under a point and the range in view come from arithmetic, and only the rows in view are laid out and drawn (a ring of cached
/// text layouts re-used as the list scrolls); 100,000 items cost the same per frame as 100. The list scrolls with its own vertical bar and with the wheel. Click selects (with Ctrl or Shift
/// in multiple mode), the arrow keys, PageUp, PageDown, Home and End move the current item and select it, Space selects it, Enter runs `OnActivate`; typing letters selects the next item whose text starts with what was typed
/// (typed quickly, the letters make a prefix; the same letter repeated cycles through the items starting with it). After the model changes call `ModelChanged`.
///
/// Thread-safety: owner-thread-only.
/// Ownership: borrows the model; owns the selection and the row cache.
class ListBox : public Widget {
public:
    /// The wheel moves this many rows per notch.
    static constexpr f32 kWheelRows = 3.0f;
    /// The seconds after which typed letters start a new prefix.
    static constexpr f64 kTypeAheadSeconds = 1.0;
    /// The preferred height of the list in rows.
    static constexpr f32 kPreferredRows = 6.0f;
    /// The preferred width of the list as a multiple of the theme's icon size.
    static constexpr f32 kWidthInIcons = 10.0f;

    /// Makes an empty list box.
    explicit ListBox(StringView name = StringView());
    /// Uses `model` (null for none); clears the selection and the scroll position.
    ///
    /// Ownership: borrows `model`, which must outlive its use here.
    void SetModel(ListModel* model);
    /// Tells the list box that the model's items or their count changed; the selection is cleared when the count changed, and the scroll position clamped.
    void ModelChanged();
    /// Sets the selection mode (default single); clears the selection when it becomes single and more than one is selected.
    void SetMode(SelectionMode mode);
    /// Returns whether item `index` is selected.
    ///
    /// Fails: returns false for an index outside the list.
    bool IsSelected(usize index) const;
    /// Returns how many items are selected.
    ///
    /// Fails: never.
    usize SelectedCount() const { return selected_count_; }
    /// Returns the current item: where the keyboard is (the last one clicked or moved to), or `kNone`.
    ///
    /// Fails: never.
    usize Current() const { return current_; }
    /// The value of "no item".
    static constexpr usize kNone = static_cast<usize>(-1);
    /// Selects only item `index` (`kNone` clears the selection), makes it current and scrolls to it. Runs the `OnSelectionChanged` listeners when the selection changed.
    void Select(usize index);
    /// Scrolls the least that makes item `index` fully visible.
    void ScrollToIndex(usize index);
    /// Returns the scroll offset in logical units.
    ///
    /// Fails: never.
    f32 Offset() const { return offset_; }
    /// Sets the scroll offset in logical units, clamped to the content (the scroll bar follows).
    void SetOffset(f32 offset);
    /// Returns the height of a row in logical units (0 before the first layout).
    ///
    /// Fails: never.
    f32 RowHeight() const { return row_height_; }
    /// Returns the index range in view as the first index and one past the last (empty when there is no model).
    ///
    /// Ownership: writes the two indices; both pointers must be non-null.
    void VisibleRange(usize* first, usize* end) const;
    /// Returns how many rows were laid out (not found in the cache) since the box was made, so a test can see that scrolling only lays out new rows.
    u64 RowLayouts() const { return row_layouts_; }
    /// Returns the listeners called when the selection changes (with the box).
    Callback<ListBox*>& OnSelectionChanged() { return on_selection_; }
    /// Returns the listeners called when Enter goes down on the current item (with the box).
    Callback<ListBox*>& OnActivate() { return on_activate_; }
    /// Returns a size of `kPreferredRows` rows and `kWidthInIcons` icons.
    Size Measure(const Constraints& constraints) override;
    /// Places the scroll bar and clamps the offset.
    void Arrange(const Rect& rect) override;
    /// Draws the box and the rows in view.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer, the wheel, the keys and typed letters.
    void OnEvent(Event& event) override;

private:
    struct Slot {
        usize index = kNone;
        u64 revision = 0;
        TextRun run;
    };
    f32 ViewHeight() const;
    f32 MaxOffset() const;
    usize IndexAt(f32 local_y) const;
    void SyncModel();
    bool SetSelectedFlag(usize index, bool selected);
    bool ClearSelection();
    bool SelectRange(usize a, usize b);
    bool SelectOnly(usize index);
    void Notify();
    void MoveCurrent(usize to, bool extend);
    void Click(usize index, u32 modifiers);
    void TypeAhead(Event& event);
    bool MeasureRow();
    static void BarMoved(void* user, ScrollBar* bar);

    ListModel* model_ = nullptr;
    SelectionMode mode_ = SelectionMode::Single;
    Vector<u8> selected_;
    usize selected_count_ = 0;
    usize count_ = 0;
    usize current_ = kNone;
    usize anchor_ = kNone;
    usize hover_ = kNone;
    f32 offset_ = 0;
    f32 row_height_ = 0;
    u64 revision_ = 1;
    u64 row_layouts_ = 0;
    ScrollBar* bar_ = nullptr;
    Vector<Slot> slots_;
    TextRun probe_;
    Vector<char> typed_;
    f64 typed_at_ = -1.0e9;
    Callback<ListBox*> on_selection_;
    Callback<ListBox*> on_activate_;
};

} // namespace engine_placeholder::ui
