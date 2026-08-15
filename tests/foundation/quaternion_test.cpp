#include <catch2/catch_test_macros.hpp>

#include "math/math_util.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "test_helpers.h"

using engine_placeholder::kPi;
using engine_placeholder::Quaternion;
using engine_placeholder::Slerp;
using engine_placeholder::Vec3;
using engine_placeholder::test::QuaternionNearlyEqual;
using engine_placeholder::test::Vec3NearlyEqual;

TEST_CASE("Quaternion multiply matches the Hamilton product", "[math][quaternion]") {
    // Unit quaternions i=(1,0,0,0), j=(0,1,0,0), k=(0,0,1,0): i*j == k,
    // j*i == -k (anti-commutative), matching quaternion-algebra identities.
    Quaternion i(1, 0, 0, 0);
    Quaternion j(0, 1, 0, 0);
    Quaternion k(0, 0, 1, 0);
    REQUIRE(QuaternionNearlyEqual(i * j, k));
    REQUIRE(QuaternionNearlyEqual(j * i, Quaternion(-k.x, -k.y, -k.z, -k.w)));
}

TEST_CASE("90-degree rotation about +Z sends +X to +Y", "[math][quaternion]") {
    // +90 degrees about +Z rotates +X to +Y (right-handed).
    Quaternion rot = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Vec3 rotated = rot.RotateVector(Vec3(1, 0, 0));
    REQUIRE(Vec3NearlyEqual(rotated, Vec3(0, 1, 0)));
}

TEST_CASE("Quaternion::ToMat3() applied to a vector matches RotateVector() on the same vector", "[math][quaternion]") {
    Quaternion rot = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Vec3 v(1, 0, 0);
    REQUIRE(Vec3NearlyEqual(rot.ToMat3() * v, rot.RotateVector(v)));
}

TEST_CASE("Quaternion -> Mat3 -> Quaternion recovers the original", "[math][quaternion]") {
    // Axis (1,2,2)/3 (unit length), angle 1.0 rad keeps trace(ToMat3()) >
    // 0 so FromMat3 takes the same branch FromAxisAngle's w-sign implies,
    // giving an exact (not just up-to-sign) round trip.
    Quaternion original = Quaternion::FromAxisAngle(Vec3(1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f), 1.0f);
    Quaternion recovered = Quaternion::FromMat3(original.ToMat3());
    REQUIRE(QuaternionNearlyEqual(original, recovered));
}

TEST_CASE("Slerp interpolates correctly at t=0, t=1, and halfway", "[math][quaternion]") {
    Quaternion identity = Quaternion::Identity();
    Quaternion ninety = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    Quaternion forty_five = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.25f);

    REQUIRE(QuaternionNearlyEqual(Slerp(identity, ninety, 0.0f), identity));
    REQUIRE(QuaternionNearlyEqual(Slerp(identity, ninety, 1.0f), ninety));
    REQUIRE(QuaternionNearlyEqual(Slerp(identity, ninety, 0.5f), forty_five));
}
