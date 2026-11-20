#pragma once

//! The resource protocol, version 1 (Phase 22 Day 331): the messages a resource client and a [[asset::ResourceServer]]
//! exchange over a [[net::Transport]]. Every message is one transport message, encoded with [[net::NetWriter]] (and so
//! decoded with [[net::NetReader]], which refuses a second encoding of one message). A message starts with its type and
//! a request id the response repeats, so responses may interleave.
//!
//! | Message | Direction | Fields after the type and the request id |
//! |---|---|---|
//! | `Hello` | client to server | protocol version (u16), the largest response the client takes (u32) |
//! | `HelloAck` | server to client | version (u16), largest read (u32), entries per page (u32), requests per poll (u32)
//! | | `Stat` / `StatOk` | both | id (u64) / size in bytes (u64) | | `Table` | client to server | the generation the
//! client has (u64, 0 for none), page (u32) | | `TableOk` | server to client | generation (u64), unchanged (bool), page
//! (u32), page count (u32), then unless unchanged: a count and per entry the id (u64), the type tag (u32) and the path
//! (string) | | `List` / `ListOk` | both | prefix (string), page (u32) / generation (u64), page (u32), more (bool), a
//! count and the ids (u64) | | `Read` / `ReadOk` | both | id (u64), offset (u64), length (u32) / offset (u64), the
//! bytes | | `Error` | server to client | a code ([[asset::ProtoError]]) |
//!
//! **Limits.** A path or prefix is at most [[asset::kProtoMaxPathBytes]] bytes, a page at most
//! [[asset::kProtoMaxPageEntries]] entries, a read at most the server's `assets.net.max_read_bytes`; every length is
//! checked against its limit and the bytes that remain before anything is allocated. The protocol has no request that
//! writes: a server is read-only. A server closes a connection that sends a message it cannot decode, one before
//! `Hello`, or a second `Hello`; a request it cannot serve is answered with an `Error`.

#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

namespace vse::asset
{

/// The protocol version this code speaks.
inline constexpr u16 kProtoVersion = 1;
/// The longest path or prefix in a message, in bytes.
inline constexpr usize kProtoMaxPathBytes = 1024;
/// The most entries of a table page or ids of a list page a message may hold.
inline constexpr u32 kProtoMaxPageEntries = 1024;
/// The room a message needs besides the bytes of a read (the type, the ids, the offset and the length of the bytes).
inline constexpr usize kProtoHeaderSlack = 64;

/// The kind of a message.
enum class ProtoMessage : u8
{
    Hello = 1,       ///< A request: the version and the response size the client takes.
    Stat = 2,        ///< A request: the size of a resource.
    Table = 3,       ///< A request: a page of the id and path table.
    List = 4,        ///< A request: a page of the ids under a path prefix.
    Read = 5,        ///< A request: a range of a resource.
    Changes = 6,     ///< A request: what changed since an epoch.
    HelloAck = 33,   ///< A response: the version and the limits.
    StatOk = 34,     ///< A response: the size.
    TableOk = 35,    ///< A response: a page of the table, or that it is unchanged.
    ListOk = 36,     ///< A response: a page of ids.
    ReadOk = 37,     ///< A response: the bytes.
    ChangesOk = 38,  ///< A response: the changes, or that the client must read the table again.
    Error = 63,      ///< A response: a request failed.
};

/// Why a request failed (the code of an `Error` message), or why a message could not be encoded or decoded.
enum class ProtoError : u8
{
    None = 0,         ///< No error.
    NotFound = 1,     ///< The resource or the path prefix does not exist.
    Range = 2,        ///< A read starts past the end of the resource.
    TooLarge = 3,     ///< A read asks for more than the server's limit, or a response would not fit.
    Busy = 4,         ///< The connection has more requests pending than the server takes at once: ask again.
    Invalid = 5,      ///< The request is well formed but not valid (a page past the last).
    Unsupported = 6,  ///< The message is not a request this server answers.
    Other = 7,        ///< The backend failed.
    Malformed = 8,    ///< (Decoding only) the bytes are not a message of this protocol.
};

/// The name of an error, for logs and tests.
///
/// - Ownership: the string is static.
/// - Fails: never.
const char* ProtoErrorName(ProtoError error);

/// One entry of a table page.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its path.
struct ProtoEntry
{
    /// An empty entry.
    ProtoEntry() : path(HeapAllocator::Default()) {}
    u64 id = 0;                  ///< The resource.
    u32 type_tag = 0;            ///< Its type ([[asset::ResourceType::Tag]]).
    String<HeapAllocator> path;  ///< Its path.
};

/// A message of the protocol, decoded or to be encoded; the fields a type does not use are ignored.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its strings and vectors.
struct ProtoData
{
    /// An empty message (a `Hello`).
    ProtoData() : prefix(HeapAllocator::Default()) {}
    ProtoMessage type = ProtoMessage::Hello;  ///< The kind.
    u32 request = 0;                          ///< The request id (a response repeats its request's).
    u16 version = 0;                          ///< `Hello`, `HelloAck`.
    u32 max_read_bytes = 0;                   ///< `Hello` (what the client takes), `HelloAck` (what the server serves).
    u32 page_entries = 0;                     ///< `HelloAck`: entries per table and list page.
    u32 max_inflight = 0;                     ///< `HelloAck`: requests per connection the server takes per poll.
    u64 id = 0;                               ///< `Stat`, `Read`.
    u64 offset = 0;                           ///< `Read`, `ReadOk`.
    u32 length = 0;                           ///< `Read`: the bytes asked for.
    u64 size = 0;                             ///< `StatOk`.
    u64 generation = 0;                       ///< `Table` (what the client has), `TableOk` and `ListOk` (the server's).
    u32 page = 0;                             ///< `Table`, `TableOk`, `List`, `ListOk`.
    u32 page_count = 0;                       ///< `TableOk`.
    bool unchanged = false;                   ///< `TableOk`: the client's generation is current, no entries follow.
    bool more = false;                        ///< `ListOk`: another page follows.
    String<HeapAllocator> prefix;             ///< `List`.
    Vector<ProtoEntry> entries;               ///< `TableOk`.
    Vector<u64> ids;                          ///< `ListOk`.
    Vector<byte> data;   ///< `ReadOk` when decoded (an encoded one takes its bytes as an argument).
    u64 since = 0;       ///< `Changes`: the epoch the client has.
    u64 epoch = 0;       ///< `ChangesOk`: the server's epoch.
    bool reset = false;  ///< `ChangesOk`: the server cannot continue from the client's epoch.
    Vector<u8> kinds;    ///< `ChangesOk`: the kind ([[asset::ChangeKind]]) of each change, beside `ids`.
    ProtoError error = ProtoError::None;  ///< `Error`.
};

/// Encodes `message` into `out`.
///
/// - Param payload: for a `ReadOk`, the bytes to carry; ignored for the other types.
/// - Param out: the buffer; the message is written from its start.
/// - Param written: receives the size of the message.
/// - Returns: `None` on success.
/// - Fails: `TooLarge` (the message does not fit `out`, or a path, a prefix or a page is above its limit),
///   `Invalid` (a message type that has no encoding).
/// - Ownership: `*written` is written on success; `payload` is read during the call only.
ProtoError EncodeProto(const ProtoData& message, ByteSpan payload, MutableByteSpan out, usize* written);
/// As above for a message that carries no payload.
///
/// - Fails: as above.
/// - Ownership: `*written` is written on success.
inline ProtoError EncodeProto(const ProtoData& message, MutableByteSpan out, usize* written)
{
    return EncodeProto(message, ByteSpan(), out, written);
}

/// Decodes one message.
///
/// - Param bytes: the message, exactly.
/// - Param out: receives the message; replaced.
/// - Param max_read_bytes: the longest run of bytes a `ReadOk` may carry and the largest `length` a `Read` may ask.
/// - Returns: `None` on success.
/// - Fails: `Malformed` for bytes that are not exactly one message (an unknown type, a truncation, a length above its
///   limit, a second encoding of a value, bytes after the end); a request that is valid but above `max_read_bytes` is
///   still decoded (the server answers `TooLarge`).
/// - Ownership: `*out` is replaced; `bytes` is read during the call only.
ProtoError DecodeProto(ByteSpan bytes, u32 max_read_bytes, ProtoData* out);

}  // namespace vse::asset
