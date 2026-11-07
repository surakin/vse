# Signal-safety gate (Phase 7 Day 97). Run as:
#   cmake -DROOT=<tree> -P check.cmake
# Code that runs inside a crashing process (the signal recorder, the crash report writer, the log ring reader) may call only
# raw system calls, atomics and a few memory functions: no allocation, no stdio formatting, no locks, no engine log. Such code
# sits between the marker comments `// SIGNAL-SAFE-BEGIN` and `// SIGNAL-SAFE-END`; between them these are forbidden:
#   malloc calloc realloc free new delete, printf-family, FILE I/O, strdup, mutex and lock types, Log(, std:: names other than
#   the allowed few (atomic, atomic_thread_fence, memory_order_*, memset, memcpy, strlen, strncmp).
# On Windows (Phase 13 Day 150) "signal-safe" means code that runs inside a crashed process's exception handler (the vectored or
# unhandled-exception filter): the heap and the loader lock may be held by the thread that crashed, so the same rules apply and these
# Win32 and C runtime names are forbidden too: HeapAlloc HeapReAlloc HeapFree LocalAlloc LocalFree GlobalAlloc GlobalFree
# CoTaskMemAlloc, EnterCriticalSection LeaveCriticalSection AcquireSRWLock* ReleaseSRWLock* (and the TryEnter/TryAcquire forms),
# wsprintf* swprintf _snprintf _snwprintf sprintf_s _vsnprintf, FormatMessage*, _wfopen fopen_s, OutputDebugString* (it takes a lock
# and allocates), LoadLibrary* (the loader lock). WriteFile, VirtualAlloc and the interlocked functions stay allowed.
# An unbalanced marker is also an error.
cmake_minimum_required(VERSION 3.20)

if(NOT ROOT)
  message(FATAL_ERROR "usage: -DROOT=<tree>")
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

function(read_lines path out)
  file(READ "${path}" content)
  string(REPLACE ";" "<SEMI>" content "${content}")
  string(REPLACE "[" "<LB>" content "${content}")
  string(REPLACE "]" "<RB>" content "${content}")
  string(REPLACE "\n" ";" content "${content}")
  set(${out} "${content}" PARENT_SCOPE)
endfunction()

set(ident "(^|[^A-Za-z0-9_])")
set(forbidden_re "${ident}(malloc|calloc|realloc|free|strdup|printf|snprintf|sprintf|vsnprintf|vprintf|fprintf|fopen|fclose|fwrite|fread|fputs|puts|new|delete|pthread_mutex_lock|pthread_mutex_unlock|mutex|lock_guard|unique_lock|scoped_lock|condition_variable)([^A-Za-z0-9_]|$)")
set(win_forbidden_re "${ident}(HeapAlloc|HeapReAlloc|HeapFree|LocalAlloc|LocalReAlloc|LocalFree|GlobalAlloc|GlobalReAlloc|GlobalFree|CoTaskMemAlloc|EnterCriticalSection|TryEnterCriticalSection|LeaveCriticalSection|AcquireSRWLockExclusive|AcquireSRWLockShared|TryAcquireSRWLockExclusive|TryAcquireSRWLockShared|ReleaseSRWLockExclusive|ReleaseSRWLockShared|wsprintfA|wsprintfW|wvsprintfA|wvsprintfW|swprintf|_snprintf|_snwprintf|sprintf_s|swprintf_s|_vsnprintf|_vsnwprintf|FormatMessageA|FormatMessageW|_wfopen|fopen_s|_wfopen_s|OutputDebugStringA|OutputDebugStringW|LoadLibraryA|LoadLibraryW|LoadLibraryExA|LoadLibraryExW)([^A-Za-z0-9_]|$)")
set(log_re "${ident}Log[ \t]*\\(")
set(std_re "std::([A-Za-z_0-9]+)")

set(files "")
foreach(dir engine game)
  file(GLOB_RECURSE found RELATIVE "${ROOT}" "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.cpp" "${ROOT}/${dir}/*.cc" "${ROOT}/${dir}/*.inl")
  list(APPEND files ${found})
endforeach()

set(regions 0)
foreach(f IN LISTS files)
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(inside FALSE)
  set(begin_line 0)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    if(l MATCHES "// SIGNAL-SAFE-BEGIN")
      if(inside)
        gate_problem("${f}:${n}: SIGNAL-SAFE-BEGIN inside a region begun at line ${begin_line}")
      endif()
      set(inside TRUE)
      set(begin_line ${n})
      math(EXPR regions "${regions}+1")
      continue()
    endif()
    if(l MATCHES "// SIGNAL-SAFE-END")
      if(NOT inside)
        gate_problem("${f}:${n}: SIGNAL-SAFE-END without a begin")
      endif()
      set(inside FALSE)
      continue()
    endif()
    if(NOT inside)
      continue()
    endif()
    set(code "${l}")
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"\"" code "${code}")
    string(FIND "${code}" "//" slashes)
    if(NOT slashes LESS 0)
      string(SUBSTRING "${code}" 0 ${slashes} code)
    endif()
    string(REGEX REPLACE "=[ \t]*delete" "" code "${code}") # a deleted function is not the delete operator
    set(why "")
    if(code MATCHES "${forbidden_re}")
      set(why "${CMAKE_MATCH_2}")
    elseif(code MATCHES "${win_forbidden_re}")
      set(why "${CMAKE_MATCH_2}")
    elseif(code MATCHES "${log_re}")
      set(why "Log")
    else()
      string(REGEX MATCHALL "${std_re}" names "${code}")
      foreach(name IN LISTS names)
        if(NOT name MATCHES "^std::(atomic|atomic_thread_fence|memory_order_[a-z_]+|memset|memcpy|strlen|strncmp|strchr)$")
          set(why "${name}")
        endif()
      endforeach()
    endif()
    if(NOT why STREQUAL "")
      string(REPLACE "<LB>" "[" shown "${l}")
      string(REPLACE "<RB>" "]" shown "${shown}")
      string(REPLACE "<SEMI>" ";" shown "${shown}")
      string(STRIP "${shown}" shown)
      gate_problem("${f}:${n}: '${why}' in signal-safe code: ${shown}")
    endif()
  endforeach()
  if(inside)
    gate_problem("${f}: SIGNAL-SAFE-BEGIN at line ${begin_line} is never closed")
  endif()
endforeach()

if(regions EQUAL 0)
  gate_problem("signal_safety_check: no SIGNAL-SAFE region found under ${ROOT}; the markers have been lost")
endif()
if(errors GREATER 0)
  message(FATAL_ERROR "signal_safety_check: ${errors} violation(s)")
endif()
message(STATUS "signal_safety_check: ${regions} region(s) clean")
