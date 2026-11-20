// The asset pipeline and resource system (Phase 22): the module's version numbers.
// Day 313 is the skeleton; resource identity, the blob container, the backends and the manager follow
// (plans/phase-22-asset-pipeline).

#pragma once

#include "types.h"

namespace vse::asset
{

/// The blob container version the runtime reads.
///
/// The runtime reads the current version only; an older one is refused with a message naming `asset_mint remint`.
/// - Fails: never.
constexpr u32 kContainerVersion = 1;

/// Returns the container version this build reads.
/// - Fails: never.
u32 CurrentContainerVersion();

}  // namespace vse::asset
