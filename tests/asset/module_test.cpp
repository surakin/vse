// The module skeleton (Phase 22 Day 313): the library links and reports the container version it reads.

#include <catch2/catch_test_macros.hpp>

#include "asset/asset_module.h"

namespace vse::asset
{

TEST_CASE("The asset module reports the container version it reads", "[asset]")
{
    CHECK(CurrentContainerVersion() == kContainerVersion);
    CHECK(kContainerVersion >= 1);
}

}  // namespace vse::asset
