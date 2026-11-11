#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "string/string.h"
#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"

//! Menus (Phase 15, Day 198): the `Menu` model of items, and `MenuView`, the widget a surface shows as a popup for it.

namespace engine_placeholder::ui
{

class Menu;

/// What a menu item does when it is chosen: called with the user pointer and the item's index.
using MenuAction = void (*)(void* user, usize index);

/// The kinds of menu item.
enum class MenuItemKind : u8
{
    Normal,   ///< Runs its action.
    Check,    ///< Toggles a check mark, then runs its action.
    Radio,    ///< Checks itself and unchecks the radio items next to it (the run of consecutive radio items), then runs
              ///< its action.
    Submenu,  ///< Opens a submenu.
    Separator,  ///< A line; never highlighted.
};

/// One item of a [[ui::Menu]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its text and its submenu.
struct MenuItem
{
    String<HeapAllocator> text = String<HeapAllocator>(
        HeapAllocator::Default(),
        StringView());  ///< The text; `&` before a letter marks it as the mnemonic, `&&` is a literal `&`.
    MenuItemKind kind = MenuItemKind::Normal;  ///< What the item is.
    bool enabled = true;                       ///< A disabled item is drawn dimmed and cannot be chosen.
    bool checked = false;                      ///< The check mark of a check or radio item.
    MenuAction action = nullptr;               ///< Run when the item is chosen; may be null.
    void* user = nullptr;                      ///< Passed to `action`; borrowed.
    u32 accelerator = 0;      ///< The id of a surface accelerator whose chord is shown at the right and whose off state
                              ///< dims the item (0 for none).
    UniquePtr<Menu> submenu;  ///< The submenu of a `Submenu` item.
};

/// A list of items. It is a model, not a widget: a [[ui::MenuView]] shows it, and a menu may be shown any number of
/// times.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its items and their submenus. A menu must outlive the views that show it.
class Menu
{
public:
    /// Makes an empty menu.
    Menu();
    /// Destroys the items and their submenus.
    ~Menu();
    /// Adds a plain item.
    /// - Param text: the text, with an optional `&` mnemonic.
    /// - Param action: run when chosen; may be null.
    /// - Param user: passed to it; borrowed.
    /// - Returns: the item's index.
    /// - Ownership: `user` is borrowed; the menu copies the text.
    usize AddItem(StringView text, MenuAction action = nullptr, void* user = nullptr);
    /// Adds an item with a check mark.
    /// - Param checked: the initial state.
    /// - Ownership: `user` is borrowed.
    usize AddCheck(StringView text, bool checked, MenuAction action = nullptr, void* user = nullptr);
    /// Adds a radio item (see [[ui::MenuItemKind]]).
    /// - Ownership: `user` is borrowed.
    usize AddRadio(StringView text, bool checked, MenuAction action = nullptr, void* user = nullptr);
    /// Adds an item that opens `submenu`.
    /// - Param submenu: the menu to open; the item owns it.
    /// - Ownership: ownership passes to the menu.
    usize AddSubmenu(StringView text, UniquePtr<Menu> submenu);
    /// Adds a separator line.
    usize AddSeparator();
    /// Returns how many items there are.
    ///
    /// - Fails: never.
    usize ItemCount() const { return items_.Size(); }
    /// Returns an item.
    /// - Aborts: `index` is out of range.
    const MenuItem& Item(usize index) const;
    /// Enables or disables an item.
    /// - Aborts: `index` is out of range.
    void SetItemEnabled(usize index, bool enabled);
    /// Checks or unchecks an item; checking a radio item unchecks its neighbours in the run.
    /// - Aborts: `index` is out of range.
    void SetItemChecked(usize index, bool checked);
    /// Shows the chord of surface accelerator `id` at the right of the item, and dims the item while that accelerator
    /// is off.
    /// - Param id: an id from [[ui::Surface::RegisterAccelerator]].
    /// - Aborts: `index` is out of range.
    void SetItemAccelerator(usize index, u32 id);
    /// Returns the submenu of an item, or null.
    /// - Ownership: borrowed; the menu owns it.
    /// - Fails: returns null for an item without a submenu.
    Menu* Submenu(usize index) const;
    /// Runs the state change and the action of item `index` as choosing it does (a check toggles, a radio selects),
    /// without closing any popup. Returns whether anything ran.
    /// - Fails: returns false for a disabled item, a separator or a submenu item.
    bool Choose(usize index);

private:
    Vector<MenuItem> items_;
};

/// Adds the Edit commands Cut, Copy, Paste and Select All (with a separator before Select All) to `menu`; choosing one
/// runs it on `surface->EditTarget()`.
/// - Param menu: the menu to add to.
/// - Param surface: the surface whose focus owner is edited; borrowed, it must outlive the menu.
/// - Returns: the index of the Cut item (Copy, Paste follow, then the separator and Select All).
/// - Ownership: borrows `surface`.
usize AddEditItems(Menu& menu, Surface* surface);
/// The offsets of the four items `AddEditItems` adds from the Cut item (a separator lies between Paste and Select All),
/// and their count.
constexpr usize kEditItemOffsets[] = {0, 1, 2, 4};
/// The number of Edit items.
constexpr usize kEditItemCount = sizeof(kEditItemOffsets) / sizeof(kEditItemOffsets[0]);

/// Enables or disables the four items `AddEditItems` added, from what the edit target can do now (`Widget::CanEdit`);
/// call it when the menu is about to show. A target that cannot edit disables all four.
/// - Param first: the index `AddEditItems` returned.
void RefreshEditItems(Menu& menu, usize first, const Surface& surface);

/// Splits `text` at its mnemonic marker: copies it without the `&` markers into `label` and returns the mnemonic letter
/// in lower case (0 for none).
/// - Param text: the text as given to the menu.
/// - Param label: receives the text to display; cleared first.
/// - Param offset: receives the byte offset of the mnemonic letter in `label` (unchanged when there is none).
/// - Returns: the mnemonic, lower case, or 0.
/// - Ownership: `label` and `offset` are written; both must be non-null.
/// - Fails: never.
char SplitMnemonic(StringView text, Vector<char>* label, u32* offset);

/// The popup that shows a [[ui::Menu]]: a column of rows with a highlight that follows the pointer and the arrow keys.
///
/// Up and Down move the highlight over the enabled items and wrap; Home and End jump; Enter and Space choose; a letter
/// chooses the item with that mnemonic; Right opens a submenu (and moves into it), Left closes a submenu; hovering a
/// submenu item opens it after `kSubmenuDelay`. Choosing a leaf item runs its action and closes every popup. A left or
/// right arrow that the menu cannot use goes to `OnEdge` (the menu bar switches menus there). Escape and clicks outside
/// are the surface's popup rules.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows or owns its menu (see the constructors); owns nothing else.
class MenuView : public Widget
{
public:
    /// Seconds the pointer must rest on a submenu item before the submenu opens.
    static constexpr f64 kSubmenuDelay = 0.3;
    /// The size of the check, radio and arrow marks, as a fraction of the row height.
    static constexpr f32 kMarkFraction = 0.4f;
    /// Where the elbow of the check mark lies, as fractions of the mark size across and down.
    static constexpr f32 kCheckElbowX = 0.3f;
    /// See `kCheckElbowX`.
    static constexpr f32 kCheckElbowY = 0.6f;
    /// The thickness of the check mark and the mnemonic underline, in logical units.
    static constexpr f32 kStroke = 1.0f;
    /// No item.
    static constexpr usize kNone = static_cast<usize>(-1);

    /// Shows `menu`, which the caller keeps alive for as long as the view exists.
    /// - Param menu: the menu; borrowed.
    /// - Ownership: borrowed; the menu must outlive the view.
    explicit MenuView(Menu* menu);
    /// Shows `menu` and owns it (a context menu built for one use).
    /// - Param menu: the menu; ownership passes to the view.
    explicit MenuView(UniquePtr<Menu> menu);
    /// Returns the menu shown.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    Menu* GetMenu() const { return menu_; }
    /// Returns the highlighted item, or `kNone`.
    ///
    /// - Fails: never.
    usize Highlight() const { return highlight_; }
    /// Highlights item `index` when it can be chosen (otherwise nothing changes); `kNone` clears the highlight.
    void SetHighlight(usize index);
    /// Highlights the first item that can be chosen (what opening by keyboard does).
    void HighlightFirst();
    /// Returns the rectangle of row `index` in surface coordinates (empty before the first layout or for an index out
    /// of range).
    Rect RowRect(usize index) const;
    /// Returns whether a submenu opened from this view is open.
    ///
    /// - Fails: never.
    bool SubmenuOpen() const;
    /// Returns the listeners called with -1 or +1 for a Left or Right key this view could not use itself.
    Callback<MenuView*, int>& OnEdge() { return on_edge_; }
    /// Returns the width of the widest row plus the marks, and the height of all rows.
    Size Measure(const Constraints& constraints) override;
    /// Draws the box and the rows.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer and the keys.
    void OnEvent(Event& event) override;
    /// Counts the submenu delay.
    void Update(f32 dt) override;
    /// Returns whether a submenu is about to open.
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return pending_ != kNone; }

private:
    struct Row
    {
        TextRun label;
        TextRun accel;
        u32 mnemonic_offset = 0;
        char mnemonic = 0;
        f32 top = 0;
        f32 height = 0;
    };
    void Rebuild();
    bool CanChoose(usize index) const;
    usize RowAt(f32 local_y) const;
    void Move(int direction);
    void Choose(usize index);
    void OpenSub(usize index, bool take_focus);
    void CloseSub();

    Menu* menu_ = nullptr;
    UniquePtr<Menu> owned_;
    Vector<Row> rows_;
    usize highlight_ = kNone;
    usize pressed_ = kNone;
    usize pending_ = kNone;
    f64 pending_time_ = 0;
    usize open_index_ = kNone;
    Widget* sub_ = nullptr;
    bool is_submenu_ = false;
    f32 label_x_ = 0;
    f32 content_width_ = 0;
    f32 content_height_ = 0;
    f32 accel_right_ = 0;
    Callback<MenuView*, int> on_edge_;
};

}  // namespace engine_placeholder::ui
