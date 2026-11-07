# Platform-neutral gate (Phase 7 Day 92). Run as:
#   cmake -DROOT=<tree> -DBOUNDARY=<file> -P check.cmake
#
# Rule 1: nothing under engine/foundation/debug/ and nothing under tests/debug/ names a platform type, constant or header:
#   ucontext_t, siginfo_t, pid_t, HANDLE, SIGxxx, /proc, fork(, waitpid(, sigaction(, and the headers <unistd.h>,
#   <windows.h>, <signal.h>, <csignal>, <sys/...>, <poll.h>, <spawn.h>, <fcntl.h>, <dirent.h>. Those files speak the
#   neutral API (CrashReason, FaultKind, ChildResult), so the Windows and macOS ports build the same sources. Files whose
#   name contains `_linux` (before a `_` or `.`) hold the Linux-only details and are exempt, and so is the per-platform
#   code under engine/foundation/os/<platform>/debug/ (it is not under these directories). Comments are ignored.
# Rule 3: ENGINE_VERIFY, ENGINE_CHECK_SLOW and ENGINE_UNREACHABLE are not used in headers. Whether they check is decided
#   per translation unit (ENGINE_DEBUG_CHECKS / ENGINE_SLOW_CHECKS), so an inline function in a header that used them
#   would have different bodies in different translation units (an ODR violation). Headers use ENGINE_ASSERT or
#   ENGINE_ENSURE; the definitions in engine/foundation/log/assert.h are the one exception.
# Rule 2: the third-party library headers are included only by the one implementation file named for each library in the
#   BOUNDARY table (`header|path|reason` lines): `backtrace.h` (libbacktrace) and `sentry.h` (sentry-native). Anywhere
#   under engine/, game/, tools/ and tests/ except fixtures. Until Days 94 and 95 add the files, nobody may include them.
# Rule 4 (Phase 13 Day 150): the Windows SDK headers (windows.h, winternl.h, wtsapi32.h, shlobj.h, dbghelp.h and the rest of the list
#   below, d3d12*.h, dxgi*.h) appear only under the Windows directories (engine/foundation/{os,io,settings}/windows/,
#   engine/platform/windows/, engine/rhi/dx12/, tests/windows/), and only through the engine's wrappers: <windows.h> is included by
#   engine/foundation/os/windows/win32.h alone (it keeps the SDK's macros from leaking), the D3D12 and DXGI headers by
#   engine/rhi/dx12/d3d12_api.h alone (Day 164), and any other SDK header only after win32.h in the same file. tests/windows/discovery/
#   is exempt: its programs probe the toolchain and are built outside the engine. Anywhere under engine/, game/, tools/ and tests/
#   except fixtures; comments are ignored.
cmake_minimum_required(VERSION 3.20)

if(NOT ROOT OR NOT BOUNDARY)
  message(FATAL_ERROR "usage: -DROOT=<tree> -DBOUNDARY=<file>")
endif()

# Reports one problem and counts it. Printed with message(NOTICE), on one line: message(SEND_ERROR) wraps its text at the console width,
# so the output (and the self-tests that match it) would depend on the length of the checkout's path (found on Windows, Phase 13 Day 150).
# The script fails at the end when any problem was reported. A function, not a macro: a macro's arguments are substituted into its
# body as text, so a quoted source line with `\w` or `${x}` in it would be evaluated again. Call it from the top level only (it counts
# in its caller's scope).
set(errors 0)
function(gate_problem text)
  message(NOTICE "error: ${text}")
  math(EXPR count "${errors}+1")
  set(errors ${count} PARENT_SCOPE)
endfunction()

get_filename_component(ROOT "${ROOT}" ABSOLUTE)
get_filename_component(BOUNDARY "${BOUNDARY}" ABSOLUTE)

function(read_lines path out)
  file(READ "${path}" content)
  string(REPLACE ";" "<SEMI>" content "${content}")
  string(REPLACE "[" "<LB>" content "${content}")
  string(REPLACE "]" "<RB>" content "${content}")
  string(REPLACE "\n" ";" content "${content}")
  set(${out} "${content}" PARENT_SCOPE)
endfunction()

# The library boundary table: allowed[<header>] lists the files that may include it.
set(allowed_backtrace "")
set(allowed_sentry "")
read_lines("${BOUNDARY}" boundary_lines)
foreach(line IN LISTS boundary_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  string(REPLACE "|" ";" parts "${line}")
  list(LENGTH parts nparts)
  if(nparts LESS 3)
    gate_problem("boundary entry needs header|path|reason: ${line}")
    continue()
  endif()
  list(GET parts 0 header)
  list(GET parts 1 path)
  if(NOT EXISTS "${ROOT}/${path}" AND NOT ROOT MATCHES "fixtures")
    gate_problem("stale boundary entry (file missing): ${path}")
  endif()
  if(header STREQUAL "backtrace.h")
    list(APPEND allowed_backtrace "${path}")
  elseif(header STREQUAL "sentry.h")
    list(APPEND allowed_sentry "${path}")
  else()
    gate_problem("boundary entry names an unknown header: ${header}")
  endif()
endforeach()

# Platform names (applied to code with strings and comments blanked).
set(ident_re "(^|[^A-Za-z0-9_])(ucontext_t|siginfo_t|pid_t|HANDLE|SIG[A-Z0-9]+|(fork|waitpid|sigaction)[ \t]*\\()([^A-Za-z0-9_]|$)")
set(platform_include_re "^[ \t]*#[ \t]*include[ \t]*[<\"](unistd\\.h|windows\\.h|signal\\.h|csignal|sys/[A-Za-z0-9_/]+\\.h|poll\\.h|spawn\\.h|fcntl\\.h|dirent\\.h)[>\"]")
set(proc_re "/proc")
set(lib_include_re "^[ \t]*#[ \t]*include[ \t]*[<\"]([A-Za-z0-9_/]*/)?(backtrace|sentry)\\.h[>\"]")

# Strips comments from the line in `l`; `in_block` tracks /* ... */ across lines. Sets `code` (strings kept) in the caller.
# It reads the caller's variable instead of taking an argument: macro arguments are parsed again, which mangles backslashes.
macro(strip_comments)
  set(code "${l}")
  if(in_block)
    string(FIND "${code}" "*/" close)
    if(close LESS 0)
      set(code "")
    else()
      math(EXPR after "${close}+2")
      string(SUBSTRING "${code}" ${after} -1 code)
      set(in_block FALSE)
    endif()
  endif()
  string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " code "${code}")
  string(FIND "${code}" "//" slashes)
  if(NOT slashes LESS 0)
    string(SUBSTRING "${code}" 0 ${slashes} code)
  endif()
  string(FIND "${code}" "/*" opens)
  if(NOT opens LESS 0)
    string(SUBSTRING "${code}" 0 ${opens} code)
    set(in_block TRUE)
  endif()
endmacro()

# --- Rule 1 ------------------------------------------------------------------------------------------------------------
set(neutral_files "")
foreach(dir engine/foundation/debug tests/debug)
  file(GLOB_RECURSE found RELATIVE "${ROOT}" "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.cpp" "${ROOT}/${dir}/*.cc")
  list(APPEND neutral_files ${found})
endforeach()

foreach(f IN LISTS neutral_files)
  get_filename_component(base "${f}" NAME)
  if(base MATCHES "_linux[_.]")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "${platform_include_re}")
      gate_problem("${f}:${n}: platform header in a neutral file: ${l}")
    endif()
    if(code MATCHES "${proc_re}")
      gate_problem("${f}:${n}: /proc path in a neutral file: ${l}")
    endif()
    set(blanked "${code}")
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"\"" blanked "${blanked}")
    if(blanked MATCHES "${ident_re}")
      gate_problem("${f}:${n}: platform type or constant in a neutral file: ${l}")
    endif()
  endforeach()
endforeach()

# --- Rule 2 ------------------------------------------------------------------------------------------------------------
set(all_files "")
foreach(dir engine game tools tests)
  file(GLOB_RECURSE found RELATIVE "${ROOT}" "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.cpp" "${ROOT}/${dir}/*.cc" "${ROOT}/${dir}/*.inl" "${ROOT}/${dir}/*.mm")
  list(APPEND all_files ${found})
endforeach()
foreach(f IN LISTS all_files)
  if(f MATCHES "(^|/)fixtures/")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "${lib_include_re}")
      set(which "${CMAKE_MATCH_2}")
      list(FIND allowed_${which} "${f}" idx)
      if(idx LESS 0)
        gate_problem("${f}:${n}: includes ${which}.h, which only its one implementation file may include: ${l}")
      endif()
    endif()
  endforeach()
endforeach()

# --- Rule 3 ------------------------------------------------------------------------------------------------------------
set(config_macro_re "(^|[^A-Za-z0-9_])(ENGINE_VERIFY|ENGINE_CHECK_SLOW|ENGINE_UNREACHABLE)[ \t]*\\(")
foreach(f IN LISTS all_files)
  if(f MATCHES "(^|/)fixtures/" OR NOT f MATCHES "\\.(h|hpp|inl)$" OR f STREQUAL "engine/foundation/log/assert.h")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "${config_macro_re}")
      gate_problem("${f}:${n}: ${CMAKE_MATCH_2} depends on a per-translation-unit setting and is not allowed in a header: ${l}")
    endif()
  endforeach()
endforeach()

# --- Rule 4 ------------------------------------------------------------------------------------------------------------
# The header's name without its folders, so <um/windows.h> counts as windows.h (Day 150 review).
set(win_sdk_re "^[ \t]*#[ \t]*include[ \t]*[<\"]([A-Za-z0-9_]+/)*([A-Za-z0-9_]+)\\.h[>\"]")
set(win_sdk_names "^(windows|winternl|wtsapi32|psapi|dbghelp|shlobj|shlobj_core|shellapi|knownfolders|objbase|combaseapi|processthreadsapi|winbase|winuser|winnt|minwindef|fileapi|handleapi|synchapi|libloaderapi|memoryapi|sysinfoapi|debugapi|errhandlingapi|consoleapi|wincon|winreg|winsock2|ws2tcpip|shtypes|propsys|mmdeviceapi|audioclient|xinput|d3d12[a-z0-9_]*|dxgi[a-z0-9_]*|d3dcommon|d3dcompiler|dxcapi)$")
set(win_dirs_re "^(engine/foundation/(os|io|settings)/windows/|engine/platform/windows/|engine/rhi/dx12/|tests/windows/)")
foreach(f IN LISTS all_files)
  if(f MATCHES "(^|/)fixtures/" OR f MATCHES "^tests/windows/discovery/")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  set(seen_wrapper FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "^[ \t]*#[ \t]*include[ \t]*\"([^\"]*/)?win32\\.h\"")
      set(seen_wrapper TRUE)
      continue()
    endif()
    if(NOT code MATCHES "${win_sdk_re}")
      continue()
    endif()
    set(h "${CMAKE_MATCH_2}")
    string(TOLOWER "${h}" h)
    if(NOT h MATCHES "${win_sdk_names}")
      continue()
    endif()
    set(why "")
    if(NOT f MATCHES "${win_dirs_re}")
      set(why "a Windows SDK header outside the Windows directories")
    elseif(h STREQUAL "windows")
      if(NOT f STREQUAL "engine/foundation/os/windows/win32.h")
        set(why "<windows.h> outside engine/foundation/os/windows/win32.h; include that wrapper instead")
      endif()
    elseif(h MATCHES "^(d3d12|dxgi|d3dcommon|d3dcompiler|dxcapi)")
      if(NOT f STREQUAL "engine/rhi/dx12/d3d12_api.h")
        set(why "a D3D12 or DXGI header outside engine/rhi/dx12/d3d12_api.h; include that wrapper instead")
      endif()
    elseif(NOT seen_wrapper)
      set(why "a Windows SDK header before engine/foundation/os/windows/win32.h; include the wrapper first")
    endif()
    if(NOT why STREQUAL "")
      gate_problem("${f}:${n}: ${why}: ${l}")
    endif()
  endforeach()
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "platform_neutral_check: ${errors} violation(s)")
endif()
