#include <catch2/catch_test_macros.hpp>

#include "os/gpu_driver_probe.h"
#include "os/os.h"

using vse::StringView;
using vse::os::DynamicLibraryHandle;
using vse::os::GetSymbol;
using vse::os::LoadLibrary;
using vse::os::ProbeVulkanCreateInstance;
using vse::os::UnloadLibrary;

TEST_CASE("LoadLibrary returns nullptr for a nonexistent library, without crashing", "[gpu_driver][os]")
{
    DynamicLibraryHandle handle = LoadLibrary("libengine_test_should_not_exist.so");
    REQUIRE(handle == nullptr);
}

TEST_CASE("LoadLibrary returns nullptr for a path too long to fit kMaxPathBytes", "[gpu_driver][os]")
{
    char long_path[vse::os::kMaxPathBytes + 1];
    for (auto& c : long_path)
    {
        c = 'a';
    }
    long_path[sizeof(long_path) - 1] = '\0';
    DynamicLibraryHandle handle = LoadLibrary(StringView(long_path, sizeof(long_path) - 1));
    REQUIRE(handle == nullptr);
}

TEST_CASE("LoadLibrary/GetSymbol resolve the Vulkan loader's vkGetInstanceProcAddr", "[gpu_driver][os]")
{
    DynamicLibraryHandle handle = LoadLibrary(StringView(vse::os::VulkanLoaderName()));  // libvulkan.so.1, vulkan-1.dll
    REQUIRE(handle != nullptr);

    void* symbol = GetSymbol(handle, "vkGetInstanceProcAddr");
    REQUIRE(symbol != nullptr);

    void* missing = GetSymbol(handle, "engine_test_symbol_should_not_exist");
    REQUIRE(missing == nullptr);

    UnloadLibrary(handle);
}

TEST_CASE("UnloadLibrary(nullptr) is a no-op", "[gpu_driver][os]")
{
    UnloadLibrary(nullptr);
}

TEST_CASE("ProbeVulkanCreateInstance resolves vkCreateInstance through the full chain", "[gpu_driver][os]")
{
    PFN_vkVoidFunction create_instance = ProbeVulkanCreateInstance();
    REQUIRE(create_instance != nullptr);
}
