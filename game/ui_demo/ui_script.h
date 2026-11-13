#pragma once

// The ui_demo script (Phase 15, Day 204): words that drive the demo without a person, from the command line (--script)
// and from the tests.

#include "containers/vector.h"
#include "string/string.h"
#include "types.h"
#include "ui/surface.h"
#include "ui_demo_app.h"

namespace ui_demo
{

using namespace engine_placeholder;

/// What the program does for the words the script cannot do on the surface alone.
///
/// - Thread-safety: owner-thread-only.
class ScriptHost
{
public:
    virtual ~ScriptHost() = default;
    /// Resizes the window (or the offscreen surface) to `width` x `height` logical units.
    virtual void Resize(u32 width, u32 height) = 0;
    /// Saves the current picture to `path`.
    virtual void Screenshot(StringView path) = 0;
    /// Runs a word the script does not know (`set:key=value`, `reset`, `loss`, `gpu:next` in the program); returns
    /// false when the host does not know it either.
    virtual bool Other(StringView) { return false; }
};

/// A list of words run one after another, each when its frame comes.
///
/// The words: `frame:N` waits until frame N; `click:name` clicks the middle of the widget called `name`; `type:text`
/// types text (`_` is a space); `key:Name` presses a key (`Enter`, `Tab`, `Escape`, `Space`, `Left`, `Right`, `Up`,
/// `Down`, a letter, `F1`..`F12`, with `ctrl+`, `shortcut+` (Command on macOS, Ctrl elsewhere), `shift+` or `alt+`
/// before it); `resize:WxH`; `theme:dark|light`; `menu:File/About` chooses an item (`Theme/Dark` for one in a submenu);
/// `openmenu:File` opens a menu and leaves it open; `context` opens the context menu of the left panel; `rendering`
/// opens the rendering settings dialog; `drag:name:dx,dy` drags from the middle of the widget by (dx, dy);
/// `wheel:name:n` turns the wheel n notches over the widget; `drop:text:...` and `drop:file:/path` drop text or a file
/// on the widget under the middle of the left panel's text field; `paste:text` puts text on the clipboard and presses
/// the paste shortcut (Cmd+V on macOS, Ctrl+V elsewhere); `shot:path` saves a picture; `quit`.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the words; borrows the app, the surface and the host.
class UiScript
{
public:
    /// Splits `text` into words at spaces. Returns the number of words.
    usize Parse(StringView text);
    /// Runs every word that is due at `frame` (the number of frames drawn so far). Returns false when a word was not
    /// understood or could not be done (its text is in `LastError`).
    bool Run(UiDemoApp* app, ui::Surface* surface, ScriptHost* host, long frame);
    /// Returns whether every word has run.
    bool Done() const { return next_ >= words_.Size(); }
    StringView LastError() const { return error_.View(); }

private:
    bool RunWord(StringView word, UiDemoApp* app, ui::Surface* surface, ScriptHost* host);
    Vector<String<HeapAllocator>> words_;
    usize next_ = 0;
    long wait_until_ = 0;
    String<HeapAllocator> error_ = String<HeapAllocator>(HeapAllocator::Default(), StringView());
};

}  // namespace ui_demo
