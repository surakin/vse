#pragma once

//! Walking the stored bytes of a record without building the component (Phase 22 Day 327): [[storage::SkipField]]
//! measures one field, [[storage::ReadReferences]] lists the resource references of a record. The level minter, the
//! level loader and the text form read records this way.

#include "bytes/byte_reader.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "storage/component_registry.h"
#include "storage/field.h"
#include "storage/status.h"
#include "types.h"

namespace engine_placeholder::storage
{

/// One resource reference found in a stored record.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `field` points into the registry's registration and is valid for the registry's life.
struct ReferenceValue
{
    u32 field_index = 0;               ///< The index of the field in the component's field list.
    const FieldDesc* field = nullptr;  ///< The field's description (its declared resource type and nullability).
    u64 id = 0;                        ///< The stored id; 0 for none.
};

/// Moves `in` past one stored field of the kind `field` describes, checking only that the bytes exist and that lengths
/// are within `limits`; values (enum ranges, UTF-8) are not checked: that is the decoder's job.
///
/// - Param field: the field as registered.
/// - Param in: positioned at the field; on success left after it.
/// - Param limits: the bounds for string lengths and array counts.
/// - Ownership: `in` is read during the call only.
/// - Fails: `Corrupt` (truncated bytes, an array count that does not match its type); `LimitExceeded` (a length above
/// its limit); `Invalid` (an array of arrays, which has no walker).
Status SkipField(const FieldDesc& field, bytes::ByteReader& in, const CodecLimits& limits);

/// Appends to `out` every resource reference of the stored record `record` of the registered component `component`
/// (in field order) and requires that the record is used up.
///
/// - Param component: the registration (a stored one).
/// - Param record: the record's bytes, after any upgrade (so at the registered version).
/// - Param limits: the bounds for string lengths and array counts.
/// - Param out: appended to.
/// - Ownership: `record` is read during the call only; `out` keeps pointers into the registry.
/// - Fails: `Corrupt` (the bytes do not fit the fields, or bytes are left over); `LimitExceeded`; `Invalid` (the
/// component is not stored).
Status ReadReferences(const RegisteredComponent& component, ByteSpan record, const CodecLimits& limits,
                      Vector<ReferenceValue>* out);

}  // namespace engine_placeholder::storage
