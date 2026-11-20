// The task bindings (Phase 27 Day 461): `task.enqueue` and `task.take`, the two native calls the behavior host builds
// `task.run`, `task.map`, `task.parallel_for`, `task.result` and `wait_task` on (docs/guide/scripting.md, "Script
// tasks"). They are `world-write` bindings, so a task state (which installs only `pure` ones) cannot start tasks.
#pragma once

#include "script/bindings.h"
#include "types.h"

namespace vse::script
{

/// Adds `task.enqueue(mode, source, data, seed, count, chunk)` (returns the handle, or text saying why the request was
/// refused) and `task.take(handle)` (returns `{ status, value }`, status 0 pending, 1 ready, 2 failed, 3 unknown, 4 not
/// yours). Requests queue in the partition's [[script::TaskOutbox]]; nothing runs until the end of the tick.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterTaskBindings(BindingRegistry* registry);

}  // namespace vse::script
