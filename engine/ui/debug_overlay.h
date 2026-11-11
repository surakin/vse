#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "string/string.h"
#include "types.h"
#include "ui/layout.h"
#include "ui/popup.h"
#include "ui/widgets/label.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/text_field.h"

//! The debug overlay (Phase 15, Day 203): a frame-time graph, counters fed by the program, and a log console with a command line.

namespace engine_placeholder::ui {

class Surface;

/// The log lines the console shows: a bounded copy of the log core's ring, read on the UI thread.
///
/// `Poll` reads the lines logged since the last call with `ReadLogLinesSince` (thread-safe, so any thread may log) and keeps the newest `kMaxLines`; the console itself adds its own output
/// with `Append`. A filter keeps only the lines that contain a text (a level name such as `WARN` works, as the lines carry it).
///
/// - Thread-safety: owner-thread-only; the log it reads is thread-safe.
/// - Ownership: owns the copied lines.
class LogCapture {
public:
    /// The most lines kept; older ones are dropped.
    static constexpr usize kMaxLines = 512;

    /// Makes an empty capture that starts at the lines logged from now on.
    LogCapture();
    /// Copies the lines logged since the last call (the oldest the ring still holds on the first call after `Restart`).
    /// - Returns: how many lines were added.
    usize Poll();
    /// Forgets all lines; the lines already logged are not read again, only those logged later.
    void Clear();
    /// Adds a line of the console's own (no trailing newline needed).
    void Append(StringView line);
    /// Keeps only lines that contain `text` in `VisibleCount`/`VisibleLine` (empty for all).
    void SetFilter(StringView text);
    /// Returns the filter.
    ///
    /// - Fails: never.
    StringView Filter() const { return filter_.View(); }
    /// Returns how many lines are kept, filtered or not.
    ///
    /// - Fails: never.
    usize Count() const { return count_; }
    /// Returns how many lines pass the filter.
    ///
    /// - Fails: never.
    usize VisibleCount() const;
    /// Returns the `index`-th line that passes the filter, oldest first, without its newline.
    ///
    /// - Aborts: `index` is not below `VisibleCount()`.
    StringView VisibleLine(usize index) const;
    /// Returns how many lines were dropped to stay within `kMaxLines`.
    ///
    /// - Fails: never.
    u64 Dropped() const { return dropped_; }

private:
    static void Take(const char* line, usize length, void* context);
    const String<HeapAllocator>& At(usize index) const;
    bool Passes(const String<HeapAllocator>& line) const;

    Vector<String<HeapAllocator>> lines_;
    usize first_ = 0;
    usize count_ = 0;
    u64 next_ = 0;
    u64 dropped_ = 0;
    String<HeapAllocator> filter_;
};

/// The numbers the program hands to the overlay each frame; the overlay does not reach into the other modules.
///
/// - Thread-safety: thread-compatible plain data.
struct DebugStats {
    f32 frame_ms = 0;        ///< The last frame's time in milliseconds (the graph and the frame-rate line).
    f32 gpu_ms = 0;          ///< The GPU time of the frame, or 0 when unknown.
    u32 draw_calls = 0;      ///< Draw calls recorded.
    u32 jobs_pending = 0;    ///< Jobs queued in the job system.
    u32 job_workers = 0;     ///< Worker threads.
    u64 gpu_bytes = 0;       ///< Device memory in use.
};

/// A bar graph of the last frame times.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the samples.
class FrameGraph : public Widget {
public:
    /// How many samples the graph keeps (one bar each).
    static constexpr usize kSamples = 120;
    /// The frame time that fills the graph's height, in milliseconds.
    static constexpr f32 kFullScaleMs = 33.4f;
    /// The preferred height in logical units.
    static constexpr f32 kHeight = 48.0f;

    /// Makes an empty graph.
    explicit FrameGraph(StringView name = StringView());
    /// Adds a sample; a negative or NaN one is taken as 0. Marks the draw dirty.
    void Push(f32 ms);
    /// Returns how many samples are held (at most `kSamples`).
    ///
    /// - Fails: never.
    usize Count() const { return count_; }
    /// Returns the largest sample held, or 0 for none.
    ///
    /// - Fails: never.
    f32 Max() const;
    /// Returns the mean of the samples held, or 0 for none.
    ///
    /// - Fails: never.
    f32 Average() const;
    /// Returns a graph as wide as its samples and `kHeight` high.
    Size Measure(const Constraints& constraints) override;
    /// Draws one bar per sample, newest at the right.
    void Draw(Canvas& canvas) override;

private:
    f32 samples_[kSamples] = {};
    usize head_ = 0;
    usize count_ = 0;
};

/// The overlay: the graph, a line of counters, the log console and a command line, in a panel at the top right of the surface.
///
/// `Attach` adds it to a surface and registers the key that shows and hides it (hidden it is collapsed: it costs no layout, no draw and no `Update`). While it is shown it polls the log each
/// frame, so the console follows what any thread logs. A line typed in the command line runs when Enter is pressed: its first word names a command registered with `AddCommand`, the rest is the argument. `help`
/// lists the commands, `clear` empties the console. The command's text output goes to the console through the `LogCapture` the function receives.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its widgets and the capture; borrows the commands' user pointers.
class DebugOverlay : public BoxLayout {
public:
    /// What a command does: it gets the pointer given to `AddCommand`, the text after the command's name (trimmed), and the capture to write lines of output to.
    using CommandFn = void (*)(void* user, StringView argument, LogCapture& out);

    /// The width of the panel in logical units.
    static constexpr f32 kWidth = 420.0f;
    /// The height of the panel in logical units.
    static constexpr f32 kPanelHeight = 320.0f;
    /// The gap between the panel and the surface edge.
    static constexpr f32 kMargin = 8.0f;
    /// The most commands that can be registered.
    static constexpr usize kMaxCommands = 32;

    /// Makes the overlay, hidden.
    explicit DebugOverlay(StringView name = StringView("debug_overlay"));
    /// Unregisters the toggle key from the surface it was attached to, if that surface is alive.
    ~DebugOverlay() override;

    /// Adds the overlay to the root of `surface` and registers `toggle` to show and hide it.
    /// - Param surface: the surface; borrowed, must outlive the overlay or be detached first.
    /// - Param toggle: the chord; the overlay stays unattached to a key when it is already taken.
    /// - Returns: false when the chord was taken (the overlay is still attached and can be shown with `Show`).
    /// - Ownership: the overlay is owned by the surface's root afterwards; `this` stays valid until the root dies.
    static bool Attach(Surface* surface, UniquePtr<DebugOverlay> overlay, const KeyChord& toggle);
    /// Shows or hides the panel.
    void Show(bool show);
    /// Returns whether the panel is shown.
    ///
    /// - Fails: never.
    bool IsShown() const { return !Collapsed(); }
    /// Records a frame: adds `stats.frame_ms` to the graph and rewrites the counters line.
    void SetStats(const DebugStats& stats);
    /// Registers a command.
    /// - Param name: one word; the name `help` and `clear` are taken.
    /// - Param help: a line for `help`.
    /// - Param function: what to run.
    /// - Param user: passed to it; borrowed.
    /// - Returns: false when the name is taken, empty, has a space, or `kMaxCommands` commands exist.
    /// - Ownership: `user` is borrowed for as long as the overlay lives.
    bool AddCommand(StringView name, StringView help, CommandFn function, void* user);
    /// Runs a line as if typed: echoes it, runs the command and shows an error line for an unknown one.
    /// - Returns: whether a command ran.
    bool Run(StringView line);
    /// Returns the capture behind the console.
    ///
    /// - Ownership: borrowed; the overlay keeps it.
    LogCapture& Capture() { return capture_; }
    /// Returns the graph.
    ///
    /// - Ownership: borrowed; the overlay keeps it.
    /// - Fails: never.
    FrameGraph* Graph() const { return graph_; }
    /// Returns the list box of the console.
    ///
    /// - Ownership: borrowed; the overlay keeps it.
    /// - Fails: never.
    ListBox* Console() const { return console_; }
    /// Returns the command line.
    ///
    /// - Ownership: borrowed; the overlay keeps it.
    /// - Fails: never.
    TextField* CommandLine() const { return command_line_; }
    /// Returns the counters line.
    ///
    /// - Ownership: borrowed; the overlay keeps it.
    /// - Fails: never.
    Label* Counters() const { return counters_; }
    /// Returns how many times the console was refreshed because new lines arrived.
    ///
    /// - Fails: never.
    u64 Refreshes() const { return refreshes_; }
    /// Polls the log and refreshes the console when lines arrived.
    void Update(f32 dt) override;
    /// Returns true while the panel is shown (the log can change at any time).
    ///
    /// - Fails: never.
    bool WantsUpdate() const override { return !Collapsed(); }
    /// Places the panel at the top right of the surface.
    void Arrange(const Rect& rect) override;
    /// Draws the panel's background.
    void Draw(Canvas& canvas) override;

private:
    struct Command {
        Command(String<HeapAllocator>&& n, String<HeapAllocator>&& h, CommandFn f, void* u)
            : name(static_cast<String<HeapAllocator>&&>(n)), help(static_cast<String<HeapAllocator>&&>(h)), function(f), user(u) {}
        String<HeapAllocator> name;
        String<HeapAllocator> help;
        CommandFn function = nullptr;
        void* user = nullptr;
    };
    class ConsoleModel;

    static void Submitted(void* user, TextField* field);
    static void FilterChanged(void* user, TextField* field);
    static void Toggle(void* user);
    void Refresh();

    LogCapture capture_;
    Vector<Command> commands_;
    UniquePtr<ConsoleModel> model_;
    FrameGraph* graph_ = nullptr;
    Label* counters_ = nullptr;
    ListBox* console_ = nullptr;
    TextField* filter_ = nullptr;
    TextField* command_line_ = nullptr;
    Surface* surface_ = nullptr;
    u32 accelerator_ = 0;
    u64 refreshes_ = 0;
    f32 bottom_offset_ = 0;
};

} // namespace engine_placeholder::ui
