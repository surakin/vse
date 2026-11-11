// Built by tests/macos/CMakeLists.txt as an engine library (engine_add_foundation_layer), so it gets the production options an engine .mm
// file gets: with -fno-exceptions this `try` does not compile, which macos_production_options_planted expects. If it compiled, the engine's
// Objective-C++ would be built with exceptions on, against CLAUDE.md's "exceptions and RTTI remain disabled" (Phase 26 Day 284 review).
namespace engine_placeholder::macos_production_options_planted {
int Planted() {
    try {
        throw 1;
    } catch (...) {
        return 1;
    }
    return 0;
}
} // namespace engine_placeholder::macos_production_options_planted
