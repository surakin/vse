# ABI gate: see check.cmake. Registered as ctest cases (label: engine_library).
set(ABI_CHECK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(F ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# The C compiler of the header check. On Windows (Day 354) the toolchain file gives the system include directories as compile flags, which a
# compiler started from a test does not see: they are passed, `|`-separated, as C_SYSTEM_INCLUDES.
set(_c_compiler -DC_COMPILER=${CMAKE_C_COMPILER})
if(ENGINE_WINDOWS_INCLUDE_DIRS)
  string(REPLACE ";" "|" _c_includes "${ENGINE_WINDOWS_INCLUDE_DIRS}")
  list(APPEND _c_compiler "-DC_SYSTEM_INCLUDES=${_c_includes}")
endif()

# The real tree: the export list agrees with the version header and, through git, with the last commit's list; the C API header (Day 346) holds no C++.
set(_real_header "")
if(EXISTS ${CMAKE_SOURCE_DIR}/include/engine/engine_c.h)
  set(_real_header -DC_HEADER=${CMAKE_SOURCE_DIR}/include/engine/engine_c.h ${_c_compiler})
endif()
add_test(NAME abi_check COMMAND ${CMAKE_COMMAND} -DEXPORTS=${CMAKE_SOURCE_DIR}/abi/engine.exports
         -DVERSION_HEADER=${CMAKE_SOURCE_DIR}/include/engine/engine_abi_version.h -DGIT_ROOT=${CMAKE_SOURCE_DIR} ${_real_header} -P ${ABI_CHECK_SCRIPT})

# The built library exports exactly the list, in every flavor (ENGINE_BUILD_SHARED, Linux); skipped stubs otherwise.
foreach(suffix "" "_asan" "_tsan")
  set(flavor_label "")
  if(suffix STREQUAL "_asan")
    set(flavor_label asan)
  elseif(suffix STREQUAL "_tsan")
    set(flavor_label tsan)
  endif()
  if(ENGINE_BUILD_SHARED AND TARGET engine${suffix} AND CMAKE_SYSTEM_NAME STREQUAL "Windows" AND EXISTS "${ENGINE_MSVC_BIN}/dumpbin.exe")
    # Windows (Day 354): the DLL's export table, through dumpbin (both compilers: the toolchain files find MSVC's tools).
    add_test(NAME abi_check_library${suffix} COMMAND ${CMAKE_COMMAND} -DEXPORTS=${CMAKE_SOURCE_DIR}/abi/engine.exports
             -DVERSION_HEADER=${CMAKE_SOURCE_DIR}/include/engine/engine_abi_version.h -DLIBRARY=$<TARGET_FILE:engine${suffix}>
             -DDUMPBIN=${ENGINE_MSVC_BIN}/dumpbin.exe -P ${ABI_CHECK_SCRIPT})
  elseif(ENGINE_BUILD_SHARED AND TARGET engine${suffix} AND CMAKE_NM)
    set(_nm_style "")
    if(APPLE)
      set(_nm_style -DNM_STYLE=macho)
    endif()
    add_test(NAME abi_check_library${suffix} COMMAND ${CMAKE_COMMAND} -DEXPORTS=${CMAKE_SOURCE_DIR}/abi/engine.exports
             -DVERSION_HEADER=${CMAKE_SOURCE_DIR}/include/engine/engine_abi_version.h -DLIBRARY=$<TARGET_FILE:engine${suffix}> -DNM=${CMAKE_NM} ${_nm_style} -P ${ABI_CHECK_SCRIPT})
  else()
    add_test(NAME abi_check_library${suffix} COMMAND ${CMAKE_COMMAND} -E echo "skipped: no engine${suffix} library in this configuration (ENGINE_BUILD_SHARED is OFF, or the platform builds it on a later day)")
    set_tests_properties(abi_check_library${suffix} PROPERTIES SKIP_REGULAR_EXPRESSION "skipped: ")
  endif()
  set_tests_properties(abi_check_library${suffix} PROPERTIES LABELS "engine_library;${flavor_label}")
  list(APPEND ABI_LIBRARY_TESTS abi_check_library${suffix})
endforeach()

function(abi_case name expect_fail message)
  add_test(NAME abi_check_${name} COMMAND ${CMAKE_COMMAND} ${ARGN} -P ${ABI_CHECK_SCRIPT})
  if(expect_fail)
    # The message says why; a second run of the same case must also exit non-zero (a regex match alone ignores the exit code).
    set_tests_properties(abi_check_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${message}")
    add_test(NAME abi_check_${name}_exits COMMAND ${CMAKE_COMMAND} ${ARGN} -P ${ABI_CHECK_SCRIPT})
    set_tests_properties(abi_check_${name}_exits PROPERTIES WILL_FAIL TRUE)
    list(APPEND ABI_CASES abi_check_${name}_exits)
  endif()
  list(APPEND ABI_CASES abi_check_${name})
  set(ABI_CASES ${ABI_CASES} PARENT_SCOPE)
endfunction()

# Clean: an added symbol with a minor bump, a library that matches, a header that is C.
abi_case(clean FALSE "" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DPREVIOUS=${F}/prev_1_0.exports -DNM_OUTPUT=${F}/nm_good.txt
         -DC_HEADER=${F}/header_ok.h ${_c_compiler})
# A comment may hold a semicolon and brackets; a nested conditional inside the C++ block and an `#else` branch are read right.
abi_case(comment_semicolon FALSE "" -DEXPORTS=${F}/comment_semicolon.exports -DVERSION_HEADER=${F}/version_1_1.h)
abi_case(header_nested FALSE "" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DC_HEADER=${F}/header_nested.h)
# Before the first release (`status unreleased`) a removal and a lower version are allowed.
abi_case(unreleased FALSE "" -DEXPORTS=${F}/unreleased_1_0.exports -DVERSION_HEADER=${F}/version_1_0.h -DPREVIOUS=${F}/prev_1_1_two.exports)
# Planted violations, each reported by name.
abi_case(bad_symbol_name TRUE "not a version line or a symbol name: bad-name" -DEXPORTS=${F}/bad_name.exports -DVERSION_HEADER=${F}/version_1_0.h)
abi_case(header_else_branch TRUE "header_else_bad.h:6: a C\\+\\+ token" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DC_HEADER=${F}/header_else_bad.h)
abi_case(version_mismatch TRUE "says 1.0 but .* says 1.1" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_0.h)
abi_case(library_extra TRUE "exports leaked, which abi/engine.exports does not list" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DNM_OUTPUT=${F}/nm_extra.txt)
# Mach-O (Day 355): `nm -gU` lines with the `_` prefix, read the same way; a leaked C++ symbol is named without the prefix.
abi_case(clean_macho FALSE "" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DNM_OUTPUT=${F}/nm_good_macho.txt -DNM_STYLE=macho)
abi_case(library_extra_macho TRUE "exports _ZN6leaked1fEv, which abi/engine.exports does not list" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DNM_OUTPUT=${F}/nm_extra_macho.txt -DNM_STYLE=macho)
# The dumpbin reader (Windows' export table, Day 354) on its recorded output, on every platform: a match, and a leak named.
abi_case(dumpbin_good FALSE "" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DDUMPBIN_OUTPUT=${F}/dumpbin_good.txt)
abi_case(dumpbin_extra TRUE "exports leaked, which abi/engine.exports does not list" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DDUMPBIN_OUTPUT=${F}/dumpbin_extra.txt)
abi_case(library_missing TRUE "lists beta, which the library does not export" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DNM_OUTPUT=${F}/nm_missing.txt)
abi_case(added_without_bump TRUE "exports added \\(beta\\) without a minor version bump" -DEXPORTS=${F}/two_1_0.exports -DVERSION_HEADER=${F}/version_1_0.h -DPREVIOUS=${F}/prev_1_0.exports)
abi_case(removed_without_major TRUE "exports removed \\(beta\\) without a major version bump" -DEXPORTS=${F}/one_1_2.exports -DVERSION_HEADER=${F}/version_1_2.h -DPREVIOUS=${F}/prev_1_1_two.exports)
abi_case(version_down TRUE "the ABI version went down: 1.3 to 1.0" -DEXPORTS=${F}/one_1_0.exports -DVERSION_HEADER=${F}/version_1_0.h -DPREVIOUS=${F}/prev_1_3.exports)
abi_case(header_cpp TRUE "header_cpp.h:3: a C\\+\\+ token" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DC_HEADER=${F}/header_cpp.h)
abi_case(header_scope TRUE "header_scope.h:4: a C\\+\\+ token" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DC_HEADER=${F}/header_scope.h)
abi_case(header_not_c TRUE "is not valid C11" -DEXPORTS=${F}/good_1_1.exports -DVERSION_HEADER=${F}/version_1_1.h -DC_HEADER=${F}/header_not_c.h ${_c_compiler})

set_tests_properties(abi_check ${ABI_CASES} PROPERTIES LABELS "engine_library")
engine_label_tests(abi_check ${ABI_LIBRARY_TESTS} ${ABI_CASES})
