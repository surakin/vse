# Per-platform implementation of os/os.h: os/linux/, os/windows/ (Phase 13) and, being ported in Phase 26, os/macos/ (PLAN.md
# assumption #11). The GPU driver probe (os/gpu_driver_probe.h) is Vulkan-only and pulls the Vulkan headers into this layer, so it is in
# the platform lists of the platforms that have Vulkan, not in macOS's (Phase 26 decision 8).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FOUNDATION_PLATFORM_SOURCES
    os/posix/os_posix.cpp
    os/linux/os.cpp
    os/linux/session.cpp
    os/gpu_driver_probe.cpp
    os/linux/vulkan_loader_name.cpp
  )

  # Phase 7: the per-platform half of the debugging facilities (debug/*.h are the neutral interfaces). Windows and macOS
  # add os/<platform>/debug/ in Phases 13 and 26 (see the BACKEND.md checklists there).
  set(FOUNDATION_DEBUG_PLATFORM_SOURCES
    debug/stack_trace_backtrace.cpp
    os/posix/debug/death_test_posix.cpp
    os/linux/debug/posix_debug_hooks.cpp
    os/linux/debug/debugger.cpp
    os/linux/debug/module_list.cpp
    os/linux/debug/crash_context.cpp
    os/linux/debug/crash_signal.cpp
    os/linux/debug/core_dump.cpp
    os/posix/debug/fault_injection_posix.cpp
  )
  # The file layer: the POSIX part shared with macOS (io/posix/, Phase 26 Day 303) and Linux's own.
  set(FOUNDATION_IO_PLATFORM_SOURCES
    io/posix/posix_error.cpp
    io/posix/file_system.cpp
    io/posix/local_file.cpp
    io/posix/posix_directory.cpp
    io/linux/posix_hooks.cpp
    io/linux/special_folders.cpp
  )
  set(FOUNDATION_SETTINGS_PLATFORM_SOURCES
    settings/linux/policy_layers.cpp
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Phase 13 ports one layer per day (ARCHITECTURE.md order): the base layer's os/windows/ backend on Day 149, the file layer on Days
  # 151-152, the debugging backend (os/windows/debug/BACKEND.md) on Days 154-156.
  set(FOUNDATION_PLATFORM_SOURCES
    os/windows/os.cpp
    os/windows/utf16.cpp
    os/windows/session.cpp
    os/gpu_driver_probe.cpp
    os/windows/vulkan_loader_name.cpp
  )
  # Day 154: stack traces through DbgHelp, the module list, the crash context; Day 155: the crash hook, death tests, fault injection;
  # Day 156: post-mortem dumps (Windows Error Reporting LocalDumps). libbacktrace is not built on Windows.
  set(FOUNDATION_DEBUG_PLATFORM_SOURCES
    os/windows/debug/stack_trace_dbghelp.cpp
    os/windows/debug/module_list.cpp
    os/windows/debug/crash_context.cpp
    os/windows/debug/crash_hook.cpp
    os/windows/debug/death_test.cpp
    os/windows/debug/fault_injection.cpp
    os/windows/debug/post_mortem.cpp
  )
  # The file layer: paths, errors, Stat and the local file on Day 151; directories and the other file-system operations on Day 152
  # (io/BACKEND.md). Asynchronous I/O is the neutral executor path, as on Linux.
  # Day 157: the registry store and the group-policy layers (settings/BACKEND.md).
  set(FOUNDATION_SETTINGS_PLATFORM_SOURCES
    settings/windows/registry_store.cpp
    settings/windows/policy_layers.cpp
  )
  set(FOUNDATION_IO_PLATFORM_SOURCES
    io/windows/win_path.cpp
    io/windows/error.cpp
    io/windows/file_system.cpp
    io/windows/file_operations.cpp
    io/windows/local_file.cpp
    io/windows/directory.cpp
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # Phase 26 ports one layer per day (ARCHITECTURE.md order). Day 301: the base layer's os backend, the POSIX half shared with Linux
  # (os/posix/) and macOS's own (os/macos/). No GPU driver probe on macOS (decision 8).
  set(FOUNDATION_PLATFORM_SOURCES
    os/posix/os_posix.cpp
    os/macos/os.cpp
    os/macos/session.mm
  )
  # Day 303: the file layer, the POSIX part shared with Linux (io/posix/) and macOS's own (io/macos/).
  set(FOUNDATION_IO_PLATFORM_SOURCES
    io/posix/posix_error.cpp
    io/posix/file_system.cpp
    io/posix/local_file.cpp
    io/posix/posix_directory.cpp
    io/macos/posix_hooks.cpp
    io/macos/special_folders.mm
  )
  # The debugging backend (os/macos/debug/BACKEND.md): stack traces through libbacktrace on Mach-O, the module list, the crash context (Day 306);
  # the crash hook on Breakpad's on_crash, death tests (shared with Linux in os/posix/debug/), fault injection, the debugger check (Day 307).
  # Post-mortem dumps (core_dump_macos.cpp) too, brought forward from Day 308.
  set(FOUNDATION_DEBUG_PLATFORM_SOURCES
    debug/stack_trace_backtrace.cpp
    os/macos/debug/module_list.cpp
    os/macos/debug/crash_context_macos.cpp
    os/macos/debug/crash_hook.cpp
    os/macos/debug/debugger.cpp
    os/posix/debug/fault_injection_posix.cpp
    os/posix/debug/death_test_posix.cpp
    os/macos/debug/posix_debug_hooks.cpp
    os/macos/debug/core_dump_macos.cpp
  )
else()
  message(FATAL_ERROR "No foundation os.h implementation for this platform yet")
endif()

# One library per layer (ARCHITECTURE.md section 1), each linking only the ones below it:
#   foundation_base      layers 0-5: base types, OS primitives, memory, assert and log core, containers, strings, math, OS services
#   foundation_io        layer 6: paths, files, directories
#   foundation_debug     layer 7: stack traces, crash handler, crash context, death tests (the only library that links libbacktrace and sentry)
#   foundation_settings  layer 8: the settings registry, stores, layers and service
# `foundation` is the aggregate every consumer that wants all of them links; a test of one layer links that layer only.
# (The windows and input code is not here: it is the `platform` library, engine/platform.)
set(FOUNDATION_BASE_SOURCES
  foundation.cpp
  os/session.cpp
  os/command_line.cpp
  mem/arena_allocator.cpp
  mem/pool_allocator.cpp
  mem/stack_allocator.cpp
  log/log.cpp
  cstr/number.cpp
  int128.cpp
  math/geometry.cpp
  string/utf8_codec.cpp
  string/grapheme.cpp
  string/generated/grapheme_break_table.cpp
  string/normalize.cpp
  string/generated/decomposition_table.cpp
  string/generated/combining_class_table.cpp
  string/generated/composition_exclusions_table.cpp
  ${FOUNDATION_PLATFORM_SOURCES}
)

set(FOUNDATION_IO_SOURCES
  io/path.cpp
  io/error.cpp
  io/directory.cpp
  ${FOUNDATION_IO_PLATFORM_SOURCES}
  io/file.cpp
)

set(FOUNDATION_DEBUG_SOURCES
  debug/crash_reason.cpp
  debug/failure_hooks.cpp
  debug/death_test.cpp
  debug/stack_trace_format.cpp
  debug/thread_info.cpp
  debug/crash_context.cpp
  debug/crash_state.cpp
  debug/program_setup.cpp
  debug/crash_report.cpp
  debug/crash_handler_sentry.cpp
  debug/saved_dumps.cpp
  ${FOUNDATION_DEBUG_PLATFORM_SOURCES}
)

set(FOUNDATION_SETTINGS_SOURCES
  settings/key.cpp
  settings/value.cpp
  settings/desc.cpp
  settings/registry.cpp
  settings/config_store.cpp
  settings/memory_store.cpp
  settings/config_text.cpp
  settings/file_store.cpp
  settings/layers.cpp
  settings/env_source.cpp
  settings/command_line_source.cpp
  settings/migration.cpp
  settings/engine_settings.cpp
  settings/report.cpp
  settings/settings.cpp
)

# ENGINE_PRODUCTION_COMPILE_OPTIONS (-fno-exceptions etc) is applied PRIVATE, not PUBLIC: PUBLIC compile options are a usage
# requirement that would leak into every consumer that links these targets, including the Catch2-based test executables, which
# need exceptions. Production executables (game/hello_window) that want the same hardening declare it themselves.
# ENGINE_PRODUCTION_COMPILE_OPTIONS is for C++ and Objective-C++ (the macOS .mm files, Phase 26), so it is scoped to CXX and OBJCXX
# translation units by a generator expression (the C sources of vendored code keep their own flags); tests/macos checks that a .mm gets it.
#
# Sanitized variants are separate targets built from the same sources, not a build-wide flag, so the plain and sanitized binaries
# exist side by side from one configure. The sanitizer flags are PUBLIC on the lowest library of a flavor (foundation_base_asan,
# foundation_base_tsan) and reach every library above it by linking: ASan/UBSan and TSan need the whole binary instrumented
# consistently, and TSan only gives trustworthy answers when every library that touches shared memory is instrumented too.
function(engine_add_foundation_layer name suffix)
  # On macOS a static archive keeps only the base name of each object, and dsymutil skips a second object of the same name in the debug map,
  # so its frames lose file and line (Phase 26 Day 306: io/error.cpp and io/posix/error.cpp were one). Two sources of one library may not share
  # a file name there; the platform file takes a prefix or suffix (posix_error.cpp, crash_context_macos.cpp).
  if(APPLE)
    set(_seen "")
    foreach(_source IN LISTS ARGN)
      get_filename_component(_base "${_source}" NAME) # the object is <name>.o: session.cpp and session.mm do not collide
      if(_base IN_LIST _seen)
        message(FATAL_ERROR "${name}${suffix}: two sources are named ${_base}; on macOS the archive and dsymutil keep one of them (rename one)")
      endif()
      list(APPEND _seen "${_base}")
    endforeach()
  endif()
  add_library(${name}${suffix} STATIC ${ARGN})
  target_include_directories(${name}${suffix} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${name}${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
endfunction()

function(engine_add_foundation_libs suffix)
  engine_add_foundation_layer(foundation_base "${suffix}" ${FOUNDATION_BASE_SOURCES})
  # os::LoadLibrary wraps dlopen/dlsym/dlclose. CMAKE_DL_LIBS is the portable way to link libdl where it is a separate library and
  # empty where dlopen already lives in libc (glibc >= 2.34): a link requirement, so PUBLIC.
  target_link_libraries(foundation_base${suffix} PUBLIC ${CMAKE_DL_LIBS})

  engine_add_foundation_layer(foundation_io "${suffix}" ${FOUNDATION_IO_SOURCES})
  target_link_libraries(foundation_io${suffix} PUBLIC foundation_base${suffix})

  engine_add_foundation_layer(foundation_debug "${suffix}" ${FOUNDATION_DEBUG_SOURCES})
  # libbacktrace (third_party/libbacktrace) and sentry-native, used only by debug/stack_trace_backtrace.cpp and crash_handler_sentry.cpp;
  # one plain build of each for every flavor.
  target_link_libraries(foundation_debug${suffix} PUBLIC foundation_io${suffix} PRIVATE backtrace sentry)

  engine_add_foundation_layer(foundation_settings "${suffix}" ${FOUNDATION_SETTINGS_SOURCES} ${FOUNDATION_SETTINGS_PLATFORM_SOURCES})
  target_link_libraries(foundation_settings${suffix} PUBLIC foundation_debug${suffix})

  add_library(foundation${suffix} INTERFACE)
  target_link_libraries(foundation${suffix} INTERFACE foundation_settings${suffix} foundation_debug${suffix} foundation_io${suffix} foundation_base${suffix})
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # The base layer and the file layer so far (see the platform selection above); the layers above them join as their Windows backends arrive.
  # os/gpu_driver_probe.h includes <vulkan/vulkan_core.h>: Linux has it in /usr/include, Windows a copy of the SDK's C headers
  # (ENGINE_VULKAN_HEADERS_DIR, made by the root CMakeLists.txt).
  set(_engine_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _engine_flavors "_asan")
  endif()
  foreach(suffix "" ${_engine_flavors})
    engine_add_foundation_layer(foundation_base "${suffix}" ${FOUNDATION_BASE_SOURCES})
    target_include_directories(foundation_base${suffix} SYSTEM PUBLIC "${ENGINE_VULKAN_HEADERS_DIR}")
    # WTSQuerySessionInformation (os/windows/session.cpp); user32 for the input desktop.
    target_link_libraries(foundation_base${suffix} PUBLIC wtsapi32 user32 bcrypt)
    engine_add_foundation_layer(foundation_io "${suffix}" ${FOUNDATION_IO_SOURCES})
    # advapi32: ACLs (FileInfo::read_only, private directories); bcrypt: random temporary names; shell32 and ole32: the known folders.
    target_link_libraries(foundation_io${suffix} PUBLIC foundation_base${suffix} advapi32 bcrypt shell32 ole32)
    engine_add_foundation_layer(foundation_debug "${suffix}" ${FOUNDATION_DEBUG_SOURCES})
    # sentry-native (Breakpad backend, no transport) as on Linux; DbgHelp and PSAPI for symbols and the module list (Day 154).
    # synchronization: sentry-native's WaitOnAddress (its CMake does not name the library for a static build).
    target_link_libraries(foundation_debug${suffix} PUBLIC foundation_io${suffix} PRIVATE sentry dbghelp psapi synchronization)
    # Day 157: the settings layer, with the registry store (settings/windows/); advapi32 for the registry.
    engine_add_foundation_layer(foundation_settings "${suffix}" ${FOUNDATION_SETTINGS_SOURCES} ${FOUNDATION_SETTINGS_PLATFORM_SOURCES})
    target_link_libraries(foundation_settings${suffix} PUBLIC foundation_debug${suffix} advapi32)
    add_library(foundation${suffix} INTERFACE)
    target_link_libraries(foundation${suffix} INTERFACE foundation_settings${suffix} foundation_debug${suffix} foundation_io${suffix} foundation_base${suffix})
  endforeach()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # The base layer, the file layer (Day 303) and the debugging layer (Day 306) in the three flavors (Apple clang has ASan, UBSan and TSan); the layers
  # above them join as their macOS backends arrive (jobs on Day 305, the rest of debugging on Days 307-308).
  foreach(suffix "" "_asan" "_tsan")
    engine_add_foundation_layer(foundation_base "${suffix}" ${FOUNDATION_BASE_SOURCES})
    # The session query (os/macos/session.mm) reads the window server's session dictionary.
    target_link_libraries(foundation_base${suffix} PUBLIC ${CMAKE_DL_LIBS} "-framework CoreGraphics" "-framework Foundation")
    engine_add_foundation_layer(foundation_io "${suffix}" ${FOUNDATION_IO_SOURCES})
    # The user directories (io/macos/special_folders.mm) ask Foundation (NSSearchPathForDirectoriesInDomains).
    target_link_libraries(foundation_io${suffix} PUBLIC foundation_base${suffix} "-framework Foundation")
    engine_add_foundation_layer(foundation_debug "${suffix}" ${FOUNDATION_DEBUG_SOURCES})
    target_link_libraries(foundation_debug${suffix} PUBLIC foundation_io${suffix} PRIVATE backtrace sentry)
  endforeach()
else()
  engine_add_foundation_libs("")
  engine_add_foundation_libs("_asan")
  engine_add_foundation_libs("_tsan")
endif()

# The sanitizer flags per compiler are in cmake/EngineCompilerOptions.cmake. On GCC/Clang they include -fno-sanitize=vptr: the
# engine libraries are built -fno-rtti, so they define no typeinfo, while the test executables (Catch2 needs RTTI) are not. With the
# vptr check on, GCC 16 makes every virtual call through a library class reference that class's typeinfo and the link fails
# (`undefined reference to typeinfo for ...NullDevice`). It must come after -fsanitize=undefined, which turns the check back on.
if(ENGINE_HAVE_ASAN)
  target_compile_options(foundation_base_asan PUBLIC ${ENGINE_ASAN_COMPILE_OPTIONS})
  if(ENGINE_ASAN_LINK_OPTIONS)  # empty with cl, which records the ASan runtime in the objects
    target_link_options(foundation_base_asan PUBLIC ${ENGINE_ASAN_LINK_OPTIONS})
  endif()
endif()
if(ENGINE_HAVE_TSAN)
  target_compile_options(foundation_base_tsan PUBLIC ${ENGINE_TSAN_COMPILE_OPTIONS})
  target_link_options(foundation_base_tsan PUBLIC ${ENGINE_TSAN_LINK_OPTIONS})
endif()
