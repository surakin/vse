# Phase 3 Day 58: GLSL shaders compiled offline to SPIR-V and embedded as committed *.spv.inc files
# (PLAN.md assumption #18). The shader compiler is an optional dev-time tool: without it the committed files are
# used as they are. `cmake --build build --target shaders` recompiles and regenerates them; the `shaders_check`
# ctest recompiles and compares when the tools are installed.
set(ENGINE_SHADERS
  triangle.vert
  triangle.frag
  fullscreen.vert
  bindings_test.frag
  fill.comp
  saxpy.comp
  gradient.comp
  cube.vert
  cube.frag
  textured.vert
  textured.frag
  lit.vert
  lit.frag
)

include(${CMAKE_CURRENT_SOURCE_DIR}/names.cmake)

find_program(GLSLC glslc)
find_program(SPIRV_VAL spirv-val)

if(GLSLC)
  execute_process(COMMAND ${GLSLC} --version OUTPUT_VARIABLE GLSLC_VERSION_TEXT OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "\n.*" "" GLSLC_VERSION "${GLSLC_VERSION_TEXT}")
  set(SHADER_WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv)
  file(MAKE_DIRECTORY ${SHADER_WORK_DIR})

  set(SHADER_COMMANDS "")
  foreach(shader ${ENGINE_SHADERS})
    shader_symbol(${shader} symbol)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${shader})
    set(spv ${SHADER_WORK_DIR}/${shader}.spv)
    set(inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.spv.inc)
    set(validate_cmd ${CMAKE_COMMAND} -E true)
    if(SPIRV_VAL)
      set(validate_cmd ${SPIRV_VAL} --target-env vulkan1.3 ${spv})
    endif()
    add_custom_command(
      OUTPUT ${inc}
      COMMAND ${GLSLC} --target-env=vulkan1.3 -O ${source} -o ${spv}
      COMMAND ${validate_cmd}
      COMMAND spv_embed --name ${symbol} --source ${shader} --compiler "${GLSLC_VERSION}" --out ${inc} ${spv}
      DEPENDS ${source} spv_embed
      COMMENT "Compiling ${shader}"
      VERBATIM)
    list(APPEND SHADER_INCS ${inc})
  endforeach()
  add_custom_target(shaders DEPENDS ${SHADER_INCS})

  add_test(NAME shaders_check
    COMMAND ${CMAKE_COMMAND}
      -DGLSLC=${GLSLC} -DSPIRV_VAL=${SPIRV_VAL} -DSPV_EMBED=$<TARGET_FILE:spv_embed>
      -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DWORK_DIR=${SHADER_WORK_DIR}/check
      "-DSHADERS=${ENGINE_SHADERS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  set_tests_properties(shaders_check PROPERTIES LABELS "shaders" SKIP_REGULAR_EXPRESSION "SKIP:")
else()
  message(STATUS "glslc not found: using the committed shaders/*.spv.inc files (the `shaders` target is unavailable)")
endif()
