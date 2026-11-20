#include <catch2/catch_test_macros.hpp>

#include "math/matrix.h"
#include "math/vector.h"
#include "test_helpers.h"

using vse::Mat3;
using vse::Mat4;
using vse::Vec3;
using vse::Vec4;
using vse::test::NearlyEqual;
using vse::test::Vec3NearlyEqual;

TEST_CASE("Mat3 multiply matches a hand-calculated reference product", "[math][matrix]")
{
    // A: columns (1,4,7),(2,5,8),(3,6,10) == rows (1,2,3),(4,5,6),(7,8,10)
    Mat3 a = Mat3::FromBasis(Vec3(1, 4, 7), Vec3(2, 5, 8), Vec3(3, 6, 10));
    // B: columns (1,0,1),(0,1,0),(2,1,1) == rows (1,0,2),(0,1,1),(1,0,1)
    Mat3 b = Mat3::FromBasis(Vec3(1, 0, 1), Vec3(0, 1, 0), Vec3(2, 1, 1));

    // Hand-computed A*B (standard row-times-column):
    //   row0=(1,2,3): (1*1+2*0+3*1, 1*0+2*1+3*0, 1*2+2*1+3*1) = (4, 2, 7)
    //   row1=(4,5,6): (4*1+5*0+6*1, 4*0+5*1+6*0, 4*2+5*1+6*1) = (10, 5, 19)
    //   row2=(7,8,10): (7*1+8*0+10*1, 7*0+8*1+10*0, 7*2+8*1+10*1) = (17, 8, 32)
    // i.e. columns (4,10,17), (2,5,8), (7,19,32).
    Mat3 product = a * b;
    REQUIRE((Vec3NearlyEqual(product.col[0], Vec3(4, 10, 17)) && Vec3NearlyEqual(product.col[1], Vec3(2, 5, 8)) &&
             Vec3NearlyEqual(product.col[2], Vec3(7, 19, 32))));

    REQUIRE((Vec3NearlyEqual(Mat3::Identity() * a.col[0], a.col[0]) &&
             Vec3NearlyEqual((Mat3::Identity() * a).col[1], a.col[1])));

    // A * v with v=(1,2,3): (1*1+2*2+3*3, 4*1+5*2+6*3, 7*1+8*2+10*3) = (14,32,53)
    REQUIRE(Vec3NearlyEqual(a * Vec3(1, 2, 3), Vec3(14, 32, 53)));
}

TEST_CASE("Mat3::Transpose swaps rows and columns as hand-calculated", "[math][matrix]")
{
    Mat3 a = Mat3::FromBasis(Vec3(1, 4, 7), Vec3(2, 5, 8), Vec3(3, 6, 10));
    Mat3 at = a.Transpose();
    // Transpose's columns are A's rows: (1,2,3), (4,5,6), (7,8,10).
    REQUIRE((Vec3NearlyEqual(at.col[0], Vec3(1, 2, 3)) && Vec3NearlyEqual(at.col[1], Vec3(4, 5, 6)) &&
             Vec3NearlyEqual(at.col[2], Vec3(7, 8, 10))));
}

TEST_CASE("composing two Mat4 translations adds their offsets", "[math][matrix]")
{
    Mat4 translate_a = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 2, 3));
    Mat4 translate_b = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(10, 20, 30));

    // Composing two pure translations should add their offsets.
    Mat4 combined = translate_a * translate_b;
    Vec4 origin_translated = combined * Vec4(0, 0, 0, 1);
    REQUIRE((NearlyEqual(origin_translated.x, 11) && NearlyEqual(origin_translated.y, 22) &&
             NearlyEqual(origin_translated.z, 33)));
}
