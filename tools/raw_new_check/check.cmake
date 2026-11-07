# Raw new/delete gate (Phase 5 Days 80a-80b). Run as:
#   cmake -DROOT=<tree> -P check.cmake
# No file under engine/, game/, tools/ or tests/ (the planted fixtures of tools/*/fixtures excepted) may use `new T`, `new T[n]`, `new (std::nothrow) T`, `delete p` or `delete[] p`:
# owning arrays are `Array<T, N>` (compile-time size, inline) or `Vector<T>`; single objects are `UniquePtr<T>` or
# `SharedPtr<T>`. Exempt: the foundation allocators (engine/foundation/mem/), placement `new (ptr) T` into storage
# a container owns, `operator new`/`operator delete` definitions, and `= delete`. Comments and strings are ignored.
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

# Heap `new` (a type name follows, or the nothrow tag then a type); placement `new (ptr) T` does not match.
set(new_re "(^|[^A-Za-z0-9_])new([ \t]+[A-Za-z_:]|[ \t]*\\(std::nothrow\\)[ \t]*[A-Za-z_:])")
# `delete p`, `delete (p)`, `delete(p)`, `delete[] p`; `= delete;` does not match.
# A `new` that ends its line: the type is on the next line.
set(new_eol_re "(^|[^A-Za-z0-9_])new[ \t]*$")
set(delete_re "(^|[^A-Za-z0-9_])delete[ \t]*(<LB><RB>|\\(|[ \t][A-Za-z_(*:])")
# `operator new` / `operator delete` (definitions, declarations) are removed from the line before matching.
set(operator_re "operator[ \t]+(new|delete)(<LB><RB>)?")

set(files "")
foreach(dir engine game tools tests)
  file(GLOB_RECURSE found RELATIVE "${ROOT}" "${ROOT}/${dir}/*.h" "${ROOT}/${dir}/*.hpp" "${ROOT}/${dir}/*.cpp"
       "${ROOT}/${dir}/*.cc" "${ROOT}/${dir}/*.inl" "${ROOT}/${dir}/*.mm")
  list(APPEND files ${found})
endforeach()

foreach(f IN LISTS files)
  # The foundation allocators, and the planted violations the other gates keep as fixtures.
  if(f MATCHES "^engine/foundation/mem/" OR f MATCHES "^tools/[^/]+/fixtures/")
    continue()
  endif()
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  set(in_block FALSE)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    set(code "${l}")
    # String literals can hold `//`, `/*` or the words themselves; blank them before looking at comments.
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
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " code "${code}") # a complete /* ... */ on one line
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
    string(REGEX REPLACE "${operator_re}" "" code "${code}")
    if(code MATCHES "${new_re}" OR code MATCHES "${new_eol_re}" OR code MATCHES "${delete_re}")
      string(REPLACE "<LB>" "[" shown "${l}")
      string(REPLACE "<RB>" "]" shown "${shown}")
      string(REPLACE "<SEMI>" ";" shown "${shown}")
      string(STRIP "${shown}" shown)
      gate_problem("${f}:${n}: raw new/delete; use Array<T, N>, Vector<T>, UniquePtr<T> or SharedPtr<T>: ${shown}")
    endif()
  endforeach()
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "raw_new_check: ${errors} violation(s)")
endif()
list(LENGTH files total)
message(STATUS "raw_new_check: ${total} files clean")
