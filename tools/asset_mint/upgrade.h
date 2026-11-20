#pragma once

//! The upgrade chains of `asset_mint remint` (Phase 22 Day 334): one function per resource type and version that turns
//! a blob of that version into a blob of the next. The runtime reads the current version only; a blob of an older one
//! is refused (`UnsupportedVersion`, naming `asset_mint remint`) and is rewritten here. Where the source is available
//! the tool mints it again instead (the result is then the fresh mint by construction); the chains are for blobs that
//! have no source in the project (a directory of loose blobs, a base pak's content). An offline tool's code: never
//! linked into the engine or a game.
//!
//! When a type's format version is raised, its author adds the function from the old version here, with a golden blob
//! of the old version in the tests. No built-in type has an older version yet (all are at version 1), so the built-in
//! registry is empty.

#include "asset/blob.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// One step: `old_blob` (of version `from`) to `*out` (a blob of version `from + 1`, same type, same id, same target).
using UpgradeFn = Status (*)(const TypeRegistry& types, ByteSpan old_blob, Vector<byte>* out);

/// The steps by type and starting version.
///
/// - Thread-safety: thread-compatible; read-only once it is used.
class UpgradeChains
{
public:
    /// Adds the step from `from_version` of `type`.
    void Register(ResourceType type, u32 from_version, UpgradeFn step);
    /// The step from `from_version` of `type`, or null.
    UpgradeFn Find(ResourceType type, u32 from_version) const;

private:
    struct Entry
    {
        ResourceType type;
        u32 from_version;
        UpgradeFn step;
    };
    Vector<Entry> entries_;
};

/// Brings a blob of an older version to the current version of its type, one step at a time.
///
/// - Param blob: a blob of any version of a registered type (its header is read; the rest is the steps' business).
/// - Param out: receives the current-version blob (a copy of the input when it is current already).
/// - Fails: `TypeMismatch` (an unregistered type), `UnsupportedVersion` (no step from a version on the way, or a
///   version above the current one: the message names it), `Invalid` (a step made a blob that is not the next version
///   of the same type and id), and what a step reports.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status UpgradeBlob(const UpgradeChains& chains, const TypeRegistry& types, ByteSpan blob, Vector<byte>* out);

}  // namespace vse::asset
