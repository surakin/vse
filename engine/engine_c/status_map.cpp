#include "engine_c/status_map.h"

namespace vse::engine_c
{

namespace
{

// Indexed by the status's value; the test (tests/engine_library/status_map_test.cpp) checks that the order matches the
// enum and that the names are unique.
constexpr const char* kNames[kStatusCount] = {
    "ok",
    "invalid_argument",
    "struct_size",
    "unsupported_version",
    "stale_handle",
    "not_found",
    "already_exists",
    "corrupt",
    "unsupported",
    "io",
    "cancelled",
    "limit_exceeded",
    "not_ready",
    "in_use",
    "type_mismatch",
    "missing_dependency",
    "dependency_cycle",
    "offline",
    "too_large",
    "denied",
    "internal",
};

}  // namespace

engine_status FromAssetError(asset::ErrorCode code)
{
    switch (code)
    {
    case asset::ErrorCode::Ok: return ENGINE_OK;
    case asset::ErrorCode::MissingDependency: return ENGINE_ERROR_MISSING_DEPENDENCY;
    case asset::ErrorCode::DependencyTypeMismatch: return ENGINE_ERROR_TYPE_MISMATCH;
    case asset::ErrorCode::DependencyCycle: return ENGINE_ERROR_DEPENDENCY_CYCLE;
    case asset::ErrorCode::DuplicateId: return ENGINE_ERROR_ALREADY_EXISTS;
    case asset::ErrorCode::NotFound: return ENGINE_ERROR_NOT_FOUND;
    case asset::ErrorCode::Corrupt: return ENGINE_ERROR_CORRUPT;
    case asset::ErrorCode::UnsupportedVersion: return ENGINE_ERROR_UNSUPPORTED_VERSION;
    case asset::ErrorCode::TypeMismatch: return ENGINE_ERROR_TYPE_MISMATCH;
    case asset::ErrorCode::TooLarge: return ENGINE_ERROR_TOO_LARGE;
    case asset::ErrorCode::Io: return ENGINE_ERROR_IO;
    case asset::ErrorCode::Cancelled: return ENGINE_ERROR_CANCELLED;
    case asset::ErrorCode::Offline: return ENGINE_ERROR_OFFLINE;
    case asset::ErrorCode::LimitExceeded: return ENGINE_ERROR_LIMIT_EXCEEDED;
    case asset::ErrorCode::WrongTarget: return ENGINE_ERROR_UNSUPPORTED;
    case asset::ErrorCode::Invalid: return ENGINE_ERROR_INVALID_ARGUMENT;
    case asset::ErrorCode::InUse: return ENGINE_ERROR_IN_USE;
    case asset::ErrorCode::NotReady: return ENGINE_ERROR_NOT_READY;
    case asset::ErrorCode::Unsupported: return ENGINE_ERROR_UNSUPPORTED;
    case asset::ErrorCode::Other: return ENGINE_ERROR_INTERNAL;
    }
    return ENGINE_ERROR_INTERNAL;
}

engine_status FromSetError(settings::SetError error)
{
    switch (error)
    {
    case settings::SetError::None: return ENGINE_OK;
    case settings::SetError::UnknownKey: return ENGINE_ERROR_NOT_FOUND;
    case settings::SetError::BadValue: return ENGINE_ERROR_INVALID_ARGUMENT;
    case settings::SetError::Unsupported: return ENGINE_ERROR_UNSUPPORTED;
    case settings::SetError::Store: return ENGINE_ERROR_IO;
    case settings::SetError::ApplyFailed: return ENGINE_ERROR_INVALID_ARGUMENT;
    case settings::SetError::Reentrant: return ENGINE_ERROR_LIMIT_EXCEEDED;
    case settings::SetError::Enforced: return ENGINE_ERROR_DENIED;
    }
    return ENGINE_ERROR_INTERNAL;
}

const char* StatusName(engine_status status)
{
    const u32 index = static_cast<u32>(status);
    return index < kStatusCount ? kNames[index] : "unknown";
}

}  // namespace vse::engine_c
