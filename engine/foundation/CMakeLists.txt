# Per-platform implementation of os/os.h — only os/linux/ exists so far
# (PLAN.md assumption #11: implementation stays Linux-only until its
# phase; this selection is where a later os/windows/os.cpp or
# os/macos/os.cpp plugs in additively, no other CMakeLists change needed).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FOUNDATION_PLATFORM_SOURCES
    os/linux/os.cpp
    os/linux/command_line.cpp
    os/linux/xkb_keyboard.cpp
    os/linux/wayland/wayland_input.cpp
    os/linux/x11/x11_window.cpp
    os/linux/wayland/wayland_platform_window.cpp
    os/linux/platform_window_host.cpp
    os/linux/wayland/wayland_connection.cpp
    os/linux/wayland/wayland_window.cpp
    os/linux/wayland/generated/xdg-shell-protocol.c
    os/linux/wayland/generated/xdg-decoration-protocol.c
  )

  # Phase 6: the per-platform half of the debugging facilities (debug/*.h are the neutral interfaces). Windows and macOS
  # add os/<platform>/debug/ in Phases 10 and 11 (see the BACKEND.md checklists there).
  set(FOUNDATION_DEBUG_PLATFORM_SOURCES
    os/linux/debug/death_test.cpp
    os/linux/debug/debugger.cpp
    os/linux/debug/module_list.cpp
    os/linux/debug/crash_context.cpp
    os/linux/debug/crash_signal.cpp
    os/linux/debug/core_dump.cpp
    os/linux/debug/fault_injection.cpp
  )

  # Day 18: libwayland-client, the reference Wayland client library
  # (PLAN.md assumption #3 — an OS/vendor-boundary exception, not a
  # third-party convenience dependency). xdg-shell bindings are
  # generated-and-committed (see
  # os/linux/wayland/generated/VENDORED.md), so wayland-scanner/
  # wayland-protocols are dev-time-only, not linked here.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
  # Day 21: libxkbcommon, the reference keymap-interpretation library
  # Wayland compositors expect clients to use. A named dependency the
  # user explicitly approved on 2026-11-03 (see
  # plans/phase-01-linux-platform.md Notes).
  pkg_check_modules(XkbCommon REQUIRED IMPORTED_TARGET xkbcommon)
  # Day 24: libX11, the reference X client library, for the X11 fallback
  # backend (PLAN.md assumption #3 — same OS-boundary category as
  # libwayland-client).
  pkg_check_modules(X11Client REQUIRED IMPORTED_TARGET x11)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "Windows is implemented in Phase 10 (the debugging backend: os/windows/debug/BACKEND.md)")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS is implemented in Phase 11 (the debugging backend: os/macos/debug/BACKEND.md)")
else()
  message(FATAL_ERROR "No foundation os.h implementation for this platform yet")
endif()

set(FOUNDATION_SOURCES
  foundation.cpp
  os/key_translate.cpp
  os/window.cpp
  os/file_util.cpp
  os/gpu_driver_probe.cpp
  mem/arena_allocator.cpp
  mem/pool_allocator.cpp
  mem/stack_allocator.cpp
  log/log.cpp
  io/path.cpp
  io/linux/error.cpp
  io/linux/file_system.cpp
  string/utf8_codec.cpp
  string/grapheme.cpp
  string/generated/grapheme_break_table.cpp
  string/normalize.cpp
  string/generated/decomposition_table.cpp
  string/generated/combining_class_table.cpp
  string/generated/composition_exclusions_table.cpp
  debug/crash_reason.cpp
  debug/death_test.cpp
  debug/stack_trace_backtrace.cpp
  debug/thread_info.cpp
  debug/crash_context.cpp
  debug/crash_state.cpp
  debug/program_setup.cpp
  debug/crash_report.cpp
  debug/crash_handler_sentry.cpp
  ${FOUNDATION_PLATFORM_SOURCES}
  ${FOUNDATION_DEBUG_PLATFORM_SOURCES}
)

# ENGINE_PRODUCTION_COMPILE_OPTIONS (-fno-exceptions etc) is applied
# PRIVATE, not PUBLIC: PUBLIC compile options are a usage requirement
# that would leak into every consumer that links this target, including
# /tests/foundation's Catch2-based foundation_tests executable, which
# needs exceptions. Production executables (engine_main*) that want the
# same hardening declare it themselves instead of inheriting it.
# ENGINE_PRODUCTION_COMPILE_OPTIONS is C++-only (-fno-exceptions/-fno-rtti
# etc); the generated xdg-shell-protocol.c source is compiled as C, so
# these flags are scoped to CXX translation units via generator
# expression rather than applied blindly to the whole target.
add_library(foundation STATIC ${FOUNDATION_SOURCES})
target_include_directories(foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(foundation PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
# Day 17: os::LoadLibrary wraps dlopen/dlsym/dlclose. CMAKE_DL_LIBS is
# the portable way to link libdl where it's a separate library (older
# glibc, other libc implementations) and empty where dlopen already
# lives in libc (glibc >= 2.34) — a link requirement, so PUBLIC is
# correct here (unlike the compile-options split above).
target_link_libraries(foundation PUBLIC ${CMAKE_DL_LIBS})
# Phase 6: libbacktrace (third_party/libbacktrace), used only by debug/stack_trace_backtrace.cpp; one plain build for every flavor.
target_link_libraries(foundation PRIVATE backtrace sentry)
# Day 18: libwayland-client (see the platform-selection block above).
target_link_libraries(foundation PUBLIC PkgConfig::WaylandClient PkgConfig::XkbCommon PkgConfig::X11Client)

add_executable(engine_main main.cpp)
target_compile_options(engine_main PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(engine_main PRIVATE foundation)

# Sanitized variants: separate targets built from the same sources, not
# a build-wide flag, so both the normal and sanitized binaries exist
# side by side from one configure — `cmake --build build --target
# engine_main_asan && ./build/engine/foundation/engine_main_asan`.
# The sanitizer flags themselves stay PUBLIC (ASan/UBSan need the whole
# binary instrumented consistently, including engine_main_asan's own
# translation units and the runtime it links against) — only the
# exceptions/RTTI hardening moved to PRIVATE above.
add_library(foundation_asan STATIC ${FOUNDATION_SOURCES})
target_include_directories(foundation_asan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(foundation_asan PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
# -fno-sanitize=vptr: the engine libraries are built -fno-rtti, so they define no typeinfo, while the test executables
# (Catch2 needs RTTI) are not. With the vptr check on, GCC 16 makes every virtual call through a library class reference
# that class's typeinfo and the link fails (`undefined reference to typeinfo for ...NullDevice`). It must come after
# -fsanitize=undefined, which turns the check back on.
target_compile_options(foundation_asan PUBLIC -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g)
target_link_options(foundation_asan PUBLIC -fsanitize=address,undefined)
target_link_libraries(foundation_asan PUBLIC ${CMAKE_DL_LIBS})
# Phase 6: libbacktrace (third_party/libbacktrace), used only by debug/stack_trace_backtrace.cpp; one plain build for every flavor.
target_link_libraries(foundation_asan PRIVATE backtrace sentry)
target_link_libraries(foundation_asan PUBLIC PkgConfig::WaylandClient PkgConfig::XkbCommon PkgConfig::X11Client)

add_executable(engine_main_asan main.cpp)
target_compile_options(engine_main_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(engine_main_asan PRIVATE foundation_asan)

# Phase 2 Day 27: ThreadSanitizer variant. A separate build from the
# ASan/UBSan one above because TSan cannot be combined with ASan. The
# job system's lock-free code (Days 30-32) must be proven race-free under
# this, and TSan only gives trustworthy answers when every library that
# touches shared memory (including foundation) is instrumented too.
add_library(foundation_tsan STATIC ${FOUNDATION_SOURCES})
target_include_directories(foundation_tsan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(foundation_tsan PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
target_compile_options(foundation_tsan PUBLIC -fsanitize=thread -fno-omit-frame-pointer -g)
target_link_options(foundation_tsan PUBLIC -fsanitize=thread)
target_link_libraries(foundation_tsan PUBLIC ${CMAKE_DL_LIBS})
# Phase 6: libbacktrace (third_party/libbacktrace), used only by debug/stack_trace_backtrace.cpp; one plain build for every flavor.
target_link_libraries(foundation_tsan PRIVATE backtrace sentry)
target_link_libraries(foundation_tsan PUBLIC PkgConfig::WaylandClient PkgConfig::XkbCommon PkgConfig::X11Client)
