# Syntax-checks the backend-neutral file-layer headers for another operating system with clang's -fsyntax-only, using the small standard
# header stand-ins of tests/debug/cross_syntax/shim, so a POSIX-only or Linux-only name that leaks into the neutral interface shows up long
# before the Windows and macOS ports (Phases 13 and 22). Run as: cmake -DTRIPLE=<triple> -DROOT=<source tree> -P check_io_cross_syntax.cmake
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
execute_process(
  COMMAND ${CLANGXX} --target=${TRIPLE} -std=c++20 -fsyntax-only -ffreestanding -fno-exceptions -fno-rtti -nostdinc++ -nostdlibinc
          -isystem ${shim} -I${ROOT}/engine/foundation -I${ROOT}/engine ${ROOT}/tests/io/cross_syntax/neutral_io_headers.cpp
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "${TRIPLE}: the neutral file-layer headers do not compile:\n${out}${err}")
endif()
message(STATUS "the neutral file-layer headers compile for ${TRIPLE}")
