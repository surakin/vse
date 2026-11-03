#include <catch2/catch_test_macros.hpp>

#include "types.h"

TEST_CASE("jobs test target builds and runs", "[jobs]") {
    REQUIRE(sizeof(engine_placeholder::u64) == 8);
}
