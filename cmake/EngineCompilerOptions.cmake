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
#   ENGINE_MACOS_COMPILE_OPTIONS, ENGINE_MACOS_DEFINITIONS  the same on macOS (Phase 21 Day 284): ARC for Objective-C++, and Apple's
#                                      AssertMacros.h kept to its underscored names
#   ENGINE_FP_COMPILE_OPTIONS          the floating-point rules of the physics step (Phase 25 Day 374, docs/superpowers/specs/2026-11-16-physics-design.md,
#                                      section 3): no contraction to fused multiply-add and no fast-math, for every target of ours, so that a build
#                                      for a machine with FMA (-march=native, arm64) gives the same bits as one without. cmake/EngineFpCheck.cmake
#                                      fails the configure step when a fast-math flag is set anywhere.
#   ENGINE_HAVE_ASAN, ENGINE_HAVE_TSAN whether the sanitizer flavors exist with this compiler
#   ENGINE_ASAN_COMPILE_OPTIONS, ENGINE_ASAN_LINK_OPTIONS, ENGINE_TSAN_COMPILE_OPTIONS, ENGINE_TSAN_LINK_OPTIONS
# and the build-wide options every target gets on Windows.

set(ENGINE_MSVC_LIKE FALSE)
if(MSVC)  # true for cl.exe and for clang-cl
  set(ENGINE_MSVC_LIKE TRUE)
endif()

# engine_asan_runtime(<target>): with MSVC, copies the AddressSanitizer runtime DLLs beside an ASan executable when it links, so it starts
# without a developer PATH (Catch2 runs every test executable at build time to list its tests). Call it before engine_discover_tests, so
# the copy runs first. Does nothing elsewhere (GCC and Clang link the runtime statically).
function(engine_asan_runtime target)
  if(ENGINE_MSVC_LIKE AND ENGINE_HAVE_ASAN)
    foreach(dll clang_rt.asan_dynamic-x86_64.dll clang_rt.asan_dbg_dynamic-x86_64.dll)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ENGINE_MSVC_BIN}/${dll}" "$<TARGET_FILE_DIR:${target}>")
    endforeach()
  endif()
endfunction()

if(NOT ENGINE_MSVC_LIKE)
  # GCC and Clang (Linux), and Apple clang (macOS, Phase 21: the same flags measured clean on Day 283). The sanitizer flags are unchanged
  # from Phase 7: -fno-sanitize=vptr because the engine libraries are built -fno-rtti while the Catch2 executables are not (see
  # engine/foundation/CMakeLists.txt). Apple clang has no LeakSanitizer: the asan flavor there is ASan and UBSan only, and a test must not
  # ask for detect_leaks=1, which aborts the process on macOS (KNOWN-ISSUES.md).
  set(ENGINE_PRODUCTION_COMPILE_OPTIONS -fno-exceptions -fno-rtti -fno-stack-protector)
  # GCC contracts a * b + c to a fused multiply-add by default (-ffp-contract=fast) and Clang does with `on` (Day 373 spike 2: the two give different
  # hashes, and so do builds with and without -march=native). Off in both, for C and C++ (and Objective-C) alike.
  set(ENGINE_FP_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffp-contract=off>" "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-fno-fast-math>")
  set(ENGINE_EXCEPTIONS_COMPILE_OPTIONS "")  # GCC and Clang have exceptions on unless told otherwise
  set(ENGINE_WINDOWS_COMPILE_OPTIONS "")
  set(ENGINE_WINDOWS_DEFINITIONS "")
  # Warnings are errors (CLAUDE.md, "Warnings are errors"): every target of ours, tests and tools included, never the vendored libraries.
  # OBJC and OBJCXX are enabled only on macOS; on Linux the generator expression never matches them.
  set(ENGINE_WARNING_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wall>" "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wextra>"
      "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Werror>")
  # macOS: Objective-C++ files use ARC (Phase 21 decision 3), and so do the Objective-C test hosts (Phase 23 Day 355). __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES is already 0 in the macOS
  # 15.5 SDK; pinning it keeps a later SDK from defining check(), verify() and require() as macros in every file that includes a framework
  # (tests/macos: macos_header_planted shows what they break).
  set(ENGINE_MACOS_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>")
  set(ENGINE_MACOS_DEFINITIONS __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0)
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
# cl: /fp:precise (never /fp:fast; contraction is off in precise mode); clang-cl: the same plus clang's own contraction switch through /clang:.
set(ENGINE_FP_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:/fp:precise>")
if(ENGINE_CLANG_CL)
  list(APPEND ENGINE_FP_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:/clang:-ffp-contract=off>")
endif()

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

# The subsystem is chosen in source, never per target in a build file (Phase 13 Day 158, platform/entry_point.h). CMake passes
# /subsystem:console to every executable, which tests and tools keep; ENGINE_WINDOWED_PROGRAM's expansion carries a
# `#pragma comment(linker, "/SUBSYSTEM:WINDOWS")` beside its wWinMain, and that directive wins over the command line in link.exe and in
# lld-link (measured: the programs' PE headers say 2; tests/windows checks it). Clearing CMake's flag instead, so the linkers would infer
# the subsystem from the entry point, fails for the tests: their `main` is in Catch2's library, and with no /SUBSYSTEM the linker stops
# with LNK1561 (no entry point) before it searches libraries.

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

# /W4 /WX for every target of ours (CLAUDE.md, "Warnings are errors"; the root CMakeLists.txt adds them after third_party/). C4324 ("structure
# was padded due to alignment specifier") is off: the engine writes alignas only where the padding is the point (cache-line separation in jobs/deque.h).
set(ENGINE_WARNING_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:/W4>" "$<$<COMPILE_LANGUAGE:C,CXX>:/WX>" "$<$<COMPILE_LANGUAGE:C,CXX>:/wd4324>")
set(ENGINE_PRODUCTION_COMPILE_OPTIONS /EHs-c- /GR- /GS- /D_HAS_EXCEPTIONS=0)

if(ENGINE_CLANG_CL)
  # Native __int128 division needs compiler-rt's builtins (the toolchain file found the library).
  link_libraries("${ENGINE_CLANG_BUILTINS}")
  # AddressSanitizer with clang-cl needs its own runtime libraries named on the lld-link line; not wired up yet (the clang-cl
  # configuration is a compile-and-unit-test check, Phase 13 rules).
  set(ENGINE_HAVE_ASAN FALSE)
else()
  set(ENGINE_HAVE_ASAN TRUE)
  # _DISABLE_STL_ANNOTATION: the MSVC standard library records whether its containers are annotated for ASan in every object, and the
  # linker refuses to mix the two (LNK2038). Catch2 is not built with ASan, so the ASan objects turn the annotations off to match; the
  # engine uses no standard container, so nothing it checks is lost (found on Day 149).
  set(ENGINE_ASAN_COMPILE_OPTIONS /fsanitize=address /D_DISABLE_STL_ANNOTATION)
  set(ENGINE_ASAN_LINK_OPTIONS "")  # cl records the runtime libraries in the objects (/INFERASANLIBS is link.exe's default)
endif()
set(ENGINE_HAVE_TSAN FALSE)  # MSVC has no ThreadSanitizer; the tsan flavor runs on Linux only
