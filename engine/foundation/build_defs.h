#pragma once

//! The operating system, compiler, processor and sanitizer the code is being built for, as engine macros.
//!
//! Engine code never tests a compiler's or a system's own predefined macro (`_WIN32`, `__linux__`, `__clang__`,
//! `_MSC_VER`,
//! `__x86_64__`, `__SANITIZE_ADDRESS__`, `__has_feature`): it asks these. A port to a new platform, compiler or
//! processor changes this header only, and the `build_defs_check` gate (`tools/build_defs_check`) fails on a raw macro
//! anywhere else.
//!
//! Every macro is always defined, as `1` or `0`, so a test is `#if ENGINE_PLATFORM_WINDOWS` (never `#ifdef`), and
//! exactly one macro of each of the first three families is `1`. (A misspelled name would evaluate to `0` silently in
//! the preprocessor; the gate and the test `tests/foundation/build_defs_test.cpp` hold the spellings.)
//!
//! | Family | Macros |
//! |---|---|
//! | operating system | `ENGINE_PLATFORM_WINDOWS`, `_LINUX`, `_MACOS`, `_IOS`, `_ANDROID` |
//! | compiler | `ENGINE_COMPILER_MSVC` (cl), `_CLANG_CL`, `_CLANG`, `_GCC`; `_CLANG_FAMILY`, `_MSVC_ABI` |
//! | processor | `ENGINE_ARCH_X64`, `_X86`, `_ARM64` |
//! | sanitizer (0 or 1) | `ENGINE_SANITIZER_ADDRESS`, `ENGINE_SANITIZER_THREAD`, `ENGINE_SANITIZED` for either |
//!
//! `ENGINE_COMPILER_CLANG` is Clang on Linux, macOS and Android only; clang-cl is `ENGINE_COMPILER_CLANG_CL`. Code that
//! means "any Clang" (an attribute both accept) tests `ENGINE_COMPILER_CLANG_FAMILY`; code that means "the MSVC calling
//! and exception conventions" (intrinsics, `__declspec`) tests `ENGINE_COMPILER_MSVC_ABI`. This is a base-layer header
//! that every layer may include; it includes nothing.

// --- operating system
// ---------------------------------------------------------------------------------------------------------

#if defined(_WIN32)
#define ENGINE_PLATFORM_WINDOWS 1
#else
#define ENGINE_PLATFORM_WINDOWS 0
#endif

#if defined(__ANDROID__)
#define ENGINE_PLATFORM_ANDROID 1
#else
#define ENGINE_PLATFORM_ANDROID 0
#endif

// Android's kernel is Linux's, and its compiler defines `__linux__` too; ENGINE_PLATFORM_LINUX is the desktop and
// server system only.
#if defined(__linux__) && !defined(__ANDROID__)
#define ENGINE_PLATFORM_LINUX 1
#else
#define ENGINE_PLATFORM_LINUX 0
#endif

// Apple's compiler says which system it targets in a predefined macro; <TargetConditionals.h> would say it too, but it
// needs the Apple SDK, and the cross-compiling syntax checks (tests/*/cross_syntax) run without one.
#if defined(__APPLE__) && defined(__MACH__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define ENGINE_PLATFORM_MACOS 1
#else
#define ENGINE_PLATFORM_MACOS 0
#endif

#if defined(__APPLE__) && defined(__MACH__) && !ENGINE_PLATFORM_MACOS
#define ENGINE_PLATFORM_IOS 1
#else
#define ENGINE_PLATFORM_IOS 0
#endif

// --- compiler
// -----------------------------------------------------------------------------------------------------------------

#if defined(__clang__) && defined(_MSC_VER)
#define ENGINE_COMPILER_CLANG_CL 1
#else
#define ENGINE_COMPILER_CLANG_CL 0
#endif

#if defined(__clang__) && !defined(_MSC_VER)
#define ENGINE_COMPILER_CLANG 1
#else
#define ENGINE_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define ENGINE_COMPILER_MSVC 1
#else
#define ENGINE_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define ENGINE_COMPILER_GCC 1
#else
#define ENGINE_COMPILER_GCC 0
#endif

#define ENGINE_COMPILER_CLANG_FAMILY (ENGINE_COMPILER_CLANG || ENGINE_COMPILER_CLANG_CL)
#define ENGINE_COMPILER_MSVC_ABI (ENGINE_COMPILER_MSVC || ENGINE_COMPILER_CLANG_CL)

/// Whether the compiler has the named builtin function (`ENGINE_HAS_BUILTIN(__builtin_debugtrap)`); `0` where it cannot
/// tell.
#if defined(__has_builtin)
#define ENGINE_HAS_BUILTIN(name) __has_builtin(name)
#else
#define ENGINE_HAS_BUILTIN(name) 0
#endif

// --- processor
// ----------------------------------------------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)
#define ENGINE_ARCH_X64 1
#else
#define ENGINE_ARCH_X64 0
#endif

#if defined(__i386__) || defined(_M_IX86)
#define ENGINE_ARCH_X86 1
#else
#define ENGINE_ARCH_X86 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define ENGINE_ARCH_ARM64 1
#else
#define ENGINE_ARCH_ARM64 0
#endif

// --- sanitizers
// --------------------------------------------------------------------------------------------------------------- GCC
// and MSVC define __SANITIZE_ADDRESS__ (GCC also __SANITIZE_THREAD__); Clang answers __has_feature instead.

#if defined(__SANITIZE_ADDRESS__)
#define ENGINE_SANITIZER_ADDRESS 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ENGINE_SANITIZER_ADDRESS 1
#endif
#endif
#ifndef ENGINE_SANITIZER_ADDRESS
#define ENGINE_SANITIZER_ADDRESS 0
#endif

#if defined(__SANITIZE_THREAD__)
#define ENGINE_SANITIZER_THREAD 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ENGINE_SANITIZER_THREAD 1
#endif
#endif
#ifndef ENGINE_SANITIZER_THREAD
#define ENGINE_SANITIZER_THREAD 0
#endif

#define ENGINE_SANITIZED (ENGINE_SANITIZER_ADDRESS || ENGINE_SANITIZER_THREAD)

// ENGINE_STATIC_ANALYSIS is 1 while the Clang static analyzer (clang-tidy's clang-analyzer-* checks, scripts/tidy.sh)
// reads the code, else 0. The assert macros use it to tell the analyzer that a failed assertion does not continue, as
// it does not in a normal build (the handler aborts).
#if defined(__clang_analyzer__)
#define ENGINE_STATIC_ANALYSIS 1
#else
#define ENGINE_STATIC_ANALYSIS 0
#endif
