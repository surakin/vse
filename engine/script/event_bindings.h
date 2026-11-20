// The event bindings (Phase 27 Day 459): `event.emit`, `event.emit_to`, `event.subscribe` and `event.unsubscribe`. The
// handler functions themselves stay in the script state: the behavior host adds `event.on`, `event.once` and
// `event.off` over these (docs/guide/scripting.md, "Events").
#pragma once

#include "script/bindings.h"
#include "types.h"

namespace vse::script
{

/// Adds `event.emit(name, payload)`, `event.emit_to(entity, name, payload)` and the two subscription calls the script
/// host builds `event.on/once/off` on. They record into the partition's [[script::EventOutbox]]; nothing runs until the
/// next tick's delivery.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterEventBindings(BindingRegistry* registry);

}  // namespace vse::script
