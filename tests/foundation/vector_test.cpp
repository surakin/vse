#include <catch2/catch_test_macros.hpp>

#include "math/vector.h"
#include "test_helpers.h"

using vse::Vec2;
using vse::Vec3;
using vse::test::NearlyEqual;
using vse::test::Vec2NearlyEqual;
using vse::test::Vec3NearlyEqual;

TEST_CASE("Vec2/Vec3 Dot matches hand-calculated values", "[math][vector]")
{
    REQUIRE(NearlyEqual(Vec3(1, 2, 3).Dot(Vec3(4, 5, 6)), 32.0f));
    REQUIRE(NearlyEqual(Vec3(1, 0, 0).Dot(Vec3(0, 1, 0)), 0.0f));
    REQUIRE(NearlyEqual(Vec2(3, 4).Dot(Vec2(3, 4)), 25.0f));
}

TEST_CASE("Vec3 Cross is right-handed and anti-commutative", "[math][vector]")
{
    REQUIRE(Vec3NearlyEqual(Vec3(1, 0, 0).Cross(Vec3(0, 1, 0)), Vec3(0, 0, 1)));
    REQUIRE(Vec3NearlyEqual(Vec3(0, 1, 0).Cross(Vec3(1, 0, 0)), Vec3(0, 0, -1)));
    REQUIRE(Vec3NearlyEqual(Vec3(2, 3, 4).Cross(Vec3(5, 6, 7)), Vec3(-3, 6, -3)));
}

TEST_CASE("Vec2/Vec3 Normalized produces a unit-length vector in the same direction", "[math][vector]")
{
    REQUIRE(Vec3NearlyEqual(Vec3(3, 4, 0).Normalized(), Vec3(0.6f, 0.8f, 0.0f)));
    REQUIRE(NearlyEqual(Vec3(3, 4, 0).Normalized().Length(), 1.0f));
    REQUIRE(Vec2NearlyEqual(Vec2(3, 4).Normalized(), Vec2(0.6f, 0.8f)));
}
