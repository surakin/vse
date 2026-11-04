# Recompiles every shader and compares the result with the committed .spv.inc file. Run through ctest as
# `shaders_check`; arguments: GLSLC and SLANGC (each may be empty), SPIRV_VAL (optional), SPV_EMBED, SHADER_DIR,
# WORK_DIR, GLSL_SHADERS and SLANG_SHADERS (;-lists). A language whose compiler is missing is skipped, and so is a shader
# whose recorded compiler version differs (reported as a note). Prints "SKIP:"
# (the test's SKIP_REGULAR_EXPRESSION) only if no shader could be checked at all.
include(${SHADER_DIR}/names.cmake)
file(MAKE_DIRECTORY ${WORK_DIR}/slang)

function(compiler_version compiler out)
  execute_process(COMMAND ${compiler} ${ARGN} OUTPUT_VARIABLE text ERROR_VARIABLE err_text
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "\n.*" "" first "${text}${err_text}")
  set(${out} "${first}" PARENT_SCOPE)
endfunction()

# check_shader(<label> <compiler text, e.g. "glslc 2026.4"> <committed .inc> <shader name> <source label> <spv> <compile args...>)
function(check_shader label compiler_text committed shader source_label spv)
  file(STRINGS ${committed} recorded REGEX "^// compiler: ")
  if(NOT recorded)
    message(FATAL_ERROR "${label}.spv.inc has no '// compiler: <name> <version>' line: regenerate it with the `shaders` target")
  endif()
  if(NOT recorded STREQUAL "// compiler: ${compiler_text}")
    message("note: ${label}: committed with '${recorded}', installed is '${compiler_text}'; not checked")
    set_property(GLOBAL APPEND PROPERTY skipped_shaders ${label})
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY checked_shaders ${label})
  execute_process(COMMAND ${ARGN} -o ${spv} RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the compiler failed on ${label}: ${err}")
  endif()
  if(SPIRV_VAL)
    execute_process(COMMAND ${SPIRV_VAL} --target-env vulkan1.3 ${spv} RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "spirv-val rejected ${label}: ${err}")
    endif()
  endif()
  shader_symbol(${shader} symbol)
  execute_process(COMMAND ${SPV_EMBED} --name ${symbol} --source ${source_label} --compiler ${compiler_text}
                          --out ${spv}.inc ${spv} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "spv_embed failed on ${label}")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${committed} ${spv}.inc RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "${label}.spv.inc is out of date: run `cmake --build build --target shaders`")
  endif()
endfunction()

if(GLSLC)
  compiler_version(${GLSLC} glslc_version --version)
  foreach(shader ${GLSL_SHADERS})
    check_shader(${shader} "glslc ${glslc_version}" ${SHADER_DIR}/${shader}.spv.inc ${shader} ${shader}
                 ${WORK_DIR}/${shader}.spv ${GLSLC} --target-env=vulkan1.3 -O ${SHADER_DIR}/${shader})
  endforeach()
endif()
if(SLANGC)
  compiler_version(${SLANGC} slangc_version -v)
  foreach(shader ${SLANG_SHADERS})
    shader_stage(${shader} stage)
    slang_compile_args(${stage} slang_args)
    check_shader(slang/${shader} "slangc ${slangc_version}" ${SHADER_DIR}/slang/${shader}.spv.inc ${shader}
                 slang/${shader}.slang ${WORK_DIR}/slang/${shader}.spv
                 ${SLANGC} ${SHADER_DIR}/slang/${shader}.slang ${slang_args})
  endforeach()
endif()

# Report SKIP (the test's SKIP_REGULAR_EXPRESSION) only when nothing at all was compared: a partial check is a pass.
get_property(checked GLOBAL PROPERTY checked_shaders)
if(NOT checked)
  message("SKIP: no shader was checked (no compiler installed, or every installed version differs from the recorded one)")
endif()
