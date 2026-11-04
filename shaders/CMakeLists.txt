# Shaders are compiled offline to SPIR-V and embedded as committed *.spv.inc files (PLAN.md assumption #18). The
# compilers are optional dev-time tools: without them the committed files are used as they are.
# `cmake --build build --target shaders` recompiles and regenerates them; the `shaders_check` ctest recompiles and
# compares when the tools are installed.
#
# Two source languages coexist during the Slang migration (Phase 4 Days 69-70): GLSL (shaders/*.vert|frag|comp,
# output shaders/<name>.spv.inc) and Slang (shaders/slang/<name>.slang, output shaders/slang/<name>.spv.inc). The
# ENGINE_SHADER_LANG option picks which committed outputs the engine and tests include; shaders without a Slang
# port fall back to the GLSL output. See README.md.
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
# The subset ported to Slang so far (each has shaders/slang/<name>.slang).
set(ENGINE_SLANG_SHADERS
  triangle.vert
  triangle.frag
  fullscreen.vert
  bindings_test.frag
  fill.comp
  saxpy.comp
  gradient.comp
)

set(ENGINE_SHADER_LANG "glsl" CACHE STRING "Shader outputs the engine includes: glsl or slang (slang falls back to glsl for shaders not ported yet)")
set_property(CACHE ENGINE_SHADER_LANG PROPERTY STRINGS glsl slang)
if(NOT ENGINE_SHADER_LANG STREQUAL "glsl" AND NOT ENGINE_SHADER_LANG STREQUAL "slang")
  message(FATAL_ERROR "ENGINE_SHADER_LANG must be glsl or slang, not '${ENGINE_SHADER_LANG}'")
endif()
# Include directories for `#include "<name>.spv.inc"`, in lookup order. Visible only to directories added after
# `shaders` (see the top-level CMakeLists.txt): tests, games and tools use it; engine libraries do not include shaders.
if(ENGINE_SHADER_LANG STREQUAL "slang")
  set(ENGINE_SHADER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/shaders/slang ${CMAKE_SOURCE_DIR}/shaders PARENT_SCOPE)
else()
  set(ENGINE_SHADER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/shaders PARENT_SCOPE)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/names.cmake)

find_program(GLSLC glslc)
find_program(SLANGC slangc)
find_program(SPIRV_VAL spirv-val)

set(SHADER_WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SHADER_WORK_DIR})

# The recorded compiler text, e.g. "glslc 2026.4" or "slangc v2026.18.2": written to every generated file.
if(GLSLC)
  execute_process(COMMAND ${GLSLC} --version OUTPUT_VARIABLE GLSLC_VERSION_TEXT OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "\n.*" "" GLSLC_VERSION "${GLSLC_VERSION_TEXT}")
endif()
if(SLANGC)
  execute_process(COMMAND ${SLANGC} -v OUTPUT_VARIABLE SLANGC_VERSION_TEXT ERROR_VARIABLE SLANGC_VERSION_ERR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "\n.*" "" SLANGC_VERSION "${SLANGC_VERSION_TEXT}${SLANGC_VERSION_ERR}")
endif()

set(SHADER_INCS "")
if(GLSLC)
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
      COMMAND spv_embed --name ${symbol} --source ${shader} --compiler "glslc ${GLSLC_VERSION}" --out ${inc} ${spv}
      DEPENDS ${source} ${CMAKE_CURRENT_SOURCE_DIR}/names.cmake spv_embed
      COMMENT "Compiling ${shader} (glslc)"
      VERBATIM)
    list(APPEND SHADER_INCS ${inc})
  endforeach()
else()
  message(STATUS "glslc not found: using the committed shaders/*.spv.inc files (GLSL shaders cannot be rebuilt)")
endif()

if(SLANGC)
  file(MAKE_DIRECTORY ${SHADER_WORK_DIR}/slang)
  foreach(shader ${ENGINE_SLANG_SHADERS})
    shader_symbol(${shader} symbol)
    shader_stage(${shader} stage)
    slang_compile_args(${stage} slang_args)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/slang/${shader}.slang)
    set(spv ${SHADER_WORK_DIR}/slang/${shader}.spv)
    set(inc ${CMAKE_CURRENT_SOURCE_DIR}/slang/${shader}.spv.inc)
    set(validate_cmd ${CMAKE_COMMAND} -E true)
    if(SPIRV_VAL)
      set(validate_cmd ${SPIRV_VAL} --target-env vulkan1.3 ${spv})
    endif()
    add_custom_command(
      OUTPUT ${inc}
      COMMAND ${SLANGC} ${source} ${slang_args} -o ${spv}
      COMMAND ${validate_cmd}
      COMMAND spv_embed --name ${symbol} --source slang/${shader}.slang --compiler "slangc ${SLANGC_VERSION}" --out ${inc} ${spv}
      DEPENDS ${source} ${CMAKE_CURRENT_SOURCE_DIR}/names.cmake spv_embed
      COMMENT "Compiling ${shader}.slang (slangc)"
      VERBATIM)
    list(APPEND SHADER_INCS ${inc})
  endforeach()
else()
  message(STATUS "slangc not found: using the committed shaders/slang/*.spv.inc files (Slang shaders cannot be rebuilt)")
endif()

if(SHADER_INCS)
  add_custom_target(shaders DEPENDS ${SHADER_INCS})
  add_test(NAME shaders_check
    COMMAND ${CMAKE_COMMAND}
      -DGLSLC=${GLSLC} -DSLANGC=${SLANGC} -DSPIRV_VAL=${SPIRV_VAL} -DSPV_EMBED=$<TARGET_FILE:spv_embed>
      -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DWORK_DIR=${SHADER_WORK_DIR}/check
      "-DGLSL_SHADERS=${ENGINE_SHADERS}" "-DSLANG_SHADERS=${ENGINE_SLANG_SHADERS}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  set_tests_properties(shaders_check PROPERTIES LABELS "shaders" SKIP_REGULAR_EXPRESSION "SKIP:")
endif()
