#include <atomic>

#include "rhi/desc.h"
#include "rhi/device.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi {

LogChannel LogRhi("LogRhi", LogVerbosity::Warn);

ShaderHandle CreateShaderFromSet(Device& device, const ShaderBlobSet& set, ShaderStage stage, const char* debug_name) {
    const ShaderFormat format = device.Caps().shader_format;
    const ShaderBlob blob = set.For(format);
    const char* name = debug_name != nullptr ? debug_name : set.name;
    if (blob.Empty()) {
        RhiLog(LogVerbosity::Error, "CreateShader: the shader '%s' has no %s bytecode, which the %s device takes", set.name != nullptr ? set.name : "",
               ShaderFormatName(format), device.BackendName());
        return ShaderHandle{};
    }
    ShaderDesc desc;
    desc.stage = stage;
    desc.SetCode(blob);
    desc.debug_name = name;
    return device.CreateShader(desc);
}

u32 AllocateDeviceSerial() {
    static std::atomic<u32> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace engine_placeholder::rhi
