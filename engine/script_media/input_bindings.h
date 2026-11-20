// The input bindings (Phase 27 Day 468): scripts read the player's input from the snapshot the host took at the start
// of the tick ([[script_media::InputSnapshot]]), so every script of the tick, on any thread, sees the same input. A
// separate target above `script` and the platform layer (layer 14): `script` itself includes no platform header.
// (docs/guide/scripting.md, "Audio and input").
#pragma once

#include "script/bindings.h"
#include "script/script_runtime.h"
#include "script/service_slots.h"
#include "script_media/input_snapshot.h"
#include "types.h"

namespace vse::script_media
{

/// Adds the `input.*` functions, all `world-read`, against the snapshot.
///
/// They are `key_down(key)`, `key_pressed(key)`,
/// `key_released(key)` (keys by lower-case name: `"a"` to `"z"`, `"0"` to `"9"`, `"f1"` to `"f12"`, `"space"`,
/// `"enter"`, `"escape"`, `"left"`, `"leftshift"`, ...); `modifier(name)` (`"shift"`, `"ctrl"`, `"alt"`, `"super"`);
/// `mouse_position()`, `mouse_delta()`, `scroll()` (a vec2), `mouse_down(button)`, `mouse_pressed(button)`,
/// `mouse_released(button)` (`"left"`, `"right"`, `"middle"`, `"back"`, `"forward"`), `pointer_inside()`;
/// `axis(name)` (a number from -1 to 1), `action(name)`, `action_pressed(name)`, `action_released(name)`; and `text()`
/// (what was typed this tick). An unknown key, button, action or axis name is a script error.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterInputBindings(script::BindingRegistry* registry);

/// The name of the event the host raises for each change of input.
inline constexpr const char* kEventInput = "on_input";

/// What an emit pass did.
///
/// - Thread-safety: thread-compatible plain data.
struct InputEventReport
{
    /// Events put on the bus.
    u32 emitted = 0;
};

/// Puts the input changes since the previous snapshot on the bus as `on_input` events.
///
/// Every subscriber gets them
/// (the order is fixed: keys by their number, mouse buttons, scroll, text): the payload has `kind` (`"key_down"`,
/// `"key_up"`, `"mouse_down"`, `"mouse_up"`, `"scroll"` or `"text"`) and `key`, `button`, `dx` and `dy` or `text` as
/// the kind says. Call it right after `InputSnapshot::Capture`, before the script tick; the events reach scripts at the
/// start of that tick.
///
/// - Param snapshot: the snapshot just captured.
/// - Param runtime: the runtime whose bus receives the events; not null.
/// - Returns: the count.
/// - Thread-safety: thread-compatible; not while a script tick runs.
/// - Ownership: borrows both for the call.
/// - Fails: never.
InputEventReport EmitInputEvents(const InputSnapshot& snapshot, script::ScriptRuntime* runtime);

}  // namespace vse::script_media
