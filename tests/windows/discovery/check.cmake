# windows_toolchain_discovery_* (Phase 13 Day 148): configures, builds and runs tests/windows/discovery with one toolchain file from an
# environment stripped of everything a Developer Command Prompt sets, proving the toolchain file finds the compiler, the Windows SDK
# and (for clang-cl) LLVM by itself.
#   cmake -DTOOLCHAIN=<file> -DWORK=<scratch dir> -DPROJECT_SOURCE=<tests/windows/discovery> -DNINJA=<ninja> -P check.cmake
foreach(_v TOOLCHAIN WORK PROJECT_SOURCE NINJA)
  if(NOT ${_v})
    message(FATAL_ERROR "check.cmake needs -D${_v}=")
  endif()
endforeach()

# PATH without the Visual Studio, Windows Kits and LLVM directories, so nothing can be found by accident.
set(_path "")
set(_inherited "$ENV{PATH}")  # (`IN LISTS ENV{PATH}` would read a variable named ENV{PATH}, not the environment)
foreach(_dir IN LISTS _inherited)
  if(NOT _dir MATCHES "Microsoft Visual Studio|Windows Kits|LLVM")
    list(APPEND _path "${_dir}")
  endif()
endforeach()
list(LENGTH _path _kept)
if(_kept EQUAL 0)
  message(FATAL_ERROR "the stripped PATH is empty: nothing was inherited to strip")
endif()
string(REPLACE ";" "\\;" _path "${_path}")
set(_env ${CMAKE_COMMAND} -E env --unset=INCLUDE --unset=LIB --unset=LIBPATH --unset=VCINSTALLDIR --unset=VCToolsInstallDir
         --unset=VSINSTALLDIR --unset=WindowsSdkDir --unset=WindowsSDKVersion --unset=UCRTVersion --unset=DevEnvDir "PATH=${_path}")

file(REMOVE_RECURSE "${WORK}")
execute_process(COMMAND ${_env} ${CMAKE_COMMAND} -S "${PROJECT_SOURCE}" -B "${WORK}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA}"
                        "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" -DCMAKE_BUILD_TYPE=Debug
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "configure with ${TOOLCHAIN} failed (${rc}):\n${out}\n${err}")
endif()
execute_process(COMMAND ${_env} ${CMAKE_COMMAND} --build "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "build with ${TOOLCHAIN} failed (${rc}):\n${out}\n${err}")
endif()
execute_process(COMMAND ${_env} "${WORK}/hello.exe" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "hello.exe built with ${TOOLCHAIN} failed (${rc}): ${out}${err}")
endif()
string(STRIP "${out}" out)
message(STATUS "discovery ok: ${out}")
