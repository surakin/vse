# UI widget style gate (Phase 15 Day 195). Run as:
#   cmake -DROOT=<tree> -P check.cmake
# A widget draws only from the theme: no .cpp under engine/ui/widgets/ may contain a hex color literal (0xRRGGBB, 0xAARRGGBB) or a decimal number of 3 or more (a pixel
# metric); sizes and colors come from `ThemeSet`, and a named constant that is a multiple of a theme metric lives in the widget's header. 0, 1, 2 and fractions such as 0.5f
# (halving, centering) are allowed. Comments and string literals are ignored.
cmake_minimum_required(VERSION 3.20)

if(NOT ROOT)
  message(FATAL_ERROR "usage: -DROOT=<tree>")
endif()

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

set(hex_re "0[xX][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
set(number_re "(^|[^A-Za-z0-9_.])([3-9]|[1-9][0-9]+)(\\.[0-9]*)?[fFuUlL]*($|[^A-Za-z0-9_.])")

file(GLOB_RECURSE files RELATIVE "${ROOT}" "${ROOT}/engine/ui/widgets/*.cpp")
foreach(f IN LISTS files)
  read_lines("${ROOT}/${f}" lines)
  set(n 0)
  foreach(l IN LISTS lines)
    math(EXPR n "${n}+1")
    string(REGEX REPLACE "\"[^\"]*\"" "\"\"" code "${l}")
    string(REGEX REPLACE "//.*$" "" code "${code}")
    if(code MATCHES "${hex_re}")
      string(REPLACE "<SEMI>" ";" shown "${l}")
      gate_problem("${f}:${n}: a color literal in a widget (take it from the theme): ${shown}")
    elseif(code MATCHES "${number_re}")
      string(REPLACE "<SEMI>" ";" shown "${l}")
      gate_problem("${f}:${n}: a number of 3 or more in a widget (take the metric from the theme): ${shown}")
    endif()
  endforeach()
endforeach()

if(errors GREATER 0)
  message(FATAL_ERROR "${errors} violation(s) in engine/ui/widgets")
endif()
message(STATUS "ui_widget_style_check: ${ROOT}: clean")
