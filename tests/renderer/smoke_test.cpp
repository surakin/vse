#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "renderer/renderer_info.h"
#include "renderer/renderer_log.h"

using namespace engine_placeholder;

namespace {

char g_capture[1024];
usize g_capture_len = 0;

long CapturingWriteSink(int, const void* buf, usize len) {
    usize copy = g_capture_len + len > sizeof(g_capture) ? sizeof(g_capture) - g_capture_len : len;
    std::memcpy(g_capture + g_capture_len, buf, copy);
    g_capture_len += copy;
    return static_cast<long>(len);
}

// Pins the channel's global state for one test and restores it afterwards, so the test does not depend on defaults or
// on test order.
struct ChannelGuard {
    LogVerbosity saved_verbosity = LogRender.Verbosity();
    bool saved_enabled = LogRender.Enabled();
    ChannelGuard() {
        g_capture_len = 0;
        LogRender.SetEnabled(true);
        SetWriteSinkForTesting(&CapturingWriteSink);
    }
    ~ChannelGuard() {
        SetWriteSinkForTesting(&engine_placeholder::os::Write);
        LogRender.SetVerbosity(saved_verbosity);
        LogRender.SetEnabled(saved_enabled);
    }
};

} // namespace

TEST_CASE("The renderer identifies itself", "[renderer][smoke]") {
    CHECK(renderer::RendererName() != nullptr);
    CHECK(std::strlen(renderer::RendererName()) > 0);
}

TEST_CASE("RenderLog formats on the render channel and honors the verbosity filter", "[renderer][smoke]") {
    ChannelGuard guard;
    LogRender.SetVerbosity(LogVerbosity::Warn);

    RenderLog(LogVerbosity::Warn, "format %s is missing (%d)", "RG16_Float", 3);
    RenderLog(LogVerbosity::Debug, "filtered out %d", 1);

    StringView out(g_capture, g_capture_len);
    CHECK(out.Contains(StringView("[LogRender]")));
    CHECK(out.Contains(StringView("format RG16_Float is missing (3)")));
    CHECK_FALSE(out.Contains(StringView("filtered out")));
}

TEST_CASE("RenderLog truncates an over-long message instead of overflowing", "[renderer][smoke]") {
    ChannelGuard guard;
    LogRender.SetVerbosity(LogVerbosity::Warn);

    char big[2000];
    std::memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    RenderLog(LogVerbosity::Warn, "%s", big);

    CHECK(g_capture_len > 0);
    CHECK(g_capture_len < 600);
}
