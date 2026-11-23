# Shaders are written in Slang, compiled offline to SPIR-V and DXIL and embedded as committed *.spv.inc and *.dxil.inc files
# (PLAN.md assumption #18). The compilers are optional dev-time tools: without them the committed files are used as they are.
# `cmake --build build --target shaders` recompiles and regenerates them; the `shaders_check` ctest recompiles and
# compares when `slangc` is installed. See README.md.
#
# Per shader: slangc writes SPIR-V and its reflection JSON, spirv-val checks the SPIR-V, and tools/shader_mint turns both
# into the committed <name>.spv.inc (the words) and <name>.refl.inc (the reflection blob). A shader with a
# <name>.perm manifest next to it is a permuted shader: shader_mint runs slangc itself once per manifest key and also
# writes <name>.perm.inc (the key -> module table); the plain pair is the key with no option on.
set(ENGINE_SHADERS
  triangle.vert
  triangle.frag
  fullscreen.vert
  bindings_test.frag
  fill.comp
  cube_sample.comp
  saxpy.comp
  gradient.comp
  cube.vert
  cube.frag
  textured.vert
  textured.frag
  lit.vert
  lit.frag
  permute_test.frag
  material_test.vert
  material_test.frag
  graph_sample.frag
  gbuffer.vert
  gbuffer.frag
  gbuffer_debug.frag
  lighting.frag
  point_light.vert
  point_light.frag
  tonemap.frag
  sky.frag
  sky_cube.frag
  terrain.vert
  terrain.frag
  mipgen.comp
  morph.comp
  text.vert
  text.frag
  ui.vert
  ui.frag
)

include(${CMAKE_CURRENT_SOURCE_DIR}/names.cmake)

# Slang sources that shaders include (not compiled alone); a change to one rebuilds every shader.
file(GLOB SHADER_COMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_common.slang)

# slangc: ENGINE_SLANGC (a cache or environment variable), then the locations BUILD.md records (the Windows PC's C:/Tools, the Mac's
# ~/tools; find_program searches HINTS before PATH), then PATH. A program that answers -v with something other than a Slang version is refused
# and the committed outputs are used: on the Mac, kitty (the terminal) puts an unrelated `slangc` on PATH (Phase 21 Day 284).
# DXIL (Phase 13 Day 163): the directory with dxcompiler.dll and dxil.dll, from ENGINE_DXC (a dxc executable or its directory), else
# the Windows SDK the toolchain found (ENGINE_SDK_BIN); never the first dxc on PATH, which may be the Vulkan SDK's older build.
if(NOT ENGINE_SLANGC AND DEFINED ENV{ENGINE_SLANGC})
  set(ENGINE_SLANGC "$ENV{ENGINE_SLANGC}")
endif()
set(ENGINE_SLANGC "${ENGINE_SLANGC}" CACHE FILEPATH "slangc to mint shaders with (empty: PATH, then the recorded install location)")
if(ENGINE_SLANGC)
  set(SLANGC "${ENGINE_SLANGC}")
else()
  find_program(SLANGC slangc HINTS "C:/Tools/slang-2026.18.2/bin" "$ENV{HOME}/tools/slang-2026.18.2/bin")
endif()
if(ENGINE_COMPILE_ONLY)
  # A compile-only build (Phase 21 Day 311) cannot run the minters it builds: the committed outputs are used as they are.
  message(STATUS "Compile-only build: shaders are not minted; the committed outputs are used")
  set(SLANGC "")
endif()
if(SLANGC)
  # The program may be an unrelated one (above): it gets no input and 10 seconds, so one that waits cannot stop the configure.
  if(CMAKE_HOST_WIN32)
    set(_slangc_null NUL)
  else()
    set(_slangc_null /dev/null)
  endif()
  execute_process(COMMAND ${SLANGC} -v OUTPUT_VARIABLE SLANGC_VERSION_TEXT ERROR_VARIABLE SLANGC_VERSION_ERR INPUT_FILE ${_slangc_null}
                  TIMEOUT 10 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "[\r\n].*" "" SLANGC_VERSION "${SLANGC_VERSION_TEXT}${SLANGC_VERSION_ERR}")  # the first line, CRLF or LF
  # A Slang version is a year, a release and an optional patch: "v2026.18.2" (Linux), "2026.18.2" (Windows, macOS).
  if(NOT SLANGC_VERSION MATCHES "^v?20[0-9][0-9]\\.[0-9]+(\\.[0-9]+)?$")
    message(WARNING "${SLANGC} is not the Slang compiler (it answered -v with '${SLANGC_VERSION}'); the committed shader outputs are used as "
                    "they are. Set ENGINE_SLANGC to the slangc of BUILD.md.")
    set(SLANGC "")
  endif()
endif()
find_program(SPIRV_VAL spirv-val)
if(NOT ENGINE_DXC AND DEFINED ENV{ENGINE_DXC})
  set(ENGINE_DXC "$ENV{ENGINE_DXC}")
endif()
set(ENGINE_DXC "${ENGINE_DXC}" CACHE PATH "dxc (or its directory) whose dxcompiler.dll and dxil.dll mint DXIL (empty: the Windows SDK's)")
set(DXIL_DIR "")
if(ENGINE_DXC)
  if(IS_DIRECTORY "${ENGINE_DXC}")
    set(DXIL_DIR "${ENGINE_DXC}")
  else()
    get_filename_component(DXIL_DIR "${ENGINE_DXC}" DIRECTORY)
  endif()
elseif(WIN32 AND ENGINE_SDK_BIN AND EXISTS "${ENGINE_SDK_BIN}/dxcompiler.dll" AND EXISTS "${ENGINE_SDK_BIN}/dxil.dll")
  set(DXIL_DIR "${ENGINE_SDK_BIN}")
endif()
set(DXC_VERSION "")
if(DXIL_DIR)
  find_program(DXC NAMES dxc PATHS "${DXIL_DIR}" NO_DEFAULT_PATH NO_CACHE)
  if(DXC)
    execute_process(COMMAND ${DXC} --version OUTPUT_VARIABLE _dxc_version ERROR_VARIABLE _dxc_version_err)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" DXC_VERSION "${_dxc_version}${_dxc_version_err}")
  endif()
  if(NOT DXC OR NOT DXC_VERSION)
    message(STATUS "DXIL: no usable dxc in ${DXIL_DIR}; the committed shaders/*.dxil.inc are used as they are")
    set(DXIL_DIR "")
  else()
    message(STATUS "DXIL: minting with dxcompiler ${DXC_VERSION} from ${DXIL_DIR}")
  endif()
endif()

if(SLANGC)
  # The Linux build prints "v2026.18.2", the Windows build "2026.18.2": recorded as "v<version>" on both, so the minted files match.
  string(REGEX REPLACE "^v" "" SLANGC_VERSION "${SLANGC_VERSION}")
  set(SLANGC_VERSION "v${SLANGC_VERSION}")
  set(SHADER_WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv)
  file(MAKE_DIRECTORY ${SHADER_WORK_DIR})

  set(DXIL_COMPILER "slangc ${SLANGC_VERSION}, dxcompiler ${DXC_VERSION}")
  # Metal (Phase 21 Day 299): slangc's Metal build on every platform (its reflection holds the Metal slots), and on a Mac
  # tools/metal_mint, which turns it into the committed <name>.metallib.inc.
  set(METAL_MINT_TARGET "")
  if(TARGET metal_mint)
    set(METAL_MINT_TARGET metal_mint)
    message(STATUS "Metal: minting .metallib files with tools/metal_mint (the driver's compiler of this macOS build)")
  endif()
  set(SHADER_INCS "")
  foreach(shader ${ENGINE_SHADERS})
    shader_base_symbol(${shader} symbol)
    shader_stage(${shader} stage)
    slang_compile_args(${stage} slang_args)
    slang_metal_args(${stage} metal_args)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.slang)
    set(metal_src ${SHADER_WORK_DIR}/${shader}.metal)
    set(metal_json ${SHADER_WORK_DIR}/${shader}.metal.json)
    set(metallib ${SHADER_WORK_DIR}/${shader}.metallib)
    set(metallib_info ${SHADER_WORK_DIR}/${shader}.metallib.info)
    set(metallib_inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.metallib.inc)
    set(metal_outputs "")
    set(metal_mint_cmd ${CMAKE_COMMAND} -E true)
    set(metal_mint_args --metal-reflection ${metal_json} --metallib-out ${metallib_inc})  # kept, or a placeholder off a Mac
    if(METAL_MINT_TARGET)
      set(metal_outputs ${metallib_inc})
      set(metal_mint_cmd $<TARGET_FILE:metal_mint> --source ${metal_src} --stage ${stage} --out ${metallib} --info ${metallib_info})
      list(APPEND metal_mint_args --metallib ${metallib} --metallib-info ${metallib_info})
    endif()
    set(spv ${SHADER_WORK_DIR}/${shader}.spv)
    set(json ${SHADER_WORK_DIR}/${shader}.json)
    set(inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.spv.inc)
    set(refl_inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.refl.inc)
    set(dxil_inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.dxil.inc)
    set(dxil_outputs "")
    set(dxil_mint_args "")
    set(dxil_compile_cmd ${CMAKE_COMMAND} -E true)
    if(DXIL_DIR)
      slang_dxil_args(${stage} "${DXIL_DIR}" dxil_args)
      set(dxil_outputs ${dxil_inc})
    endif()
    set(validate_cmd ${CMAKE_COMMAND} -E true)
    if(SPIRV_VAL)
      set(validate_cmd ${SPIRV_VAL} --target-env vulkan1.3 ${spv})
    endif()
    set(manifest ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.perm)
    if(EXISTS ${manifest})
      set(perm_inc ${CMAKE_CURRENT_SOURCE_DIR}/${shader}.perm.inc)
      set(perm_args "")
      foreach(arg ${slang_args})
        list(APPEND perm_args --slang-arg ${arg})
      endforeach()
      foreach(arg ${metal_args})
        list(APPEND perm_args --metal-arg ${arg})
      endforeach()
      list(APPEND perm_args --metallib-out ${metallib_inc})  # kept, or a placeholder off a Mac
      if(METAL_MINT_TARGET)
        list(APPEND perm_args --metal-mint $<TARGET_FILE:metal_mint>)
      endif()
      if(SPIRV_VAL)
        list(APPEND perm_args --spirv-val ${SPIRV_VAL})
      endif()
      if(DXIL_DIR)
        foreach(arg ${dxil_args})
          list(APPEND perm_args --dxil-arg ${arg})
        endforeach()
        list(APPEND perm_args --dxil-out ${dxil_inc} --dxil-compiler "${DXIL_COMPILER}")
      endif()
      add_custom_command(
        OUTPUT ${inc} ${refl_inc} ${perm_inc} ${dxil_outputs} ${metal_outputs}
        COMMAND $<TARGET_FILE:shader_mint> --permute --name ${symbol} --source ${shader}.slang --source-path ${source}
                --manifest ${manifest} --compiler "slangc ${SLANGC_VERSION}" --slangc ${SLANGC} ${perm_args}
                --work-dir ${SHADER_WORK_DIR} --spv-out ${inc} --refl-out ${refl_inc} --perm-out ${perm_inc}
        DEPENDS ${source} ${SHADER_COMMON_SOURCES} ${manifest} ${CMAKE_CURRENT_SOURCE_DIR}/names.cmake shader_mint ${METAL_MINT_TARGET}
        COMMENT "Compiling ${shader}.slang (permuted)"
        VERBATIM)
      list(APPEND SHADER_INCS ${inc} ${refl_inc} ${perm_inc} ${dxil_outputs} ${metal_outputs})
      continue()
    endif()
    if(DXIL_DIR)
      set(dxil ${SHADER_WORK_DIR}/${shader}.dxil)
      set(dxil_json ${SHADER_WORK_DIR}/${shader}.dxil.json)
      set(dxil_compile_cmd ${SLANGC} ${source} ${dxil_args} -o ${dxil} -reflection-json ${dxil_json})
      set(dxil_mint_args --dxil ${dxil} --dxil-reflection ${dxil_json} --dxil-out ${dxil_inc} --dxil-compiler "${DXIL_COMPILER}")
    endif()
    add_custom_command(
      OUTPUT ${inc} ${refl_inc} ${dxil_outputs} ${metal_outputs}
      COMMAND ${SLANGC} ${source} ${slang_args} -o ${spv} -reflection-json ${json}
      COMMAND ${validate_cmd}
      COMMAND ${dxil_compile_cmd}
      COMMAND ${SLANGC} ${source} ${metal_args} -o ${metal_src} -reflection-json ${metal_json}
      COMMAND ${metal_mint_cmd}
      COMMAND $<TARGET_FILE:shader_mint> --name ${symbol} --source ${shader}.slang --compiler "slangc ${SLANGC_VERSION}" --spv ${spv}
              --reflection ${json} --spv-out ${inc} --refl-out ${refl_inc} ${dxil_mint_args} ${metal_mint_args}
      DEPENDS ${source} ${SHADER_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/names.cmake shader_mint ${METAL_MINT_TARGET}
      COMMENT "Compiling ${shader}.slang"
      VERBATIM)
    list(APPEND SHADER_INCS ${inc} ${refl_inc} ${dxil_outputs} ${metal_outputs})
  endforeach()
  add_custom_target(shaders DEPENDS ${SHADER_INCS})

  set(_metal_mint_define "")
  if(METAL_MINT_TARGET)
    set(_metal_mint_define "-DMETAL_MINT=$<TARGET_FILE:metal_mint>")
  endif()
  add_test(NAME shaders_check
    COMMAND ${CMAKE_COMMAND}
      -DSLANGC=${SLANGC} -DSPIRV_VAL=${SPIRV_VAL} -DSHADER_MINT=$<TARGET_FILE:shader_mint>
      -DDXIL_DIR=${DXIL_DIR} "-DDXIL_COMPILER=${DXIL_COMPILER}" ${_metal_mint_define}
      -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DWORK_DIR=${SHADER_WORK_DIR}/check
      "-DSHADERS=${ENGINE_SHADERS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  set_tests_properties(shaders_check PROPERTIES LABELS "shaders" SKIP_REGULAR_EXPRESSION "SKIP:")

  # Every shader (and permutation key) must also compile to HLSL and Metal Shading Language; `dxc` adds a DXIL check of the HLSL,
  # required to pass on Windows (the SDK's dxc) and a note elsewhere (the Linux package aborts; KNOWN-ISSUES.md).
  if(NOT DXC)
    find_program(DXC dxc)
  endif()
  set(DXC_REQUIRED OFF)
  if(WIN32 AND DXIL_DIR)
    set(DXC_REQUIRED ON)
  endif()
  add_test(NAME shaders_portability
    COMMAND ${CMAKE_COMMAND}
      -DSLANGC=${SLANGC} -DDXC=${DXC} -DDXC_REQUIRED=${DXC_REQUIRED} ${_metal_mint_define} -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DWORK_DIR=${SHADER_WORK_DIR}/portability
      "-DSHADERS=${ENGINE_SHADERS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/portability.cmake)
  set_tests_properties(shaders_portability PROPERTIES LABELS "shaders")
  engine_label_tests(shaders_check shaders_portability)
else()
  message(STATUS "slangc not found: using the committed shaders/*.spv.inc and *.refl.inc files (the `shaders` target is unavailable)")
endif()
