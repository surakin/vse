# Fails if a built Mach-O binary links a network or TLS library or imports their functions (Phase 21 Day 291, the macOS form of
# check_no_network.cmake; PLAN.md assumption #20: the crash handler must not be able to upload anything).
#   cmake -DBINARY=<file> -P check_no_network_macos.cmake
#   cmake -DTOOL_TEXT_FILE=<file with otool -L and nm -u output> -P check_no_network_macos.cmake   (to test the check itself)
cmake_minimum_required(VERSION 3.20)

if(TOOL_TEXT_FILE)
  file(READ "${TOOL_TEXT_FILE}" libraries_text)
  set(symbol_text "${libraries_text}") # the canned text holds both parts
else()
  execute_process(COMMAND xcrun --find otool OUTPUT_VARIABLE OTOOL OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND xcrun --find nm OUTPUT_VARIABLE NM OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT OTOOL OR NOT NM)
    message(STATUS "otool or nm not found: cannot check ${BINARY}")
    return()
  endif()
  execute_process(COMMAND "${OTOOL}" -L "${BINARY}" OUTPUT_VARIABLE libraries_text RESULT_VARIABLE rc1)
  execute_process(COMMAND "${NM}" -u "${BINARY}" OUTPUT_VARIABLE symbol_text RESULT_VARIABLE rc2)
  if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "otool or nm failed on ${BINARY}")
  endif()
endif()

# The linked libraries and frameworks (otool -L): a network or TLS library, or the system's networking frameworks.
set(network_libraries "(lib(curl|ssl|crypto|gnutls|nghttp[0-9]*|ngtcp2[a-z_]*|mbedtls|wolfssl|boringssl|websockets|ssh2)[.0-9]*\\.dylib|CFNetwork\\.framework|Network\\.framework)")
string(REGEX MATCHALL "[^\n]*\\(compatibility version[^\n]*" linked "${libraries_text}")
foreach(entry IN LISTS linked)
  if(entry MATCHES "${network_libraries}")
    message(FATAL_ERROR "${BINARY} links a network library: ${CMAKE_MATCH_1}")
  endif()
endforeach()
# Functions and classes a transport would use (nm -u lists only what the binary imports, so a sanitizer runtime's interceptors do not count).
if(symbol_text MATCHES "(_curl_easy_[a-z_]+|_SSL_(connect|read|write|new)|_gnutls_handshake|_getaddrinfo|_gethostbyname|_nw_connection_[a-z_]+|_CFSocketCreate[A-Za-z]*|_OBJC_CLASS_\\$_NSURL(Session|Connection))")
  message(FATAL_ERROR "${BINARY} imports a network function: ${CMAKE_MATCH_1}")
endif()
message(STATUS "${BINARY}: no network library, no network imports")
