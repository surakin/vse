# Phase 23: the C API (engine_c.h, Day 346) as the static library `engine_c` (plain, asan, tsan; Linux, Windows from Day 354 and macOS from Day 355), and
# the aggregate shared engine library (ENGINE_BUILD_SHARED, Day 343) made of every module's static library, whole-archived, with the export list given to the
# linker (a version script on Linux, an exported-symbols list on macOS, a module-definition file on Windows). The static `engine_c` is built always, so a C
# program can link the C API directly (tests/engine_library/host_c.c); the shared library is built only when the option is ON.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE AND NOT WIN32)
  return()
endif()

# The state table of the boundary tests (the extension `engine.test_state`: the singletons' addresses, a job run, a crash inside a job) is compiled in by
# default while the phase is under way; a release build turns it off, and the library then returns NULL for the extension.
option(ENGINE_C_TEST_EXTENSION "Compile the engine.test_state extension of the C API (for the tests of tests/engine_library)" ON)
if(ENGINE_C_TEST_EXTENSION)
  set(ENGINE_C_TEST_EXTENSION_VALUE 1)
else()
  set(ENGINE_C_TEST_EXTENSION_VALUE 0)
endif()

# The glTF importer (engine/asset_import, with the image decoders) is part of the library by default; a library without it answers import_gltf with UNSUPPORTED
# and reports no ENGINE_CAP_IMPORT_GLTF (Day 351). Only import.cpp reaches the importer (the gate asset_boundary_check lists the file and this one).
option(ENGINE_LIBRARY_WITH_IMPORTER "Build the glTF importer into the engine library (the C API's import_gltf)" ON)
if(ENGINE_LIBRARY_WITH_IMPORTER)
  set(ENGINE_C_WITH_IMPORTER_VALUE 1)
else()
  set(ENGINE_C_WITH_IMPORTER_VALUE 0)
endif()

set(ENGINE_C_SOURCES engine_c.cpp audio_api.cpp host_texture.cpp live_textures.cpp import.cpp resources.cpp runtime.cpp status_map.cpp surface.cpp surface_scene.cpp)
if(APPLE)
  # Day 355: a host's NSView or CAMetalLayer as a surface (AppKit and QuartzCore, inside an autorelease pool).
  list(APPEND ENGINE_C_SOURCES macos/cocoa_surface.mm)
endif()
# The C header declares engine_get_api with ENGINE_C_API, empty for a host. The MSVC family wants the declaration to carry the dllexport the definition has
# (ENGINE_API of build_defs.h; C2375 otherwise), so the library's own sources see it there too (Day 354).
set(ENGINE_C_LIBRARY_DEFINITIONS ENGINE_BUILDING_LIBRARY=1)
if(MSVC)
  list(APPEND ENGINE_C_LIBRARY_DEFINITIONS "ENGINE_C_API=__declspec(dllexport)")
endif()
if(ENGINE_C_TEST_EXTENSION)
  list(APPEND ENGINE_C_SOURCES test_state.cpp)
endif()
foreach(suffix "" "_asan" "_tsan")
  if(suffix STREQUAL "_asan" AND NOT ENGINE_HAVE_ASAN)
    continue()
  endif()
  if(suffix STREQUAL "_tsan" AND NOT ENGINE_HAVE_TSAN)
    continue()
  endif()
  add_library(engine_c${suffix} STATIC ${ENGINE_C_SOURCES})
  target_include_directories(engine_c${suffix} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${ENGINE_PUBLIC_INCLUDE_DIR})
  target_compile_options(engine_c${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
  # ENGINE_BUILDING_LIBRARY: engine_get_api is the one exported function (engine_c.h); ENGINE_TEST_EXTENSION: see the option above.
  target_compile_definitions(engine_c${suffix} PRIVATE ${ENGINE_C_LIBRARY_DEFINITIONS} ENGINE_C_UNLOAD_HOOK=0 ENGINE_TEST_EXTENSION=${ENGINE_C_TEST_EXTENSION_VALUE} ENGINE_C_WITH_IMPORTER=${ENGINE_C_WITH_IMPORTER_VALUE})
  target_link_libraries(engine_c${suffix} PUBLIC asset${suffix} ecs${suffix} jobs${suffix} platform${suffix} rhi${suffix} rhi_backends${suffix} renderer_passes${suffix} renderer${suffix} scene${suffix} animation${suffix} physics${suffix} audio${suffix} foundation${suffix})
  if(APPLE)
    target_link_libraries(engine_c${suffix} PUBLIC "-framework AppKit" "-framework QuartzCore")
  endif()
  if(ENGINE_LIBRARY_WITH_IMPORTER)
    target_link_libraries(engine_c${suffix} PUBLIC asset_import${suffix})
  endif()
endforeach()

# The same C API compiled without the importer (plain flavor only): the test of a library built with ENGINE_LIBRARY_WITH_IMPORTER=OFF (Day 351).
if(ENGINE_LIBRARY_WITH_IMPORTER)
  add_library(engine_c_noimport STATIC ${ENGINE_C_SOURCES})
  target_include_directories(engine_c_noimport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${ENGINE_PUBLIC_INCLUDE_DIR})
  target_compile_options(engine_c_noimport PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
  target_compile_definitions(engine_c_noimport PRIVATE ${ENGINE_C_LIBRARY_DEFINITIONS} ENGINE_C_UNLOAD_HOOK=0 ENGINE_TEST_EXTENSION=${ENGINE_C_TEST_EXTENSION_VALUE} ENGINE_C_WITH_IMPORTER=0)
  target_link_libraries(engine_c_noimport PUBLIC asset ecs jobs platform rhi rhi_backends renderer_passes renderer scene animation physics audio foundation)
  if(APPLE)
    target_link_libraries(engine_c_noimport PUBLIC "-framework AppKit" "-framework QuartzCore")
  endif()
endif()

# The SDK a host links against a pre-built engine: the public headers (include/engine/, the only headers a host needs; none of the engine's own) go to
# include/engine/ of the install prefix, with or without the shared library (engine_loader.h needs nothing else).
install(DIRECTORY ${ENGINE_PUBLIC_INCLUDE_DIR}/engine DESTINATION include COMPONENT engine_sdk FILES_MATCHING PATTERN "*.h")

if(NOT ENGINE_BUILD_SHARED)
  return()
endif()

# The modules whose static libraries make up the engine (the order of the root CMakeLists.txt).
set(ENGINE_SHARED_MODULE_DIRS foundation platform jobs text ecs spatial database storage net ui ui_window rhi renderer scene asset animation physics audio)
if(ENGINE_LIBRARY_WITH_IMPORTER)
  list(APPEND ENGINE_SHARED_MODULE_DIRS asset_import)
endif()

# The static libraries of one flavor ("" plain, _asan, _tsan) under those directories, tests and executables excluded.
function(engine_shared_collect suffix out)
  set(libs "")
  foreach(dir IN LISTS ENGINE_SHARED_MODULE_DIRS)
    get_property(targets DIRECTORY ${CMAKE_SOURCE_DIR}/engine/${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      if(NOT type STREQUAL "STATIC_LIBRARY")
        continue()
      endif()
      if(target MATCHES "_tests?(_asan|_tsan)?$")
        continue()
      endif()
      set(is_asan FALSE)
      set(is_tsan FALSE)
      if(target MATCHES "_asan$")
        set(is_asan TRUE)
      elseif(target MATCHES "_tsan$")
        set(is_tsan TRUE)
      endif()
      if((suffix STREQUAL "" AND NOT is_asan AND NOT is_tsan) OR (suffix STREQUAL "_asan" AND is_asan) OR (suffix STREQUAL "_tsan" AND is_tsan))
        list(APPEND libs ${target})
      endif()
    endforeach()
  endforeach()
  set(${out} ${libs} PARENT_SCOPE)
endfunction()

# The version script is generated from abi/engine.exports (the one list of what the library exports): a node named ENGINE_<major> with the symbols global and
# everything else local. The file is re-read when the list changes.
set(ENGINE_ABI_EXPORTS ${CMAKE_SOURCE_DIR}/abi/engine.exports)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ENGINE_ABI_EXPORTS})
file(READ ${ENGINE_ABI_EXPORTS} _export_text)
string(REPLACE ";" "<SEMI>" _export_text "${_export_text}")
string(REPLACE "[" "<LB>" _export_text "${_export_text}")
string(REPLACE "]" "<RB>" _export_text "${_export_text}")
string(REPLACE "\n" ";" _export_lines "${_export_text}")
set(_exported "")
set(_abi_major "")
set(_abi_minor "")
foreach(line IN LISTS _export_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  elseif(line MATCHES "^version ([0-9]+)\\.([0-9]+)$")
    set(_abi_major ${CMAKE_MATCH_1})
    set(_abi_minor ${CMAKE_MATCH_2})
  elseif(line STREQUAL "status unreleased")
    continue()
  elseif(line MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    string(APPEND _exported "    ${line};\n")
  else()
    message(FATAL_ERROR "abi/engine.exports: not a version line or a symbol: ${line}")
  endif()
endforeach()
if(_abi_major STREQUAL "")
  message(FATAL_ERROR "abi/engine.exports has no `version MAJOR.MINOR` line")
endif()
set(ENGINE_SHARED_VERSION_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/engine_shared.map)
file(WRITE ${ENGINE_SHARED_VERSION_SCRIPT} "# Generated from abi/engine.exports (engine/engine_c/CMakeLists.txt): do not edit.\nENGINE_${_abi_major} {\n  global:\n${_exported}  local:\n    *;\n};\n")
# macOS (Day 355): ld64 takes a list of the exported names, with the C symbol prefix `_`; everything else becomes local (no version nodes on Mach-O: the
# library's compatibility version is the major).
string(REGEX REPLACE "    ([A-Za-z_][A-Za-z0-9_]*);\n" "_\\1\n" _exported_macho "${_exported}")
set(ENGINE_SHARED_EXPORTED_SYMBOLS ${CMAKE_CURRENT_BINARY_DIR}/engine_shared.exported_symbols)
file(WRITE ${ENGINE_SHARED_EXPORTED_SYMBOLS} "${_exported_macho}")
# Windows (Day 354): the same list as a module-definition file. A DLL exports only what dllexport or the .def names, so the .def is the list that counts;
# ENGINE_API's dllexport on engine_get_api says the same. The import library the linker writes beside the DLL is not used: hosts load engine.dll with
# LoadLibraryW and find engine_get_api with GetProcAddress (include/engine/engine_loader.h).
string(REPLACE ";\n" "\n" _def_exports "${_exported}")
set(ENGINE_SHARED_DEF_FILE ${CMAKE_CURRENT_BINARY_DIR}/engine_shared.def)
file(WRITE ${ENGINE_SHARED_DEF_FILE} "; Generated from abi/engine.exports (engine/engine_c/CMakeLists.txt): do not edit.\nEXPORTS\n${_def_exports}")

function(engine_add_shared suffix)
  engine_shared_collect("${suffix}" libs)
  # The C API's own sources are compiled into the library (the static `engine_c` is for the programs that link the C API directly); every other module comes
  # whole-archived below.
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(engine${suffix} SHARED ${ENGINE_C_SOURCES} ${ENGINE_SHARED_DEF_FILE})
  else()
    add_library(engine${suffix} SHARED ${ENGINE_C_SOURCES})
  endif()
  target_include_directories(engine${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${ENGINE_PUBLIC_INCLUDE_DIR})
  target_compile_options(engine${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_compile_definitions(engine${suffix} PRIVATE ${ENGINE_C_LIBRARY_DEFINITIONS} ENGINE_C_UNLOAD_HOOK=1 ENGINE_TEST_EXTENSION=${ENGINE_C_TEST_EXTENSION_VALUE} ENGINE_C_WITH_IMPORTER=${ENGINE_C_WITH_IMPORTER_VALUE})
  # Every module's objects go in whole (nothing is dropped for being unreferenced: the C API reaches them at run time), their third-party libraries
  # (the vendored C libraries, already position independent) come in through the modules' usage requirements.
  target_link_libraries(engine${suffix} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${libs}>")
  set_target_properties(engine${suffix} PROPERTIES VERSION ${_abi_major}.${_abi_minor} SOVERSION ${_abi_major} OUTPUT_NAME engine${suffix})
  if(APPLE)
    # libengine.<major>.dylib, found by a host through its rpath (install name @rpath/libengine.1.dylib): the host decides where the library lives.
    target_link_options(engine${suffix} PRIVATE "LINKER:-exported_symbols_list,${ENGINE_SHARED_EXPORTED_SYMBOLS}")
    set_property(TARGET engine${suffix} APPEND PROPERTY LINK_DEPENDS ${ENGINE_SHARED_EXPORTED_SYMBOLS})
    set_target_properties(engine${suffix} PROPERTIES MACOSX_RPATH ON INSTALL_NAME_DIR "@rpath")
    target_link_libraries(engine${suffix} PRIVATE "-framework AppKit" "-framework QuartzCore")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET engine${suffix} APPEND PROPERTY LINK_DEPENDS ${ENGINE_SHARED_DEF_FILE})
  else()
    target_link_options(engine${suffix} PRIVATE "LINKER:--version-script=${ENGINE_SHARED_VERSION_SCRIPT}")
    set_property(TARGET engine${suffix} APPEND PROPERTY LINK_DEPENDS ${ENGINE_SHARED_VERSION_SCRIPT})
  endif()
  if(suffix STREQUAL "_asan")
    target_compile_options(engine_asan PRIVATE ${ENGINE_ASAN_COMPILE_OPTIONS})
    target_link_options(engine_asan PRIVATE ${ENGINE_ASAN_LINK_OPTIONS})
    engine_asan_runtime(engine_asan)  # Windows: the runtime DLLs beside the library (nothing elsewhere)
  elseif(suffix STREQUAL "_tsan")
    target_compile_options(engine_tsan PRIVATE ${ENGINE_TSAN_COMPILE_OPTIONS})
    target_link_options(engine_tsan PRIVATE ${ENGINE_TSAN_LINK_OPTIONS})
  endif()
  list(LENGTH libs count)
  # Every flavor has the same libraries: a library that exists in one flavor only (or a module directory that gave none) is a mistake of a module's CMake file.
  if(NOT DEFINED ENGINE_SHARED_PLAIN_COUNT)
    set(ENGINE_SHARED_PLAIN_COUNT ${count} PARENT_SCOPE)
  elseif(NOT count EQUAL ENGINE_SHARED_PLAIN_COUNT)
    message(FATAL_ERROR "engine${suffix} has ${count} module libraries, the plain engine ${ENGINE_SHARED_PLAIN_COUNT}: ${libs}")
  endif()
  message(STATUS "engine${suffix}: shared library of ${count} module libraries")
endfunction()

engine_add_shared("")
if(ENGINE_HAVE_ASAN)
  engine_add_shared("_asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_shared("_tsan")
endif()

# Install (plain flavor only): libengine.so.<major> (macOS: libengine.<major>.dylib) and the package config, so a host builds with find_package(Engine) and links engine::engine.
include(CMakePackageConfigHelpers)
target_include_directories(engine INTERFACE $<BUILD_INTERFACE:${ENGINE_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:include>)
install(TARGETS engine EXPORT EngineTargets LIBRARY DESTINATION lib COMPONENT engine_sdk NAMELINK_COMPONENT engine_sdk RUNTIME DESTINATION bin COMPONENT engine_sdk
        ARCHIVE DESTINATION lib COMPONENT engine_sdk)
install(EXPORT EngineTargets NAMESPACE engine:: DESTINATION lib/cmake/Engine COMPONENT engine_sdk)
configure_package_config_file(${CMAKE_SOURCE_DIR}/cmake/EngineConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/EngineConfig.cmake INSTALL_DESTINATION lib/cmake/Engine)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/EngineConfigVersion.cmake VERSION ${_abi_major}.${_abi_minor}.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/EngineConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/EngineConfigVersion.cmake DESTINATION lib/cmake/Engine COMPONENT engine_sdk)
