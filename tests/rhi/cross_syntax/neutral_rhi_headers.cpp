// Every backend-neutral RHI header, included in one translation unit for the cross-target syntax check (Phase 7 Day 114). Nothing
// here may depend on Vulkan or on a Linux-only facility: the DX12 and Metal backends implement these interfaces.
#include "rhi/adapter.h"
#include "rhi/caps.h"
#include "rhi/command_list.h"
#include "rhi/desc.h"
#include "rhi/device.h"
#include "rhi/device_host.h"
#include "rhi/format.h"
#include "rhi/handle.h"
#include "rhi/rhi.h"
#include "rhi/swapchain.h"
#include "os/window.h"
