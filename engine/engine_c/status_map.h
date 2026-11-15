#pragma once
//! The C API's status codes and the one table that maps the engine's own result types onto them (Phase 23 Day 346).
//! Internal to the module `engine_c`: the C header is `engine_c.h`.
#include "asset/status.h"
#include "engine/engine_c.h"
#include "settings/settings.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

/// The number of statuses: the values `0` through `kStatusCount - 1` of `engine_status` are all defined.
inline constexpr u32 kStatusCount = 21;

/// Returns the C status for an asset error. `Ok` is `ENGINE_OK`; every other code has a status of its own meaning, and
/// `Other` is `ENGINE_ERROR_INTERNAL`.
///
/// - Fails: never.
engine_status FromAssetError(asset::ErrorCode code);

/// Returns the C status for a refused settings change; `None` is `ENGINE_OK`.
///
/// - Fails: never.
engine_status FromSetError(settings::SetError error);

/// Returns the name of a status (`"ok"`, `"stale_handle"`), or `"unknown"` for a number that is not one; never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* StatusName(engine_status status);

}  // namespace engine_placeholder::engine_c
