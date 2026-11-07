// Uses of engine names that <windows.h> also defines as macros, compiled after the engine headers and then <windows.h> (through
// os/windows/win32.h in windows_header_check, bare in windows_header_planted). Shared by both so they differ only in the wrapper.
// Never called: the targets are object libraries that only compile.
namespace windows_header_test {

// ENGINE_NO_UNIQUE_ADDRESS (debug/compiler.h): cl and clang-cl ignore the standard [[no_unique_address]], which made a UniquePtr with
// its stateless deleter twice the size of a pointer (found on Phase 13 Day 148; tests/foundation/unique_ptr_test.cpp asserts the same).
static_assert(sizeof(engine_placeholder::UniquePtr<int>) == sizeof(int*), "the deleter of UniquePtr must take no space");

inline void UseClashingNames() {
    namespace ep = engine_placeholder;
    ep::os::DynamicLibraryHandle library = ep::os::LoadLibrary(ep::StringView("example.dll"));
    ep::os::UnloadLibrary(library);
    ep::io::Path path;
    (void)ep::io::CreateDirectory(path);
    (void)ep::io::SetCurrentDirectory(path);
    float near = 0.1f;  // the camera's parameter names; <windows.h> defines both as empty macros
    float far = 100.0f;
    float small = near + far;
    (void)small;
}

} // namespace windows_header_test
