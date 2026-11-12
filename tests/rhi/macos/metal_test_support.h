#pragma once

// Helpers the Metal backend's tests share (Phase 26 Day 317). Objective-C++.

// AppKit before anything else: its QuickDraw headers declare global names (`Handle`, `Pattern`) that clash with the
// engine's once a test file's `using namespace` is in scope, which a unity build merges ahead of later files.
#import <AppKit/AppKit.h>

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

// The Metal device behind a device the Metal backend made.
inline MetalDevice& AsMetal(Device& device)
{
    return static_cast<MetalDevice&>(device);
}

}  // namespace engine_placeholder::rhi::metal::test
