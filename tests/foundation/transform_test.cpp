#include <catch2/catch_test_macros.hpp>

#include "math/math_util.h"
#include "math/quaternion.h"
#include "math/transform.h"
#include "math/vector.h"
#include "test_helpers.h"

using engine_placeholder::Compose;
using engine_placeholder::kPi;
using engine_placeholder::Quaternion;
using engine_placeholder::Transform;
using engine_placeholder::Vec3;
using engine_placeholder::Vec4;
using engine_placeholder::test::NearlyEqual;
using engine_placeholder::test::QuaternionNearlyEqual;
using engine_placeholder::test::Vec3NearlyEqual;

TEST_CASE("Compose(parent, child) rotates the child's local offset into parent space before adding it",
          "[math][transform]")
{
    Transform parent;
    parent.position = Vec3(1, 0, 0);
    parent.rotation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);

    Transform child;
    child.position = Vec3(1, 0, 0);

    Transform composed = Compose(parent, child);
    // child's local (1,0,0) offset gets rotated 90 degrees about Z by the
    // parent (-> (0,1,0)) then added to the parent's own position (1,0,0).
    REQUIRE(Vec3NearlyEqual(composed.position, Vec3(1, 1, 0)));
    REQUIRE(QuaternionNearlyEqual(composed.rotation, parent.rotation));
}

TEST_CASE("Transform::ToMatrix() applies scale before translation", "[math][transform]")
{
    Transform t;
    t.position = Vec3(2, 3, 4);
    t.scale = Vec3(2, 3, 4);

    Vec4 transformed = t.ToMatrix() * Vec4(1, 1, 1, 1);
    REQUIRE((NearlyEqual(transformed.x, 4) && NearlyEqual(transformed.y, 6) && NearlyEqual(transformed.z, 8)));
    Vec4 origin_transformed = t.ToMatrix() * Vec4(0, 0, 0, 1);
    REQUIRE((NearlyEqual(origin_transformed.x, 2) && NearlyEqual(origin_transformed.y, 3) &&
             NearlyEqual(origin_transformed.z, 4)));
}
