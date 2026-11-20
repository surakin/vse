#include "storage/status.h"

namespace vse::storage
{

const char* ErrorCodeName(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::NotFound: return "not found";
    case ErrorCode::Exists: return "exists";
    case ErrorCode::Corrupt: return "corrupt";
    case ErrorCode::TooNew: return "too new";
    case ErrorCode::TooOld: return "too old";
    case ErrorCode::LimitExceeded: return "limit exceeded";
    case ErrorCode::Mismatch: return "mismatch";
    case ErrorCode::Invalid: return "invalid";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::Io: return "io";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::MissingReference: return "missing reference";
    case ErrorCode::Duplicate: return "duplicate";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Other: return "other";
    }
    return "other";
}

}  // namespace vse::storage
