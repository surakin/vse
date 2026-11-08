# Platform-neutral gate (Phase 7 Day 92). Run as:
#   cmake -DROOT=<tree> -DBOUNDARY=<file> -P check.cmake
#
# Rule 1: nothing under engine/foundation/debug/ and nothing under tests/debug/ names a platform type, constant or header:
#   ucontext_t, siginfo_t, pid_t, HANDLE, SIGxxx, /proc, fork(, waitpid(, sigaction(, and the headers <unistd.h>,
#   <windows.h>, <signal.h>, <csignal>, <sys/...>, <poll.h>, <spawn.h>, <fcntl.h>, <dirent.h>. Those files speak the
#   neutral API (CrashReason, FaultKind, ChildResult), so the Windows and macOS ports build the same sources. Files whose
#   name contains `_linux`, `_windows` or `_macos` (before a `_` or `.`) hold that platform's details and are exempt, and so is the per-platform
#   code under engine/foundation/os/<platform>/debug/ (it is not under these directories). Comments are ignored.
# Rule 3: ENGINE_VERIFY, ENGINE_CHECK_SLOW and ENGINE_UNREACHABLE are not used in headers. Whether they check is decided
#   per translation unit (ENGINE_DEBUG_CHECKS / ENGINE_SLOW_CHECKS), so an inline function in a header that used them
#   would have different bodies in different translation units (an ODR violation). Headers use ENGINE_ASSERT or
#   ENGINE_ENSURE; the definitions in engine/foundation/log/assert.h are the one exception.
# Rule 2: the third-party library headers are included only by the one implementation file named for each library in the
#   BOUNDARY table (`header|path|reason` lines): `backtrace.h` (libbacktrace), `sentry.h` (sentry-native) and `ft2build.h` (FreeType, Phase 14 Day 175: every other FreeType header is reached through it). Anywhere
#   under engine/, game/, tools/ and tests/ except fixtures. Until Days 94 and 95 add the files, nobody may include them.
# Rule 4 (Phase 13 Day 150): the Windows SDK headers (windows.h, winternl.h, wtsapi32.h, shlobj.h, dbghelp.h and the rest of the list
#   below, d3d12*.h, dxgi*.h) appear only under the Windows directories (engine/foundation/{os,io,settings}/windows/, engine/jobs/windows/,
#   engine/platform/windows/, engine/rhi/vulkan/windows/, engine/rhi/dx12/, tests/windows/, tools/crash_extract/windows/), and only through the engine's wrappers: <windows.h> is included by
#   engine/foundation/os/windows/win32.h alone (it keeps the SDK's macros from leaking), the D3D12 and DXGI headers by
#   engine/rhi/dx12/d3d12_api.h alone (Day 164), and any other SDK header only after win32.h in the same file. tests/windows/discovery/
#   is exempt: its programs probe the toolchain and are built outside the engine. Anywhere under engine/, game/, tools/ and tests/
#   except fixtures; comments are ignored.
# Rule 5 (Phase 13 Day 158): a windowed program under game/ (a file including platform/window_host.h or platform/window.h) defines no
#   main, wmain, WinMain or wWinMain; it writes ENGINE_WINDOWED_PROGRAM (platform/entry_point.h).
# Rule 6 (build_defs): no file names a compiler's or an operating system's own predefined macro (_WIN32, __linux__, __APPLE__, __ANDROID__,
#   __clang__, __GNUC__, _MSC_VER, __x86_64__, __i386__, __aarch64__, _M_X64, _M_IX86, _M_ARM64, __SANITIZE_ADDRESS__, __SANITIZE_THREAD__,
#   __has_feature, __has_builtin). Code asks ENGINE_PLATFORM_*, ENGINE_COMPILER_*, ENGINE_ARCH_* and ENGINE_SANITIZER_* (engine/foundation/build_defs.h,
#   the one file that reads the raw macros). Exempt: tests/windows/discovery/ (toolchain probes built outside the engine) and generated code.
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
set(allowed_ft2build "")
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
  elseif(header STREQUAL "ft2build.h")
    list(APPEND allowed_ft2build "${path}")
  else()
    gate_problem("boundary entry names an unknown header: ${header}")
  endif()
endforeach()

# Platform names (applied to code with strings and comments blanked).
set(ident_re "(^|[^A-Za-z0-9_])(ucontext_t|siginfo_t|pid_t|HANDLE|SIG[A-Z0-9]+|(fork|waitpid|sigaction)[ \t]*\\()([^A-Za-z0-9_]|$)")
set(platform_include_re "^[ \t]*#[ \t]*include[ \t]*[<\"](unistd\\.h|windows\\.h|signal\\.h|csignal|sys/[A-Za-z0-9_/]+\\.h|poll\\.h|spawn\\.h|fcntl\\.h|dirent\\.h)[>\"]")
set(proc_re "/proc")
set(lib_include_re "^[ \t]*#[ \t]*include[ \t]*[<\"]([A-Za-z0-9_/]*/)?(backtrace|sentry|ft2build)\\.h[>\"]")

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
  if(base MATCHES "_(linux|windows|macos)[_.]") # the per-platform test files (Phase 13 Day 154: Windows as well as Linux)
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
    set(which "")
    if(code MATCHES "${lib_include_re}")
      set(which "${CMAKE_MATCH_2}")
    elseif(code MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]freetype/[A-Za-z0-9_/]+\\.h[>\"]")
      set(which "ft2build") # a <freetype/...> include counts as ft2build.h: FreeType's own headers are the same library: they need no ft2build.h to be reached
    endif()
    if(NOT which STREQUAL "")
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
set(win_dirs_re "^(engine/foundation/(os|io|settings)/windows/|engine/jobs/windows/|engine/platform/windows/|engine/rhi/vulkan/windows/|engine/rhi/dx12/|tests/windows/|tools/crash_extract/windows/|tools/docgen/src/windows/|tools/shader_mint/windows/)")
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
    # The D3D12 wrapper (Day 164) is the engine's own header and includes win32.h first: it is the include the rule asks for.
    if(code MATCHES "^[ \t]*#[ \t]*include[ \t]*\"rhi/dx12/d3d12_api\\.h\"")
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

# --- Rule 5 ------------------------------------------------------------------------------------------------------------
# A windowed program under game/ (a file that includes platform/window_host.h or platform/window.h) defines no main, wmain, WinMain or
# wWinMain of its own: it writes ENGINE_WINDOWED_PROGRAM (platform/entry_point.h), which makes it a GUI program on Windows (the demos
# moved to it on Day 161).
set(entry_def_re "^[ \t]*(extern[ \t]+\"C\"[ \t]+)?int[ \t]+(__stdcall[ \t]+|WINAPI[ \t]+)?(main|wmain|WinMain|wWinMain)[ \t]*\\(")
set(window_include_re "^[ \t]*#[ \t]*include[ \t]*\"platform/(window_host|window)\\.h\"")
foreach(f IN LISTS all_files)
  if(f MATCHES "(^|/)fixtures/" OR NOT f MATCHES "^game/")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  set(windowed FALSE)
  set(entry_lines "")
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "${window_include_re}")
      set(windowed TRUE)
    endif()
    if(code MATCHES "${entry_def_re}")
      list(APPEND entry_lines "${n}")
    endif()
  endforeach()
  if(windowed)
    foreach(n IN LISTS entry_lines)
      gate_problem("${f}:${n}: a windowed program defines its own entry point; write ENGINE_WINDOWED_PROGRAM(EngineMain) (platform/entry_point.h)")
    endforeach()
  endif()
endforeach()

# --- Rule 6 ------------------------------------------------------------------------------------------------------------
set(raw_define_re "(^|[^A-Za-z0-9_])(_WIN32|_WIN64|__linux__|__APPLE__|__ANDROID__|__clang__|__GNUC__|_MSC_VER|__x86_64__|__i386__|__aarch64__|_M_X64|_M_IX86|_M_ARM64|__SANITIZE_ADDRESS__|__SANITIZE_THREAD__|__has_feature|__has_builtin)([^A-Za-z0-9_]|$)")
foreach(f IN LISTS all_files)
  if(f MATCHES "(^|/)fixtures/" OR f MATCHES "/generated/" OR f MATCHES "^tests/windows/discovery/" OR f STREQUAL "engine/foundation/build_defs.h")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    strip_comments()
    if(code MATCHES "${raw_define_re}")
      set(raw_name "${CMAKE_MATCH_2}")
      string(STRIP "${l}" shown)
      gate_problem("${f}:${n}: a raw compiler or system macro (${raw_name}); use ENGINE_PLATFORM_*, ENGINE_COMPILER_*, ENGINE_ARCH_* or ENGINE_SANITIZER_* from build_defs.h: ${shown}")
    endif()
  endforeach()
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "platform_neutral_check: ${errors} violation(s)")
endif()
