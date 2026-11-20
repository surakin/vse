#include "asset/resource_protocol.h"

#include "asset/backend.h"
#include "net/codec.h"

namespace vse::asset
{

namespace
{

using net::CodecError;
using net::NetReader;
using net::NetWriter;

// Writes return TooLarge for any failure: the only ways a write fails are a buffer that is too small and a value above
// its limit, both of which mean "this message cannot be sent".
#define PROTO_WRITE(expression)                                                                                        \
    if ((expression) != CodecError::Ok)                                                                                \
    {                                                                                                                  \
        return ProtoError::TooLarge;                                                                                   \
    }

#define PROTO_READ(expression)                                                                                         \
    if ((expression) != CodecError::Ok)                                                                                \
    {                                                                                                                  \
        return ProtoError::Malformed;                                                                                  \
    }

}  // namespace

const char* ProtoErrorName(ProtoError error)
{
    switch (error)
    {
    case ProtoError::None: return "none";
    case ProtoError::NotFound: return "not found";
    case ProtoError::Range: return "range";
    case ProtoError::TooLarge: return "too large";
    case ProtoError::Busy: return "busy";
    case ProtoError::Invalid: return "invalid";
    case ProtoError::Unsupported: return "unsupported";
    case ProtoError::Other: return "other";
    case ProtoError::Malformed: return "malformed";
    }
    return "unknown";
}

ProtoError EncodeProto(const ProtoData& message, ByteSpan payload, MutableByteSpan out, usize* written)
{
    NetWriter w(out);
    PROTO_WRITE(w.WriteU8(static_cast<u8>(message.type)));
    PROTO_WRITE(w.WriteU32(message.request));
    switch (message.type)
    {
    case ProtoMessage::Hello:
        PROTO_WRITE(w.WriteU16(message.version));
        PROTO_WRITE(w.WriteU32(message.max_read_bytes));
        break;
    case ProtoMessage::HelloAck:
        PROTO_WRITE(w.WriteU16(message.version));
        PROTO_WRITE(w.WriteU32(message.max_read_bytes));
        PROTO_WRITE(w.WriteU32(message.page_entries));
        PROTO_WRITE(w.WriteU32(message.max_inflight));
        break;
    case ProtoMessage::Stat: PROTO_WRITE(w.WriteU64(message.id)); break;
    case ProtoMessage::StatOk: PROTO_WRITE(w.WriteU64(message.size)); break;
    case ProtoMessage::Table:
        PROTO_WRITE(w.WriteU64(message.generation));
        PROTO_WRITE(w.WriteU32(message.page));
        break;
    case ProtoMessage::TableOk:
        PROTO_WRITE(w.WriteU64(message.generation));
        PROTO_WRITE(w.WriteBool(message.unchanged));
        PROTO_WRITE(w.WriteU32(message.page));
        PROTO_WRITE(w.WriteU32(message.page_count));
        if (!message.unchanged)
        {
            if (message.entries.Size() > kProtoMaxPageEntries)
            {
                return ProtoError::TooLarge;
            }
            PROTO_WRITE(w.WriteVarint(message.entries.Size()));
            for (usize i = 0; i < message.entries.Size(); ++i)
            {
                PROTO_WRITE(w.WriteU64(message.entries[i].id));
                PROTO_WRITE(w.WriteU32(message.entries[i].type_tag));
                PROTO_WRITE(w.WriteString(message.entries[i].path.View(), kProtoMaxPathBytes));
            }
        }
        break;
    case ProtoMessage::List:
        PROTO_WRITE(w.WriteString(message.prefix.View(), kProtoMaxPathBytes));
        PROTO_WRITE(w.WriteU32(message.page));
        break;
    case ProtoMessage::ListOk:
        if (message.ids.Size() > kProtoMaxPageEntries)
        {
            return ProtoError::TooLarge;
        }
        PROTO_WRITE(w.WriteU64(message.generation));
        PROTO_WRITE(w.WriteU32(message.page));
        PROTO_WRITE(w.WriteBool(message.more));
        PROTO_WRITE(w.WriteVarint(message.ids.Size()));
        for (usize i = 0; i < message.ids.Size(); ++i)
        {
            PROTO_WRITE(w.WriteU64(message.ids[i]));
        }
        break;
    case ProtoMessage::Read:
        PROTO_WRITE(w.WriteU64(message.id));
        PROTO_WRITE(w.WriteU64(message.offset));
        PROTO_WRITE(w.WriteU32(message.length));
        break;
    case ProtoMessage::Changes: PROTO_WRITE(w.WriteU64(message.since)); break;
    case ProtoMessage::ChangesOk:
        if (message.ids.Size() > kProtoMaxPageEntries || message.kinds.Size() != message.ids.Size())
        {
            return ProtoError::TooLarge;
        }
        PROTO_WRITE(w.WriteU64(message.epoch));
        PROTO_WRITE(w.WriteBool(message.reset));
        PROTO_WRITE(w.WriteVarint(message.ids.Size()));
        for (usize i = 0; i < message.ids.Size(); ++i)
        {
            PROTO_WRITE(w.WriteU8(message.kinds[i]));
            PROTO_WRITE(w.WriteU64(message.ids[i]));
        }
        break;
    case ProtoMessage::ReadOk:
        PROTO_WRITE(w.WriteU64(message.offset));
        PROTO_WRITE(w.WriteBytes(payload, payload.Size()));
        break;
    case ProtoMessage::Error: PROTO_WRITE(w.WriteU8(static_cast<u8>(message.error))); break;
    default: return ProtoError::Invalid;
    }
    PROTO_WRITE(w.AlignToByte());
    *written = w.BytesWritten();
    return ProtoError::None;
}

ProtoError DecodeProto(ByteSpan bytes, u32 max_read_bytes, ProtoData* out)
{
    *out = ProtoData();
    NetReader r(bytes);
    u8 type = 0;
    PROTO_READ(r.ReadU8(&type));
    PROTO_READ(r.ReadU32(&out->request));
    out->type = static_cast<ProtoMessage>(type);
    switch (out->type)
    {
    case ProtoMessage::Hello:
        PROTO_READ(r.ReadU16(&out->version));
        PROTO_READ(r.ReadU32(&out->max_read_bytes));
        break;
    case ProtoMessage::HelloAck:
        PROTO_READ(r.ReadU16(&out->version));
        PROTO_READ(r.ReadU32(&out->max_read_bytes));
        PROTO_READ(r.ReadU32(&out->page_entries));
        PROTO_READ(r.ReadU32(&out->max_inflight));
        break;
    case ProtoMessage::Stat: PROTO_READ(r.ReadU64(&out->id)); break;
    case ProtoMessage::StatOk: PROTO_READ(r.ReadU64(&out->size)); break;
    case ProtoMessage::Table:
        PROTO_READ(r.ReadU64(&out->generation));
        PROTO_READ(r.ReadU32(&out->page));
        break;
    case ProtoMessage::TableOk:
    {
        PROTO_READ(r.ReadU64(&out->generation));
        PROTO_READ(r.ReadBool(&out->unchanged));
        PROTO_READ(r.ReadU32(&out->page));
        PROTO_READ(r.ReadU32(&out->page_count));
        if (!out->unchanged)
        {
            u64 count = 0;
            PROTO_READ(r.ReadVarint(&count));
            // An entry takes at least 12 bytes: the count cannot be above what the bytes that remain can hold.
            if (count > kProtoMaxPageEntries || count * 12 * 8 > r.BitsLeft())
            {
                return ProtoError::Malformed;
            }
            for (u64 i = 0; i < count; ++i)
            {
                ProtoEntry entry;
                PROTO_READ(r.ReadU64(&entry.id));
                PROTO_READ(r.ReadU32(&entry.type_tag));
                PROTO_READ(r.ReadString(kProtoMaxPathBytes, &entry.path));
                out->entries.Push(static_cast<ProtoEntry&&>(entry));
            }
        }
        break;
    }
    case ProtoMessage::List:
        PROTO_READ(r.ReadString(kProtoMaxPathBytes, &out->prefix));
        PROTO_READ(r.ReadU32(&out->page));
        break;
    case ProtoMessage::ListOk:
    {
        PROTO_READ(r.ReadU64(&out->generation));
        PROTO_READ(r.ReadU32(&out->page));
        PROTO_READ(r.ReadBool(&out->more));
        u64 count = 0;
        PROTO_READ(r.ReadVarint(&count));
        if (count > kProtoMaxPageEntries || count * 64 > r.BitsLeft())
        {
            return ProtoError::Malformed;
        }
        for (u64 i = 0; i < count; ++i)
        {
            u64 id = 0;
            PROTO_READ(r.ReadU64(&id));
            out->ids.Push(id);
        }
        break;
    }
    case ProtoMessage::Read:
        PROTO_READ(r.ReadU64(&out->id));
        PROTO_READ(r.ReadU64(&out->offset));
        PROTO_READ(r.ReadU32(&out->length));
        break;
    case ProtoMessage::Changes: PROTO_READ(r.ReadU64(&out->since)); break;
    case ProtoMessage::ChangesOk:
    {
        PROTO_READ(r.ReadU64(&out->epoch));
        PROTO_READ(r.ReadBool(&out->reset));
        u64 count = 0;
        PROTO_READ(r.ReadVarint(&count));
        if (count > kProtoMaxPageEntries || count * 72 > r.BitsLeft())
        {
            return ProtoError::Malformed;
        }
        for (u64 i = 0; i < count; ++i)
        {
            u8 kind = 0;
            u64 id = 0;
            PROTO_READ(r.ReadU8(&kind));
            PROTO_READ(r.ReadU64(&id));
            if (kind > static_cast<u8>(ChangeKind::Removed))
            {
                return ProtoError::Malformed;
            }
            out->kinds.Push(kind);
            out->ids.Push(id);
        }
        break;
    }
    case ProtoMessage::ReadOk:
        PROTO_READ(r.ReadU64(&out->offset));
        PROTO_READ(r.ReadBytes(max_read_bytes, &out->data));
        break;
    case ProtoMessage::Error:
    {
        u8 code = 0;
        PROTO_READ(r.ReadU8(&code));
        if (code == 0 || code > static_cast<u8>(ProtoError::Other))
        {
            return ProtoError::Malformed;
        }
        out->error = static_cast<ProtoError>(code);
        break;
    }
    default: return ProtoError::Malformed;
    }
    PROTO_READ(r.AlignToByte());
    if (r.BitsLeft() != 0)
    {
        return ProtoError::Malformed;  // bytes after the end of the message
    }
    return ProtoError::None;
}

}  // namespace vse::asset
