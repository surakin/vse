#pragma once

#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"
#include "ui/widgets/menu.h"

//! The menu bar (Phase 15, Day 198): a row of titles, each opening a [[ui::Menu]] below it.

namespace engine_placeholder::ui
{

/// A row of menu titles.
///
/// A click on a title opens its menu; while one is open, moving the pointer to another title switches to it, and Left
/// and Right in the open menu (when it has no submenu to enter or leave) do the same. The bar is also driven from the
/// keyboard: install it with `Surface::SetMenuKeyTarget`, then F10 or a lone Alt press takes the focus and highlights
/// the first title (the mnemonic letters are underlined), Left and Right move, Down, Enter and Space open the menu, a
/// letter opens the title with that mnemonic, Escape (or F10 / Alt again) leaves the bar. Alt with a letter opens the
/// matching menu from anywhere. Leaving the bar (or closing its menu) gives the focus back to the widget that had it.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the menus; owns its title layouts. A menu must outlive the bar.
class MenuBar : public Widget
{
public:
    /// No title.
    static constexpr usize kNone = static_cast<usize>(-1);

    /// Makes an empty bar.
    explicit MenuBar(StringView name = StringView());
    /// Adds a title that opens `menu`.
    /// - Param text: the title, with an optional `&` mnemonic.
    /// - Param menu: the menu; borrowed, must outlive the bar.
    /// - Returns: the title's index.
    /// - Ownership: `menu` is borrowed.
    usize AddMenu(StringView text, Menu* menu);
    /// Returns how many titles there are.
    ///
    /// - Fails: never.
    usize MenuCount() const { return titles_.Size(); }
    /// Returns the title that is highlighted or open, or `kNone`.
    ///
    /// - Fails: never.
    usize Active() const { return active_; }
    /// Opens menu `index` below its title and highlights its first item (does nothing for an index out of range).
    void OpenMenu(usize index);
    /// Returns whether one of the bar's menus is open.
    ///
    /// - Fails: never.
    bool MenuOpen() const;
    /// Closes the open menu, if any.
    void CloseMenu();
    /// Returns a size that fits all the titles in a row.
    Size Measure(const Constraints& constraints) override;
    /// Draws the titles.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer and the keys.
    void OnEvent(Event& event) override;
    /// Notices that the open menu closed.
    void Update(f32 dt) override;
    /// Returns whether a menu is open.
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return open_ != kNone; }

private:
    struct Title
    {
        TextRun run;
        Menu* menu = nullptr;
        char mnemonic = 0;
        u32 mnemonic_offset = 0;
        f32 x = 0;
        f32 width = 0;
    };
    Size Layout();
    usize TitleAt(f32 local_x) const;
    usize TitleWithMnemonic(char letter) const;
    void Enter();
    void Leave();
    void Step(int direction);
    static void MenuEdge(void* user, MenuView* view, int direction);

    Vector<Title> titles_;
    usize active_ = kNone;
    usize open_ = kNone;
    Widget* popup_ = nullptr;
    f32 height_ = 0;
};

}  // namespace engine_placeholder::ui
