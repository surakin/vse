# The input probe (Phase 15 Day 186): opens a window and prints the input event stream, so a person can check the events, the cursor shapes, a custom cursor
# image and the pointer lock by eye on both backends. Keys: L toggles the pointer lock, C cycles the cursor shapes, I toggles a generated cursor image, Escape quits.
add_executable(input_probe main.cpp)
target_compile_options(input_probe PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(input_probe PRIVATE platform)
