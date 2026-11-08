#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/spirv.h"
#include "rhi/vulkan/vulkan_device.h"
#include "spirv_builder.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace {

// Silences the RHI log for a scope and restores the previous state, even if an assertion fails.
struct QuietLog {
    bool saved = LogRhi.Enabled();
    QuietLog() { LogRhi.SetEnabled(false); }
    ~QuietLog() { LogRhi.SetEnabled(saved); }
};

bool Rejects(const char* message, const char* needle) {
    if (message == nullptr) {
        return false;
    }
    for (const char* m = message; *m != '\0'; ++m) {
        const char* a = m;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

const char* Check(const std::vector<u32>& w, ShaderStage stage = ShaderStage::Vertex, const char* entry = "main") {
    return ValidateSpirv(w.data(), w.size(), stage, entry);
}

} // namespace

TEST_CASE("ValidateSpirv accepts the minimal module for each stage", "[rhi][spirv]") {
    CHECK(Check(spirv_test::MinimalModule(ShaderStage::Vertex), ShaderStage::Vertex) == nullptr);
    CHECK(Check(spirv_test::MinimalModule(ShaderStage::Fragment), ShaderStage::Fragment) == nullptr);
    CHECK(Check(spirv_test::MinimalModule(ShaderStage::Compute), ShaderStage::Compute) == nullptr);
}

TEST_CASE("ValidateSpirv accepts the committed triangle shaders", "[rhi][spirv]") {
    CHECK(ValidateSpirv(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount, ShaderStage::Vertex, "main") == nullptr);
    CHECK(ValidateSpirv(shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount, ShaderStage::Fragment, "main") == nullptr);
    // ...and refuses them for the other stage.
    CHECK(Rejects(ValidateSpirv(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount, ShaderStage::Fragment, "main"), "different stage"));
    CHECK(Rejects(ValidateSpirv(shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount, ShaderStage::Compute, "main"), "different stage"));
}

TEST_CASE("ValidateSpirv rejects bad headers", "[rhi][spirv]") {
    std::vector<u32> w = spirv_test::MinimalModule(ShaderStage::Vertex);
    SECTION("null or tiny") {
        CHECK(Rejects(ValidateSpirv(nullptr, 0, ShaderStage::Vertex, "main"), "smaller"));
        CHECK(Rejects(ValidateSpirv(w.data(), 4, ShaderStage::Vertex, "main"), "smaller"));
    }
    SECTION("bad magic") {
        w[0] = 0x12345678u;
        CHECK(Rejects(Check(w), "bad magic"));
    }
    SECTION("byte-swapped module") {
        w[0] = 0x03022307u;
        CHECK(Rejects(Check(w), "byte-swapped"));
    }
    SECTION("version") {
        w[1] = 0x00010700u; // 1.7
        CHECK(Rejects(Check(w), "version"));
        w[1] = 0x00020000u; // 2.0
        CHECK(Rejects(Check(w), "version"));
        w[1] = 0x00010000u; // 1.0 is fine
        CHECK(Check(w) == nullptr);
        w[1] = 0x00010600u; // 1.6 is fine
        CHECK(Check(w) == nullptr);
        w[1] = 0x00010601u; // reserved bits set
        CHECK(Rejects(Check(w), "version"));
    }
    SECTION("zero id bound") {
        w[3] = 0;
        CHECK(Rejects(Check(w), "id bound"));
    }
}

TEST_CASE("ValidateSpirv rejects broken instruction streams", "[rhi][spirv]") {
    std::vector<u32> w = spirv_test::MinimalModule(ShaderStage::Vertex);
    SECTION("truncated mid-instruction") {
        w.pop_back();
        CHECK(Rejects(Check(w), "truncated"));
    }
    SECTION("every truncation point is rejected without reading out of bounds") {
        for (usize cut = 0; cut < w.size(); ++cut) {
            CHECK(ValidateSpirv(w.data(), cut, ShaderStage::Vertex, "main") != nullptr);
        }
    }
    SECTION("zero-length instruction") {
        w[5] = 17u; // OpCapability with word count 0
        CHECK(Rejects(Check(w), "zero length"));
    }
    SECTION("garbage after the last instruction") {
        w.push_back(0);
        CHECK(Check(w) != nullptr);
    }
}

TEST_CASE("ValidateSpirv checks the declared stage, entry name and the supported subset", "[rhi][spirv]") {
    SECTION("wrong stage") {
        CHECK(Rejects(Check(spirv_test::MinimalModule(ShaderStage::Fragment), ShaderStage::Vertex), "different stage"));
        CHECK(Rejects(Check(spirv_test::MinimalModule(ShaderStage::Vertex), ShaderStage::Compute), "different stage"));
        CHECK(Rejects(Check(spirv_test::MinimalModule(ShaderStage::Vertex), ShaderStage::AllGraphics), "different stage"));
    }
    SECTION("entry point names") {
        const std::vector<u32> w = spirv_test::MinimalModule(ShaderStage::Vertex, "vs_main");
        CHECK(Check(w, ShaderStage::Vertex, "vs_main") == nullptr);
        CHECK(Rejects(Check(w, ShaderStage::Vertex, "main"), "no entry point"));
        CHECK(Rejects(Check(w, ShaderStage::Vertex, "vs_mai"), "no entry point"));
        CHECK(Rejects(Check(w, ShaderStage::Vertex, "vs_main2"), "no entry point"));
        CHECK(Rejects(Check(w, ShaderStage::Vertex, ""), "empty"));
        // A name that fills whole words exactly (four bytes) still terminates.
        const std::vector<u32> four = spirv_test::MinimalModule(ShaderStage::Vertex, "abcd");
        CHECK(Check(four, ShaderStage::Vertex, "abcd") == nullptr);
        CHECK(Rejects(Check(four, ShaderStage::Vertex, "abc"), "no entry point"));
    }
    SECTION("entry name too long") {
        const char* name = "0123456789012345678901234567890123456789012345678901234567890123"; // 64 bytes
        CHECK(Rejects(Check(spirv_test::MinimalModule(ShaderStage::Vertex), ShaderStage::Vertex, name), "longer than 63"));
    }
    SECTION("unterminated entry point name") {
        // "main" fills one word exactly; the terminator is the next word. Drop it and shrink the instruction to match.
        std::vector<u32> cut = spirv_test::MinimalModule(ShaderStage::Vertex, "main");
        cut.pop_back();
        cut[10] = (4u << 16) | 15u; // OpEntryPoint: head, model, id, "main"
        CHECK(Rejects(Check(cut), "malformed OpEntryPoint"));
        // An interface id after an unterminated name must not be read as its terminator: 256 has a zero low byte.
        cut[10] = (5u << 16) | 15u;
        cut.push_back(256u);
        CHECK(Rejects(Check(cut), "malformed OpEntryPoint"));
    }
    SECTION("capabilities") {
        std::vector<u32> w = spirv_test::MinimalModule(ShaderStage::Vertex);
        w[6] = 6; // Kernel instead of Shader
        CHECK(Rejects(Check(w), "capability"));
        w[6] = 2; // Matrix: not Shader
        CHECK(Rejects(Check(w), "capability"));
    }
    SECTION("addressing and memory model") {
        std::vector<u32> w = spirv_test::MinimalModule(ShaderStage::Vertex);
        w[8] = 5348; // PhysicalStorageBuffer64
        CHECK(Rejects(Check(w), "Logical"));
        w[8] = 0;
        w[9] = 0; // Simple memory model
        CHECK(Rejects(Check(w), "memory model"));
        w[9] = 3; // Vulkan
        CHECK(Check(w) == nullptr);
    }
}

TEST_CASE("The null device checks shader contents", "[rhi][null][spirv]") {
    QuietLog quiet;
    NullDevicePtr device = CreateNullDevice();
    ShaderDesc desc;
    desc.stage = ShaderStage::Vertex;
    desc.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    ShaderHandle ok = device->CreateShader(desc);
    CHECK(ok.IsValid());
    CHECK(device->ErrorCount() == 0);

    desc.stage = ShaderStage::Fragment; // the vertex module declared as a fragment shader
    CHECK_FALSE(device->CreateShader(desc).IsValid());
    CHECK(device->ErrorCount() == 1);

    std::vector<u32> garbage(16, 0xdeadbeefu);
    desc.stage = ShaderStage::Vertex;
    desc.SetCode(SpirvBlob(garbage.data(), garbage.size()));
    CHECK_FALSE(device->CreateShader(desc).IsValid());
    CHECK(device->ErrorCount() == 2);

    device->DestroyShader(ok);
    device.Reset();
}

TEST_CASE("Vulkan creates shader modules from the committed triangle shaders", "[rhi][vulkan][gpu][spirv]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    vulkan::VulkanDevicePtr device = vulkan::VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    vs.debug_name = "triangle.vert";
    ShaderDesc fs;
    fs.stage = ShaderStage::Fragment;
    fs.SetCode(SpirvBlob(shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount));
    fs.debug_name = "triangle.frag";

    const ShaderHandle v = device->CreateShader(vs);
    const ShaderHandle f = device->CreateShader(fs);
    REQUIRE(v.IsValid());
    REQUIRE(f.IsValid());
    CHECK(device->Counts().shaders == 2);

    vulkan::VulkanDevice::ShaderInfo shader_info;
    REQUIRE(device->LookupShader(v, &shader_info));
    CHECK(shader_info.module != nullptr);
    CHECK(shader_info.stage == ShaderStage::Vertex);
    CHECK(std::strcmp(shader_info.entry, "main") == 0);

    // Garbage and a wrong stage are refused before the driver sees them, so the validation layer stays quiet.
    fx.ExpectErrors(0);
    ShaderDesc wrong = fs;
    wrong.stage = ShaderStage::Vertex;
    std::vector<u32> garbage(32, 1u);
    ShaderDesc bad = vs;
    bad.SetCode(SpirvBlob(garbage.data(), garbage.size()));
    {
        QuietLog quiet;
        CHECK_FALSE(device->CreateShader(wrong).IsValid());
        CHECK_FALSE(device->CreateShader(bad).IsValid());
    }

    device->DestroyShader(v);
    device->DestroyShader(f);
    device->BeginFrame();
    device->EndFrame();
    device->BeginFrame();
    device->EndFrame();
    device->WaitIdle();
    device->BeginFrame();
    CHECK_FALSE(device->LookupShader(v, &shader_info)); // stale handle
    CHECK(device->Counts().shaders == 0);
    device->EndFrame();
    vulkan::VulkanDevice::ResetLeakCounts();
    device.Reset();
    CHECK(vulkan::VulkanDevice::GetLeakCounts().user_objects == 0);
}

TEST_CASE("Vulkan frees a shader the application forgot", "[rhi][vulkan][gpu][spirv]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    vulkan::VulkanDevicePtr device = vulkan::VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    REQUIRE(device->CreateShader(vs).IsValid());
    vulkan::VulkanDevice::ResetLeakCounts();
    {
        QuietLog quiet;
        device.Reset(); // frees the module (the validation layer would report a leak otherwise)
    }
    CHECK(vulkan::VulkanDevice::GetLeakCounts().user_objects == 1);
    vulkan::VulkanDevice::ResetLeakCounts();
}
