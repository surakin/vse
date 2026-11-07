# Windows x64 with MSVC (cl.exe, link.exe), found without a Developer Command Prompt. See windows-common.cmake for the search
# and the overrides; the preset `windows-msvc` (CMakePresets.json) uses this file.
include("${CMAKE_CURRENT_LIST_DIR}/windows-common.cmake")

set(CMAKE_C_COMPILER "${ENGINE_MSVC_BIN}/cl.exe")
set(CMAKE_CXX_COMPILER "${ENGINE_MSVC_BIN}/cl.exe")
set(CMAKE_LINKER "${ENGINE_MSVC_BIN}/link.exe")
set(CMAKE_AR "${ENGINE_MSVC_BIN}/lib.exe")
_engine_windows_flags("/external:I")
string(APPEND CMAKE_C_FLAGS_INIT " /external:W0")
string(APPEND CMAKE_CXX_FLAGS_INIT " /external:W0")
