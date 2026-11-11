// Included last by the generated macos_header_planted.mm (tests/macos/CMakeLists.txt), after AppKit, Metal and QuartzCore were included
// with __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES set to 1: an ordinary function named like one of the old AssertMacros.h macros.
// With the macros defined, the declaration below expands into AssertMacros.h's statement and does not compile, which is why the engine's
// targets pin the setting to 0 (cmake/EngineCompilerOptions.cmake, ENGINE_MACOS_DEFINITIONS).
namespace engine_placeholder::macos_header_planted {
inline bool verify(bool condition) { return condition; }
} // namespace engine_placeholder::macos_header_planted
