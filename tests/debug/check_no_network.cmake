# Fails if a built binary links a network or TLS library or imports their functions (Phase 7, PLAN.md assumption #20: the
# crash handler must not be able to upload anything).
#   cmake -DBINARY=<file> -P check_no_network.cmake
#   cmake -DREADELF_TEXT_FILE=<file with readelf output> -P check_no_network.cmake   (to test the check itself)
cmake_minimum_required(VERSION 3.20)

if(READELF_TEXT_FILE)
  file(READ "${READELF_TEXT_FILE}" dynamic_text)
  set(symbol_text "${dynamic_text}") # the canned text holds both parts
else()
  find_program(READELF readelf)
  if(NOT READELF)
    message(STATUS "readelf not found: cannot check ${BINARY}")
    return()
  endif()
  execute_process(COMMAND "${READELF}" -d "${BINARY}" OUTPUT_VARIABLE dynamic_text RESULT_VARIABLE rc1)
  execute_process(COMMAND "${READELF}" --dyn-syms --wide "${BINARY}" OUTPUT_VARIABLE symbol_text RESULT_VARIABLE rc2)
  if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "readelf failed on ${BINARY}")
  endif()
endif()

set(network_libraries "lib(curl|ssl|crypto|gnutls|nghttp[0-9]*|ngtcp2[a-z_]*|mbedtls|wolfssl|boringssl|winhttp|wininet|websockets|ssh2)")
string(REGEX MATCHALL "Shared library: \\[[^]]*\\]" needed "${dynamic_text}")
foreach(entry IN LISTS needed)
  if(entry MATCHES "${network_libraries}")
    message(FATAL_ERROR "${BINARY} links a network library: ${entry}")
  endif()
endforeach()
# Functions a transport would import, in case a library were linked statically or loaded some other way. Only undefined
# symbols count: a sanitizer runtime linked into the binary defines interceptors with the names of libc's resolver calls.
string(REGEX MATCHALL "[^\n]* UND [^\n]*" imported_lines "${symbol_text}")
string(REPLACE ";" "\n" imported "${imported_lines}")
if(imported MATCHES "(curl_easy_[a-z_]+|SSL_(connect|read|write|new)|gnutls_handshake|getaddrinfo|gethostbyname)")
  message(FATAL_ERROR "${BINARY} imports a network function: ${CMAKE_MATCH_1}")
endif()
message(STATUS "${BINARY}: no network library, no network imports")
