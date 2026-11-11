#pragma once

//! One stored component of one entity, framed: [[storage::ComponentRecord]] (id, version, bytes) and its read and write
//! (Phase 19 Day 257). The entity container of Day 260 is a count followed by these. A record the process does not know
//! is carried as [[storage::OpaqueComponent]] and written back unchanged.

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "storage/field.h"
#include "storage/status.h"
#include "types.h"

namespace engine_placeholder::storage
{

/// A component as stored: the registry's stable id, the version it was written with, and its encoded fields.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `bytes` borrows; the underlying bytes must outlive the record.
struct ComponentRecord
{
    u32 id = 0;       ///< The stable component id.
    u32 version = 0;  ///< The version the bytes were written with.
    ByteSpan bytes;   ///< The encoded fields.
};

/// A record of a component this process does not know, kept whole so a save writes it back unchanged.
///
/// - Thread-safety: thread-compatible; not-thread-safe if shared.
/// - Ownership: owns a copy of the bytes.
struct OpaqueComponent
{
    u32 id = 0;          ///< The stable component id.
    u32 version = 0;     ///< The version the bytes were written with.
    Vector<byte> bytes;  ///< The encoded fields, as read.

    /// Returns a view of this component as a record.
    ComponentRecord View() const;
    /// Returns an owning copy of `record`.
    static OpaqueComponent From(const ComponentRecord& record);
};

/// The unknown components of one entity, kept by `UnknownPolicy::KeepOpaque` so that the next save writes them back
/// unchanged. A component of the world; registered with id [[storage::kOpaqueComponentsId]], never stored by its own
/// record.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the bytes.
struct OpaqueComponents
{
    Vector<OpaqueComponent> items;  ///< One entry per unknown component, in the order they were stored.
};

/// Appends `record` to `out` as `id` (4 bytes), `version` (4 bytes), the byte count (varint) and the bytes.
///
/// - Param record: the record to frame.
/// - Param out: receives the framing.
/// - Ownership: `record.bytes` is read during the call only.
/// - Fails: `LimitExceeded` when the bytes are longer than `limits.max_record_bytes`; `Invalid` for id 0 or version 0.
Status WriteComponentRecord(const ComponentRecord& record, const CodecLimits& limits, bytes::ByteWriter& out);

/// Reads one framed record. The bytes of the result view `in`'s input.
///
/// - Param in: positioned at a record; on success it is left after it, on failure it is not moved.
/// - Param limits: `max_record_bytes` bounds the byte count before anything is taken.
/// - Param record: receives the record.
/// - Ownership: `record->bytes` borrows `in`'s input.
/// - Fails: `Corrupt` (truncated, id 0, version 0, a malformed count); `LimitExceeded` (the count is above
/// `max_record_bytes`).
Status ReadComponentRecord(bytes::ByteReader& in, const CodecLimits& limits, ComponentRecord* record);

}  // namespace engine_placeholder::storage
