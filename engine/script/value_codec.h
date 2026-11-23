// The binary form of a script value (Phase 27 Day 469): what a save file and a replay record hold. The encoding is
// canonical little-endian (`bytes::ByteWriter`), versioned, and read with every length held to a limit, so a damaged or
// hostile record is an error value and never a crash or a huge allocation.
#pragma once

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "script/value.h"
#include "storage/identity.h"
#include "types.h"

namespace vse::script
{

/// The version of the value record written by [[script::EncodeValueRecord]].
inline constexpr u8 kValueRecordVersion = 2;

/// What reading a value record found.
///
/// - Thread-safety: thread-compatible plain data.
enum class ValueRecordStatus : u8
{
    /// The record decoded and is within the limits.
    Ok,
    /// The record ends before the value does.
    Truncated,
    /// The bytes are not a value record: a version this build does not know, an unknown kind, a count above its limit
    /// or a lie about a length.
    Corrupt,
    /// The value is within the encoding but above the caller's [[script::ValueLimits]].
    OverLimit,
};

/// The name of a status, for messages.
///
/// - Ownership: static; not freed.
/// - Fails: never.
const char* ValueRecordStatusName(ValueRecordStatus status);

/// Writes `value` as a record: the version, how entity ids are written, then the value.
///
/// With `index` null an entity id is written as its packed 64 bits (`ecs::Entity::ToU64`), which is meaningful only to
/// a reader in a world with the same ids. With an index an entity id is written as its stable 128-bit key (the null key
/// for an entity the index does not know), so a load into another world can find the entity again with
/// [[script::DecodeValueRecord]].
///
/// - Param value: the value to write.
/// - Param out: receives the bytes (appended).
/// - Param index: the keys of the world the entities are in; may be null (see above); it must be synced.
/// - Ownership: `out` and `index` are borrowed for the call.
void EncodeValueRecord(const Value& value, bytes::ByteWriter* out, const storage::KeyIndex* index);

/// Reads a record written by [[script::EncodeValueRecord]].
///
/// - Param in: the bytes; the reader moves past the record on success.
/// - Param out: receives the value on success; unchanged on failure.
/// - Param index: resolves entity keys back to entities; may be null, then a keyed entity reads as an invalid entity.
/// - Param limits: the limits the value must meet.
/// - Returns: the status.
/// - Fails: `Truncated`, `Corrupt` or `OverLimit`, as described; nothing is changed.
/// - Ownership: all pointers are borrowed for the call.
ValueRecordStatus DecodeValueRecord(bytes::ByteReader* in, Value* out, const storage::KeyIndex* index,
                                    const ValueLimits& limits);

}  // namespace vse::script
