#include "net/frame.h"

#include "cstr/mem_ops.h"

namespace engine_placeholder::net
{

Status FrameWriter::Header(usize size, byte (&header)[kFrameHeaderBytes]) const
{
    if (size == 0)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (size > max_)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    header[0] = static_cast<byte>((size >> 24) & 0xff);
    header[1] = static_cast<byte>((size >> 16) & 0xff);
    header[2] = static_cast<byte>((size >> 8) & 0xff);
    header[3] = static_cast<byte>(size & 0xff);
    return Status::Success();
}

Status FrameWriter::Encode(ByteSpan payload, Vector<byte>* out) const
{
    byte header[kFrameHeaderBytes];
    const Status s = Header(payload.Size(), header);
    if (s.Failed())
    {
        return s;
    }
    const usize before = out->Size();
    if (!out->TryResizeUninitialized(before + kFrameHeaderBytes + payload.Size()))
    {
        return Status::Of(ErrorCode::Other);
    }
    MemCopy(out->Data() + before, header, kFrameHeaderBytes);
    MemCopy(out->Data() + before + kFrameHeaderBytes, payload.Data(), payload.Size());
    return Status::Success();
}

Status FrameReader::Consume(ByteSpan bytes, usize* used)
{
    *used = 0;
    if (failure_.Failed())
    {
        return failure_;
    }
    if (complete_)
    {
        return Status::Success();  // a frame waits to be popped: take nothing
    }
    usize at = 0;
    while (at < bytes.Size() && !complete_)
    {
        if (!reading_payload_)
        {
            header_[header_have_++] = bytes[at++];
            if (header_have_ < kFrameHeaderBytes)
            {
                continue;
            }
            const usize length = (static_cast<usize>(header_[0]) << 24) | (static_cast<usize>(header_[1]) << 16) |
                                 (static_cast<usize>(header_[2]) << 8) | static_cast<usize>(header_[3]);
            // The limit is checked here, before anything is sized for the payload.
            if (length == 0)
            {
                failure_ = Status::Of(ErrorCode::Invalid);
            }
            else if (length > max_)
            {
                failure_ = Status::Of(ErrorCode::LimitExceeded);
            }
            if (failure_.Failed())
            {
                *used = at;
                return failure_;
            }
            expected_ = length;
            reading_payload_ = true;
            payload_.Clear();
            continue;
        }
        // Take as much of the payload as is here, growing the buffer with what really arrived.
        const usize want = expected_ - payload_.Size();
        const usize take = (bytes.Size() - at) < want ? (bytes.Size() - at) : want;
        const usize old = payload_.Size();
        if (!payload_.TryResizeUninitialized(old + take))
        {
            failure_ = Status::Of(ErrorCode::Other);
            *used = at;
            return failure_;
        }
        MemCopy(payload_.Data() + old, bytes.Data() + at, take);
        at += take;
        if (payload_.Size() == expected_)
        {
            complete_ = true;
            reading_payload_ = false;
            header_have_ = 0;
        }
    }
    *used = at;
    return Status::Success();
}

void FrameReader::Pop()
{
    complete_ = false;
    payload_.Clear();
    expected_ = 0;
}

void FrameReader::Reset()
{
    complete_ = false;
    reading_payload_ = false;
    header_have_ = 0;
    expected_ = 0;
    payload_.Clear();
    failure_ = Status::Success();
}

}  // namespace engine_placeholder::net
