// The result of restoring a physics snapshot (Phase 25 Day 406).
#pragma once

#include "types.h"

namespace vse::physics
{

/// What `PhysicsWorld::Restore` found. The numbers are stored nowhere but are never renumbered.
enum class SnapshotStatus : u8
{
    Ok = 0,               ///< The world now holds the snapshot's state.
    BadMagic = 1,         ///< The bytes are not a physics snapshot.
    UnknownVersion = 2,   ///< A version this build does not read (older or newer).
    Truncated = 3,        ///< The bytes end before the snapshot does.
    Corrupt = 4,          ///< A value is not valid (a count too large, a bad id, a number that is not finite, ...).
    LibraryMismatch = 5,  ///< A shape names a hull, mesh or compound the world's libraries do not hold.
    OutOfMemory = 6,      ///< Memory ran out while the state was built; the world is unchanged.
};

/// Returns a stable lower-case name for a status; never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* SnapshotStatusName(SnapshotStatus status);

}  // namespace vse::physics
