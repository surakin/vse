// Points ThreadSanitizer at the script tests' suppression (tests/script/tsan.supp; the reason is documented there):
// libgcc's lock-coupled B-tree of registered unwind frames. Only consulted when the binary is built with
// -fsanitize=thread; harmless otherwise.
extern "C" const char* __tsan_default_options()
{
    return "suppressions=" ENGINE_SCRIPT_TSAN_SUPP;
}
