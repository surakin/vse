#include "storage/component_record.h"

namespace engine_placeholder::storage {

ComponentRecord OpaqueComponent::View() const {
    ComponentRecord r;
    r.id = id;
    r.version = version;
    r.bytes = ByteSpan(bytes.Data(), bytes.Size());
    return r;
}

OpaqueComponent OpaqueComponent::From(const ComponentRecord& record) {
    OpaqueComponent o;
    o.id = record.id;
    o.version = record.version;
    for (usize i = 0; i < record.bytes.Size(); ++i) {
        o.bytes.Push(record.bytes[i]);
    }
    return o;
}

Status WriteComponentRecord(const ComponentRecord& record, const CodecLimits& limits, bytes::ByteWriter& out) {
    if (record.id == 0 || record.version == 0) {
        return Status::Make(ErrorCode::Invalid, "a record needs a nonzero id and version");
    }
    if (record.bytes.Size() > limits.max_record_bytes) {
        return Status::Make(ErrorCode::LimitExceeded, "a record is larger than the record limit");
    }
    out.WriteU32(record.id);
    out.WriteU32(record.version);
    out.WriteVarint(record.bytes.Size());
    out.WriteBytes(record.bytes);
    return Status::Success();
}

Status ReadComponentRecord(bytes::ByteReader& in, const CodecLimits& limits, ComponentRecord* record) {
    bytes::ByteReader probe = in; // a failed read leaves `in` where it was
    ComponentRecord r;
    bytes::ByteError e = probe.ReadU32(&r.id);
    if (e == bytes::ByteError::Ok) e = probe.ReadU32(&r.version);
    u64 length = 0;
    if (e == bytes::ByteError::Ok) e = probe.ReadLength(limits.max_record_bytes, &length);
    if (e == bytes::ByteError::Ok) e = probe.ReadBytes(static_cast<usize>(length), &r.bytes);
    if (e != bytes::ByteError::Ok) {
        return detail::FromByteError(e);
    }
    if (r.id == 0 || r.version == 0) {
        return Status::Make(ErrorCode::Corrupt, "a record has id 0 or version 0");
    }
    in = probe;
    *record = r;
    return Status::Success();
}

} // namespace engine_placeholder::storage
