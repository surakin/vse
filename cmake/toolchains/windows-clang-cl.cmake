# Windows x64 with clang-cl and lld-link, against MSVC's headers and libraries, found without a Developer Command Prompt.
# See windows-common.cmake for the MSVC and SDK search and the overrides; the preset `windows-clang-cl` uses this file.
#
# clang-cl is looked for in Visual Studio's own LLVM (VC/Tools/Llvm/x64/bin), then the LLVM installer's registry key
# (HKLM\SOFTWARE\LLVM\LLVM), then %ProgramFiles%/LLVM. The winget package LLVM.LLVM writes no registry key and does not put
# itself on PATH, so the last place is the usual one.
include("${CMAKE_CURRENT_LIST_DIR}/windows-common.cmake")

_engine_override(ENGINE_LLVM_DIR ENGINE_LLVM)
set(_llvm_searched "")
if(NOT ENGINE_LLVM)
  set(_candidates "${ENGINE_VS_DIR}/VC/Tools/Llvm/x64")
  get_filename_component(_reg "[HKEY_LOCAL_MACHINE\\SOFTWARE\\LLVM\\LLVM]" ABSOLUTE)
  if(NOT _reg MATCHES "/registry$" AND NOT _reg STREQUAL "")
    list(APPEND _candidates "${_reg}")
  endif()
  set(_pf "$ENV{ProgramFiles}")
  if(NOT _pf)
    set(_pf "C:/Program Files")
  endif()
  file(TO_CMAKE_PATH "${_pf}/LLVM" _pf_llvm)
  list(APPEND _candidates "${_pf_llvm}")
  foreach(_c IN LISTS _candidates)
    list(APPEND _llvm_searched "${_c}/bin/clang-cl.exe")
    if(EXISTS "${_c}/bin/clang-cl.exe" AND EXISTS "${_c}/bin/lld-link.exe")
      set(ENGINE_LLVM "${_c}")
      break()
    endif()
  endforeach()
endif()
if(NOT ENGINE_LLVM OR NOT EXISTS "${ENGINE_LLVM}/bin/clang-cl.exe")
  string(REPLACE ";" ", " _llvm_searched "${_llvm_searched}")
  message(FATAL_ERROR "No clang-cl found (looked for ${_llvm_searched}). Install LLVM (BUILD.md: winget install --id LLVM.LLVM -e) or set ENGINE_LLVM_DIR.")
endif()

set(CMAKE_C_COMPILER "${ENGINE_LLVM}/bin/clang-cl.exe")
set(CMAKE_CXX_COMPILER "${ENGINE_LLVM}/bin/clang-cl.exe")
set(CMAKE_LINKER "${ENGINE_LLVM}/bin/lld-link.exe")
set(CMAKE_AR "${ENGINE_LLVM}/bin/llvm-lib.exe")
_engine_windows_flags("/imsvc")

# Native unsigned __int128 division and remainder call compiler-rt (__udivti3, __umodti3), which clang-cl does not link against the
# MSVC runtime (found on Day 147). The builtins library ships with the LLVM install, under lib/clang/<major>/lib/windows/.
# Several lib/clang/<major> directories can exist after an upgrade: take the highest version, compared as numbers (so 23 beats 9).
file(GLOB _builtins "${ENGINE_LLVM}/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
set(ENGINE_CLANG_BUILTINS "")
if(_builtins)
  list(SORT _builtins COMPARE NATURAL ORDER DESCENDING)
  list(GET _builtins 0 ENGINE_CLANG_BUILTINS)
endif()
if(NOT ENGINE_CLANG_BUILTINS OR NOT EXISTS "${ENGINE_CLANG_BUILTINS}")
  message(FATAL_ERROR "LLVM at ${ENGINE_LLVM} has no lib/clang/<version>/lib/windows/clang_rt.builtins-x86_64.lib (needed for __int128 division).")
endif()
# Linked into every program by cmake/EngineCompilerOptions.cmake (setting CMAKE_CXX_STANDARD_LIBRARIES_INIT here would replace
# CMake's default Windows libraries).
