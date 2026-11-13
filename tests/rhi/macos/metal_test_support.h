#pragma once

// Helpers the Metal backend's tests share (Phase 21 Day 301). Objective-C++.

// AppKit before anything else: its QuickDraw headers declare global names (`Handle`, `Pattern`) that clash with the
// engine's once a test file's `using namespace` is in scope, which a unity build merges ahead of later files.
#import <AppKit/AppKit.h>

#include "os/console.h"
#include "rhi/backends/backends.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

namespace engine_placeholder::rhi::metal::test
{

// A description for a Metal device, with the compiled backends registered.
inline DeviceCreateInfo MetalInfo(const char* application = "metal_test")
{
    RegisterCompiledBackends();
    DeviceCreateInfo info;
    info.backend = Backend::Metal;
    info.application_name = application;
    return info;
}

// A shader of `stage` from a minted blob set's Metal library (with its entry and slot table).
inline ShaderHandle MetalShader(Device& device, const ShaderBlobSet& set, ShaderStage stage)
{
    ShaderDesc d;
    d.stage = stage;
    d.SetCode(set.For(ShaderFormat::MetalLib));
    d.debug_name = set.name;
    return device.CreateShader(d);
}

// The Metal device behind a device the Metal backend made.
inline MetalDevice& AsMetal(Device& device)
{
    return static_cast<MetalDevice&>(device);
}

// Whether Metal's shader validation is on for this process, which leaves the pipeline archive off (Day 309). The
// archive tests carry the tag [archive] and run in a ctest of their own without it (tests/rhi/CMakeLists.txt).
inline bool ShaderValidationOn()
{
    const char* value = os::GetEnvironment("MTL_SHADER_VALIDATION");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

}  // namespace engine_placeholder::rhi::metal::test
