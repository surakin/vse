# File access gate (Phase 9 Day 124). Run as:
#   cmake -DROOT=<tree> [-DALLOWLIST=<file>] -P check.cmake
# No C or C++ file under engine/, game/, tools/ or tests/ may call the operating system's file or directory API directly
# (fopen, open, opendir, readdir, stat, mkdir, rename, unlink, realpath, getcwd, access, ... or std::ifstream/ofstream/filesystem):
# files go through io::File, io::Path and io::Directory (engine/foundation/io), and the tools and tests through the same layer.
# Exempt: lines between `// SIGNAL-SAFE-BEGIN` and `// SIGNAL-SAFE-END` (they run inside a crashing process and cannot allocate),
# member calls (`x.open(...)`, `p->stat(...)`), comments and string literals, and the files in the allow list, each with a reason.
# A stale allow-list entry (it no longer exempts anything) fails the gate, so the list only shrinks.
# Calls on something already open (read, write, close, fstat, lseek, fsync, fwrite to stderr) are not path access: opening is what is gated.
cmake_minimum_required(VERSION 3.20)

if(NOT ROOT)
  message(FATAL_ERROR "usage: -DROOT=<tree> [-DALLOWLIST=<file>]")
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
if(NOT ALLOWLIST)
  set(ALLOWLIST "${ROOT}/tools/file_access_check/allowlist.txt")
endif()

function(read_lines path out)
  file(READ "${path}" content)
  string(REPLACE ";" "<SEMI>" content "${content}")
  string(REPLACE "[" "<LB>" content "${content}")
  string(REPLACE "]" "<RB>" content "${content}")
  string(REPLACE "\n" ";" content "${content}")
  set(${out} "${content}" PARENT_SCOPE)
endfunction()

# The allow list: `path|reason`. A path ending in `/` covers a directory.
set(allow_paths "")
set(allow_reasons "")
if(EXISTS "${ALLOWLIST}")
  file(STRINGS "${ALLOWLIST}" allow_lines)
  foreach(line IN LISTS allow_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
      continue()
    endif()
    string(FIND "${line}" "|" bar)
    if(bar LESS 1)
      gate_problem("${ALLOWLIST}: an entry needs `path|reason`: ${line}")
      continue()
    endif()
    string(SUBSTRING "${line}" 0 ${bar} p)
    math(EXPR after "${bar}+1")
    string(SUBSTRING "${line}" ${after} -1 reason)
    string(STRIP "${reason}" reason)
    if(reason STREQUAL "")
      gate_problem("${ALLOWLIST}: the entry for ${p} has no reason")
    endif()
    list(APPEND allow_paths "${p}")
    list(APPEND allow_reasons "${reason}")
  endforeach()
endif()
set(allow_used "")

# The calls. Preceded by a character that is not part of an identifier, `.` or `>` (a member call), and followed by `(`.
# POSIX and C library names, then (Phase 13 Day 150) the Win32 and Microsoft C runtime file functions.
set(names "fopen|fdopen|freopen|tmpfile|open|openat|creat|opendir|fdopendir|readdir|closedir|stat|lstat|statx|fstatat|mkdir|mkdirat|rmdir|rename|renameat|renameat2|unlink|unlinkat|remove|realpath|getcwd|chdir|access|faccessat|readlink|readlinkat|symlink|symlinkat|link|linkat|truncate|mkstemp|mkdtemp|nftw|ftw|chmod|fchmodat|chown|utimes|utimensat|memfd_create|CreateFileW|CreateFileA|CreateFile2|FindFirstFileW|FindFirstFileA|FindFirstFileExW|FindFirstFileExA|FindNextFileW|FindNextFileA|MoveFileW|MoveFileA|MoveFileExW|MoveFileExA|ReplaceFileW|ReplaceFileA|DeleteFileW|DeleteFileA|CopyFileW|CopyFileA|CopyFile2|CreateDirectoryW|CreateDirectoryA|RemoveDirectoryW|RemoveDirectoryA|GetFileAttributesW|GetFileAttributesA|GetFileAttributesExW|SetFileAttributesW|GetFullPathNameW|GetFullPathNameA|GetFinalPathNameByHandleW|GetTempPathW|GetTempPathA|GetTempPath2W|GetCurrentDirectoryW|SetCurrentDirectoryW|_wfopen|_wfopen_s|fopen_s|_open|_wopen|_sopen_s|_wsopen_s|_unlink|_wunlink|_mkdir|_wmkdir|_rmdir|_wrmdir|_stat|_wstat|_stat64|_wstat64|_access|_waccess|_findfirst|_wfindfirst|_chdir|_wchdir|_getcwd|_wgetcwd")
set(call_re "(^|[^A-Za-z0-9_.>])(::|std::)?(${names})[ \t]*\\(")
set(stream_re "std::(ifstream|ofstream|fstream|filesystem)|<fstream>|<filesystem>")

set(files "")
foreach(dir engine game tools tests)
  file(GLOB_RECURSE found RELATIVE "${ROOT}" "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.cpp"
       "${ROOT}/${dir}/*.cc" "${ROOT}/${dir}/*.inl" "${ROOT}/${dir}/*.mm" "${ROOT}/${dir}/*.m" "${ROOT}/${dir}/*.c")
  list(APPEND files ${found})
endforeach()

set(scanned 0)
foreach(f IN LISTS files)
  if(f MATCHES "/fixtures/" OR f MATCHES "/generated/")
    continue() # data for the tools' own tests, and generated protocol code
  endif()
  math(EXPR scanned "${scanned}+1")
  # Which allow entry covers this file?
  set(entry -1)
  set(i 0)
  foreach(p IN LISTS allow_paths)
    if((p MATCHES "/$" AND f MATCHES "^${p}") OR f STREQUAL p)
      set(entry ${i})
    endif()
    math(EXPR i "${i}+1")
  endforeach()
  # A file with no raw line that could hit (a call name before `(`, a stream name) cannot hit after its comments and strings are
  # blanked either, so it is not walked (maintenance "shorter test runs", Day 4: the line walk in CMake took 17 s). Skipping it marks
  # no allow entry used, as walking it would not.
  file(STRINGS "${ROOT}/${f}" maybe REGEX "(${names})[ \t]*\\(|${stream_re}" LIMIT_COUNT 1 ENCODING UTF-8)  # whole lines, not split at non-ASCII bytes
  if(NOT maybe)
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  set(in_safe FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    if(l MATCHES "SIGNAL-SAFE-BEGIN")
      set(in_safe TRUE)
      continue()
    endif()
    if(l MATCHES "SIGNAL-SAFE-END")
      set(in_safe FALSE)
      continue()
    endif()
    set(code "${l}")
    string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "\"\"" code "${code}")
    if(in_block)
      string(FIND "${code}" "*/" close)
      if(close LESS 0)
        continue()
      endif()
      math(EXPR after "${close}+2")
      string(SUBSTRING "${code}" ${after} -1 code)
      set(in_block FALSE)
    endif()
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " code "${code}")
    string(FIND "${code}" "//" slashes)
    if(NOT slashes LESS 0)
      string(SUBSTRING "${code}" 0 ${slashes} code)
    endif()
    string(FIND "${code}" "/*" open)
    if(NOT open LESS 0)
      string(FIND "${code}" "*/" close)
      if(close LESS 0)
        string(SUBSTRING "${code}" 0 ${open} code)
        set(in_block TRUE)
      endif()
    endif()
    # #include <fstream> is a violation too, but the stream pattern must see the <...>; look at the unstripped include line.
    set(hit FALSE)
    if(code MATCHES "${call_re}")
      set(hit TRUE)
    endif()
    if(l MATCHES "${stream_re}" AND NOT l MATCHES "^[ \t]*//")
      set(hit TRUE)
    endif()
    if(NOT hit)
      continue()
    endif()
    if(in_safe)
      continue()
    endif()
    if(NOT entry EQUAL -1)
      list(APPEND allow_used ${entry})
      continue()
    endif()
    string(REPLACE "<LB>" "[" shown "${l}")
    string(REPLACE "<RB>" "]" shown "${shown}")
    string(REPLACE "<SEMI>" ";" shown "${shown}")
    string(STRIP "${shown}" shown)
    gate_problem("${f}:${n}: direct file access; use io::File, io::Path or io::Directory (or add the file to tools/file_access_check/allowlist.txt with a reason): ${shown}")
  endforeach()
endforeach()

# A stale allow-list entry fails: it covers nothing now.
set(i 0)
foreach(p IN LISTS allow_paths)
  list(FIND allow_used ${i} found_at)
  if(found_at LESS 0)
    gate_problem("${ALLOWLIST}: stale entry ${p}: nothing in it makes a direct file access any more; remove it")
  endif()
  math(EXPR i "${i}+1")
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "file_access_check: ${errors} problem(s)")
endif()
message(STATUS "file_access_check: ${scanned} files clean")
