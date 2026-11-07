# RHI boundary gate: see check.cmake. Registered as ctest cases (label: boundary).
set(RHI_BOUNDARY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(RHI_BOUNDARY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(rhi_boundary_test name root allow)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allow} -P ${RHI_BOUNDARY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS boundary)
endfunction()

# The real tree must be clean.
rhi_boundary_test(rhi_boundary_check ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/allowlist.txt)

# Planted violations: all three kinds must be reported (include, identifier, backend include in an agnostic header).
rhi_boundary_test(rhi_boundary_check_planted ${RHI_BOUNDARY_FIXTURES}/bad ${RHI_BOUNDARY_FIXTURES}/bad/allow.txt)
set_tests_properties(rhi_boundary_check_planted PROPERTIES WILL_FAIL TRUE)
rhi_boundary_test(rhi_boundary_check_planted_count ${RHI_BOUNDARY_FIXTURES}/bad ${RHI_BOUNDARY_FIXTURES}/bad/allow.txt)
set_tests_properties(rhi_boundary_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/foundation/x.cpp:1.*engine/platform/windows/vulkan_in_win32.cpp:3.*engine/renderer/leak.cpp:1.*engine/rhi/device.h:2.*engine/rhi/vulkan/dx_in_vulkan.cpp:1.*game/after_comment.cpp:1.*game/dx_leak.cpp:1.*game/dx_leak.cpp:2.*game/leak.cpp:1.*game/leak.cpp:2.*tests/renderer/t.cpp:1.*11 problem"
  WILL_FAIL FALSE)

# A clean tree (comments and PerspectiveVulkan-style names, Vulkan inside engine/rhi/vulkan, Win32 virtual keys in engine/platform/windows) passes.
rhi_boundary_test(rhi_boundary_check_clean ${RHI_BOUNDARY_FIXTURES}/good ${RHI_BOUNDARY_FIXTURES}/good/allow.txt)

# Allow-list hygiene: stale and reasonless entries fail.
rhi_boundary_test(rhi_boundary_check_stale ${RHI_BOUNDARY_FIXTURES}/good ${RHI_BOUNDARY_FIXTURES}/bad/allow_stale.txt)
set_tests_properties(rhi_boundary_check_stale PROPERTIES WILL_FAIL TRUE)
rhi_boundary_test(rhi_boundary_check_noreason ${RHI_BOUNDARY_FIXTURES}/good ${RHI_BOUNDARY_FIXTURES}/good/allow_noreason.txt)
set_tests_properties(rhi_boundary_check_noreason PROPERTIES WILL_FAIL TRUE)

# Phase labels (tests/phases.txt).
engine_label_tests(rhi_boundary_check rhi_boundary_check_planted rhi_boundary_check_planted_count rhi_boundary_check_clean
  rhi_boundary_check_stale rhi_boundary_check_noreason)
