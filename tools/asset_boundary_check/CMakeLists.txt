# Asset boundary gate: see check.cmake. Registered as ctest cases (label: asset).
set(ASSET_BOUNDARY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(ASSET_BOUNDARY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(asset_boundary_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${ASSET_BOUNDARY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS asset)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME asset_boundary_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${ASSET_BOUNDARY_SCRIPT})
set_tests_properties(asset_boundary_check PROPERTIES LABELS asset)

# Planted violations (a runtime include of the importer and of a tool, a link, a game and a tool that include the importer, a decoder header) are
# reported, each by line.
asset_boundary_test(asset_boundary_check_planted ${ASSET_BOUNDARY_FIXTURES}/bad ${ASSET_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(asset_boundary_check_planted PROPERTIES WILL_FAIL TRUE)
asset_boundary_test(asset_boundary_check_planted_count ${ASSET_BOUNDARY_FIXTURES}/bad ${ASSET_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(asset_boundary_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/renderer/CMakeLists.txt:2:.*engine/renderer/leak.cpp:1:.*engine/renderer/leak.cpp:2:.*engine/renderer/leak.cpp:3:.*game/app/main.cpp:1:.*tools/other/tool.cpp:1:.*7 problem")

# A clean tree passes: the importer includes the decoders, asset_mint includes the importer, an allow-listed game does, comments are not violations.
asset_boundary_test(asset_boundary_check_clean ${ASSET_BOUNDARY_FIXTURES}/good ${ASSET_BOUNDARY_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
asset_boundary_test(asset_boundary_check_stale ${ASSET_BOUNDARY_FIXTURES}/good ${ASSET_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(asset_boundary_check_stale PROPERTIES WILL_FAIL TRUE)
asset_boundary_test(asset_boundary_check_no_reason ${ASSET_BOUNDARY_FIXTURES}/good ${ASSET_BOUNDARY_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(asset_boundary_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(asset_boundary_check asset_boundary_check_planted asset_boundary_check_planted_count asset_boundary_check_clean
                   asset_boundary_check_stale asset_boundary_check_no_reason)
