// Saving and loading scripted state (Phase 27 Day 469): the components a save holds (`script`, `script_persist`), the
// report of a capture, and what the runtime does with them (docs/guide/scripting.md, "Saving scripted state").
#pragma once

#include "containers/vector.h"
#include "script/value.h"
#include "script/value_codec.h"
#include "storage/component_registry.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The stable ids of the script components in the storage registry.
enum ScriptComponentIds : u32
{
    /// `script::ScriptComponent`, name `script`.
    kScriptComponentId = 130,
    /// `script::ScriptPersist`, name `script_persist`.
    kScriptPersistId = 131,
};

/// The limits a saved script record is held to: the encoded `script_persist` blob and the parameters of a `script`
/// component.
inline constexpr usize kMaxPersistBytes = usize{4} << 20;
/// The longest behavior name a `script` record may hold.
inline constexpr usize kMaxBehaviorNameBytes = 256;

/// The persistent fields of one instance, in the form a save holds them: a record of [[script::EncodeValueRecord]]
/// whose value is a map from field name to value, with entities written as stable keys.
///
/// [[script::ScriptRuntime::CapturePersistent]] writes it before a save; [[script::ScriptRuntime::ResolveLoaded]] reads
/// it after a load and removes it. It is not meaningful in a running world in between.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its bytes.
struct ScriptPersist
{
    /// Names the component in logs.
    static constexpr const char* kComponentName = "script_persist";
    /// The encoded record.
    Vector<u8> blob;
};

/// Registers `script` (version 1) and `script_persist` (version 1) with their codecs, so a world with scripts saves and
/// loads through the storage module. The codecs read bytes that may be hostile: every length is held to the registry's
/// limits and to [[script::kMaxPersistBytes]], and a record that does not decode is refused as `Corrupt`.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: whether both registered (false when an id or name is taken).
/// - Fails: returns false, as described.
/// - Ownership: `registry` is borrowed for the call.
bool RegisterScriptComponents(storage::ComponentRegistry* registry);

/// What [[script::ScriptRuntime::CapturePersistent]] did.
///
/// - Thread-safety: thread-compatible plain data.
struct PersistReport
{
    /// Instances whose persistent fields were written to a `script_persist` component.
    u64 instances_saved = 0;
    /// Persistent fields written in all.
    u64 fields_saved = 0;
    /// Fields left out because their value cannot be saved (a function, a userdata); the instance's other fields are
    /// still saved.
    u64 fields_skipped = 0;
    /// Instances whose save call failed (their previous record, if any, is kept).
    u64 instances_failed = 0;
    /// Instances that were not captured because they have not run yet (a load pending for them is written back).
    u64 instances_pending = 0;
    /// Coroutines waiting in the partitions at capture time: they are not saved (a restored script starts without
    /// them; see `KNOWN-ISSUES.md`).
    u64 coroutines_not_saved = 0;
    /// The runtime's tick count when captured ([[script::ScriptRuntime::RestoreClock]] takes it back).
    u64 tick = 0;
    /// The runtime's simulation time when captured.
    f64 sim_time = 0.0;
    /// A hash of every captured instance's entity key and persistent values, in entity order: equal for equal state.
    u64 state_hash = 0;
};

/// What [[script::ScriptRuntime::ResolveLoaded]] did.
///
/// - Thread-safety: thread-compatible plain data.
struct LoadReport
{
    /// Entities whose persistent fields are held for their instance to take (`on_load`).
    u64 queued = 0;
    /// Records that did not decode (the entity's instance starts without them).
    u64 corrupt = 0;
};

}  // namespace engine_placeholder::script
