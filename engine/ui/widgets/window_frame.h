#pragma once

#include "ui/callback.h"
#include "ui/text_run.h"
#include "ui/widget.h"

//! The window frame: a panel with a title bar, a content area and an optional footer, that the user
//! can move and resize inside its parent.

namespace vse::ui
{

/// A framed panel: title bar (title text and an optional close button), content, and an optional footer row, in the
/// `window` and `titlebar` styles.
///
/// Dragging the title bar moves the frame; it stays inside its parent's bounds (the surface or a layer). With
/// `SetResizable(true)` the edges and corners resize it (the resize cursors show over them), down to a minimum and up
/// to the parent's bounds. The frame is positioned by its bounds, so put it in a parent that leaves the bounds of its
/// children alone (the root, the popup or modal layer, a plain widget), not in a layout container. The close button,
/// when shown, calls `CloseRequested`, which runs `OnCloseRequested`; the frame does not remove itself.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its content and footer.
class WindowFrame : public Widget
{
public:
    /// The width of the resize band along each edge, in logical units.
    static constexpr f32 kEdge = 6.0f;
    /// The close glyph as a fraction of the theme's icon size.
    static constexpr f32 kCloseGlyph = 0.5f;
    /// The widest the content may be before it wraps, as a multiple of the theme's icon size.
    static constexpr f32 kMaxWidthIcons = 30.0f;
    /// The smallest width a resize leaves, as a multiple of the theme's icon size, unless `SetMinSize` says otherwise.
    static constexpr f32 kMinWidthIcons = 8.0f;

    /// Makes a frame.
    /// - Param name: the widget name.
    /// - Param title: the title text.
    explicit WindowFrame(StringView name = StringView(), StringView title = StringView());
    /// Sets the title; marks layout dirty.
    void SetTitle(StringView title);
    /// Returns the title.
    StringView Title() const { return title_.Text(); }
    /// Sets the content (replacing the old one, which is destroyed).
    /// - Param content: the widget; may be null for none.
    /// - Ownership: the frame owns the content.
    void SetContent(UniquePtr<Widget> content);
    /// Returns the content, or null.
    ///
    /// - Ownership: borrowed; the frame owns it.
    /// - Fails: never.
    Widget* Content() const { return content_; }
    /// Sets the footer (a row of buttons, for example), shown under the content; replaces the old one.
    /// - Param footer: the widget; may be null for none.
    /// - Ownership: the frame owns the footer.
    void SetFooter(UniquePtr<Widget> footer);
    /// Returns the footer, or null.
    ///
    /// - Ownership: borrowed; the frame owns it.
    /// - Fails: never.
    Widget* Footer() const { return footer_; }
    /// Shows a close button in the title bar (default off); marks layout dirty.
    void SetClosable(bool closable);
    /// Returns whether the close button shows.
    ///
    /// - Fails: never.
    bool Closable() const { return closable_; }
    /// Lets the title bar move the frame (default on).
    void SetMovable(bool movable) { movable_ = movable; }
    /// Lets the edges resize the frame (default off).
    void SetResizable(bool resizable) { resizable_ = resizable; }
    /// Sets the smallest size a resize leaves; a size of 0 in either dimension uses the default for it.
    void SetMinSize(Size min) { min_size_ = min; }
    /// Returns the rectangle of the title bar in surface coordinates.
    Rect TitleBarRect();
    /// Returns the rectangle of the close button in surface coordinates (empty when it does not show).
    Rect CloseButtonRect();
    /// Returns the rectangle the content gets in surface coordinates.
    Rect ContentRect();
    /// Returns the listeners called when the close button is pressed (with the frame).
    Callback<WindowFrame*>& OnCloseRequested() { return on_close_; }
    /// Called when the close button is pressed; the default runs `OnCloseRequested`. A [[ui::Dialog]] closes itself
    /// here.
    virtual void CloseRequested();
    /// Returns the size the title, content and footer need, with the frame's padding.
    Size Measure(const Constraints& constraints) override;
    /// Places the title bar, the content and the footer.
    void Arrange(const Rect& rect) override;
    /// Draws the frame, the title bar and the close button.
    void Draw(Canvas& canvas) override;
    /// Handles moving, resizing and the close button.
    void OnEvent(Event& event) override;

private:
    enum Edge : u32
    {
        kLeft = 1,
        kRight = 2,
        kTop = 4,
        kBottom = 8
    };
    f32 TitleHeight();
    u32 EdgesAt(Point local) const;
    Size Floor();
    void Layout();

    TextRun title_;
    Widget* content_ = nullptr;
    Widget* footer_ = nullptr;
    bool closable_ = false;
    bool movable_ = true;
    bool resizable_ = false;
    bool close_hover_ = false;
    bool close_pressed_ = false;
    f32 title_h_ = 0;
    f32 footer_h_ = 0;
    Size min_size_;
    u32 drag_edges_ = 0;
    bool dragging_ = false;
    Point grab_;
    Callback<WindowFrame*> on_close_;
};

}  // namespace vse::ui
