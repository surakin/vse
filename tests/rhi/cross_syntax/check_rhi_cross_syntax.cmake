# Syntax-checks the backend-neutral RHI headers for another operating system with clang's -fsyntax-only, using the small standard
# header stand-ins of tests/debug/cross_syntax/shim, so a Vulkan-only or Linux-only name that leaks into the neutral interface
# (adapter enumeration, the device lifecycle, the swapchain, the device host) shows up long before the DX12 and Metal ports
# (Phases 11 and 12). Run as: cmake -DTRIPLE=<triple> -DROOT=<source tree> -P check_rhi_cross_syntax.cmake
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
          -isystem ${shim} -I${ROOT}/engine/foundation -I${ROOT}/engine ${ROOT}/tests/rhi/cross_syntax/neutral_rhi_headers.cpp
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "${TRIPLE}: the neutral RHI headers do not compile:\n${out}${err}")
endif()
message(STATUS "the neutral RHI headers compile for ${TRIPLE}")
