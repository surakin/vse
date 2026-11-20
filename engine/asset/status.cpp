#include "asset/status.h"

namespace vse::asset
{

const char* ErrorCodeName(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::MissingDependency: return "missing dependency";
    case ErrorCode::DependencyTypeMismatch: return "dependency type mismatch";
    case ErrorCode::DependencyCycle: return "dependency cycle";
    case ErrorCode::DuplicateId: return "duplicate id";
    case ErrorCode::NotFound: return "not found";
    case ErrorCode::Corrupt: return "corrupt";
    case ErrorCode::UnsupportedVersion: return "unsupported version";
    case ErrorCode::TypeMismatch: return "type mismatch";
    case ErrorCode::TooLarge: return "too large";
    case ErrorCode::Io: return "io";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Offline: return "offline";
    case ErrorCode::LimitExceeded: return "limit exceeded";
    case ErrorCode::WrongTarget: return "wrong target";
    case ErrorCode::Invalid: return "invalid";
    case ErrorCode::InUse: return "in use";
    case ErrorCode::NotReady: return "not ready";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Other: return "other";
    }
    return "other";
}

}  // namespace vse::asset
