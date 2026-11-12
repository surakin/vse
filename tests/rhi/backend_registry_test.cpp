// The RHI backend registry (Phase 13 Day 162): backends register explicitly, CreateDevice and EnumerateAdapters
// dispatch through the registry, Auto picks the available backend of highest rank, and a backend the program never
// registered fails loudly. Each test that empties the registry leaves it to tests/support/register_backends.cpp to fill
// again before the next test.
#include "cstr/format.h"
#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "renderer/renderer.h"
#include "string_view.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

// Counts the calls of the fake backends.
int g_created_a = 0;
int g_created_b = 0;
int g_enumerated_b = 0;
bool g_b_available = true;

DevicePtr CreateFakeA(const DeviceCreateInfo& info)
{
    ++g_created_a;
    CHECK(info.backend == Backend::Vulkan);
    return CreateNullDevice(NullDeviceOptions());
}

DevicePtr CreateFakeB(const DeviceCreateInfo& info)
{
    ++g_created_b;
    CHECK(info.backend == Backend::Dx12);  // Auto was resolved before the factory is called
    NullDeviceOptions options;
    options.caps.shader_format = ShaderFormat::Dxil;
    return CreateNullDevice(options);
}

bool EnumerateFake(const DeviceCreateInfo&, AdapterList* out)
{
    DefaultNullAdapters(out);
    return true;
}

bool EnumerateFakeB(const DeviceCreateInfo& info, AdapterList* out)
{
    ++g_enumerated_b;
    return EnumerateFake(info, out);
}

bool FakeBAvailable(char* reason, usize reason_size)
{
    if (!g_b_available)
    {
        FormatTo(reason, reason_size, "the fake driver is switched off");
    }
    return g_b_available;
}

BackendDescriptor FakeA()
{
    BackendDescriptor d;
    d.backend = Backend::Vulkan;
    d.name = "fake-a";
    d.create = &CreateFakeA;
    d.enumerate_adapters = &EnumerateFake;
    d.auto_rank = 10;
    return d;
}

BackendDescriptor FakeB()
{
    BackendDescriptor d;
    d.backend = Backend::Dx12;
    d.name = "fake-b";
    d.create = &CreateFakeB;
    d.enumerate_adapters = &EnumerateFakeB;
    d.available = &FakeBAvailable;
    d.shader_format = ShaderFormat::Dxil;
    d.auto_rank = 20;
    return d;
}

struct Reset
{
    Reset()
    {
        ClearBackendRegistryForTest();
        g_created_a = g_created_b = g_enumerated_b = 0;
        g_b_available = true;
    }
    ~Reset()
    {
        ClearBackendRegistryForTest();
        RegisterCompiledBackends();
    }
};

}  // namespace

TEST_CASE("Backends register in order, once, and clashing descriptors are refused", "[rhi][registry]")
{
    Reset reset;
    CHECK(RegisteredBackendCount() == 0);
    REQUIRE(RegisterBackend(FakeA()));
    REQUIRE(RegisterBackend(FakeB()));
    CHECK(RegisterBackend(FakeA()));  // the same descriptor again changes nothing
    REQUIRE(RegisteredBackendCount() == 2);
    CHECK(StringView(RegisteredBackend(0).name) == "fake-a");
    CHECK(StringView(RegisteredBackend(1).name) == "fake-b");
    CHECK(FindBackend(Backend::Dx12) == &RegisteredBackend(1));
    CHECK(FindBackendByName("fake-a") == &RegisteredBackend(0));
    CHECK(FindBackendByName("fake") == nullptr);
    CHECK(FindBackend(Backend::Null) == nullptr);
    CHECK(FindBackend(Backend::Auto) == nullptr);

    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    BackendDescriptor same_value = FakeA();
    same_value.name = "other";
    CHECK_FALSE(RegisterBackend(same_value));  // the Vulkan value is taken
    BackendDescriptor same_name = FakeB();
    same_name.backend = Backend::Null;
    same_name.name = "fake-a";
    CHECK_FALSE(RegisterBackend(same_name));
    BackendDescriptor auto_value = FakeA();
    auto_value.backend = Backend::Auto;
    auto_value.name = "auto";
    CHECK_FALSE(RegisterBackend(auto_value));
    BackendDescriptor no_create = FakeA();
    no_create.backend = Backend::Null;
    no_create.name = "no-create";
    no_create.create = nullptr;
    CHECK_FALSE(RegisterBackend(no_create));
    LogRhi.SetEnabled(was);
    CHECK(RegisteredBackendCount() == 2);
}

TEST_CASE("Auto picks the available backend of highest rank and falls back when it is unavailable", "[rhi][registry]")
{
    Reset reset;
    REQUIRE(RegisterBackend(FakeA()));
    REQUIRE(RegisterBackend(FakeB()));
    char reason[160] = {};
    CHECK(PickAutoBackend(reason, sizeof(reason)) == FindBackend(Backend::Dx12));

    DeviceCreateInfo info;  // backend defaults to Auto
    CHECK(info.backend == Backend::Auto);
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    CHECK(g_created_b == 1);
    CHECK(device->Caps().shader_format == ShaderFormat::Dxil);
    AdapterList list;
    CHECK(EnumerateAdapters(info, &list));
    CHECK(g_enumerated_b == 1);
    device.Reset();

    g_b_available = false;
    CHECK(PickAutoBackend(reason, sizeof(reason)) == FindBackend(Backend::Vulkan));
    device = CreateDevice(info);
    REQUIRE(device);
    CHECK(g_created_a == 1);
    CHECK(g_created_b == 1);
}

TEST_CASE("A backend that is registered but unavailable is refused with its own reason", "[rhi][registry]")
{
    Reset reset;
    REQUIRE(RegisterBackend(FakeB()));
    g_b_available = false;
    char reason[256] = {};
    CHECK(ResolveBackend(Backend::Dx12, reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "fake-b backend is not available here") != nullptr);
    CHECK(CStrFind(reason, "the fake driver is switched off") != nullptr);
    CHECK(PickAutoBackend(reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "the fake driver is switched off") != nullptr);
    char why[160] = {};
    CHECK_FALSE(BackendAvailable(*FindBackend(Backend::Dx12), why, sizeof(why)));
    CHECK(StringView(why) == "the fake driver is switched off");

    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    CHECK_FALSE(CreateDevice(info));
    LogRhi.SetEnabled(was);
    CHECK(g_created_b == 0);
}

TEST_CASE("A backend this program never registered fails loudly, never as a missing GPU", "[rhi][registry]")
{
    Reset reset;
    char reason[256] = {};
    CHECK(ResolveBackend(Backend::Vulkan, reason, sizeof(reason)) == nullptr);
#if ENGINE_PLATFORM_MACOS
    CHECK(CStrFind(reason, "the vulkan backend is not built for macOS") != nullptr);  // not a registration slip
#else
    CHECK(CStrFind(reason, "the vulkan backend is not registered in this program") != nullptr);
    CHECK(CStrFind(reason, "rhi::RegisterCompiledBackends") != nullptr);
#endif
#if !ENGINE_PLATFORM_WINDOWS
    CHECK(ResolveBackend(Backend::Dx12, reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "the dx12 backend is not built for") != nullptr);  // a platform fact, not a slip
#endif
    CHECK(ResolveBackend(Backend::Null, reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "the null backend is not registered") != nullptr);
    CHECK(PickAutoBackend(reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "no backend that auto may pick is registered") != nullptr);

    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    DeviceCreateInfo info;
    info.backend = Backend::Vulkan;
    CHECK_FALSE(CreateDevice(info));
    AdapterList list;
    list.count = 3;
    CHECK_FALSE(EnumerateAdapters(info, &list));
    CHECK(list.count == 0);
    LogRhi.SetEnabled(was);
}

TEST_CASE("RegisterCompiledBackends registers null and Vulkan, and Direct3D 12 on Windows; Auto never picks null",
          "[rhi][registry]")
{
    Reset reset;
    CHECK(RegisterCompiledBackends());
    CHECK(RegisterCompiledBackends());  // again: nothing changes
    const BackendDescriptor* null_backend = FindBackend(Backend::Null);
    REQUIRE(null_backend != nullptr);
    CHECK(StringView(null_backend->name) == "null");
    CHECK(null_backend->auto_rank == 0);
#if ENGINE_RHI_HAVE_VULKAN
    const BackendDescriptor* vulkan = FindBackend(Backend::Vulkan);
    REQUIRE(vulkan != nullptr);
    CHECK(StringView(vulkan->name) == "vulkan");
    CHECK(vulkan->auto_rank > 0);
    CHECK(vulkan->shader_format == ShaderFormat::Spirv);
#if ENGINE_PLATFORM_WINDOWS
    const BackendDescriptor* dx12 = FindBackend(Backend::Dx12);  // Phase 13 Day 164
    REQUIRE(dx12 != nullptr);
    CHECK(dx12->shader_format == ShaderFormat::Dxil);
    CHECK(RegisteredBackendCount() == 3);
#else
    CHECK(RegisteredBackendCount() == 2);
#endif
#else
    // macOS (Phase 26 Day 314): no Vulkan; the null backend alone until Metal (Day 316).
    CHECK(FindBackend(Backend::Vulkan) == nullptr);
    CHECK(RegisteredBackendCount() == 1);
#endif
    // With only the null backend registered, Auto has nothing to pick, on every platform (macOS too: only the backend
    // setting turns that into the null backend, renderer::BackendForSetting).
    ClearBackendRegistryForTest();
    REQUIRE(RegisterNullBackend());
    char reason[160] = {};
    CHECK(PickAutoBackend(reason, sizeof(reason)) == nullptr);
}

TEST_CASE(
    "The backend setting's auto is the null backend on macOS only with no GPU backend built; one that is built and fails is "
    "never replaced; metal is refused as not built",
    "[rhi][registry]")
{
    // The macOS fallback (Phase 26 Day 314, removed with Metal on Day 316) applies only when no backend auto may pick
    // is registered at all: a registered backend that cannot run (Metal from Day 316, here a fake) is a loud failure
    // with its own reason, and the setting's backend stays auto.
    Reset reset;
    REQUIRE(RegisterNullBackend());
    const char* note = "untouched";
#if ENGINE_PLATFORM_MACOS
    CHECK(renderer::BackendForSetting(static_cast<u32>(Backend::Auto), &note) == Backend::Null);
    REQUIRE(note != nullptr);
    CHECK(CStrFind(note, "no GPU backend is built for macOS yet") != nullptr);
#else
    CHECK(renderer::BackendForSetting(static_cast<u32>(Backend::Auto), &note) == Backend::Auto);
    CHECK(note == nullptr);
#endif
    CHECK(renderer::BackendForSetting(static_cast<u32>(Backend::Vulkan), &note) ==
          Backend::Vulkan);  // a named backend is never replaced
    CHECK(note == nullptr);
    ClearBackendRegistryForTest();
    REQUIRE(RegisterNullBackend());
    REQUIRE(RegisterBackend(FakeB()));
    g_b_available = false;
    CHECK(renderer::BackendForSetting(static_cast<u32>(Backend::Auto), &note) ==
          Backend::Auto);  // a GPU backend is built: no fallback
    CHECK(note == nullptr);
    char reason[256] = {};
    CHECK(PickAutoBackend(reason, sizeof(reason)) == nullptr);
    CHECK(CStrFind(reason, "fake-b is not available: the fake driver is switched off") != nullptr);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    DeviceCreateInfo info;
    info.backend = Backend::Auto;
    CHECK_FALSE(CreateDevice(info));  // no device at all: never a null one in disguise
    LogRhi.SetEnabled(was);
    CHECK(ResolveBackend(Backend::Metal, reason, sizeof(reason)) == nullptr);
#if ENGINE_PLATFORM_MACOS
    CHECK(CStrFind(reason, "the metal backend is not built yet") != nullptr);
#else
    CHECK(CStrFind(reason, "the metal backend is not built for") != nullptr);  // and never will be here
#endif
}

TEST_CASE("A device refuses bytecode in a format other than its own, naming both", "[rhi][registry][desc]")
{
    NullDeviceOptions options;
    options.caps.shader_format = ShaderFormat::Dxil;
    NullDevicePtr device = CreateNullDevice(options);
    static const u32 words[6] = {0x07230203, 0x00010000, 0, 1, 0, 0};
    ShaderBlobSet set{"only_spirv.frag", SpirvBlob(words, 6), {}};
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    CHECK_FALSE(CreateShaderFromSet(*device, set, ShaderStage::Fragment).IsValid());
    ShaderDesc d;
    d.stage = ShaderStage::Fragment;
    d.SetCode(set.spirv);
    d.debug_name = "only_spirv.frag";
    CHECK_FALSE(device->CreateShader(d).IsValid());
    LogRhi.SetEnabled(was);
    CHECK(device->ErrorCount() ==
          1);  // the direct call is a contract error on the null device; the set lookup fails before it
}
