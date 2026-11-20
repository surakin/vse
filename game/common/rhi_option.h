#pragma once

// The benchmarks' launch-only `--rhi NAME` option (the demos have it as the `renderer.backend` setting's flag): the
// graphics backend a name chooses.

#include "cstr/cstr.h"

#include "rhi/rhi.h"

namespace demo
{

// The backend `name` chooses: null, vulkan, dx12, metal or auto. Returns false, leaving `out` alone, for any other
// name.
inline bool ParseRhiBackend(const char* name, ::vse::rhi::Backend* out)
{
    using ::vse::rhi::Backend;
    static const struct
    {
        const char* name;
        Backend backend;
    } kNames[] = {{"null", Backend::Null},
                  {"vulkan", Backend::Vulkan},
                  {"dx12", Backend::Dx12},
                  {"metal", Backend::Metal},
                  {"auto", Backend::Auto}};
    for (const auto& n : kNames)
    {
        if (vse::CStrCompare(name, n.name) == 0)
        {
            *out = n.backend;
            return true;
        }
    }
    return false;
}

// The name `--rhi` takes for a backend, for messages.
inline const char* RhiBackendName(::vse::rhi::Backend backend)
{
    using ::vse::rhi::Backend;
    switch (backend)
    {
    case Backend::Null: return "null";
    case Backend::Vulkan: return "vulkan";
    case Backend::Dx12: return "dx12";
    case Backend::Metal: return "metal";
    case Backend::Auto: return "auto";
    }
    return "?";
}

}  // namespace demo
