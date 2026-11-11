#include "database/status.h"

#include "cstr/mem_ops.h"

namespace engine_placeholder::database
{

const char* ErrorCodeName(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::Locked: return "locked";
    case ErrorCode::Constraint: return "constraint";
    case ErrorCode::NotFound: return "not_found";
    case ErrorCode::Corrupt: return "corrupt";
    case ErrorCode::Full: return "full";
    case ErrorCode::ReadOnly: return "read_only";
    case ErrorCode::Misuse: return "misuse";
    case ErrorCode::Syntax: return "syntax";
    case ErrorCode::Io: return "io";
    case ErrorCode::TooBig: return "too_big";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Interrupted: return "interrupted";
    case ErrorCode::Other: return "other";
    }
    return "other";
}

Status Status::Make(ErrorCode code, i32 backend_code, StringView message)
{
    Status s;
    s.code_ = code;
    s.backend_code_ = backend_code;
    const usize length = message.Length() < kMaxMessageBytes ? message.Length() : kMaxMessageBytes;
    if (length != 0)
    {
        MemCopy(s.message_.Data(), message.Data(), length);
    }
    s.length_ = static_cast<u8>(length);
    return s;
}

}  // namespace engine_placeholder::database
