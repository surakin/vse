#pragma once

#include "os/console.h"

#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "containers/vector.h"
#include "test_text.h"

#include "rhi/vulkan/vk_instance.h"

// The shared fixture for every `[gpu]` test (docs/superpowers/specs/2026-11-04-rhi-design.md, sections 10 and 12).
//
// Construct it at the top of a test and call `Require()`: it skips the test when this machine has no Vulkan
// (or fails it under ENGINE_RHI_REQUIRE_GPU=1). It owns a validation-enabled instance with synchronization
// validation on, records every validation message, and at teardown fails the test if any validation error was
// reported. A test that provokes an error on purpose calls `ExpectErrors(n)` first.
namespace gpu_test {

inline bool GpuRequired() {
    const char* v = engine_placeholder::os::GetEnvironment("ENGINE_RHI_REQUIRE_GPU");
    return v != nullptr && v[0] == '1';
}

struct CapturedMessage {
    engine_placeholder::LogVerbosity severity;
    engine_test::Text id;
    engine_test::Text text;
};

class GpuFixture {
public:
    explicit GpuFixture(bool presentable = false) {
        using namespace engine_placeholder::rhi::vulkan;
        if (LoadVulkanLoader(&loader_) != LoaderStatus::Ok) {
            return;
        }
        ResetValidationCounts();
        SetValidationCapture(&GpuFixture::Capture, this);
        VulkanInstanceDesc desc;
        desc.application_name = "gpu-test";
        desc.presentable = presentable;
        desc.validation = ValidationMode::On;
        desc.sync_validation = true;
        instance_ = VulkanInstance::Create(loader_, desc);
    }

    GpuFixture(const GpuFixture&) = delete;
    GpuFixture& operator=(const GpuFixture&) = delete;

    ~GpuFixture() {
        using namespace engine_placeholder::rhi::vulkan;
        instance_ = VulkanInstance{}; // destroy before the final count so messages from vkDestroyInstance are included
        SetValidationCapture(nullptr, nullptr);
        // Skip the check while a failed REQUIRE/SKIP is unwinding: the test already reported its own cause.
        if (loader_.library != nullptr && !engine_test::TestIsUnwinding()) {
            ValidationCounts counts = GetValidationCounts();
            CHECK(counts.errors == expected_errors_);
            ResetValidationCounts();
        }
    }

    // Skips the test if there is no usable Vulkan; fails it instead under ENGINE_RHI_REQUIRE_GPU=1.
    void Require() const {
        if (!instance_.Valid()) {
            if (GpuRequired()) {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no usable Vulkan instance could be created");
            }
            SKIP("no usable Vulkan instance on this machine");
        }
    }

    // Like Require(), then also skips (fails under ENGINE_RHI_REQUIRE_GPU=1) if the validation layer is not active.
    void RequireValidation() const {
        Require();
        if (!instance_.ValidationEnabled()) {
            if (GpuRequired()) {
                FAIL("ENGINE_RHI_REQUIRE_GPU=1 but VK_LAYER_KHRONOS_validation is not installed");
            }
            SKIP("validation layer not installed");
        }
    }

    // Declares that the test deliberately provokes this many validation errors.
    void ExpectErrors(unsigned long long n) { expected_errors_ = n; }

    const engine_placeholder::rhi::vulkan::VulkanLoader& Loader() const { return loader_; }
    const engine_placeholder::rhi::vulkan::VulkanInstance& Instance() const { return instance_; }
    const engine_placeholder::Vector<CapturedMessage>& Messages() const { return messages_; }

private:
    static void Capture(void* user, engine_placeholder::LogVerbosity severity, const char* id, const char* text) {
        static_cast<GpuFixture*>(user)->messages_.Push(CapturedMessage{severity, engine_test::MakeText(engine_placeholder::StringView(id)), engine_test::MakeText(engine_placeholder::StringView(text))});
    }

    engine_placeholder::rhi::vulkan::VulkanLoader loader_;
    engine_placeholder::rhi::vulkan::VulkanInstance instance_;
    engine_placeholder::Vector<CapturedMessage> messages_;
    unsigned long long expected_errors_ = 0;
};

} // namespace gpu_test
