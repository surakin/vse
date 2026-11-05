# Shaders are written in Slang, compiled offline to SPIR-V and embedded as committed *.spv.inc files (PLAN.md
# assumption #18). The compiler is an optional dev-time tool: without it the committed files are used as they are.
# `cmake --build build --target shaders` recompiles and regenerates them; the `shaders_check` ctest recompiles and
# compares when `slangc` is installed. See README.md.
#
# Per shader: slangc writes SPIR-V and its reflection JSON, spirv-val checks the SPIR-V, and tools/shader_cook turns both
# into the committed <name>.spv.inc (the words) and <name>.refl.inc (the reflection blob).
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

find_program(SLANGC slangc)
find_program(SPIRV_VAL spirv-val)

if(SLANGC)
  execute_process(COMMAND ${SLANGC} -v OUTPUT_VARIABLE SLANGC_VERSION_TEXT ERROR_VARIABLE SLANGC_VERSION_ERR
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "\n.*" "" SLANGC_VERSION "${SLANGC_VERSION_TEXT}${SLANGC_VERSION_ERR}")
  set(SHADER_WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv)
  file(MAKE_DIRECTORY ${SHADER_WORK_DIR})

  set(SHADER_INCS "")
  foreach(shader ${ENGINE_SHADERS})
    shader_base_symbol(${shader} symbol)
    shader_stage(${shader} stage)
    slang_compile_args(${stage} slang_args)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.slang)
    set(spv ${SHADER_WORK_DIR}/${shader}.spv)
    set(json ${SHADER_WORK_DIR}/${shader}.json)
    set(inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.spv.inc)
    set(refl_inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.refl.inc)
    set(validate_cmd ${CMAKE_COMMAND} -E true)
    if(SPIRV_VAL)
      set(validate_cmd ${SPIRV_VAL} --target-env vulkan1.3 ${spv})
    endif()
    add_custom_command(
      OUTPUT ${inc} ${refl_inc}
      COMMAND ${SLANGC} ${source} ${slang_args} -o ${spv} -reflection-json ${json}
      COMMAND ${validate_cmd}
      COMMAND shader_cook --name ${symbol} --source ${shader}.slang --compiler "slangc ${SLANGC_VERSION}" --spv ${spv}
              --reflection ${json} --spv-out ${inc} --refl-out ${refl_inc}
      DEPENDS ${source} ${CMAKE_CURRENT_SOURCE_DIR}/names.cmake shader_cook
      COMMENT "Compiling ${shader}.slang"
      VERBATIM)
    list(APPEND SHADER_INCS ${inc} ${refl_inc})
  endforeach()
  add_custom_target(shaders DEPENDS ${SHADER_INCS})

  add_test(NAME shaders_check
    COMMAND ${CMAKE_COMMAND}
      -DSLANGC=${SLANGC} -DSPIRV_VAL=${SPIRV_VAL} -DSHADER_COOK=$<TARGET_FILE:shader_cook>
      -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DWORK_DIR=${SHADER_WORK_DIR}/check
      "-DSHADERS=${ENGINE_SHADERS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  set_tests_properties(shaders_check PROPERTIES LABELS "shaders" SKIP_REGULAR_EXPRESSION "SKIP:")
else()
  message(STATUS "slangc not found: using the committed shaders/*.spv.inc and *.refl.inc files (the `shaders` target is unavailable)")
endif()
