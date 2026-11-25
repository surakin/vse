# LuaJIT (the pinned commit in VENDORED.md), vendored: PLAN.md assumption #13. One static C library `luajit`, built from source with no `make`: the host
# tools of LuaJIT's own build (minilua, DynASM, buildvm) are built and run here, and the files they generate go to the build tree. Reached only through
# the script module's LuaJIT backend (engine/script/luajit/, gate luajit_boundary_check). The sanitizer flavors link this same library: LuaJIT's
# assembler core and generated code are not instrumented (KNOWN-ISSUES.md). Linux x86-64 (Phase 27 Day 445), Windows x64 (Day 473: msvcbuild.bat's steps,
# DynASM with -D WIN and buildvm -m peobj, with MSVC or clang-cl) and macOS x86-64 and arm64 (Day 474: buildvm -m machasm, the deployment target from
# CMAKE_OSX_DEPLOYMENT_TARGET; arm64 cross-built on an Intel Mac, compile-only).
cmake_minimum_required(VERSION 3.20)

# The JIT is off by default on Apple silicon: the first run of the arm64 build (a hosted runner) ended the one test that uses a JIT state in a
# segmentation fault, every interpreter-only state passing (KNOWN-ISSUES.md). Ask for it with -DENGINE_LUAJIT_JIT=ON once it is checked there.
set(_engine_luajit_jit_default ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  set(_engine_luajit_jit_default OFF)
endif()
option(ENGINE_LUAJIT_JIT "Build LuaJIT with its JIT compiler (OFF: interpreter only, LUAJIT_DISABLE_JIT; Phase 27 Day 470)" ${_engine_luajit_jit_default})

# The target processor: CMAKE_OSX_ARCHITECTURES on macOS (one architecture per tree; CMAKE_SYSTEM_PROCESSOR is the host's there), else the system's.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
    message(FATAL_ERROR "third_party/luajit: one architecture per tree (CMAKE_OSX_ARCHITECTURES is ${CMAKE_OSX_ARCHITECTURES})")
  endif()
  set(LJ_TARGET_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")
  if(LJ_TARGET_PROCESSOR STREQUAL "")
    set(LJ_TARGET_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
else()
  set(LJ_TARGET_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(LJ_TARGET_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(LJ_ARCH x64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND LJ_TARGET_PROCESSOR STREQUAL "arm64")
  set(LJ_ARCH arm64)
else()
  set(LJ_ARCH "")
endif()
if(LJ_ARCH STREQUAL "" OR NOT (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR (CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$" AND LJ_ARCH STREQUAL "x64")))
  message(STATUS "third_party/luajit: builds on Linux and Windows x86-64 and macOS x86-64/arm64 so far; no `luajit` target here")
  return()
endif()
set(LJ_WINDOWS FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(LJ_WINDOWS TRUE)
else()
  enable_language(ASM)
endif()

# Third-party C, warnings off, optimized in every configuration as LuaJIT's own builds are (-O2 on Linux, /O2 in msvcbuild.bat). On Windows the Debug
# defaults of this directory lose /Od and /Ob0 (the root CMakeLists.txt already drops /RTC1), so cl does not report overriding them.
if(LJ_WINDOWS)
  set(LJ_NO_WARNINGS /w)
  set(LJ_HOST_OPT /O1)
  foreach(_lj_var CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
    string(REGEX REPLACE "(^| )/O[bd0-9]*" " " ${_lj_var} "${${_lj_var}}")
  endforeach()
else()
  set(LJ_NO_WARNINGS -w)
  set(LJ_HOST_OPT -O1)
endif()

set(LJ_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LJ_DYNASM ${CMAKE_CURRENT_SOURCE_DIR}/dynasm)
set(LJ_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${LJ_GEN}/host)

# Host tools: third-party C, warnings off. They run during the build, so on macOS they are built for the host's processor (an arm64 tree on an Intel Mac
# builds them for x86-64), told the target with LUAJIT_TARGET as LuaJIT's Makefile does for a cross build.
set(LJ_HOST_DEFS LUAJIT_TARGET=LUAJIT_ARCH_${LJ_ARCH} LJ_ARCH_HASFPU=1 LJ_ABI_SOFTFP=0)
add_executable(luajit_minilua ${LJ_SRC}/host/minilua.c)
target_compile_definitions(luajit_minilua PRIVATE ${LJ_HOST_DEFS})
target_compile_options(luajit_minilua PRIVATE ${LJ_NO_WARNINGS} ${LJ_HOST_OPT})
if(LJ_WINDOWS)
  target_compile_definitions(luajit_minilua PRIVATE _CRT_SECURE_NO_DEPRECATE)
else()
  target_link_libraries(luajit_minilua PRIVATE m)
endif()
set_target_properties(luajit_minilua PROPERTIES UNITY_BUILD OFF RUNTIME_OUTPUT_DIRECTORY ${LJ_GEN})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set_target_properties(luajit_minilua PROPERTIES OSX_ARCHITECTURES ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

# DynASM turns vm_<arch>.dasc into buildvm_arch.h. The flags are the ones LuaJIT's Makefile derives from lj_arch.h's defines (`cc -E -dM lj_arch.h`) with
# 64-bit GC objects (LJ_GC64, the default): x86-64 has no version; arm64 has dual-number mode (LJ_DUALNUM) and LJ_ARCH_VERSION 80, and no pointer
# authentication (LJ_ABI_PAUTH is 0 for arm64; only arm64e sets it). On Windows msvcbuild.bat's (-D WIN, the Windows x64 calling convention; no HFABI).
if(LJ_ARCH STREQUAL "arm64")
  set(LJ_DASM_FLAGS -D ENDIAN_LE -D P64 -D FFI -D DUALNUM -D FPU -D HFABI -D VER=80)
  set(LJ_DASM_ARCH_FILES ${LJ_DYNASM}/dasm_arm64.lua)
elseif(LJ_WINDOWS)
  set(LJ_DASM_FLAGS -D WIN -D ENDIAN_LE -D P64 -D FFI -D FPU)
  set(LJ_DASM_ARCH_FILES ${LJ_DYNASM}/dasm_x86.lua ${LJ_DYNASM}/dasm_x64.lua)
else()
  set(LJ_DASM_FLAGS -D ENDIAN_LE -D P64 -D FFI -D FPU -D HFABI -D VER=)
  set(LJ_DASM_ARCH_FILES ${LJ_DYNASM}/dasm_x86.lua ${LJ_DYNASM}/dasm_x64.lua)
endif()
if(ENGINE_LUAJIT_JIT)
  list(APPEND LJ_DASM_FLAGS -D JIT)
endif()
add_custom_command(OUTPUT ${LJ_GEN}/host/buildvm_arch.h
  COMMAND $<TARGET_FILE:luajit_minilua> ${LJ_DYNASM}/dynasm.lua ${LJ_DASM_FLAGS} -o ${LJ_GEN}/host/buildvm_arch.h ${LJ_SRC}/vm_${LJ_ARCH}.dasc
  DEPENDS luajit_minilua ${LJ_SRC}/vm_${LJ_ARCH}.dasc ${LJ_SRC}/lj_arch.h ${LJ_SRC}/lua.h ${LJ_SRC}/luaconf.h ${LJ_DYNASM}/dynasm.lua ${LJ_DASM_ARCH_FILES}
  COMMENT "luajit: DynASM vm_${LJ_ARCH}.dasc" VERBATIM)

# luajit.h carries the rolling release version (the commit time of the pin, luajit_relver.txt).
add_custom_command(OUTPUT ${LJ_GEN}/luajit.h
  COMMAND $<TARGET_FILE:luajit_minilua> ${LJ_SRC}/host/genversion.lua ${LJ_SRC}/luajit_rolling.h ${CMAKE_CURRENT_SOURCE_DIR}/luajit_relver.txt ${LJ_GEN}/luajit.h
  DEPENDS luajit_minilua ${LJ_SRC}/luajit_rolling.h ${CMAKE_CURRENT_SOURCE_DIR}/luajit_relver.txt ${LJ_SRC}/host/genversion.lua
  COMMENT "luajit: luajit.h" VERBATIM)

add_executable(luajit_buildvm ${LJ_SRC}/host/buildvm.c ${LJ_SRC}/host/buildvm_asm.c ${LJ_SRC}/host/buildvm_peobj.c ${LJ_SRC}/host/buildvm_lib.c
               ${LJ_SRC}/host/buildvm_fold.c ${LJ_GEN}/host/buildvm_arch.h ${LJ_GEN}/luajit.h)
target_include_directories(luajit_buildvm PRIVATE ${LJ_SRC} ${LJ_GEN}/host ${LJ_GEN})
target_compile_definitions(luajit_buildvm PRIVATE ${LJ_HOST_DEFS})
target_compile_options(luajit_buildvm PRIVATE ${LJ_NO_WARNINGS} ${LJ_HOST_OPT})
if(LJ_WINDOWS)
  target_compile_definitions(luajit_buildvm PRIVATE _CRT_SECURE_NO_DEPRECATE)
endif()
if(NOT ENGINE_LUAJIT_JIT)
  target_compile_definitions(luajit_buildvm PRIVATE LUAJIT_DISABLE_JIT)  # the generated library tables must match the library's configuration
endif()
set_target_properties(luajit_buildvm PROPERTIES UNITY_BUILD OFF RUNTIME_OUTPUT_DIRECTORY ${LJ_GEN})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set_target_properties(luajit_buildvm PROPERTIES OSX_ARCHITECTURES ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

set(LJ_LIB_NAMES lib_base lib_math lib_bit lib_string lib_table lib_io lib_os lib_package lib_debug lib_jit lib_ffi lib_buffer)
set(LJ_LIB_SOURCES "")
foreach(n IN LISTS LJ_LIB_NAMES)
  list(APPEND LJ_LIB_SOURCES ${LJ_SRC}/${n}.c)
endforeach()

# buildvm generates the VM (assembly for the system assembler: ELF on Linux, Mach-O on macOS; a COFF object on Windows) and the library tables from the
# library sources.
if(LJ_WINDOWS)
  set(LJ_VM ${LJ_GEN}/lj_vm.obj)
  set(LJ_VM_MODE peobj)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(LJ_VM ${LJ_GEN}/lj_vm.S)
  set(LJ_VM_MODE machasm)
else()
  set(LJ_VM ${LJ_GEN}/lj_vm.S)
  set(LJ_VM_MODE elfasm)
endif()
set(LJ_GENERATED ${LJ_VM} ${LJ_GEN}/lj_bcdef.h ${LJ_GEN}/lj_ffdef.h ${LJ_GEN}/lj_libdef.h ${LJ_GEN}/lj_recdef.h ${LJ_GEN}/lj_folddef.h)
add_custom_command(OUTPUT ${LJ_VM}
  COMMAND $<TARGET_FILE:luajit_buildvm> -m ${LJ_VM_MODE} -o ${LJ_VM} DEPENDS luajit_buildvm COMMENT "luajit: VM (${LJ_VM_MODE})" VERBATIM)
foreach(mode bcdef ffdef libdef recdef)
  add_custom_command(OUTPUT ${LJ_GEN}/lj_${mode}.h
    COMMAND $<TARGET_FILE:luajit_buildvm> -m ${mode} -o ${LJ_GEN}/lj_${mode}.h ${LJ_LIB_SOURCES}
    DEPENDS luajit_buildvm ${LJ_LIB_SOURCES} COMMENT "luajit: lj_${mode}.h" VERBATIM)
endforeach()
add_custom_command(OUTPUT ${LJ_GEN}/lj_folddef.h
  COMMAND $<TARGET_FILE:luajit_buildvm> -m folddef -o ${LJ_GEN}/lj_folddef.h ${LJ_SRC}/lj_opt_fold.c
  DEPENDS luajit_buildvm ${LJ_SRC}/lj_opt_fold.c COMMENT "luajit: lj_folddef.h" VERBATIM)

set(LJ_CORE lj_assert lj_gc lj_err lj_char lj_bc lj_obj lj_buf lj_str lj_tab lj_func lj_udata lj_meta lj_debug lj_prng lj_state lj_dispatch lj_vmevent
    lj_vmmath lj_strscan lj_strfmt lj_strfmt_num lj_serialize lj_api lj_profile lj_lex lj_parse lj_bcread lj_bcwrite lj_load lj_ir lj_opt_mem lj_opt_fold
    lj_opt_narrow lj_opt_dce lj_opt_loop lj_opt_split lj_opt_sink lj_mcode lj_snap lj_record lj_crecord lj_ffrecord lj_asm lj_trace lj_gdbjit lj_ctype
    lj_cdata lj_cconv lj_ccall lj_ccallback lj_carith lj_clib lj_cparse lj_lib lj_alloc lib_aux ${LJ_LIB_NAMES} lib_init)
set(LJ_SOURCES ${LJ_VM})  # on Windows an object CMake links in as it is (EXTERNAL_OBJECT, from the .obj extension)
foreach(n IN LISTS LJ_CORE)
  list(APPEND LJ_SOURCES ${LJ_SRC}/${n}.c)
endforeach()

add_library(luajit STATIC ${LJ_SOURCES} ${LJ_GENERATED} ${LJ_GEN}/luajit.h)
# Public headers (lua.h, lauxlib.h, lualib.h, lua.hpp, luajit.h) as system headers: third-party C is not under our warnings.
target_include_directories(luajit SYSTEM PUBLIC ${LJ_SRC} ${LJ_GEN})
target_include_directories(luajit PRIVATE ${LJ_GEN})
if(LJ_WINDOWS)
  # msvcbuild.bat's flags for the static library: /O2, the C runtime's secure-function warnings off. Errors unwind through the system's tables on Windows
  # x64 always (RtlUnwindEx, LJ_ABI_WIN: lj_err.c), so no LUAJIT_UNWIND_EXTERNAL is needed. Plain C, the same with cl and clang-cl.
  target_compile_options(luajit PRIVATE /w /O2)
  target_compile_definitions(luajit PRIVATE _CRT_SECURE_NO_DEPRECATE ${LJ_HOST_DEFS})
else()
  # The flags of LuaJIT's Makefile for Linux: -O2 and no frame pointer for the interpreter's speed, no fortify and no stack protector (its assembler core
  # does not expect them), unwinding through the system's tables (LUAJIT_UNWIND_EXTERNAL: errors thrown through C++ frames stay correct).
  target_compile_options(luajit PRIVATE -w -O2 -fomit-frame-pointer -U_FORTIFY_SOURCE -fno-stack-protector)
  target_compile_definitions(luajit PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE LUAJIT_UNWIND_EXTERNAL ${LJ_HOST_DEFS})
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND LJ_ARCH STREQUAL "x64")
    target_compile_options(luajit PRIVATE -fno-stack-check)  # the Makefile's flag for x86-64 macOS: the probes Apple clang adds break the VM's frames
  endif()
endif()
if(NOT ENGINE_LUAJIT_JIT)
  target_compile_definitions(luajit PUBLIC LUAJIT_DISABLE_JIT)
endif()
if(NOT LJ_WINDOWS)
  target_link_libraries(luajit PUBLIC m ${CMAKE_DL_LIBS})
endif()
set_target_properties(luajit PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON)
