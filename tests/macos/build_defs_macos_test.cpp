// build_defs.h on Apple clang (Phase 26 Day 300): the macOS build selects macOS, x86-64 and Clang, and the sanitizer
// macros say which flavor this executable is. Built in the plain, asan and tsan flavors; ENGINE_TEST_EXPECT_ASAN and
// ENGINE_TEST_EXPECT_TSAN come from tests/macos/CMakeLists.txt. The neutral checks (exactly one of each family) are
// tests/foundation/build_defs_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"

TEST_CASE("build_defs.h selects macOS on the macOS build", "[build_defs][macos]")
{
    CHECK(ENGINE_PLATFORM_MACOS == 1);
    CHECK(ENGINE_PLATFORM_IOS == 0);
    CHECK(ENGINE_PLATFORM_LINUX == 0);
    CHECK(ENGINE_PLATFORM_WINDOWS == 0);
    CHECK(ENGINE_PLATFORM_ANDROID == 0);
}

TEST_CASE("build_defs.h selects x86-64 and Clang with Apple clang", "[build_defs][macos]")
{
    CHECK(ENGINE_ARCH_X64 == 1);  // the macOS build is x86-64 only until the arm64 compile check (Phase 26 Day 326)
    CHECK(ENGINE_ARCH_ARM64 == 0);
    CHECK(ENGINE_COMPILER_CLANG == 1);
    CHECK(ENGINE_COMPILER_GCC == 0);
    CHECK(ENGINE_COMPILER_MSVC == 0);
    CHECK(ENGINE_COMPILER_CLANG_CL == 0);
    CHECK(ENGINE_COMPILER_CLANG_FAMILY == 1);
    CHECK(ENGINE_COMPILER_MSVC_ABI == 0);
    CHECK(ENGINE_HAS_BUILTIN(__builtin_expect) == 1);  // Apple clang answers __has_builtin
}

TEST_CASE("build_defs.h names the sanitizer this executable is built with", "[build_defs][macos]")
{
    CHECK(ENGINE_SANITIZER_ADDRESS == ENGINE_TEST_EXPECT_ASAN);
    CHECK(ENGINE_SANITIZER_THREAD == ENGINE_TEST_EXPECT_TSAN);
    CHECK(ENGINE_SANITIZED == (ENGINE_TEST_EXPECT_ASAN || ENGINE_TEST_EXPECT_TSAN));
}
