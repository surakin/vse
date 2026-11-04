// Points LeakSanitizer at tests/rhi/lsan.supp (a reason is documented for every entry there). Only consulted when the
// binary is built with -fsanitize=address; harmless otherwise.
extern "C" const char* __lsan_default_options() {
    return "suppressions=" ENGINE_RHI_LSAN_SUPP;
}
