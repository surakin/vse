# The platform layer (layer 9, ARCHITECTURE.md): windows, input and the display backends. It is its own library so that only the
# programs and libraries that need a window link Wayland, X11 and xkbcommon; the foundation does not.
# Headers are included as "platform/<name>.h", rooted at engine/ (the foundation's own headers come from engine/foundation).
set(PLATFORM_SOURCES
  window.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND PLATFORM_SOURCES
    linux/key_translate.cpp  # X11 keysyms to Key, shared by both Linux backends
    linux/xkb_keyboard.cpp
    linux/keyboard_events.cpp  # evdev key transitions to KeyDown/KeyUp/Text and client-side repeat (Wayland)
    linux/wayland/wayland_input.cpp
    linux/x11/x11_window.cpp
    linux/wayland/wayland_platform_window.cpp
    linux/window_host_linux.cpp
    linux/entry_point_linux.cpp
    linux/wayland/wayland_connection.cpp
    linux/wayland/wayland_window.cpp
    linux/wayland/generated/xdg-shell-protocol.c
    linux/wayland/generated/xdg-decoration-protocol.c
    linux/wayland/generated/cursor-shape-v1-protocol.c
    linux/wayland/generated/tablet-v2-protocol.c
    linux/wayland/generated/pointer-constraints-unstable-v1-protocol.c
    linux/wayland/generated/relative-pointer-unstable-v1-protocol.c
    linux/wayland/wayland_cursor.cpp
  )

  # libwayland-client, the reference Wayland client library (PLAN.md assumption #3: an OS/vendor-boundary exception, not a
  # third-party convenience dependency). The xdg-shell bindings are generated and committed (see
  # linux/wayland/generated/VENDORED.md), so wayland-scanner and wayland-protocols are dev-time only and not linked.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
  # libxkbcommon, the reference keymap-interpretation library Wayland compositors expect clients to use. A named dependency
  # the user explicitly approved on 2026-11-03 (see plans/phase-01-linux-platform.md Notes).
  pkg_check_modules(XkbCommon REQUIRED IMPORTED_TARGET xkbcommon)
  # libX11, the reference X client library, for the X11 fallback backend (PLAN.md assumption #3, same category as libwayland-client).
  pkg_check_modules(X11Client REQUIRED IMPORTED_TARGET x11)
  # libXcursor (cursor themes and ARGB cursors) and libXi (XInput 2 raw motion for the pointer lock) are X.org client libraries of libX11's own family, shipped
  # with it on every X11 system (PLAN.md assumption #3).
  pkg_check_modules(XcursorClient REQUIRED IMPORTED_TARGET xcursor)
  pkg_check_modules(XiClient REQUIRED IMPORTED_TARGET xi)
  set(PLATFORM_DISPLAY_LIBS PkgConfig::WaylandClient PkgConfig::XkbCommon PkgConfig::X11Client PkgConfig::XcursorClient PkgConfig::XiClient)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Phase 13 Day 158: the Win32 window and the windowed programs' entry point (input on Day 159). user32 holds the window functions,
  # shell32 CommandLineToArgvW.
  list(APPEND PLATFORM_SOURCES
    windows/win32_window.cpp
    windows/win32_keys.cpp
    windows/window_host_windows.cpp
    windows/entry_point_windows.cpp
  )
  set(PLATFORM_DISPLAY_LIBS user32 shell32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS is implemented in Phase 22 (the window backend: platform/macos/FULLSCREEN.md)")
else()
  message(FATAL_ERROR "No window backend for this platform yet")
endif()

# The pure-data input headers (input.h, later input_event.h) without the display libraries, for the modules that only need the types (ui).
add_library(platform_input INTERFACE)
target_include_directories(platform_input INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# One library per sanitizer flavor, each on the matching foundation_debug (crash state, the lowest layer it uses; the flags come with it, as for jobs and the RHI).
function(engine_add_platform_lib suffix foundation_lib)
  add_library(platform${suffix} STATIC ${PLATFORM_SOURCES})
  target_include_directories(platform${suffix} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_options(platform${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
  target_link_libraries(platform${suffix} PUBLIC ${foundation_lib} ${PLATFORM_DISPLAY_LIBS})
endfunction()

engine_add_platform_lib("" foundation_debug)
if(ENGINE_HAVE_ASAN)
  engine_add_platform_lib("_asan" foundation_debug_asan)
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_platform_lib("_tsan" foundation_debug_tsan)
endif()
