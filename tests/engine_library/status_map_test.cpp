// Phase 23 Day 346: the table that maps the engine's result types onto the C API's statuses (engine/engine_c/status_map.h). Completeness has two halves: the
// mapping functions have no default case, so a new asset::ErrorCode or settings::SetError is a -Wswitch error until it is mapped; and these tests check that the
// mapping is the intended one, that every status has a unique name, and that the names follow the enum.
#include <catch2/catch_test_macros.hpp>

#include "engine/engine_c.h"
#include "engine_c/status_map.h"
#include "string/string.h"

using namespace engine_placeholder;
using engine_c::FromAssetError;
using engine_c::FromSetError;
using engine_c::kStatusCount;
using engine_c::StatusName;

TEST_CASE("Every status has a unique non-empty name, in the order of the enum", "[engine_c][status]")
{
    static_assert(kStatusCount == ENGINE_ERROR_INTERNAL + 1, "the last status is the last in the table");
    for (u32 i = 0; i < kStatusCount; ++i)
    {
        const StringView name(StatusName(static_cast<engine_status>(i)));
        REQUIRE(name.Length() > 0);
        REQUIRE(name != StringView("unknown"));
        for (u32 j = i + 1; j < kStatusCount; ++j)
        {
            REQUIRE(name != StringView(StatusName(static_cast<engine_status>(j))));
        }
    }
    CHECK(StringView(StatusName(ENGINE_OK)) == StringView("ok"));
    CHECK(StringView(StatusName(ENGINE_ERROR_INVALID_ARGUMENT)) == StringView("invalid_argument"));
    CHECK(StringView(StatusName(ENGINE_ERROR_STALE_HANDLE)) == StringView("stale_handle"));
    CHECK(StringView(StatusName(ENGINE_ERROR_DENIED)) == StringView("denied"));
    CHECK(StringView(StatusName(ENGINE_ERROR_INTERNAL)) == StringView("internal"));
    CHECK(StringView(StatusName(static_cast<engine_status>(kStatusCount))) == StringView("unknown"));
    CHECK(StringView(StatusName(static_cast<engine_status>(-1))) == StringView("unknown"));
}

TEST_CASE("Every asset error code maps to a status, and only Other is internal", "[engine_c][status]")
{
    using asset::ErrorCode;
    const ErrorCode all[] = {ErrorCode::Ok,
                             ErrorCode::MissingDependency,
                             ErrorCode::DependencyTypeMismatch,
                             ErrorCode::DependencyCycle,
                             ErrorCode::DuplicateId,
                             ErrorCode::NotFound,
                             ErrorCode::Corrupt,
                             ErrorCode::UnsupportedVersion,
                             ErrorCode::TypeMismatch,
                             ErrorCode::TooLarge,
                             ErrorCode::Io,
                             ErrorCode::Cancelled,
                             ErrorCode::Offline,
                             ErrorCode::LimitExceeded,
                             ErrorCode::WrongTarget,
                             ErrorCode::Invalid,
                             ErrorCode::InUse,
                             ErrorCode::NotReady,
                             ErrorCode::Unsupported,
                             ErrorCode::Other};
    // The list is every code: the enum's values are 0 through Other, in order.
    REQUIRE(sizeof(all) / sizeof(all[0]) == static_cast<usize>(ErrorCode::Other) + 1);
    for (usize i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
    {
        REQUIRE(static_cast<usize>(all[i]) == i);
        const engine_status status = FromAssetError(all[i]);
        REQUIRE(static_cast<u32>(status) < kStatusCount);
        if (all[i] == ErrorCode::Ok)
        {
            CHECK(status == ENGINE_OK);
        }
        else if (all[i] == ErrorCode::Other)
        {
            CHECK(status == ENGINE_ERROR_INTERNAL);
        }
        else
        {
            CHECK(status != ENGINE_OK);
            CHECK(status != ENGINE_ERROR_INTERNAL);
        }
    }
    CHECK(FromAssetError(ErrorCode::NotFound) == ENGINE_ERROR_NOT_FOUND);
    CHECK(FromAssetError(ErrorCode::Corrupt) == ENGINE_ERROR_CORRUPT);
    CHECK(FromAssetError(ErrorCode::UnsupportedVersion) == ENGINE_ERROR_UNSUPPORTED_VERSION);
    CHECK(FromAssetError(ErrorCode::DuplicateId) == ENGINE_ERROR_ALREADY_EXISTS);
    CHECK(FromAssetError(ErrorCode::Invalid) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(FromAssetError(ErrorCode::WrongTarget) == ENGINE_ERROR_UNSUPPORTED);
}

TEST_CASE("Every settings error maps to a status", "[engine_c][status]")
{
    using settings::SetError;
    CHECK(FromSetError(SetError::None) == ENGINE_OK);
    CHECK(FromSetError(SetError::UnknownKey) == ENGINE_ERROR_NOT_FOUND);
    CHECK(FromSetError(SetError::BadValue) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(FromSetError(SetError::Unsupported) == ENGINE_ERROR_UNSUPPORTED);
    CHECK(FromSetError(SetError::Store) == ENGINE_ERROR_IO);
    CHECK(FromSetError(SetError::ApplyFailed) == ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(FromSetError(SetError::Reentrant) == ENGINE_ERROR_LIMIT_EXCEEDED);
    CHECK(FromSetError(SetError::Enforced) == ENGINE_ERROR_DENIED);
}
