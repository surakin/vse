#pragma once

#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/text_field.h"

//! The combo box (Phase 15, Day 198): a closed box showing one item of a list, and a drop-down list to choose another.

namespace vse::ui
{

/// A box that shows the selected item of a [[ui::ListModel]] and opens a drop-down [[ui::ListBox]] (virtualized, so a
/// model of 100,000 items is as cheap as one of ten).
///
/// While closed, Up and Down select the previous or next item (they run `OnSelectionChanged`), Home and End the first
/// and last, letters select by type-ahead, and the wheel selects when `SetWheelSelects(true)`. Alt+Down, F4, Space and
/// Enter open the list, and so does a press on the box. While open, the list has the keyboard (arrows, Home, End,
/// PageUp, PageDown, type-ahead); Enter or a click on an item chooses it and closes the list, Escape or a click
/// elsewhere closes it and keeps the old selection; the focus goes back to the box.
///
/// With `SetEditable(true)` the box holds a [[ui::TextField]]: choosing an item puts its text in the field, and typing
/// text equal to an item selects that item (any other text selects none), so the text and the selection never disagree.
/// Down in the field opens the list.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the model; owns its text field and, while it is open, hands the drop-down list to the surface.
class ComboBox : public Widget
{
public:
    /// The most rows the drop-down list shows before it scrolls.
    static constexpr usize kDropRows = 8;
    /// The width of the arrow column as a multiple of the theme's icon size.
    static constexpr f32 kButtonIcons = 1.5f;
    /// The arrow's half width as a fraction of the arrow column.
    static constexpr f32 kArrowReach = 0.2f;
    /// The seconds after which typed letters start a new prefix.
    static constexpr f64 kTypeAheadSeconds = 1.0;
    /// The value of "no item".
    static constexpr usize kNone = static_cast<usize>(-1);

    /// Makes a combo box with no model.
    explicit ComboBox(StringView name = StringView());
    /// Uses `model` (null for none) and clears the selection.
    /// - Ownership: borrows `model`, which must outlive its use here.
    void SetModel(ListModel* model);
    /// Tells the box the model changed: the selection is cleared when it is no longer a valid index, the text is
    /// refreshed.
    void ModelChanged();
    /// Switches the editable variant on or off; marks layout dirty.
    void SetEditable(bool editable);
    /// Returns whether the box is editable.
    ///
    /// - Fails: never.
    bool Editable() const { return editable_; }
    /// Returns the selected item's index, or `kNone`.
    ///
    /// - Fails: never.
    usize Selected() const { return selected_; }
    /// Selects item `index` (`kNone` for none) and shows its text. Runs no listener.
    /// - Param index: an item, or `kNone`; an index past the end selects none.
    void Select(usize index);
    /// Returns the text shown: the field's text when editable, else the selected item's text (empty for none).
    String<HeapAllocator> Text() const;
    /// Lets the wheel over the closed box select the previous or next item (default off).
    void SetWheelSelects(bool wheel) { wheel_selects_ = wheel; }
    /// Returns the embedded text field (always present; hidden unless editable).
    ///
    /// - Ownership: borrowed; the box owns it.
    /// - Fails: never.
    TextField* Field() const { return field_; }
    /// Opens the drop-down list (does nothing when it is open or the model is empty).
    void Open();
    /// Closes the drop-down list, keeping the selection.
    void Close();
    /// Returns whether the drop-down list is open.
    ///
    /// - Fails: never.
    bool IsOpen() const;
    /// Returns the open drop-down list, or null.
    ///
    /// - Ownership: borrowed; the surface owns it.
    /// - Fails: returns null when the list is closed.
    ListBox* DropDown() const { return IsOpen() ? list_ : nullptr; }
    /// Returns the listeners called when the user changes the selection (with the box).
    Callback<ComboBox*>& OnSelectionChanged() { return on_selection_; }
    /// Returns the size of a list-box wide box plus the arrow column.
    Size Measure(const Constraints& constraints) override;
    /// Places the text field left of the arrow column.
    void Arrange(const Rect& rect) override;
    /// Draws the box, the selected text and the arrow.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer, the wheel, the keys and typed letters.
    void OnEvent(Event& event) override;

private:
    f32 ButtonWidth();
    void UserSelect(usize index);
    void ShowSelected();
    void TypeAhead(Event& event);
    static void Activated(void* user, ListBox* list);
    static void FieldChanged(void* user, TextField* field);

    ListModel* model_ = nullptr;
    usize selected_ = kNone;
    bool editable_ = false;
    bool wheel_selects_ = false;
    bool syncing_ = false;
    TextField* field_ = nullptr;
    ListBox* list_ = nullptr;
    TextRun run_;
    Vector<char> typed_;
    f64 typed_at_ = -1.0e9;
    Callback<ComboBox*> on_selection_;
};

}  // namespace vse::ui
