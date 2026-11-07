# Finds Visual Studio's MSVC tools, the Windows SDK and (for clang-cl) LLVM without a Developer Command Prompt (Phase 13 Day 148).
#
# Included by windows-msvc.cmake and windows-clang-cl.cmake. Configuring from a plain PowerShell or Git Bash must work: nothing
# here reads INCLUDE, LIB, LIBPATH, VCINSTALLDIR or WindowsSdkDir. The include and library directories are passed to the compiler
# and linker as flags, so the build (Ninja run from any shell) does not depend on the environment either.
#
# Overrides for unusual machines (cache variables or environment variables of the same name):
#   ENGINE_VS_INSTALL_DIR       a Visual Studio installation (Build Tools, Community, Professional or Enterprise)
#   ENGINE_WINDOWS_SDK_DIR      the Windows Kits 10 root (the directory holding Include/ and Lib/)
#   ENGINE_WINDOWS_SDK_VERSION  a version directory under it, such as 10.0.26100.0
#   ENGINE_LLVM_DIR             an LLVM installation (the directory holding bin/clang-cl.exe)
#
# A toolchain file is read again for every try_compile, so everything is computed from the inputs each time instead of cached.

if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "${CMAKE_CURRENT_LIST_FILE}: the Windows toolchain files run on a Windows host")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Values set here must reach the try_compile projects CMake creates while checking the compiler.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  ENGINE_VS_INSTALL_DIR ENGINE_WINDOWS_SDK_DIR ENGINE_WINDOWS_SDK_VERSION ENGINE_LLVM_DIR)

function(_engine_override name out)
  if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
    set(${out} "${${name}}" PARENT_SCOPE)
  elseif(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{${name}}" value)
    set(${out} "${value}" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# --- Visual Studio and MSVC ---------------------------------------------------------------------------------------------
_engine_override(ENGINE_VS_INSTALL_DIR ENGINE_VS_DIR)
set(_engine_vs_overridden FALSE)
if(ENGINE_VS_DIR)
  set(_engine_vs_overridden TRUE)
endif()
_engine_override(ENGINE_WINDOWS_SDK_DIR _engine_sdk_given)
_engine_override(ENGINE_WINDOWS_SDK_VERSION _engine_sdk_version_given)
if(NOT ENGINE_VS_DIR)
  set(_pf86 "$ENV{ProgramFiles\(x86\)}")
  if(NOT _pf86)
    set(_pf86 "C:/Program Files (x86)")
  endif()
  file(TO_CMAKE_PATH "${_pf86}/Microsoft Visual Studio/Installer/vswhere.exe" _vswhere)
  if(NOT EXISTS "${_vswhere}")
    message(FATAL_ERROR "No Visual Studio found: ${_vswhere} does not exist. Install the Visual Studio Build Tools (BUILD.md) or set ENGINE_VS_INSTALL_DIR.")
  endif()
  execute_process(COMMAND "${_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                          -property installationPath -utf8
                  OUTPUT_VARIABLE ENGINE_VS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0 OR ENGINE_VS_DIR STREQUAL "")
    message(FATAL_ERROR "vswhere (${_vswhere}) found no Visual Studio with the C++ x64 tools (component Microsoft.VisualStudio.Component.VC.Tools.x86.x64). "
                        "Install the C++ build tools workload (BUILD.md) or set ENGINE_VS_INSTALL_DIR.")
  endif()
  file(TO_CMAKE_PATH "${ENGINE_VS_DIR}" ENGINE_VS_DIR)
endif()
set(_ver_file "${ENGINE_VS_DIR}/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt")
if(NOT EXISTS "${_ver_file}")
  message(FATAL_ERROR "Visual Studio at ${ENGINE_VS_DIR} has no ${_ver_file}: it lacks the C++ tools.")
endif()
file(STRINGS "${_ver_file}" ENGINE_MSVC_VERSION LIMIT_COUNT 1)
string(STRIP "${ENGINE_MSVC_VERSION}" ENGINE_MSVC_VERSION)
set(ENGINE_MSVC_DIR "${ENGINE_VS_DIR}/VC/Tools/MSVC/${ENGINE_MSVC_VERSION}")
set(ENGINE_MSVC_BIN "${ENGINE_MSVC_DIR}/bin/Hostx64/x64")
foreach(_tool cl.exe link.exe lib.exe)
  if(NOT EXISTS "${ENGINE_MSVC_BIN}/${_tool}")
    message(FATAL_ERROR "MSVC ${ENGINE_MSVC_VERSION}: ${ENGINE_MSVC_BIN}/${_tool} not found.")
  endif()
endforeach()

# --- Windows SDK --------------------------------------------------------------------------------------------------------
_engine_override(ENGINE_WINDOWS_SDK_DIR ENGINE_SDK_DIR)
if(NOT ENGINE_SDK_DIR)
  get_filename_component(ENGINE_SDK_DIR "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
  if(ENGINE_SDK_DIR STREQUAL "" OR ENGINE_SDK_DIR MATCHES "/registry$" OR NOT EXISTS "${ENGINE_SDK_DIR}/Include")
    message(FATAL_ERROR "No Windows SDK found: the registry value HKLM\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots\\KitsRoot10 "
                        "is missing or names no Include directory. Install a Windows SDK (BUILD.md) or set ENGINE_WINDOWS_SDK_DIR.")
  endif()
endif()
string(REGEX REPLACE "/$" "" ENGINE_SDK_DIR "${ENGINE_SDK_DIR}")
_engine_override(ENGINE_WINDOWS_SDK_VERSION ENGINE_SDK_VERSION)
if(NOT ENGINE_SDK_VERSION)
  file(GLOB _sdk_versions LIST_DIRECTORIES true RELATIVE "${ENGINE_SDK_DIR}/Include" "${ENGINE_SDK_DIR}/Include/10.*")
  set(ENGINE_SDK_VERSION "")
  foreach(_v IN LISTS _sdk_versions)
    if(EXISTS "${ENGINE_SDK_DIR}/Include/${_v}/um/windows.h" AND EXISTS "${ENGINE_SDK_DIR}/Lib/${_v}/um/x64/kernel32.lib"
       AND (ENGINE_SDK_VERSION STREQUAL "" OR _v VERSION_GREATER ENGINE_SDK_VERSION))
      set(ENGINE_SDK_VERSION "${_v}")
    endif()
  endforeach()
  if(ENGINE_SDK_VERSION STREQUAL "")
    message(FATAL_ERROR "The Windows SDK at ${ENGINE_SDK_DIR} has no version with Include/<v>/um/windows.h and Lib/<v>/um/x64/kernel32.lib.")
  endif()
endif()
set(ENGINE_SDK_BIN "${ENGINE_SDK_DIR}/bin/${ENGINE_SDK_VERSION}/x64")
foreach(_tool rc.exe mt.exe)
  if(NOT EXISTS "${ENGINE_SDK_BIN}/${_tool}")
    message(FATAL_ERROR "Windows SDK ${ENGINE_SDK_VERSION}: ${ENGINE_SDK_BIN}/${_tool} not found.")
  endif()
endforeach()

# --- A Developer Command Prompt must not mix in a second toolset --------------------------------------------------------
# cl and link also read INCLUDE and LIB from the environment, after the directories passed on the command line, so a prompt for another
# MSVC or another Windows SDK would add a second set of headers and libraries behind ours. The configure stops unless the found tools are
# the prompt's; an explicit ENGINE_VS_INSTALL_DIR / ENGINE_WINDOWS_SDK_* wins with a warning (the developer chose the mix).
function(_engine_check_prompt what env_value found overridden)
  file(TO_CMAKE_PATH "${env_value}" _env)
  string(REGEX REPLACE "/$" "" _env "${_env}")
  string(TOLOWER "${_env}" _env_l)
  string(TOLOWER "${found}" _found_l)
  if(NOT _env_l STREQUAL _found_l)
    if(overridden)
      message(WARNING "This shell's Developer Command Prompt names ${what} ${_env}, but the override selects ${found}; the prompt's "
                      "INCLUDE and LIB stay behind the override's directories.")
    else()
      message(FATAL_ERROR "This shell's Developer Command Prompt names ${what} ${_env}, but the build found ${found}. Configure from a "
                          "plain shell, or set ENGINE_VS_INSTALL_DIR / ENGINE_WINDOWS_SDK_DIR / ENGINE_WINDOWS_SDK_VERSION to what you mean.")
    endif()
  endif()
endfunction()
if(NOT "$ENV{VCToolsInstallDir}" STREQUAL "")
  _engine_check_prompt("MSVC" "$ENV{VCToolsInstallDir}" "${ENGINE_MSVC_DIR}" ${_engine_vs_overridden})
endif()
set(_engine_sdk_overridden FALSE)
if(_engine_sdk_given OR _engine_sdk_version_given)
  set(_engine_sdk_overridden TRUE)
endif()
if(NOT "$ENV{WindowsSdkDir}" STREQUAL "")
  _engine_check_prompt("the Windows SDK at" "$ENV{WindowsSdkDir}" "${ENGINE_SDK_DIR}" ${_engine_sdk_overridden})
endif()
if(NOT "$ENV{WindowsSDKVersion}" STREQUAL "")
  _engine_check_prompt("Windows SDK version" "$ENV{WindowsSDKVersion}" "${ENGINE_SDK_VERSION}" ${_engine_sdk_overridden})
endif()

# --- Include and library directories, as flags ---------------------------------------------------------------------------
set(ENGINE_WINDOWS_INCLUDE_DIRS
  "${ENGINE_MSVC_DIR}/include"
  "${ENGINE_SDK_DIR}/Include/${ENGINE_SDK_VERSION}/ucrt"
  "${ENGINE_SDK_DIR}/Include/${ENGINE_SDK_VERSION}/um"
  "${ENGINE_SDK_DIR}/Include/${ENGINE_SDK_VERSION}/shared"
  "${ENGINE_SDK_DIR}/Include/${ENGINE_SDK_VERSION}/winrt")
set(ENGINE_WINDOWS_LIB_DIRS
  "${ENGINE_MSVC_DIR}/lib/x64"
  "${ENGINE_SDK_DIR}/Lib/${ENGINE_SDK_VERSION}/ucrt/x64"
  "${ENGINE_SDK_DIR}/Lib/${ENGINE_SDK_VERSION}/um/x64")

# _engine_windows_flags(<system-include-flag>): the compile and link flags every target gets. The system-include flag is
# /external:I for cl and /imsvc for clang-cl, so the compiler treats these headers as system headers (no warnings from them).
macro(_engine_windows_flags include_flag)
  set(_inc "")
  foreach(_d IN LISTS ENGINE_WINDOWS_INCLUDE_DIRS)
    string(APPEND _inc " ${include_flag}\"${_d}\"")
  endforeach()
  set(_lib "")
  foreach(_d IN LISTS ENGINE_WINDOWS_LIB_DIRS)
    string(APPEND _lib " /LIBPATH:\"${_d}\"")
  endforeach()
  set(CMAKE_C_FLAGS_INIT "${_inc}")
  set(CMAKE_CXX_FLAGS_INIT "${_inc}")
  set(CMAKE_RC_FLAGS_INIT "")
  foreach(_d IN LISTS ENGINE_WINDOWS_INCLUDE_DIRS)
    string(APPEND CMAKE_RC_FLAGS_INIT " /I\"${_d}\"")
  endforeach()
  set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib}")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib}")
  set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib}")
endmacro()

set(CMAKE_RC_COMPILER "${ENGINE_SDK_BIN}/rc.exe")
set(CMAKE_MT "${ENGINE_SDK_BIN}/mt.exe")

# --- Vulkan SDK (headers: os/gpu_driver_probe.h and the Vulkan backend include <vulkan/vulkan_core.h>) ------------------------
# ENGINE_VULKAN_SDK, then VULKAN_SDK (set by the SDK installer, but only in shells started afterwards), then the newest version
# under C:/VulkanSDK. Not an error here: the targets that need the headers report it (engine/foundation/CMakeLists.txt).
_engine_override(ENGINE_VULKAN_SDK ENGINE_VULKAN_SDK_DIR)
if(NOT ENGINE_VULKAN_SDK_DIR)
  _engine_override(VULKAN_SDK ENGINE_VULKAN_SDK_DIR)
endif()
if(NOT ENGINE_VULKAN_SDK_DIR)
  file(GLOB _vk_versions LIST_DIRECTORIES true "C:/VulkanSDK/*")
  set(_vk_best "")
  foreach(_v IN LISTS _vk_versions)
    get_filename_component(_vn "${_v}" NAME)
    if(EXISTS "${_v}/Include/vulkan/vulkan_core.h" AND (_vk_best STREQUAL "" OR _vn VERSION_GREATER _vk_best_name))
      set(_vk_best "${_v}")
      set(_vk_best_name "${_vn}")
    endif()
  endforeach()
  set(ENGINE_VULKAN_SDK_DIR "${_vk_best}")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ENGINE_VULKAN_SDK)

# Directories a test needs on PATH to start (the AddressSanitizer runtime DLL is beside cl.exe), recorded for the test CMake files.
set(ENGINE_WINDOWS_RUNTIME_DIRS "${ENGINE_MSVC_BIN}")
