// The component and query bindings (Phase 27 Day 456): `component.get/set/add/remove/has/exists/define` over every
// component of the host's `storage::ComponentRegistry` (and the ones scripts define), and `query.each/all/page`. A
// read sees the tick-start world plus the script's own writes; a write is a command applied by the merge
// (docs/guide/scripting.md, "Components and queries").
#pragma once

#include "script/bindings.h"
#include "script/command_buffer.h"
#include "script/merge.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The command kinds the component bindings record (see [[script::RegisterComponentCommandKinds]]).
enum ComponentCommandKind : u16
{
    /// Replaces the stored record of a component the entity has (payload: the stored id, then the record).
    kCommandComponentSet = 20,
    /// Adds a component the entity lacks, with the record (payload as above); a component it has is replaced.
    kCommandComponentAdd = 21,
    /// Removes a component (payload: the stored id).
    kCommandComponentRemove = 22,
};

/// The overlay fields of the component bindings are `kFieldComponentBase` plus the ECS id of the component: the value
/// is the record as bytes, or `false` for a component the script removed.
inline constexpr u32 kFieldComponentBase = 0x1000;

/// The most entities one query call returns (a value that crosses into a script holds at most 65,536 values in all, the
/// array itself and every entity in it).
inline constexpr usize kMaxQueryResults = 60000;

/// Registers the commands that apply component writes at the merge (kinds 20 to 22). The merge's `user` must be a
/// [[script::MergeServices]] whose `components` is set.
///
/// - Param kinds: the table to add to; not null.
/// - Returns: false when an id is taken.
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterComponentCommandKinds(CommandKindTable* kinds);

/// Adds `component.get/set/add/remove/has/exists/define` and `query.each/all/page`. Reads are `world-read`, writes and
/// `define` `world-write`.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterComponentBindings(BindingRegistry* registry);

}  // namespace engine_placeholder::script
