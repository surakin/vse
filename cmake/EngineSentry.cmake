# engine_add_sentry([C11]): sentry-native for the crash handler (third_party/sentry_native, PLAN.md assumption #20), configured the same way on
# every platform: crash capture and minidumps only, the Breakpad backend, no transport (nothing can be uploaded), a static library, no tests,
# examples, benchmarks or install. Called by each platform's branch of the root CMakeLists.txt before the engine's own options, so the library
# keeps its own flags (Phase 26 Day 307: one definition in place of three copies).
#   C11: build the library's C as C11 (it sets C99 when no standard is given; glibc's memchr/strchr macros use _Generic, so every call warned,
#        -Wc11-extensions on Clang). Scoped to this call: no other C target changes.
function(engine_add_sentry)
  cmake_parse_arguments(SENTRY_ARG "C11" "" "" ${ARGN})
  set(SENTRY_BACKEND breakpad CACHE STRING "" FORCE)
  set(SENTRY_TRANSPORT none CACHE STRING "" FORCE)
  set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  if(SENTRY_ARG_C11)
    set(CMAKE_C_STANDARD 11) # function scope: visible to the subdirectory, gone after the call
  endif()
  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/sentry_native ${CMAKE_BINARY_DIR}/third_party/sentry_native)
endfunction()
