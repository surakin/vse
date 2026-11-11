#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"

// The macros of build_defs.h are always defined, as 0 or 1, and exactly one of each family is 1 (the first three tables
// of its comment). The gate platform_neutral_check keeps raw compiler and system macros out of every other file.

namespace
{

constexpr int kPlatforms = ENGINE_PLATFORM_WINDOWS + ENGINE_PLATFORM_LINUX + ENGINE_PLATFORM_MACOS +
                           ENGINE_PLATFORM_IOS + ENGINE_PLATFORM_ANDROID;
constexpr int kCompilers = ENGINE_COMPILER_MSVC + ENGINE_COMPILER_CLANG_CL + ENGINE_COMPILER_CLANG +
                           ENGINE_COMPILER_GCC;
constexpr int kArchitectures = ENGINE_ARCH_X64 + ENGINE_ARCH_X86 + ENGINE_ARCH_ARM64;

static_assert(kPlatforms == 1, "exactly one ENGINE_PLATFORM_* macro is 1");
static_assert(kCompilers == 1, "exactly one of ENGINE_COMPILER_MSVC, _CLANG_CL, _CLANG and _GCC is 1");
static_assert(kArchitectures == 1, "exactly one ENGINE_ARCH_* macro is 1");

}  // namespace

TEST_CASE("Exactly one platform, compiler and processor is selected", "[build_defs]")
{
    CHECK(kPlatforms == 1);
    CHECK(kCompilers == 1);
    CHECK(kArchitectures == 1);
}

TEST_CASE("The combined compiler macros agree with the single ones", "[build_defs]")
{
    CHECK((ENGINE_COMPILER_CLANG_FAMILY != 0) == (ENGINE_COMPILER_CLANG == 1 || ENGINE_COMPILER_CLANG_CL == 1));
    CHECK((ENGINE_COMPILER_MSVC_ABI != 0) == (ENGINE_COMPILER_MSVC == 1 || ENGINE_COMPILER_CLANG_CL == 1));
#if ENGINE_PLATFORM_LINUX || ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_ANDROID
    CHECK(ENGINE_COMPILER_MSVC_ABI == 0);  // the MSVC family builds for Windows only
#endif
}

TEST_CASE("The sanitizer macros are 0 or 1 and ENGINE_SANITIZED is their union", "[build_defs]")
{
    CHECK((ENGINE_SANITIZER_ADDRESS == 0 || ENGINE_SANITIZER_ADDRESS == 1));
    CHECK((ENGINE_SANITIZER_THREAD == 0 || ENGINE_SANITIZER_THREAD == 1));
    CHECK((ENGINE_SANITIZED != 0) == (ENGINE_SANITIZER_ADDRESS == 1 || ENGINE_SANITIZER_THREAD == 1));
    CHECK((ENGINE_SANITIZER_ADDRESS == 1 && ENGINE_SANITIZER_THREAD == 1) == false);  // the two cannot be combined
}

TEST_CASE("ENGINE_HAS_BUILTIN is usable in an #if", "[build_defs]")
{
#if ENGINE_HAS_BUILTIN(__builtin_trap)
    SUCCEED();
#else
    CHECK(ENGINE_COMPILER_MSVC == 1);  // only cl answers no
#endif
}
