// The service slots (Phase 27 Day 466): where a target above `script` hangs the object its bindings need (the physics
// world, later the audio mixer and the input state) without `script` knowing the type. The same slot number is used by
// the host to fill [[script::MergeServices::extensions]], by the runtime to copy it into every
// [[script::PartitionContext]], and by the bindings of the target that owns the slot.
#pragma once

#include "types.h"

namespace engine_placeholder::script
{

/// The number of service slots.
inline constexpr usize kMaxServiceExtensions = 4;

/// The slots. The numbers are fixed: a target owns its slot for good.
///
/// - Thread-safety: thread-compatible plain data.
enum ServiceSlot : u32
{
    kServicePhysics = 0,  ///< The physics bindings' services (`script_physics`: `PhysicsScriptServices`).
    kServiceAudio = 1,    ///< The audio bindings' services (`script_media`: `AudioScriptServices`).
    kServiceInput = 2,    ///< The input bindings' services (`script_media`: `InputScriptServices`).
};

}  // namespace engine_placeholder::script
