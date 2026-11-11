#include <catch2/catch_test_macros.hpp>

#include "database/sqlite/sqlite_version.h"
#include "cstr/cstr.h"

namespace engine_placeholder::database {

TEST_CASE("SQLite: the vendored library reports the pinned version", "[database][sqlite]") {
    // third_party/sqlite/VENDORED.md pins 3.54.1; a change of the pin updates this line with it.
    const char* version = SqliteVersion();
    REQUIRE(version != nullptr);
    CHECK(CStrCompare(version, "3.54.1") == 0);
}

} // namespace engine_placeholder::database
