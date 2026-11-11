
#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "rhi/rhi_log.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;

namespace
{

char g_capture[1024];
usize g_capture_len = 0;

long CapturingWriteSink(int, const void* buf, usize len)
{
    usize copy = g_capture_len + len > sizeof(g_capture) ? sizeof(g_capture) - g_capture_len : len;
    MemCopy(g_capture + g_capture_len, buf, copy);
    g_capture_len += copy;
    return static_cast<long>(len);
}

// Pins the channel's global state for one test and restores it afterwards,
// so the test does not depend on defaults or on test order.
struct ChannelGuard
{
    LogVerbosity saved_verbosity = rhi::LogRhi.Verbosity();
    bool saved_enabled = rhi::LogRhi.Enabled();
    ChannelGuard()
    {
        g_capture_len = 0;
        rhi::LogRhi.SetEnabled(true);
        SetWriteSinkForTesting(&CapturingWriteSink);
    }
    ~ChannelGuard()
    {
        SetWriteSinkForTesting(&engine_placeholder::os::Write);
        rhi::LogRhi.SetVerbosity(saved_verbosity);
        rhi::LogRhi.SetEnabled(saved_enabled);
    }
};

}  // namespace

TEST_CASE("RHI log channel is named LogRhi", "[rhi]")
{
    // Only the name is asserted here: the verbosity is global mutable state
    // that configuration may override, so the default is checked via the
    // construction constant in the channel's own definition (rhi.cpp).
    CHECK(StringView(rhi::LogRhi.Name()) == StringView("LogRhi"));
}

TEST_CASE("RhiLog formats on the RHI channel and honors the verbosity filter", "[rhi]")
{
    ChannelGuard guard;
    rhi::LogRhi.SetVerbosity(LogVerbosity::Warn);

    RhiLog(LogVerbosity::Warn, "device %d has %s", 3, "no queue");
    RhiLog(LogVerbosity::Debug, "filtered out %d", 1);

    StringView out(g_capture, g_capture_len);
    CHECK(out.Contains(StringView("[LogRhi]")));
    CHECK(out.Contains(StringView("device 3 has no queue")));
    CHECK_FALSE(out.Contains(StringView("filtered out")));
}

TEST_CASE("RhiLog truncates an over-long message instead of overflowing", "[rhi]")
{
    ChannelGuard guard;
    rhi::LogRhi.SetVerbosity(LogVerbosity::Warn);

    char big[2000];
    MemSet(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    RhiLog(LogVerbosity::Warn, "%s", big);

    CHECK(g_capture_len > 0);
    CHECK(g_capture_len < 600);
}
