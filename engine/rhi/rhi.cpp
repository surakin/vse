#include <atomic>

#include "rhi/device.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi {

LogChannel LogRhi("LogRhi", LogVerbosity::Warn);

u32 AllocateDeviceSerial() {
    static std::atomic<u32> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace engine_placeholder::rhi
