# Syntax-checks the neutral debugging headers and the platform-free facade sources for another operating system, with clang's
# -fsyntax-only and small stand-ins for the standard headers (tests/debug/cross_syntax/shim), so a Linux-only dependency that
# slipped into the neutral code shows up long before the Windows and macOS ports (Phases 12 and 13). Run as:
#   cmake -DTRIPLE=<triple> -DROOT=<source tree> -P check_cross_syntax.cmake
# Skips (message "clang++ not found") where there is no clang++.
cmake_minimum_required(VERSION 3.20)
if(NOT TRIPLE OR NOT ROOT)
  message(FATAL_ERROR "usage: -DTRIPLE=<triple> -DROOT=<tree>")
endif()
find_program(CLANGXX clang++)
if(NOT CLANGXX)
  message(STATUS "clang++ not found: the cross-target syntax check needs it")
  return()
endif()
set(shim "${ROOT}/tests/debug/cross_syntax/shim")
set(sources
  "${ROOT}/tests/debug/cross_syntax/neutral_headers.cpp"
  "${ROOT}/engine/foundation/debug/crash_report.cpp"
  "${ROOT}/engine/foundation/debug/thread_info.cpp"
  "${ROOT}/engine/foundation/debug/crash_reason.cpp"
  "${ROOT}/engine/foundation/debug/crash_state.cpp"
  "${ROOT}/engine/foundation/debug/crash_context.cpp"
  "${ROOT}/engine/foundation/debug/program_setup.cpp"
  "${ROOT}/engine/foundation/debug/death_test.cpp")
set(failed 0)
foreach(source IN LISTS sources)
  execute_process(
    COMMAND ${CLANGXX} --target=${TRIPLE} -std=c++20 -fsyntax-only -ffreestanding -fno-exceptions -fno-rtti -nostdinc++ -nostdlibinc
            -isystem ${shim} -I${ROOT}/engine/foundation ${source}
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0)
    message(SEND_ERROR "${TRIPLE}: ${source} does not compile:\n${out}${err}")
    math(EXPR failed "${failed}+1")
  endif()
endforeach()
if(failed GREATER 0)
  message(FATAL_ERROR "cross-target syntax check for ${TRIPLE}: ${failed} file(s) failed")
endif()
list(LENGTH sources count)
message(STATUS "${count} file(s) compile for ${TRIPLE}")
