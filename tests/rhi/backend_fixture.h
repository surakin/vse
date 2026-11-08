#pragma once

#include <exception>

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "containers/vector.h"
#include "gpu_fixture.h"
#include "rhi/backend_registry.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_instance.h"
#if ENGINE_PLATFORM_WINDOWS
#include "rhi/dx12/dx12_debug.h"
#endif

// GPU tests that run once per backend (Phase 13 Day 170): the golden images and the checks around them, through the RHI interface only, so
// one test body is checked against Vulkan and, on Windows, Direct3D 12. Write
//
//     FOR_EACH_GPU_BACKEND(backend) {
//         gpu_test::BackendValidation validation(backend);
//         rhi::DevicePtr device = gpu_test::CreateTestDevice(backend);
//         ...
//     }
//
// Each backend is its own section, named after it, so a failure names the backend; a backend the machine cannot run is skipped (failed
// under ENGINE_RHI_REQUIRE_GPU=1). Vulkan-only checks (the loader, the instance, memory policy, SPIR-V) stay in the vulkan_* files with
// gpu_fixture.h. Guide: docs/guide/building-and-testing.md, "GPU tests per backend".
namespace gpu_test {

// The GPU backends registered in this program that this machine can use: Vulkan where its loader and a device are found, Direct3D 12 on
// Windows. The null backend is not a GPU.
inline engine_placeholder::Vector<engine_placeholder::rhi::Backend> GpuBackends() {
    using namespace engine_placeholder::rhi;
    engine_placeholder::Vector<Backend> out;
    for (engine_placeholder::usize i = 0; i < RegisteredBackendCount(); ++i) {
        const BackendDescriptor& d = RegisteredBackend(i);
        char reason[256];
        if (d.backend != Backend::Null && BackendAvailable(d, reason, sizeof(reason))) out.Push(d.backend);
    }
    return out;
}

// The backend's name as a section name and in messages.
inline const char* BackendTag(engine_placeholder::rhi::Backend backend) {
    using engine_placeholder::rhi::Backend;
    return backend == Backend::Vulkan ? "vulkan" : backend == Backend::Dx12 ? "dx12" : backend == Backend::Null ? "null" : "other";
}

// Fails the test if the backend's validation reported an error while this lives (and, for Direct3D 12, a warning: its debug layer warns
// about real mistakes too). Construct it before the device, so messages from the device's creation count.
class BackendValidation {
public:
    explicit BackendValidation(engine_placeholder::rhi::Backend backend) : backend_(backend) {
        engine_placeholder::rhi::vulkan::ResetValidationCounts();
#if ENGINE_PLATFORM_WINDOWS
        engine_placeholder::rhi::dx12::ResetDebugMessageCounts();
#endif
    }
    ~BackendValidation() {
        if (std::uncaught_exceptions() != 0) {
            return; // a failed REQUIRE or a SKIP is unwinding: the test reported its own cause
        }
        INFO("backend " << BackendTag(backend_));
        if (backend_ == engine_placeholder::rhi::Backend::Vulkan) {
            CHECK(engine_placeholder::rhi::vulkan::GetValidationCounts().errors == expected_errors_);
        }
#if ENGINE_PLATFORM_WINDOWS
        if (backend_ == engine_placeholder::rhi::Backend::Dx12) {
            const engine_placeholder::rhi::dx12::DebugMessageCounts c = engine_placeholder::rhi::dx12::GetDebugMessageCounts();
            CHECK(c.errors == expected_errors_);
            CHECK(c.warnings == 0);
        }
#endif
    }
    BackendValidation(const BackendValidation&) = delete;
    BackendValidation& operator=(const BackendValidation&) = delete;

    // Declares that the test provokes this many validation errors on purpose.
    void ExpectErrors(unsigned long long n) { expected_errors_ = n; }

private:
    engine_placeholder::rhi::Backend backend_;
    unsigned long long expected_errors_ = 0;
};

// A device on `backend` with validation on (and Vulkan's synchronization validation), no pipeline cache; skips the test when the machine
// cannot make one (fails it under ENGINE_RHI_REQUIRE_GPU=1).
inline engine_placeholder::rhi::DevicePtr CreateTestDevice(engine_placeholder::rhi::Backend backend,
                                                           engine_placeholder::rhi::DeviceCreateInfo info = engine_placeholder::rhi::DeviceCreateInfo{}) {
    using namespace engine_placeholder::rhi;
    info.backend = backend;
    info.validation = ValidationSetting::On;
    info.sync_validation = backend == Backend::Vulkan;
    info.pipeline_cache = false;
    DevicePtr device = CreateDevice(info);
    if (!device) {
        if (GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no " << BackendTag(backend) << " device with validation could be created");
        }
        SKIP("no " << BackendTag(backend) << " device with validation on this machine");
    }
    return device;
}

} // namespace gpu_test

namespace gpu_test {

// Walks the GPU backends for FOR_EACH_GPU_BACKEND (Vector has no iterators).
struct BackendCursor {
    engine_placeholder::Vector<engine_placeholder::rhi::Backend> backends = GpuBackends();
    engine_placeholder::usize next = 0;
    // With no GPU backend the body would run zero times and the test pass with nothing checked: skip instead (fail under
    // ENGINE_RHI_REQUIRE_GPU=1), as the Vulkan-only fixtures do.
    BackendCursor() {
        if (backends.Size() == 0) {
            if (GpuRequired()) {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no GPU backend is available");
            }
            SKIP("no GPU backend is available on this machine");
        }
    }
    bool Next() { return next++ < backends.Size(); }
    engine_placeholder::rhi::Backend Current() const { return backends[next - 1]; }
};

} // namespace gpu_test

// Runs the following block once per GPU backend (gpu_test::GpuBackends), each in a section named after the backend. The inner loop runs
// once: it only declares `var`.
#define FOR_EACH_GPU_BACKEND(var)                                                                                          \
    for (::gpu_test::BackendCursor gpu_backend_cursor_; gpu_backend_cursor_.Next();)                                       \
        for (const ::engine_placeholder::rhi::Backend var = gpu_backend_cursor_.Current(), *gpu_backend_once_ = &var;      \
             gpu_backend_once_ != nullptr; gpu_backend_once_ = nullptr)                                                    \
            DYNAMIC_SECTION(::gpu_test::BackendTag(var))
