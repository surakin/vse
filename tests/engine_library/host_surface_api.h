/* Phase 24 Day 381: the graphics API a C test host asks for, read from HOST_SURFACE_API, shared by the window hosts
   (host_window.h) and the off-screen hosts (host_offscreen.c, host_acceptance_scene.c), so each GPU test on Windows runs
   once with Direct3D 12 and once with Vulkan. Plain C. */
#ifndef HOST_SURFACE_API_H
#define HOST_SURFACE_API_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"

/* The graphics API a scene test asks for (Day 369): HOST_SURFACE_API=vulkan or d3d12 gives that ENGINE_SURFACE_FLAG_*,
   anything else 0 (the platform's own). The hosts write it into engine_surface_info.flags after host_window_describe,
   so each scene test on Windows runs once with Direct3D 12 and once with Vulkan. */
static inline uint32_t host_surface_api_flags(void)
{
    const char* api = getenv("HOST_SURFACE_API");
    if (api != NULL && strcmp(api, "vulkan") == 0)
    {
        return ENGINE_SURFACE_FLAG_VULKAN;
    }
    if (api != NULL && strcmp(api, "d3d12") == 0)
    {
        return ENGINE_SURFACE_FLAG_D3D12;
    }
    return 0;
}

#endif
