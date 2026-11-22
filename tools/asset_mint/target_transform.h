#pragma once

//! Retargeting minted blobs for another platform: the `TargetTransform` registry of `asset_mint pack
//! --target`. A blob is laid out for one target (its header's target tag; 0 is the development target) and a pak holds
//! blobs of one target only. Packing for another target rewrites the blobs through one optional function per resource
//! type. An offline tool's code: never linked into the engine or a game.
//!
//! A transform returns a current-version blob of the same type and the same id for the requested target. The tool
//! checks it: the dependency section may not change (a transform cannot add or drop a reference), the target tag is the
//! requested one, the content hash holds, and the same input gives the same bytes. A type without a transform is
//! retagged: the header's target tag is rewritten and the hash recomputed (the bytes after the header are the same).
//! The built-in transform of textures relays the rows out for the target's pitch and slice alignment.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// What a transform may ask for.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows what it points to; the pointers must outlive the calls.
struct TransformContext
{
    const TypeRegistry* types = nullptr;  ///< The resource types.
    u32 target = 0;                       ///< The target tag the blob is wanted for.
    /// Reads the source text of the resource, or returns false when there is none (a transform that would otherwise
    /// compress twice, such as BC7 to ASTC, re-mints from the source with the target's options). Null when the tool has
    /// no sources (a directory of blobs).
    bool (*source_of)(void* user, ResourceId id, Vector<byte>* text) = nullptr;
    void* source_user = nullptr;  ///< Passed to `source_of`.
};

/// One type's conversion: `blob` (a current-version blob for another target) to `*out` (the same blob for
/// `context.target`).
using TargetTransformFn = Status (*)(const TransformContext& context, ByteSpan blob, Vector<byte>* out);

/// The transforms by resource type.
///
/// - Thread-safety: thread-compatible; a registry is read-only once it is used.
class TargetTransforms
{
public:
    /// The built-in set: the texture relayout; every other type is retagged.
    static TargetTransforms Builtin();

    /// Sets the transform of a type (replacing an earlier one).
    void Register(ResourceType type, TargetTransformFn function);

    /// The transform of a type, or null (the blob is then retagged).
    TargetTransformFn Find(ResourceType type) const;

private:
    struct Entry
    {
        ResourceType type;
        TargetTransformFn function;
    };
    Vector<Entry> entries_;
};

/// Rewrites the target tag in a blob's header and recomputes its content hash.
///
/// - Fails: `Corrupt` for something that is not a blob.
/// - Ownership: borrows `blob` for the call; `*out` is replaced.
Status RetagBlob(ByteSpan blob, u32 target, Vector<byte>* out);

/// Converts one blob for `context.target` and checks the result (current version, same type and id, the requested
/// target, the same dependencies, a valid hash). A blob already laid out for the target is copied.
///
/// - Fails: `Invalid` when the result breaks a rule (the message names it), and what the transform or `BlobReader`
///   report.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status TransformBlob(const TargetTransforms& transforms, const TransformContext& context, ByteSpan blob,
                     Vector<byte>* out);

}  // namespace vse::asset
