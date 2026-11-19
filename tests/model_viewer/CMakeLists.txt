# The model viewer's orbit camera (maintenance, 2026-11-19): plain math, no window or device. Catch2's macros use exceptions, so the
# production compile options are not applied, as in the other test directories.
find_package(Threads REQUIRED)
include(Catch)
add_executable(model_viewer_tests orbit_camera_test.cpp)
target_link_libraries(model_viewer_tests PRIVATE model_viewer_lib Catch2::Catch2WithMain Threads::Threads)
engine_discover_tests(model_viewer_tests PROPERTIES LABELS "model_viewer")
