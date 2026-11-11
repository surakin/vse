#include "storage/status.h"

namespace engine_placeholder::storage {

const char* ErrorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::NotFound: return "not found";
    case ErrorCode::Exists: return "exists";
    case ErrorCode::Corrupt: return "corrupt";
    case ErrorCode::TooNew: return "too new";
    case ErrorCode::TooOld: return "too old";
    case ErrorCode::LimitExceeded: return "limit exceeded";
    case ErrorCode::Mismatch: return "mismatch";
    case ErrorCode::Invalid: return "invalid";
    case ErrorCode::MissingReference: return "missing_reference";
    case ErrorCode::Duplicate: return "duplicate";
    case ErrorCode::Other: return "other";
    }
    return "other";
}

} // namespace engine_placeholder::storage
