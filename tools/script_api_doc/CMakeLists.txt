# script_api_doc: Phase 27 Day 450. Renders docs/reference/script-api.md from the binding registry; the ctest docs_script_api fails when the page on disk
# is not what the registry renders, and the `docs` target rewrites it. Never linked into the engine.
add_executable(script_api_doc main.cpp)
target_compile_options(script_api_doc PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(script_api_doc PRIVATE script foundation)
add_test(NAME docs_script_api COMMAND script_api_doc --check WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(docs_script_api PROPERTIES LABELS "docs" TIMEOUT 300)
engine_label_tests(docs_script_api)
if(TARGET docs)
  add_custom_target(script_api_page COMMAND script_api_doc --write WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Regenerating docs/reference/script-api.md" VERBATIM)
  add_dependencies(docs script_api_page)
endif()
