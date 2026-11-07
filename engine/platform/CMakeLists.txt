# The platform layer (layer 9, ARCHITECTURE.md): windows, input and the display backends. It is its own library so that only the
# programs and libraries that need a window link Wayland, X11 and xkbcommon; the foundation does not.
# Headers are included as "platform/<name>.h", rooted at engine/ (the foundation's own headers come from engine/foundation).
set(PLATFORM_SOURCES
  window.cpp
  key_translate.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND PLATFORM_SOURCES
    linux/xkb_keyboard.cpp
    linux/wayland/wayland_input.cpp
    linux/x11/x11_window.cpp
    linux/wayland/wayland_platform_window.cpp
    linux/window_host_linux.cpp
    linux/wayland/wayland_connection.cpp
    linux/wayland/wayland_window.cpp
    linux/wayland/generated/xdg-shell-protocol.c
    linux/wayland/generated/xdg-decoration-protocol.c
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
  set(PLATFORM_DISPLAY_LIBS PkgConfig::WaylandClient PkgConfig::XkbCommon PkgConfig::X11Client)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "Windows is implemented in Phase 13 (the window backend: platform/windows/FULLSCREEN.md)")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS is implemented in Phase 14 (the window backend: platform/macos/FULLSCREEN.md)")
else()
  message(FATAL_ERROR "No window backend for this platform yet")
endif()

# One library per sanitizer flavor, each on the matching foundation (the flags come with it, as for jobs and the RHI).
function(engine_add_platform_lib suffix foundation_lib)
  add_library(platform${suffix} STATIC ${PLATFORM_SOURCES})
  target_include_directories(platform${suffix} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_options(platform${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
  target_link_libraries(platform${suffix} PUBLIC ${foundation_lib} ${PLATFORM_DISPLAY_LIBS})
endfunction()

engine_add_platform_lib("" foundation)
engine_add_platform_lib("_asan" foundation_asan)
engine_add_platform_lib("_tsan" foundation_tsan)
