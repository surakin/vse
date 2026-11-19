// The input snapshot scripts read (Phase 27 Day 468): what the keyboard, the mouse and the host's actions and axes were
// at the start of the tick, copied once, so every partition (and every thread) sees the same input however long the
// tick runs. The snapshot is also what a replay records: [[script_media::InputSnapshot::Serialize]] writes it, and a
// recorded session fed back through [[script_media::InputSnapshot::Deserialize]] gives scripts the input they had
// (docs/guide/scripting.md, "Audio and input").
#pragma once

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "platform/input.h"
#include "script/service_slots.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script_media
{

/// What an action or an axis reads: a keyboard key or mouse buttons.
///
/// - Thread-safety: thread-compatible plain data.
struct InputBinding
{
    /// Keyboard key; `key` is used.
    static constexpr u32 kKey = 0;
    /// Mouse buttons; `buttons` is a mask of `os::MouseButtonBit`.
    static constexpr u32 kMouse = 1;

    /// `kKey` or `kMouse`.
    u32 kind = kKey;
    /// The key (for `kKey`).
    os::Key key = os::Key::Unknown;
    /// The button mask (for `kMouse`).
    u32 buttons = 0;

    /// A key binding.
    static InputBinding OfKey(os::Key key)
    {
        InputBinding b;
        b.kind = kKey;
        b.key = key;
        return b;
    }
    /// A mouse binding.
    static InputBinding OfMouse(u32 buttons)
    {
        InputBinding b;
        b.kind = kMouse;
        b.buttons = buttons;
        return b;
    }
};

/// The most actions and axes a map holds, and the most bindings of one action.
inline constexpr u32 kMaxInputNames = 128;
/// See `kMaxInputNames`.
inline constexpr u32 kMaxInputBindings = 8;
/// The longest action or axis name in bytes.
inline constexpr u32 kMaxInputNameBytes = 63;
/// The longest text one snapshot carries, in bytes (what a player types in one tick).
inline constexpr u32 kMaxInputTextBytes = 1024;

/// The names the host gives to input: its actions and axes.
///
/// An action is down while any of its bindings is; an axis is the sum of its positive and negative bindings, clamped to
/// -1..1, plus the host's own value (a gamepad stick, say).
///
/// There is no action-mapping system in the platform layer, so the host fills this map (from its settings or code); the
/// scripts only see the names.
///
/// - Thread-safety: thread-compatible; build it before the first capture.
/// - Ownership: owns the names.
class InputMap
{
public:
    /// Makes an empty map.
    InputMap() = default;

    /// Adds an action.
    ///
    /// - Param name: not empty, at most `kMaxInputNameBytes`, unused as an action name.
    /// - Param bindings: what presses it; 1 to `kMaxInputBindings`.
    /// - Returns: false when the name or the bindings are not valid or the map is full.
    /// - Ownership: copied.
    /// - Fails: returns false, as described.
    bool AddAction(StringView name, Span<const InputBinding> bindings);

    /// Adds an axis: -1 for the negative bindings, +1 for the positive ones.
    ///
    /// - Param name: as for an action, unused as an axis name.
    /// - Param negative: what pulls it down; 0 to `kMaxInputBindings`.
    /// - Param positive: what pushes it up; 0 to `kMaxInputBindings`.
    /// - Returns: false when the name or the bindings are not valid or the map is full.
    /// - Ownership: copied.
    /// - Fails: returns false, as described.
    bool AddAxis(StringView name, Span<const InputBinding> negative, Span<const InputBinding> positive);

    /// One action of the map.
    ///
    /// - Thread-safety: thread-compatible plain data.
    struct Action
    {
        /// The name.
        String<HeapAllocator> name{HeapAllocator::Default()};
        /// What presses it.
        Vector<InputBinding> bindings;
    };

    /// One axis of the map.
    ///
    /// - Thread-safety: thread-compatible plain data.
    struct Axis
    {
        /// The name.
        String<HeapAllocator> name{HeapAllocator::Default()};
        /// What pulls it down.
        Vector<InputBinding> negative;
        /// What pushes it up.
        Vector<InputBinding> positive;
    };

    /// The actions, in the order they were added.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    const Vector<Action>& Actions() const { return actions_; }
    /// The axes, in the order they were added.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    const Vector<Axis>& Axes() const { return axes_; }

private:
    Vector<Action> actions_;
    Vector<Axis> axes_;
};

/// A value the host reads itself (a gamepad stick) and adds to the axis of that name.
///
/// - Thread-safety: thread-compatible plain data.
struct ExternalAxis
{
    /// The axis name (an axis of the map; an unknown name is added as an axis of its own).
    StringView name;
    /// The value, -1 to 1.
    f32 value = 0.0f;
};

/// The state of one action.
///
/// - Thread-safety: thread-compatible plain data.
struct ActionState
{
    /// Whether it is down now.
    bool down = false;
    /// Whether it went down since the previous snapshot.
    bool pressed = false;
    /// Whether it went up since the previous snapshot.
    bool released = false;
};

/// The input of one tick.
///
/// - Thread-safety: thread-compatible; while the script phase runs it is only read (every partition at once); the host
/// captures it between ticks.
/// - Ownership: owns its text and names.
class InputSnapshot
{
public:
    /// Makes an empty snapshot (nothing down, the mouse at the origin).
    InputSnapshot() = default;

    /// Takes the snapshot for a tick: keeps the previous one for the edges (`pressed`, `released`, the mouse delta).
    ///
    /// - Param tick: the tick number the snapshot belongs to.
    /// - Param state: the platform's input state as of now.
    /// - Param text: the text typed since the previous capture (UTF-8); cut at `kMaxInputTextBytes`.
    /// - Param map: the actions and axes; borrowed for the call.
    /// - Param externals: values the host read itself (may be empty).
    /// - Ownership: nothing is kept but the copied values.
    /// - Fails: never; a press and release between two captures is not seen.
    void Capture(u64 tick, const os::InputState& state, StringView text, const InputMap& map,
                 Span<const ExternalAxis> externals = Span<const ExternalAxis>());

    /// The tick of the last capture.
    ///
    /// - Fails: never.
    u64 Tick() const { return tick_; }
    /// Whether the key is down.
    ///
    /// - Fails: never.
    bool KeyDown(os::Key key) const { return keys_.IsDown(key); }
    /// Whether the key went down since the previous capture.
    ///
    /// - Fails: never.
    bool KeyPressed(os::Key key) const { return keys_.IsDown(key) && !before_.IsDown(key); }
    /// Whether the key went up since the previous capture.
    ///
    /// - Fails: never.
    bool KeyReleased(os::Key key) const { return !keys_.IsDown(key) && before_.IsDown(key); }
    /// The modifiers (`os::ModifierBit`).
    ///
    /// - Fails: never.
    u32 Modifiers() const { return keys_.modifiers; }
    /// The mouse x in window pixels.
    ///
    /// - Fails: never.
    f64 MouseX() const { return mouse_x_; }
    /// The mouse y in window pixels.
    ///
    /// - Fails: never.
    f64 MouseY() const { return mouse_y_; }
    /// The mouse movement since the previous capture.
    ///
    /// - Fails: never.
    f64 MouseDeltaX() const { return mouse_dx_; }
    /// See `MouseDeltaX`.
    ///
    /// - Fails: never.
    f64 MouseDeltaY() const { return mouse_dy_; }
    /// The scroll since the previous capture.
    ///
    /// - Fails: never.
    f64 ScrollX() const { return scroll_x_; }
    /// See `ScrollX`.
    ///
    /// - Fails: never.
    f64 ScrollY() const { return scroll_y_; }
    /// Whether the pointer is inside the window.
    ///
    /// - Fails: never.
    bool PointerInside() const { return inside_; }
    /// Whether a mouse button (one `os::MouseButtonBit`) is down.
    ///
    /// - Fails: never.
    bool MouseDown(u32 button) const { return (buttons_ & button) != 0; }
    /// Whether the button went down since the previous capture.
    ///
    /// - Fails: never.
    bool MousePressed(u32 button) const { return (buttons_ & button) != 0 && (buttons_before_ & button) == 0; }
    /// Whether the button went up since the previous capture.
    ///
    /// - Fails: never.
    bool MouseReleased(u32 button) const { return (buttons_ & button) == 0 && (buttons_before_ & button) != 0; }
    /// The text typed since the previous capture.
    ///
    /// - Ownership: borrowed; valid until the next capture.
    /// - Fails: never.
    StringView Text() const { return text_.View(); }

    /// The state of an action.
    ///
    /// - Param name: the action name.
    /// - Param out: receives the state.
    /// - Returns: false for no such action (`out` is then all false).
    /// - Ownership: `out` is the caller's; nothing is kept.
    /// - Fails: returns false, as described.
    bool Action(StringView name, ActionState* out) const;
    /// The value of an axis.
    ///
    /// - Param name: the axis name.
    /// - Param out: receives the value (0 for no such axis).
    /// - Returns: false for no such axis.
    /// - Ownership: `out` is the caller's; nothing is kept.
    /// - Fails: returns false, as described.
    bool Axis(StringView name, f32* out) const;
    /// How many actions and axes the snapshot holds.
    ///
    /// - Fails: never.
    usize ActionCount() const { return actions_.Size(); }
    /// See `ActionCount`.
    ///
    /// - Fails: never.
    usize AxisCount() const { return axes_.Size(); }

    /// Writes the snapshot for a replay log (little endian, versioned).
    ///
    /// - Param out: appended to.
    /// - Ownership: borrowed for the call.
    /// - Fails: never.
    void Serialize(bytes::ByteWriter* out) const;

    /// Reads a snapshot written by `Serialize`, replacing this one.
    ///
    /// - Param in: positioned at the snapshot; advanced past it.
    /// - Returns: false for a short, oversized or unknown-version record (this snapshot is then left empty).
    /// - Ownership: borrowed for the call.
    /// - Fails: returns false, as described.
    bool Deserialize(bytes::ByteReader* in);

    /// Whether two snapshots hold the same input (what replay tests compare).
    ///
    /// - Fails: never.
    bool Equals(const InputSnapshot& other) const;

private:
    struct NamedAction
    {
        String<HeapAllocator> name{HeapAllocator::Default()};
        ActionState state;
    };
    struct NamedAxis
    {
        String<HeapAllocator> name{HeapAllocator::Default()};
        f32 value = 0.0f;
    };

    u64 tick_ = 0;
    os::KeyboardState keys_;
    os::KeyboardState before_;
    f64 mouse_x_ = 0.0;
    f64 mouse_y_ = 0.0;
    f64 mouse_dx_ = 0.0;
    f64 mouse_dy_ = 0.0;
    f64 scroll_x_ = 0.0;
    f64 scroll_y_ = 0.0;
    bool inside_ = false;
    bool captured_ = false;
    u32 buttons_ = 0;
    u32 buttons_before_ = 0;
    String<HeapAllocator> text_{HeapAllocator::Default()};
    Vector<NamedAction> actions_;
    Vector<NamedAxis> axes_;
};

/// What the input bindings need: put its address in `MergeServices::extensions[script::kServiceInput]` (and the
/// runtime hands it to every partition).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the snapshot, which must outlive the runtime.
struct InputScriptServices
{
    /// The snapshot of the tick; not null.
    const InputSnapshot* snapshot = nullptr;
};

}  // namespace engine_placeholder::script_media
