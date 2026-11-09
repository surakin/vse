#pragma once

#include "containers/vector.h"
#include "ui/callback.h"
#include "ui/clipboard.h"
#include "ui/text_run.h"
#include "ui/widget.h"

//! The single-line text field (Phase 15, Day 197): UTF-8 text edited by grapheme cluster with the keyboard and the mouse, a selection, word movement, undo and redo, and the clipboard.

namespace engine_placeholder::ui {

class TextField;

/// Decides whether a proposed text may replace the field's text: the function gets the pointer given to `SetValidator`, the field and the whole text the edit would make (valid for
/// the call only), and returns false to keep the old text.
using TextValidator = bool (*)(void* user, const TextField* field, StringView proposed);

/// An editable line of text.
///
/// Offsets are byte offsets into the UTF-8 text and are always on grapheme boundaries (`text::NextBoundary` and `text::PreviousBoundary` over the field's layout): a combining mark,
/// a flag, a ZWJ sequence or a Hangul syllable moves and deletes as one unit. Only `Text` events insert, so a key the field does not know does nothing; there is no input method
/// and no complex-script shaping (Phase 14 scope). Ctrl+Left and Ctrl+Right move by word ([[ui::WordStart]], [[ui::WordEnd]]). The field scrolls sideways to keep the caret in view.
///
/// The keys are Left, Right, Home, End (Shift extends the selection, Ctrl moves by word), Backspace, Delete (Ctrl: by word), Ctrl+A, Ctrl+C / Ctrl+Insert, Ctrl+X / Shift+Delete, Ctrl+V /
/// Shift+Insert, Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y, Enter (`OnSubmit`), Escape (`OnCancel`). Tab, Up and Down are left to the surface and the parents. The mouse: a click places the caret,
/// Shift+click extends, a drag selects (the pointer is captured), a double click selects a word, a triple click all.
///
/// Edits go through one path that applies, in order, the read-only flag, the maximum length (in clusters; a longer insertion is cut), and the validator; an edit that changes
/// nothing is not recorded. Consecutive typing, and consecutive Backspace or Delete presses, are one undo step. A password field draws bullets, refuses copy and cut, and still
/// moves and edits by cluster.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns its text, its undo history and its layouts; borrows the validator's user pointer and the surface's clipboard.
class TextField : public Widget {
public:
    /// The preferred width, as a multiple of the theme's icon size.
    static constexpr f32 kWidthInIcons = 8.0f;
    /// Seconds the caret stays on, and then off.
    static constexpr f64 kBlinkSeconds = 0.5;
    /// Seconds within which a second press counts as a double click, and a third as a triple.
    static constexpr f64 kMultiClickSeconds = 0.4;
    /// Logical units a press may be away from the previous one and still count as a multiple click.
    static constexpr f32 kClickSlop = 4.0f;
    /// Logical units kept between the caret and the edge of the field when it scrolls.
    static constexpr f32 kScrollMargin = 8.0f;
    /// The caret's width in logical units.
    static constexpr f32 kCaretWidth = 1.0f;
    /// The most undo steps kept; the oldest is dropped.
    static constexpr usize kMaxUndo = 100;
    /// The alpha (0 to 255) the accent color gets for the selection.
    static constexpr u32 kSelectionAlpha = 96;
    /// What a password field draws for each cluster (U+2022), and its length in bytes.
    static constexpr char kBullet[] = "\xE2\x80\xA2";
    /// The length of `kBullet` in bytes.
    static constexpr u32 kBulletBytes = 3;

    /// Makes an empty field that takes the focus, accepts text and shows the I-beam.
    explicit TextField(StringView name = StringView());
    /// Cancels a paste still waiting for the clipboard.
    ~TextField() override;

    /// Replaces the text without running a listener or the validator; the caret goes to the end, the selection is dropped and the undo history is cleared.
    /// Param text: UTF-8 text; cut to the maximum length.
    /// Fails: never.
    void SetText(StringView text);
    /// Returns the text.
    ///
    /// Ownership: borrowed; valid until the next edit.
    StringView Text() const { return StringView(text_.Data(), text_.Size()); }
    /// Sets the text shown while the field is empty and not focused.
    void SetPlaceholder(StringView text);
    /// Sets whether edits are refused (the caret, the selection and copy still work).
    void SetReadOnly(bool read_only) { read_only_ = read_only; }
    /// Returns whether edits are refused.
    ///
    /// Fails: never.
    bool ReadOnly() const { return read_only_; }
    /// Sets whether the field draws bullets and refuses copy and cut.
    void SetPassword(bool password);
    /// Returns whether the field is a password field.
    ///
    /// Fails: never.
    bool Password() const { return password_; }
    /// Sets the most clusters the text may have (0 for no limit); text already longer is kept until edited.
    void SetMaxLength(usize clusters) { max_length_ = clusters; }
    /// Returns the maximum length in clusters.
    usize MaxLength() const { return max_length_; }
    /// Sets the validator (null removes it).
    /// Param function: the check.
    /// Param user: passed to it; borrowed.
    /// Ownership: `user` is borrowed until the validator is replaced or the field is destroyed.
    void SetValidator(TextValidator function, void* user) {
        validator_ = function;
        validator_user_ = user;
    }
    /// Sets whether the field draws its own box (the default); a parent that draws the box turns it off.
    void SetFrame(bool frame);
    /// Returns the caret's byte offset.
    u32 Caret() const { return caret_; }
    /// Returns the byte offset of the other end of the selection (the caret's when nothing is selected).
    u32 Anchor() const { return anchor_; }
    /// Returns whether text is selected.
    ///
    /// Fails: never.
    bool HasSelection() const { return caret_ != anchor_; }
    /// Selects from `anchor` to `caret`; both are snapped to grapheme boundaries and clamped to the text.
    void SetSelection(u32 anchor, u32 caret);
    /// Selects all the text.
    void SelectAll();
    /// Returns the selected text (empty for none); valid until the next edit.
    StringView SelectedText() const;
    /// Undoes the last edit step; returns whether there was one.
    ///
    /// Fails: returns false when there is nothing to undo.
    bool Undo();
    /// Redoes the last undone step; returns whether there was one.
    ///
    /// Fails: returns false when there is nothing to redo.
    bool Redo();
    /// Returns whether `Undo` has a step.
    ///
    /// Fails: never.
    bool CanUndo() const { return !undo_.Empty(); }
    /// Returns whether `Redo` has a step.
    ///
    /// Fails: never.
    bool CanRedo() const { return !redo_.Empty(); }
    /// Returns the caret rectangle in surface coordinates (what is drawn when it is on), after the last layout of the text; empty before the first.
    Rect CaretRect();
    /// Returns how far the text is scrolled sideways, in logical units.
    f32 ScrollX() const { return scroll_x_; }
    /// Returns whether the caret is in the on part of its blink at the surface's time.
    ///
    /// Fails: never.
    bool CaretOn() const;
    /// Returns the listeners called after the user (or undo) changed the text.
    Callback<TextField*>& OnChanged() { return on_changed_; }
    /// Returns the listeners called on Enter.
    Callback<TextField*>& OnSubmit() { return on_submit_; }
    /// Returns the listeners called on Escape.
    Callback<TextField*>& OnCancel() { return on_cancel_; }
    /// Returns the listeners called when the field loses the keyboard focus.
    Callback<TextField*>& OnFocusLost() { return on_focus_lost_; }
    /// Returns a width of `kWidthInIcons` icons and a height of one line plus the padding.
    Size Measure(const Constraints& constraints) override;
    /// Draws the box, the selection, the text (or the placeholder) and the caret.
    void Draw(Canvas& canvas) override;
    /// Handles the pointer, the keys and typed text.
    void OnEvent(Event& event) override;
    /// Repaints when the caret's blink changes.
    void Update(f32 dt) override;
    /// Returns whether the field has the focus (the caret blinks).
    ///
    /// Fails: never.
    bool WantsUpdate() const override;

private:
    enum class EditKind : u8 { Typing, Backspace, Delete, Other };
    struct Edit {
        u32 at = 0;
        Vector<char> removed;
        Vector<char> inserted;
        u32 caret_before = 0;
        u32 anchor_before = 0;
        EditKind kind = EditKind::Other;
    };

    bool Prepare();
    u32 Length() const { return static_cast<u32>(text_.Size()); }
    u32 NextCluster(u32 offset);
    u32 PreviousCluster(u32 offset);
    u32 Snap(u32 offset);
    u32 ToShown(u32 offset) const;
    u32 FromShown(u32 shown) const;
    TextRun& Shown() { return password_ ? mask_ : run_; }
    u32 OffsetAtX(f32 local_x);
    void MoveCaret(u32 to, bool extend);
    void ResetBlink();
    void EnsureCaretVisible();
    bool Replace(u32 begin, u32 end, StringView insert, EditKind kind);
    void Apply(u32 at, u32 remove_length, StringView insert);
    void DeleteSelectionOrStep(bool forward, bool by_word);
    void Copy(bool cut);
    void Paste();
    static void PasteReady(void* user, StringView text);
    void InsertPasted(StringView text);
    void Press(Event& event);
    void Notify();
    f32 PaddingX(const ResolvedStyle& style) const;

    Vector<char> text_;
    TextRun run_;
    TextRun mask_;
    TextRun placeholder_run_;
    Vector<char> mask_text_; // one bullet per cluster of the text
    Vector<char> placeholder_;
    Vector<Edit> undo_;
    Vector<Edit> redo_;
    u32 caret_ = 0;
    u32 anchor_ = 0;
    f32 scroll_x_ = 0;
    bool read_only_ = false;
    bool password_ = false;
    bool frame_ = true;
    bool chain_ = false; // the next edit may join the last undo step
    usize max_length_ = 0;
    TextValidator validator_ = nullptr;
    void* validator_user_ = nullptr;
    f64 blink_epoch_ = 0;
    bool blink_shown_ = true;
    f64 last_press_time_ = -1e9;
    Point last_press_ = Point();
    u32 press_count_ = 0;
    Clipboard::Ticket paste_ticket_ = 0;
    Clipboard* paste_clipboard_ = nullptr;
    Callback<TextField*> on_changed_;
    Callback<TextField*> on_submit_;
    Callback<TextField*> on_cancel_;
    Callback<TextField*> on_focus_lost_;
};

} // namespace engine_placeholder::ui
