# Phase 15: the UI toolkit (CPU side): the widget tree, layout, themes, input routing and the 2D draw list. It depends on the text layer, the platform's
# input headers (header-only: platform_input) and the foundation, and names no RHI type: the renderer turns its draw list into a pass. Built plain, ASan and
# TSan from one source list, each linked against the matching text variant, like text. Headers are included as "ui/<name>.h", rooted at engine/.
set(UI_SOURCES
  version.cpp
  draw_list_canvas.cpp
  theme.cpp
  widget.cpp
  surface.cpp
  surface_drag.cpp
  drag_drop.cpp
  layout.cpp
  text_block.cpp
  text_run.cpp
  widgets/display.cpp
  widgets/buttons.cpp
  widgets/scroll.cpp
  widgets/text_field.cpp
  clipboard.cpp
  popup.cpp
  widgets/tooltip.cpp
  widgets/modal_dim.cpp
  widgets/tab_view.cpp
  widgets/splitter.cpp
  widgets/window_frame.cpp
  widgets/dialog.cpp
  widgets/message_box.cpp
  widgets/menu.cpp
  widgets/menu_bar.cpp
  widgets/combobox.cpp
  text_words.cpp
  widgets/value_widgets.cpp
  widgets/list_box.cpp
  viewport.cpp
  update_policy.cpp
  surface_input.cpp
  settings_view.cpp
  debug_overlay.cpp
  testing/software_raster.cpp
  testing/harness.cpp
  testing/recording_canvas.cpp
)
set(UI_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_ui_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _ui_flavors "_asan")
  endif()
  foreach(suffix "" ${_ui_flavors})
    add_library(ui${suffix} STATIC ${UI_SOURCES})
    target_include_directories(ui${suffix} PUBLIC ${UI_INCLUDE_ROOT})
    target_compile_options(ui${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    # text links only foundation_io here (on Linux the foundation aggregate); the settings screen (settings_view.cpp) needs the settings layer.
    target_link_libraries(ui${suffix} PUBLIC text${suffix} foundation_settings${suffix} platform_input)
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(ui${suffix} STATIC ${UI_SOURCES})
  target_include_directories(ui${suffix} PUBLIC ${UI_INCLUDE_ROOT})
  target_compile_options(ui${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(ui${suffix} PUBLIC text${suffix} platform_input)
endforeach()
