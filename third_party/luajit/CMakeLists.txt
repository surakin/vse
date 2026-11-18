# LuaJIT (the pinned commit in VENDORED.md), vendored: PLAN.md assumption #13. One static C library `luajit`, built from source with no `make`: the host
# tools of LuaJIT's own build (minilua, DynASM, buildvm) are built and run here, and the files they generate go to the build tree. Reached only through
# the script module's LuaJIT backend (engine/script/luajit/, gate luajit_boundary_check). The sanitizer flavors link this same library: LuaJIT's
# assembler core and generated code are not instrumented (KNOWN-ISSUES.md). Linux x86-64 today; Windows (msvcbuild.bat's steps: DynASM with -D WIN,
# buildvm -m peobj) and macOS (buildvm -m machasm, MACOSX_DEPLOYMENT_TARGET) arrive on Days 473 and 474.
cmake_minimum_required(VERSION 3.20)
enable_language(ASM)

option(ENGINE_LUAJIT_JIT "Build LuaJIT with its JIT compiler (OFF: interpreter only, LUAJIT_DISABLE_JIT; Phase 27 Day 470)" ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  message(STATUS "third_party/luajit: builds on Linux x86-64 only so far (Windows: Phase 27 Day 473, macOS: Day 474); no `luajit` target here")
  return()
endif()

set(LJ_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LJ_DYNASM ${CMAKE_CURRENT_SOURCE_DIR}/dynasm)
set(LJ_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${LJ_GEN}/host)

# Host tools: third-party C, warnings off.
set(LJ_HOST_DEFS LUAJIT_TARGET=LUAJIT_ARCH_x64 LJ_ARCH_HASFPU=1 LJ_ABI_SOFTFP=0)
add_executable(luajit_minilua ${LJ_SRC}/host/minilua.c)
target_compile_definitions(luajit_minilua PRIVATE ${LJ_HOST_DEFS})
target_compile_options(luajit_minilua PRIVATE -w -O1)
target_link_libraries(luajit_minilua PRIVATE m)
set_target_properties(luajit_minilua PROPERTIES UNITY_BUILD OFF RUNTIME_OUTPUT_DIRECTORY ${LJ_GEN})

# DynASM turns vm_x64.dasc into buildvm_arch.h. The flags are the ones LuaJIT's Makefile derives for x86-64 with 64-bit GC objects (LJ_GC64, the default).
set(LJ_DASM_FLAGS -D ENDIAN_LE -D P64 -D FFI -D FPU -D HFABI -D VER=)
if(ENGINE_LUAJIT_JIT)
  list(APPEND LJ_DASM_FLAGS -D JIT)
endif()
add_custom_command(OUTPUT ${LJ_GEN}/host/buildvm_arch.h
  COMMAND luajit_minilua ${LJ_DYNASM}/dynasm.lua ${LJ_DASM_FLAGS} -o ${LJ_GEN}/host/buildvm_arch.h ${LJ_SRC}/vm_x64.dasc
  DEPENDS luajit_minilua ${LJ_SRC}/vm_x64.dasc ${LJ_SRC}/lj_arch.h ${LJ_SRC}/lua.h ${LJ_SRC}/luaconf.h ${LJ_DYNASM}/dynasm.lua ${LJ_DYNASM}/dasm_x86.lua ${LJ_DYNASM}/dasm_x64.lua
  COMMENT "luajit: DynASM vm_x64.dasc" VERBATIM)

# luajit.h carries the rolling release version (the commit time of the pin, luajit_relver.txt).
add_custom_command(OUTPUT ${LJ_GEN}/luajit.h
  COMMAND luajit_minilua ${LJ_SRC}/host/genversion.lua ${LJ_SRC}/luajit_rolling.h ${CMAKE_CURRENT_SOURCE_DIR}/luajit_relver.txt ${LJ_GEN}/luajit.h
  DEPENDS luajit_minilua ${LJ_SRC}/luajit_rolling.h ${CMAKE_CURRENT_SOURCE_DIR}/luajit_relver.txt ${LJ_SRC}/host/genversion.lua
  COMMENT "luajit: luajit.h" VERBATIM)

add_executable(luajit_buildvm ${LJ_SRC}/host/buildvm.c ${LJ_SRC}/host/buildvm_asm.c ${LJ_SRC}/host/buildvm_peobj.c ${LJ_SRC}/host/buildvm_lib.c
               ${LJ_SRC}/host/buildvm_fold.c ${LJ_GEN}/host/buildvm_arch.h ${LJ_GEN}/luajit.h)
target_include_directories(luajit_buildvm PRIVATE ${LJ_SRC} ${LJ_GEN}/host ${LJ_GEN})
target_compile_definitions(luajit_buildvm PRIVATE ${LJ_HOST_DEFS})
target_compile_options(luajit_buildvm PRIVATE -w -O1)
if(NOT ENGINE_LUAJIT_JIT)
  target_compile_definitions(luajit_buildvm PRIVATE LUAJIT_DISABLE_JIT)  # the generated library tables must match the library's configuration
endif()
set_target_properties(luajit_buildvm PROPERTIES UNITY_BUILD OFF RUNTIME_OUTPUT_DIRECTORY ${LJ_GEN})

set(LJ_LIB_NAMES lib_base lib_math lib_bit lib_string lib_table lib_io lib_os lib_package lib_debug lib_jit lib_ffi lib_buffer)
set(LJ_LIB_SOURCES "")
foreach(n IN LISTS LJ_LIB_NAMES)
  list(APPEND LJ_LIB_SOURCES ${LJ_SRC}/${n}.c)
endforeach()

# buildvm generates the VM assembly and the library tables from the library sources.
set(LJ_GENERATED ${LJ_GEN}/lj_vm.S ${LJ_GEN}/lj_bcdef.h ${LJ_GEN}/lj_ffdef.h ${LJ_GEN}/lj_libdef.h ${LJ_GEN}/lj_recdef.h ${LJ_GEN}/lj_folddef.h)
add_custom_command(OUTPUT ${LJ_GEN}/lj_vm.S
  COMMAND luajit_buildvm -m elfasm -o ${LJ_GEN}/lj_vm.S DEPENDS luajit_buildvm COMMENT "luajit: lj_vm.S" VERBATIM)
foreach(mode bcdef ffdef libdef recdef)
  add_custom_command(OUTPUT ${LJ_GEN}/lj_${mode}.h
    COMMAND luajit_buildvm -m ${mode} -o ${LJ_GEN}/lj_${mode}.h ${LJ_LIB_SOURCES}
    DEPENDS luajit_buildvm ${LJ_LIB_SOURCES} COMMENT "luajit: lj_${mode}.h" VERBATIM)
endforeach()
add_custom_command(OUTPUT ${LJ_GEN}/lj_folddef.h
  COMMAND luajit_buildvm -m folddef -o ${LJ_GEN}/lj_folddef.h ${LJ_SRC}/lj_opt_fold.c
  DEPENDS luajit_buildvm ${LJ_SRC}/lj_opt_fold.c COMMENT "luajit: lj_folddef.h" VERBATIM)

set(LJ_CORE lj_assert lj_gc lj_err lj_char lj_bc lj_obj lj_buf lj_str lj_tab lj_func lj_udata lj_meta lj_debug lj_prng lj_state lj_dispatch lj_vmevent
    lj_vmmath lj_strscan lj_strfmt lj_strfmt_num lj_serialize lj_api lj_profile lj_lex lj_parse lj_bcread lj_bcwrite lj_load lj_ir lj_opt_mem lj_opt_fold
    lj_opt_narrow lj_opt_dce lj_opt_loop lj_opt_split lj_opt_sink lj_mcode lj_snap lj_record lj_crecord lj_ffrecord lj_asm lj_trace lj_gdbjit lj_ctype
    lj_cdata lj_cconv lj_ccall lj_ccallback lj_carith lj_clib lj_cparse lj_lib lj_alloc lib_aux ${LJ_LIB_NAMES} lib_init)
set(LJ_SOURCES ${LJ_GEN}/lj_vm.S)
foreach(n IN LISTS LJ_CORE)
  list(APPEND LJ_SOURCES ${LJ_SRC}/${n}.c)
endforeach()

add_library(luajit STATIC ${LJ_SOURCES} ${LJ_GENERATED} ${LJ_GEN}/luajit.h)
# Public headers (lua.h, lauxlib.h, lualib.h, lua.hpp, luajit.h) as system headers: third-party C is not under our warnings.
target_include_directories(luajit SYSTEM PUBLIC ${LJ_SRC} ${LJ_GEN})
target_include_directories(luajit PRIVATE ${LJ_GEN})
# The flags of LuaJIT's Makefile for Linux: -O2 and no frame pointer for the interpreter's speed, no fortify and no stack protector (its assembler core
# does not expect them), unwinding through the system's tables (LUAJIT_UNWIND_EXTERNAL: errors thrown through C++ frames stay correct).
target_compile_options(luajit PRIVATE -w -O2 -fomit-frame-pointer -U_FORTIFY_SOURCE -fno-stack-protector)
target_compile_definitions(luajit PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE LUAJIT_UNWIND_EXTERNAL ${LJ_HOST_DEFS})
if(NOT ENGINE_LUAJIT_JIT)
  target_compile_definitions(luajit PUBLIC LUAJIT_DISABLE_JIT)
endif()
target_link_libraries(luajit PUBLIC m ${CMAKE_DL_LIBS})
set_target_properties(luajit PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
