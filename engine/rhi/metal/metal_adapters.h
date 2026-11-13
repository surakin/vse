#pragma once

// The adapters of the Metal backend (Phase 26 Day 316): every MTLDevice of the system, as rhi::AdapterInfo.
// Objective-C++.

#include "rhi/adapter.h"

#import <Metal/Metal.h>

namespace engine_placeholder::rhi::metal
{

// The adapters and the MTLDevice behind each (the same order).
struct MetalAdapterSet
{
    AdapterList list;
    NSArray<id<MTLDevice>>* devices = nil;
};

// Lists the system's devices (MTLCopyAllDevices; no queue is created). The id is the registry ID in lowercase hex, the
// driver the macOS version; a device is suitable with the Mac2 family and Metal 3. False when Metal has no device.
bool ListMetalAdapters(MetalAdapterSet* out);

// Records that the system asked to remove the GPU with this registry ID (the device observer); later lists mark it
// unsuitable, so a reset after the loss picks another GPU. Thread-safety: thread-safe.
void NoteMetalRemovalRequested(u64 registry_id);
bool MetalRemovalRequested(u64 registry_id);

// The adapter of `device` as rhi::AdapterInfo (the fields above).
AdapterInfo MetalAdapterInfo(id<MTLDevice> device);

// Picks the adapter a request names (an id, an index or part of the name: rhi::MatchAdapter), else the best suitable
// one (a discrete GPU before an integrated one). -1 with the reason in `reason` when none fits.
int PickMetalAdapter(const MetalAdapterSet& set, const char* request, char* reason, usize reason_size);

}  // namespace engine_placeholder::rhi::metal
