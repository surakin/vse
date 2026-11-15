# The iOS compile check of the C API's header (Phase 23 Day 355, ctest engine_c_header_ios): iOS links the engine statically (no host may load a library of
# its own there), so the header a host includes must compile for an iOS target. The Mac has the Command Line Tools and no iOS SDK, so the check is
# freestanding: engine_c.h needs only <stddef.h> and <stdint.h>, which clang carries itself. engine_loader.h (dlopen) is not for iOS and is not checked.
#   cmake -DCOMPILER=<clang> -DINCLUDE=<repo>/include -DSOURCE=<ios_header_tu.c> -P ios_header_check.cmake
foreach(language c c++)
  if(language STREQUAL "c")
    set(standard -std=c11)
  else()
    set(standard -std=c++20)
  endif()
  execute_process(COMMAND ${COMPILER} -target arm64-apple-ios15.0 -ffreestanding -fsyntax-only -x ${language} ${standard} -pedantic -Wall -Wextra -Werror
                          -I ${INCLUDE} ${SOURCE}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "engine_c.h does not compile as ${language} for arm64-apple-ios15.0:\n${out}${err}")
  endif()
  message(STATUS "engine_c.h compiles as ${language} for arm64-apple-ios15.0")
endforeach()
