#pragma once

// Helpers the Metal backend's tests share (Phase 21 Day 301). Objective-C++.

// AppKit before anything else: its QuickDraw headers declare global names (`Handle`, `Pattern`) that clash with the
// engine's once a test file's `using namespace` is in scope, which a unity build merges ahead of later files.
#import <AppKit/AppKit.h>

#include "os/console.h"
#include "rhi/backends/backends.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

namespace vse::rhi::metal::test
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

// True when this Mac has a Metal device the backend accepts (the Mac2 family and Metal 3). A virtual machine, as on a
// hosted runner, has none: the tests that need a device skip there, like the Vulkan ones with no driver.
inline bool MetalPresent()
{
    AdapterList adapters;
    if (!EnumerateAdapters(MetalInfo("metal_present"), &adapters))
    {
        return false;
    }
    for (u32 i = 0; i < adapters.count; ++i)
    {
        if (adapters.items[i].suitable)
        {
            return true;
        }
    }
    return false;
}

// Skips the running test when this Mac has no usable Metal device (for a test whose first step is not a device
// creation).
#define METAL_SKIP_WITHOUT_DEVICE()                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!::vse::rhi::metal::test::MetalPresent())                                                                  \
        {                                                                                                              \
            SKIP("no Metal device of the Mac2 family supports Metal 3 here");                                          \
        }                                                                                                              \
    } while (false)

// REQUIRE for the first device a test makes: skips instead of failing when the Mac has no usable Metal device.
#define METAL_REQUIRE_DEVICE(device)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(device) && !::vse::rhi::metal::test::MetalPresent())                                                     \
        {                                                                                                              \
            SKIP("no Metal device of the Mac2 family supports Metal 3 here");                                          \
        }                                                                                                              \
        REQUIRE(device);                                                                                               \
    } while (false)

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

}  // namespace vse::rhi::metal::test
