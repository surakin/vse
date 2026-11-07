# Fails if a built Windows binary imports a network or TLS library or names one (Phase 13 Day 155; PLAN.md assumption #20: the crash handler
# must not be able to upload anything). The Windows counterpart of check_no_network.cmake.
#   cmake -DBINARY=<file> -DDUMP_TOOL=<link.exe or llvm-readobj.exe> -P check_no_network_windows.cmake
#   cmake -DIMPORTS_TEXT_FILE=<file with the tool's output> -P check_no_network_windows.cmake   (to test the check itself)
#
# Two halves: the import table (static and delay-loaded imports, from `link /dump /imports` or `llvm-readobj --coff-imports`), and the
# binary's own strings, ASCII and UTF-16, so a library loaded by name at run time (LoadLibrary("winhttp.dll")) is found too.
cmake_minimum_required(VERSION 3.20)

set(network_libraries "(winhttp|wininet|ws2_32|wsock32|mswsock|webio|urlmon|dnsapi|winhttpcom|libcurl|libssl|libcrypto)")
set(network_functions "(winhttpopen|winhttpconnect|internetopen[aw]?|internetconnect[aw]?|wsastartup|getaddrinfo[w]?|gethostbyname|urldownloadtofile[aw]?|curl_easy_[a-z_]+)")

if(IMPORTS_TEXT_FILE)
  file(READ "${IMPORTS_TEXT_FILE}" imports_text)
  set(BINARY "${IMPORTS_TEXT_FILE}")
else()
  if(NOT DUMP_TOOL OR NOT EXISTS "${DUMP_TOOL}")
    message(STATUS "no import dump tool found: cannot check ${BINARY}")
    return()
  endif()
  get_filename_component(tool_name "${DUMP_TOOL}" NAME)
  string(TOLOWER "${tool_name}" tool_name)
  if(tool_name MATCHES "readobj")
    execute_process(COMMAND "${DUMP_TOOL}" --coff-imports "${BINARY}" OUTPUT_VARIABLE imports_text RESULT_VARIABLE rc)
  else()
    execute_process(COMMAND "${DUMP_TOOL}" /dump /nologo /imports "${BINARY}" OUTPUT_VARIABLE imports_text RESULT_VARIABLE rc)
  endif()
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${DUMP_TOOL} failed on ${BINARY}")
  endif()
endif()

string(TOLOWER "${imports_text}" imports_lower)
# Every Windows program imports kernel32: if it is missing, the dump said nothing and the check would pass for the wrong reason.
if(NOT imports_lower MATCHES "kernel32\\.dll")
  message(FATAL_ERROR "the import list of ${BINARY} names no kernel32.dll: the dump did not work")
endif()
if(imports_lower MATCHES "${network_libraries}\\.dll")
  message(FATAL_ERROR "${BINARY} imports a network library: ${CMAKE_MATCH_1}.dll")
endif()
if(imports_lower MATCHES "[^a-z_]${network_functions}[^a-z_]")
  message(FATAL_ERROR "${BINARY} imports a network function: ${CMAKE_MATCH_1}")
endif()

if(NOT IMPORTS_TEXT_FILE)
  foreach(encoding "" "UTF-16LE")
    if(encoding STREQUAL "")
      file(STRINGS "${BINARY}" names REGEX "[Ww][Ii][Nn][Hh][Tt][Tt][Pp]|[Ww][Ii][Nn][Ii][Nn][Ee][Tt]|[Ww][Ss]2_32|[Ww][Ss][Oo][Cc][Kk]32")
    else()
      file(STRINGS "${BINARY}" names ENCODING ${encoding} REGEX "[Ww][Ii][Nn][Hh][Tt][Tt][Pp]|[Ww][Ii][Nn][Ii][Nn][Ee][Tt]|[Ww][Ss]2_32|[Ww][Ss][Oo][Cc][Kk]32")
    endif()
    if(names)
      list(GET names 0 first)
      message(FATAL_ERROR "${BINARY} names a network library in its data: ${first}")
    endif()
  endforeach()
endif()
message(STATUS "${BINARY}: no network library imported or named")
