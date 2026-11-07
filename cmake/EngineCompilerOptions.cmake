# Compiler options per compiler family (Phase 13 Day 148). Included by the root CMakeLists.txt after project().
#
# Sets:
#   ENGINE_PRODUCTION_COMPILE_OPTIONS  exceptions, RTTI and the stack protector off for the engine's own libraries (applied PRIVATE
#                                      per target, so Catch2 and the test executables keep exceptions)
#   ENGINE_MSVC_LIKE                   TRUE for cl and clang-cl (the MSVC command-line syntax)
#   ENGINE_EXCEPTIONS_COMPILE_OPTIONS  exceptions on, for the non-engine targets that need them (Catch2 and the tests through it,
#                                      tools that use a library which throws, such as nlohmann/json in tools/shader_mint)
#   ENGINE_WINDOWS_COMPILE_OPTIONS, ENGINE_WINDOWS_DEFINITIONS  the engine's own options on Windows; the root CMakeLists.txt applies
#                                      them after the vendored libraries are added, so third-party code keeps its own settings
#   ENGINE_HAVE_ASAN, ENGINE_HAVE_TSAN whether the sanitizer flavors exist with this compiler
#   ENGINE_ASAN_COMPILE_OPTIONS, ENGINE_ASAN_LINK_OPTIONS, ENGINE_TSAN_COMPILE_OPTIONS, ENGINE_TSAN_LINK_OPTIONS
# and the build-wide options every target gets on Windows.

set(ENGINE_MSVC_LIKE FALSE)
if(MSVC)  # true for cl.exe and for clang-cl
  set(ENGINE_MSVC_LIKE TRUE)
endif()

if(NOT ENGINE_MSVC_LIKE)
  # GCC and Clang (Linux). The sanitizer flags are unchanged from Phase 7: -fno-sanitize=vptr because the engine libraries are built
  # -fno-rtti while the Catch2 executables are not (see engine/foundation/CMakeLists.txt).
  set(ENGINE_PRODUCTION_COMPILE_OPTIONS -fno-exceptions -fno-rtti -fno-stack-protector)
  set(ENGINE_EXCEPTIONS_COMPILE_OPTIONS "")  # GCC and Clang have exceptions on unless told otherwise
  set(ENGINE_WINDOWS_COMPILE_OPTIONS "")
  set(ENGINE_WINDOWS_DEFINITIONS "")
  set(ENGINE_HAVE_ASAN TRUE)
  set(ENGINE_HAVE_TSAN TRUE)
  set(ENGINE_ASAN_COMPILE_OPTIONS -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
  set(ENGINE_ASAN_LINK_OPTIONS -fsanitize=address,undefined)
  set(ENGINE_TSAN_COMPILE_OPTIONS -fsanitize=thread -fno-omit-frame-pointer -g)
  set(ENGINE_TSAN_LINK_OPTIONS -fsanitize=thread)
  return()
endif()

# --- Windows: cl and clang-cl -------------------------------------------------------------------------------------------
set(ENGINE_CLANG_CL FALSE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(ENGINE_CLANG_CL TRUE)
endif()

# The engine's own code: UTF-8 source and execution character sets, standard conformance, an honest __cplusplus, the conforming
# preprocessor (cl only; clang-cl always has these behaviors), and the Windows headers in their lean, Unicode form without the min/max
# macros. _CRT_SECURE_NO_WARNINGS: the C library is used on purpose (C4996 on strncpy, getenv; spec, decision 1). Not applied here:
# the root CMakeLists.txt adds them after third_party/, because vendored code (sentry-native, Breakpad) builds with its own settings.
# C and C++ only, so a resource compiler (rc.exe) never sees them.
# (One generator expression per flag: a `;` inside one would split it when the list is expanded.)
set(ENGINE_WINDOWS_COMPILE_OPTIONS
  "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
  "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->"
  "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:__cplusplus>"
  "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>")
set(ENGINE_WINDOWS_DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
set(ENGINE_EXCEPTIONS_COMPILE_OPTIONS /EHsc)

# CMake's default C++ flags for MSVC hold /EHsc and /GR. The engine's libraries turn both off, and passing /EHsc and then /EHs-c-
# makes cl warn about the override on every file, so they are removed here and given back, as ENGINE_EXCEPTIONS_COMPILE_OPTIONS, to the
# targets that need exceptions: Catch2 (PUBLIC, so every test executable), the vendored libraries' directories, and any tool that uses
# a library which throws (tools/shader_mint with nlohmann/json, from Phase 13 Day 150).
# /RTC1 (the Debug default) cannot be combined with /fsanitize=address; Linux has no counterpart, so it is dropped everywhere.
foreach(_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
  string(REGEX REPLACE "(^| )/EHsc( |$)" " " ${_var} "${${_var}}")
  string(REGEX REPLACE "(^| )/GR( |$)" " " ${_var} "${${_var}}")
  string(REGEX REPLACE "(^| )/RTC1( |$)" " " ${_var} "${${_var}}")
endforeach()

# Symbols: every program has a PDB and a CodeView (RSDS) record, in every configuration, which the crash report and the symbol store
# use as the build id (decision 5). /FS lets parallel compiles share a PDB.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/Zi;/FS>")  # (not for rc.exe)
add_link_options(/DEBUG:FULL)

# Every executable carries cmake/engine.manifest (UTF-8 active code page, long paths, per-monitor-v2 DPI, supported OS versions).
# CMake merges a target's .manifest sources into the program with the mt.exe the toolchain file found (CMAKE_MT); a linker
# /MANIFEST:EMBED flag would make link.exe look for mt.exe on PATH instead, which a plain shell does not have. So at the end of
# configuring, every executable target in the build gets the manifest as a source.
function(engine_add_manifest_to_executables dir)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/engine.manifest")
    endif()
  endforeach()
  get_property(subdirectories DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    engine_add_manifest_to_executables("${subdirectory}")
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL engine_add_manifest_to_executables "${CMAKE_SOURCE_DIR}")

# /W4 for the engine's own code; warnings are not errors (as on Linux). C4324 ("structure was padded due to alignment specifier") is off:
# the engine writes alignas only where the padding is the point (cache-line separation in jobs/deque.h).
set(ENGINE_PRODUCTION_COMPILE_OPTIONS /EHs-c- /GR- /GS- /D_HAS_EXCEPTIONS=0 /W4 /wd4324)

if(ENGINE_CLANG_CL)
  # Native __int128 division needs compiler-rt's builtins (the toolchain file found the library).
  link_libraries("${ENGINE_CLANG_BUILTINS}")
  # AddressSanitizer with clang-cl needs its own runtime libraries named on the lld-link line; not wired up yet (the clang-cl
  # configuration is a compile-and-unit-test check, Phase 13 rules).
  set(ENGINE_HAVE_ASAN FALSE)
else()
  set(ENGINE_HAVE_ASAN TRUE)
  set(ENGINE_ASAN_COMPILE_OPTIONS /fsanitize=address)
  set(ENGINE_ASAN_LINK_OPTIONS "")  # cl records the runtime libraries in the objects (/INFERASANLIBS is link.exe's default)
endif()
set(ENGINE_HAVE_TSAN FALSE)  # MSVC has no ThreadSanitizer; the tsan flavor runs on Linux only
