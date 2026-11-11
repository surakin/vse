# Module dependency gate: see check.cmake and ARCHITECTURE.md. Registered as ctest cases (phase 12).
set(MODULE_DEPS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(MODULE_DEPS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# The real tree, with its own module map and allow list. The floor is the number of module edges the gate must find: it was 142 when
# the gate was written, so a gate that stops resolving includes fails instead of passing on an empty graph.
add_test(NAME module_deps_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DMODULES=${CMAKE_CURRENT_SOURCE_DIR}/modules.txt
         -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/allowlist.txt -DMIN_EDGES=100 -P ${MODULE_DEPS_SCRIPT})

# A fixture tree is checked with the fixtures' own map; `expect` is a regular expression the output must match (the exit code is
# not looked at then). The gate prints "module_deps_check: <what>: <from> (<layer>) -> <to> (<layer>): <file>:<line>".
function(module_deps_fixture name tree allowlist expect)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${MODULE_DEPS_FIXTURES}/${tree} -DMODULES=${MODULE_DEPS_FIXTURES}/modules.txt
           -DALLOWLIST=${MODULE_DEPS_FIXTURES}/${allowlist} -P ${MODULE_DEPS_SCRIPT})
  set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
  list(APPEND MODULE_DEPS_TESTS ${name})
  set(MODULE_DEPS_TESTS ${MODULE_DEPS_TESTS} PARENT_SCOPE)
endfunction()

set(MODULE_DEPS_TESTS module_deps_check)
module_deps_fixture(module_deps_check_clean clean clean/allowlist.txt "2 module edges from includes, 0 problem")
module_deps_fixture(module_deps_check_upward upward upward/allowlist.txt "upward include: low \\(1\\) -> mid \\(2\\): engine/low/a.h:3")
# The same through an Objective-C++ #import (Phase 26 Day 286: .mm files are scanned, #import counts as #include).
module_deps_fixture(module_deps_check_upward_objc upward_objc upward_objc/allowlist.txt "upward include: low \\(1\\) -> mid \\(2\\): engine/low/a.mm:2")
module_deps_fixture(module_deps_check_cycle cycle cycle/allowlist.txt "cycle: these modules reach themselves through includes or links: mid, mid2")
module_deps_fixture(module_deps_check_platform platform platform/allowlist.txt "platform include: prog -> low: game/prog/main.cpp:2 includes the per-platform file low/linux/os.h")
module_deps_fixture(module_deps_check_link link link/allowlist.txt "upward link: low \\(1\\) -> mid \\(2\\): engine/low/CMakeLists.txt")
module_deps_fixture(module_deps_check_syslib syslib syslib/allowlist.txt "system library: mid links PkgConfig::Display")
# Phase 13 Day 150: neutral code may not include a platform system header; per-platform code (under a platform-dir) may.
module_deps_fixture(module_deps_check_platform_header platform_header platform_header/allowlist.txt
  "platform header: engine/low/a.h:3 includes <windows.h>.*platform header: game/prog/main.cpp:3 includes <X11/Xlib.h>.*2 problem")
module_deps_fixture(module_deps_check_unresolved unresolved unresolved/allowlist.txt "unresolved include \"nowhere/missing.h\" in game/prog/main.cpp")
module_deps_fixture(module_deps_check_unmapped unmapped unmapped/allowlist.txt "no module for engine/newdir/x.h.*no module for the CMake target mystery")
module_deps_fixture(module_deps_check_stale stale stale/allowlist.txt "stale allow-list entry: edge low -> mid")
module_deps_fixture(module_deps_check_no_reason no_reason no_reason/allowlist.txt "the entry 'edge low -> mid' needs a reason and the day that removes it")
module_deps_fixture(module_deps_check_allowed allowed allowed/allowlist.txt "[0-9]+ module edges from includes, 0 problem")
# A target mapped to a module that has no layer would pass every rule silently.
add_test(NAME module_deps_check_no_layer COMMAND ${CMAKE_COMMAND} -DROOT=${MODULE_DEPS_FIXTURES}/clean -DMODULES=${MODULE_DEPS_FIXTURES}/modules_no_layer.txt
         -DALLOWLIST=${MODULE_DEPS_FIXTURES}/clean/allowlist.txt -P ${MODULE_DEPS_SCRIPT})
set_tests_properties(module_deps_check_no_layer PROPERTIES PASS_REGULAR_EXPRESSION "the module 'ghost' .* has no layer")
list(APPEND MODULE_DEPS_TESTS module_deps_check_no_layer)
# Exit code: a violation must make the gate fail, not only print.
add_test(NAME module_deps_check_fails COMMAND ${CMAKE_COMMAND} -DROOT=${MODULE_DEPS_FIXTURES}/upward -DMODULES=${MODULE_DEPS_FIXTURES}/modules.txt
         -DALLOWLIST=${MODULE_DEPS_FIXTURES}/upward/allowlist.txt -P ${MODULE_DEPS_SCRIPT})
set_tests_properties(module_deps_check_fails PROPERTIES WILL_FAIL TRUE)
list(APPEND MODULE_DEPS_TESTS module_deps_check_fails)

engine_label_tests(${MODULE_DEPS_TESTS})
