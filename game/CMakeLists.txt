# Command-line checks of the settings options every demo has (--list-settings, --help, --set, --save-settings, the environment, the old flags).
# They need no GPU: each program exits before it opens a window or a device. See common/settings_cli_check.cmake.
function(demo_settings_tests demo)
  set(cli ${CMAKE_SOURCE_DIR}/game/common/settings_cli_check.cmake)
  set(common -DEXE=$<TARGET_FILE:${demo}> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_${demo})
  set(golden ${CMAKE_SOURCE_DIR}/game/${demo})
  add_test(NAME ${demo}_settings_list COMMAND ${CMAKE_COMMAND} ${common} -DMODE=golden "-DARGS=--list-settings" -DGOLDEN=${golden}/listsettings.golden -P ${cli})
  add_test(NAME ${demo}_settings_help COMMAND ${CMAKE_COMMAND} ${common} -DMODE=golden "-DARGS=--help" -DGOLDEN=${golden}/help.golden -P ${cli})
  add_test(NAME ${demo}_settings_set_every_type COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex
    "-DARGS=--set;log.logrhi.verbosity=debug;--set;renderer.fullscreen=true;--set;renderer.present_mode=mailbox;--set;renderer.render_scale=0.5;--set;renderer.adapter=\"two words\";--set;renderer.width=96;--list-settings"
    "-DEXPECT=log.logrhi.verbosity +debug .*from command line \\(--set log.logrhi.verbosity=debug\\).*renderer.adapter +\"two words\" .*renderer.fullscreen +true .*renderer.present_mode +mailbox .*renderer.render_scale +0.5 .*renderer.width +96 .*from command line \\(--set renderer.width=96\\)"
    -DEXPECT_CODE=0 -P ${cli})
  add_test(NAME ${demo}_settings_environment COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex "-DENV=ENGINE_RHI_GPU=llvmpipe;ENGINE_SETTING_RENDERER__WIDTH=800" "-DARGS=--list-settings"
    "-DEXPECT=renderer.adapter +\"llvmpipe\" .*from environment \\(\\$ENGINE_RHI_GPU\\).*renderer.width +800 .*from environment \\(\\$ENGINE_SETTING_RENDERER__WIDTH\\)" -DEXPECT_CODE=0 -P ${cli})
  add_test(NAME ${demo}_settings_old_flags COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex "-DARGS=--no-vsync;--validation;--gpu;abc;--backend;x11;--list-settings"
    "-DEXPECT=platform.window_backend +x11 .*renderer.adapter +\"abc\" .*renderer.present_mode +immediate .*renderer.sync_validation +true .*renderer.validation +on " -DEXPECT_CODE=0 -P ${cli})
  add_test(NAME ${demo}_settings_legacy_flag_yields COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex "-DARGS=--no-vsync;--set;renderer.present_mode=fifo;--list-settings"
    "-DEXPECT=renderer.present_mode +fifo .*from command line \\(--set renderer.present_mode=fifo\\)" -DEXPECT_CODE=0 -P ${cli})
  add_test(NAME ${demo}_settings_bad_key COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex "-DARGS=--set;renderer.widht=1;--list-settings"
    "-DEXPECT=did you mean 'renderer.width'" -DEXPECT_CODE=2 -P ${cli})
  add_test(NAME ${demo}_settings_bad_value COMMAND ${CMAKE_COMMAND} ${common} -DMODE=regex "-DARGS=--set;renderer.width=wide;--list-settings"
    "-DEXPECT=command line: renderer.width: bad value" -DEXPECT_CODE=2 -P ${cli})
  add_test(NAME ${demo}_settings_save_reproduce COMMAND ${CMAKE_COMMAND} ${common} -DMODE=reproduce "-DARGS=--set;renderer.width=96;--set;renderer.height=64;--no-vsync"
    "-DEXPECT=\\[renderer\\].*height = 64.*present_mode = \"?immediate\"?.*width = 96"
    "-DEXPECT_LISTING=renderer.height +64 +default 0 +from [^\n]*saved.cfg \\(line [0-9]+\\).*renderer.present_mode +immediate .*renderer.width +96 +default 0 +from [^\n]*saved.cfg" -P ${cli})
  set_tests_properties(${demo}_settings_list ${demo}_settings_help ${demo}_settings_set_every_type ${demo}_settings_environment ${demo}_settings_old_flags
    ${demo}_settings_bad_key ${demo}_settings_bad_value ${demo}_settings_save_reproduce ${demo}_settings_legacy_flag_yields PROPERTIES LABELS "demo")
  engine_label_tests(${demo}_settings_list ${demo}_settings_help ${demo}_settings_set_every_type ${demo}_settings_environment ${demo}_settings_old_flags
    ${demo}_settings_bad_key ${demo}_settings_bad_value ${demo}_settings_save_reproduce ${demo}_settings_legacy_flag_yields)
endfunction()

add_subdirectory(rhi_demo)
add_subdirectory(rhi_bench)
add_subdirectory(renderer_demo)
add_subdirectory(renderer_bench)

# The README's "Settings options" block must be the settings part of --help (so the documentation cannot drift from the registry).
function(demo_readme_test demo)
  add_test(NAME ${demo}_readme_current COMMAND ${CMAKE_COMMAND} -DHELP=${CMAKE_SOURCE_DIR}/game/${demo}/help.golden -DREADME=${CMAKE_SOURCE_DIR}/game/${demo}/README.md
    -P ${CMAKE_SOURCE_DIR}/game/common/readme_current.cmake)
  set_tests_properties(${demo}_readme_current PROPERTIES LABELS "demo;docs")
  engine_label_tests(${demo}_readme_current)
endfunction()
demo_readme_test(rhi_demo)
demo_readme_test(renderer_demo)
