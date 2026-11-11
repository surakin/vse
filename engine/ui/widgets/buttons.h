#pragma once

#include "ui/callback.h"
#include "ui/layout.h"
#include "ui/text_run.h"
#include "ui/theme.h"
#include "ui/widget.h"

//! The button widgets (Phase 15, Day 195): button, toggle button, check box, radio button and radio group.

namespace engine_placeholder::ui {

class RadioGroup;

/// A push button: text and/or an icon on a themed box.
///
/// It clicks when the left button is released inside it after being pressed inside it (a press followed by a drag out and a release elsewhere does not click), and when Space or
/// Enter goes down on it while it has the focus. `OnClick` listeners then run. The look is the `button` style of the theme in the widget's state (normal, hover, pressed, focused,
/// disabled; a toggle's checked state too) and a variant such as `primary`; a state change repaints the widget and does not touch layout. A disabled button gets no input.
/// The default-button flag only marks the button a dialog should press on Enter; the dialog (Day 203) acts on it.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its text; borrows the icon's image.
class Button : public Widget {
public:
    /// Makes a button.
    /// - Param name: the widget name; also what a test finds it by.
    /// - Param text: the UTF-8 label, may be empty (an icon button).
    explicit Button(StringView name = StringView(), StringView text = StringView());
    /// Sets the text; marks layout dirty when it changes.
    void SetText(StringView text);
    /// Returns the text.
    StringView GetText() const { return run_.Text(); }
    /// Shows `image` (an icon of the theme's icon size, drawn left of the text); 0 for none. Marks layout dirty.
    void SetImage(ImageId image);
    /// Uses the style variant called `variant` (for example `primary`); the empty name is the default style. Marks layout dirty.
    void SetVariant(StringView variant);
    /// Sets the default-button flag.
    void SetDefault(bool is_default) { default_ = is_default; }
    /// Returns the default-button flag.
    ///
    /// - Fails: never.
    bool IsDefault() const { return default_; }
    /// Returns the listeners called after a click (with the button).
    Callback<Button*>& OnClick() { return on_click_; }
    /// Clicks the button as the pointer or the keyboard would. Does nothing while the button is disabled.
    void Click();
    /// Returns the size of the content plus the padding of the style.
    Size Measure(const Constraints& constraints) override;
    /// Draws the box, the leading element (icon or indicator) and the text.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer and the keys described in the class comment.
    void OnEvent(Event& event) override;

protected:
    /// Makes a button of another widget kind (the check box and the radio button).
    Button(WidgetKind kind, StringView name, StringView text);
    /// Called by `Click` before the listeners run; a toggle changes its state here.
    virtual void Activated() {}
    /// Returns whether the whole bounds are painted as a box (a button) or only the leading element (a check box).
    virtual bool PaintsBox() const { return true; }
    /// Returns whether the content is centered (a button) or starts at the left padding (a check box).
    virtual bool CentersContent() const { return true; }
    /// Returns the size of the leading element in logical units.
    virtual Size LeadingSize(f32 scale);
    /// Draws the leading element in `rect`.
    virtual void DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale);
    /// Returns the style variant.
    VariantId Variant() const { return variant_; }
    /// Returns the widget kind whose style is used.
    WidgetKind Kind() const { return kind_; }

private:
    ResolvedStyle Look(f32 scale);
    WidgetKind kind_;
    VariantId variant_ = 0;
    TextRun run_;
    ImageId image_ = 0;
    bool default_ = false;
    Callback<Button*> on_click_;
};

/// A button that stays down: each click flips its checked state.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: as [[ui::Button]].
class ToggleButton : public Button {
public:
    /// Makes a toggle button, unchecked.
    explicit ToggleButton(StringView name = StringView(), StringView text = StringView()) : Button(name, text) {}
    /// Sets the state without running the listeners; marks the draw dirty.
    void SetOn(bool on) { SetChecked(on); }
    /// Returns whether the button is down.
    ///
    /// - Fails: never.
    bool On() const { return Checked(); }
    /// Returns the listeners called when a click changes the state (with the button).
    Callback<ToggleButton*>& OnToggled() { return on_toggled_; }

protected:
    /// Flips the state and runs the `OnToggled` listeners.
    void Activated() override;

private:
    Callback<ToggleButton*> on_toggled_;
};

/// The three states of a [[ui::CheckBox]].
enum class CheckState : u8 {
    Unchecked,     ///< Empty box.
    Checked,       ///< Box with a mark; the widget's checked bit is set.
    Indeterminate, ///< Box with a dash: neither (a parent of a mixed set); only with `SetTriState(true)`.
};

/// A box with a label that is checked or not (or, optionally, neither).
///
/// A click cycles unchecked, checked and, in tri-state mode, indeterminate. The box is the theme's icon size; its colors come from the `checkbox` style (the checked state fills with the
/// accent) and the mark from the window background color.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: as [[ui::Button]].
class CheckBox : public Button {
public:
    /// Makes an unchecked check box.
    explicit CheckBox(StringView name = StringView(), StringView text = StringView()) : Button(widget_kind::kCheckbox, name, text) {}
    /// Allows the indeterminate state in the click cycle (default off). Turning it off while indeterminate makes the box unchecked.
    void SetTriState(bool tri_state);
    /// Returns whether the indeterminate state is part of the cycle.
    ///
    /// - Fails: never.
    bool TriState() const { return tri_state_; }
    /// Sets the state without running the listeners; marks the draw dirty.
    void SetCheckState(CheckState state);
    /// Returns the state.
    CheckState GetCheckState() const { return state_; }
    /// Returns the listeners called when a click changes the state (with the box).
    Callback<CheckBox*>& OnChanged() { return on_changed_; }

protected:
    /// Moves to the next state and runs the listeners.
    void Activated() override;
    /// Only the box is painted.
    bool PaintsBox() const override { return false; }
    /// The content starts at the left.
    bool CentersContent() const override { return false; }
    /// Returns a square of the icon size.
    Size LeadingSize(f32 scale) override;
    /// Draws the box with its mark.
    void DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale) override;

private:
    CheckState state_ = CheckState::Unchecked;
    bool tri_state_ = false;
    Callback<CheckBox*> on_changed_;
};

/// A round choice that belongs to a [[ui::RadioGroup]].
///
/// Clicking selects it (and deselects the others of its group); the arrow keys move the selection (and the focus) within the group, wrapping at the ends. Without a group ancestor it
/// only turns itself on. Its look is the `radio` style of the theme.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: as [[ui::Button]].
class RadioButton : public Button {
public:
    /// Makes a radio button, not selected.
    explicit RadioButton(StringView name = StringView(), StringView text = StringView());
    /// Returns whether this button is the selected one of its group.
    ///
    /// - Fails: never.
    bool Selected() const { return Checked(); }
    /// Returns the group this button belongs to (its nearest ancestor that is a group), or null.
    ///
    /// - Ownership: borrowed; the tree owns it.
    /// - Fails: returns null when there is none.
    RadioGroup* Group() const;
    /// Moves the selection with the arrow keys; other events as [[ui::Button]].
    void OnEvent(Event& event) override;

protected:
    /// Selects this button in its group.
    void Activated() override;
    /// Only the dot is painted.
    bool PaintsBox() const override { return false; }
    /// The content starts at the left.
    bool CentersContent() const override { return false; }
    /// Returns a square of the icon size.
    Size LeadingSize(f32 scale) override;
    /// Draws the circle with its dot.
    void DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale) override;
};

/// A container whose [[ui::RadioButton]] descendants are one choice: exactly one or none selected.
///
/// A vertical [[ui::BoxLayout]]. Once a button is selected only it is a Tab stop (the others have `TabIndex` -1), so Tab enters the group once and the arrow keys move inside it.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class RadioGroup : public BoxLayout {
public:
    /// The role tag of a group (see [[ui::Widget::Role]]).
    static constexpr u32 kRole = 1;
    /// The role tag of a radio button.
    static constexpr u32 kButtonRole = 2;
    /// Makes an empty group.
    explicit RadioGroup(StringView name = StringView());
    /// Selects `button` (one of the group's descendants; any other button is ignored) and deselects the rest; null clears the selection. Runs the `OnChanged` listeners when the selection changes.
    /// - Param button: the button, or null.
    /// - Ownership: borrows `button`; it stays owned by the tree.
    void Select(RadioButton* button);
    /// Returns the selected button, or null.
    ///
    /// - Ownership: borrowed; the tree owns it.
    /// - Fails: returns null when none is selected.
    RadioButton* Selected() const;
    /// Moves the selection from `from` by one enabled, shown button (`delta` -1 or 1), wrapping, and gives it the focus.
    /// - Ownership: borrows `from`; it stays owned by the tree.
    void Step(RadioButton* from, int delta);
    /// Returns the listeners called when the selection changes (with the group).
    Callback<RadioGroup*>& OnChanged() { return on_changed_; }

private:
    void Collect(Widget* widget, Vector<RadioButton*>& out) const;
    Callback<RadioGroup*> on_changed_;
};

} // namespace engine_placeholder::ui
