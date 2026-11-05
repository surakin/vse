// Points LeakSanitizer at the RHI tests' suppressions (tests/rhi/lsan.supp; a reason is documented for every entry
// there): the renderer's GPU tests create a real Vulkan device and hit the same driver-owned process-lifetime
// allocations. Only consulted when the binary is built with -fsanitize=address; harmless otherwise.
extern "C" const char* __lsan_default_options() {
    return "suppressions=" ENGINE_RHI_LSAN_SUPP;
}
