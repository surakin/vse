// Phase 23 Day 352: the engine renders into a Wayland surface the host owns. The host here is the test, which opens a
// window with the platform layer, as an application with its own Wayland code would, and hands the engine the window's
// wl_display and wl_surface (the engine makes no second connection). The same checks as the Xlib host (host_surface.c):
// a golden frame read back through the function table, the host's resize and a hidden surface, and a surface rebound to
// a new window.
#include <catch2/catch_test_macros.hpp>

#include "display_session.h"

#include "engine/engine_c.h"
#include "os/console.h"
#include "os/os.h"
#include "platform/input.h"
#include "platform/window_host.h"

using namespace engine_placeholder;

namespace
{

const engine_api* Api()
{
    const engine_api* api = nullptr;
    REQUIRE(engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) == ENGINE_OK);
    return api;
}

engine_surface_info InfoOf(const os::Window& window, u32 width, u32 height)
{
    const os::NativeWindowInfo native = window.GetNativeInfo();
    engine_surface_info info = {};
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_WAYLAND;
    info.width = width;
    info.height = height;
    info.scale = 1.0f;
    info.display = native.display;
    info.surface = native.surface;
    return info;
}

// One frame, then the pixels it drew.
engine_status FrameAndRead(const engine_api* api, engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    engine_handle ticket = {};
    *pixels = {};
    pixels->struct_size = sizeof(*pixels);
    const engine_status asked = api->surface_read_pixels(surface, &ticket);
    if (asked != ENGINE_OK)
    {
        return asked;
    }
    engine_frame_info frame = {};
    frame.struct_size = sizeof(frame);
    CHECK(api->frame(engine, &frame, nullptr) == ENGINE_OK);
    const engine_status status = api->pixels_finish(ticket, pixels);
    CHECK(api->pixels_destroy(ticket) == ENGINE_OK);
    return status;
}

bool AllRed(const engine_pixels& pixels)
{
    const byte* data = reinterpret_cast<const byte*>(pixels.data.data);
    if (data == nullptr || pixels.data.size < static_cast<usize>(pixels.stride) * pixels.height)
    {
        return false;
    }
    for (u32 y = 0; y < pixels.height; ++y)
    {
        for (u32 x = 0; x < pixels.width; ++x)
        {
            const byte* p = data + static_cast<usize>(y) * pixels.stride + static_cast<usize>(x) * 4;
            if (p[0] != 255 || p[1] != 0 || p[2] != 0 || p[3] != 255)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("The engine renders into a host's Wayland surface, follows its resize and rebinds to a new window",
          "[engine_c][surface][wayland][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    const char* wayland = os::GetEnvironment("WAYLAND_DISPLAY");
    if (wayland == nullptr || wayland[0] == '\0')
    {
        SKIP("no Wayland session");
    }
    const engine_api* api = Api();
    engine_create_info create = {};
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {};
    REQUIRE(api->create(&create, &engine) == ENGINE_OK);
    engine_info engine_caps = {};
    engine_caps.struct_size = sizeof(engine_caps);
    REQUIRE(api->get_info(engine, &engine_caps) == ENGINE_OK);
    if ((engine_caps.caps & ENGINE_CAP_SURFACE_WAYLAND) == 0)
    {
        CHECK(api->destroy(engine) == ENGINE_OK);
        SKIP("the library has no Wayland surface support");
    }

    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(os::WindowBackend::Wayland, "engine surface test", 200, 120, &input))
    {
        CHECK(api->destroy(engine) == ENGINE_OK);
        SKIP("the window could not be opened");
    }
    for (int i = 0; i < 10; ++i)
    {
        host.Get()->PumpEvents();
    }
    REQUIRE(host.Get()->GetNativeInfo().kind == os::NativeWindowKind::Wayland);

    engine_surface_info info = InfoOf(*host.Get(), 200, 120);
    engine_handle surface = {};
    const engine_status made = api->surface_create(engine, &info, &surface);
    if (made == ENGINE_ERROR_UNSUPPORTED)
    {
        CHECK(api->destroy(engine) == ENGINE_OK);
        SKIP("no Vulkan device can present to this window");
    }
    REQUIRE(made == ENGINE_OK);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    CHECK(api->surface_set_background(surface, red) == ENGINE_OK);

    engine_pixels pixels = {};
    SECTION("a frame of a known color is the golden image")
    {
        REQUIRE(FrameAndRead(api, engine, surface, &pixels) == ENGINE_OK);
        CHECK(pixels.width == 200);
        CHECK(pixels.height == 120);
        CHECK(AllRed(pixels));
        engine_buffer_release(&pixels.data);
    }
    SECTION("the host's resize, then a hidden surface, then back")
    {
        host.Get()->PumpEvents();
        CHECK(api->surface_resized(surface, 320, 200, 1.0f) == ENGINE_OK);
        REQUIRE(FrameAndRead(api, engine, surface, &pixels) == ENGINE_OK);
        CHECK(pixels.width == 320);
        CHECK(pixels.height == 200);
        CHECK(AllRed(pixels));
        engine_buffer_release(&pixels.data);

        CHECK(api->surface_resized(surface, 0, 0, 1.0f) == ENGINE_OK);
        CHECK(FrameAndRead(api, engine, surface, &pixels) == ENGINE_ERROR_NOT_READY);
        engine_surface_state state = {};
        state.struct_size = sizeof(state);
        CHECK(api->surface_state(surface, &state) == ENGINE_OK);
        CHECK(state.visible == 0);
        CHECK(state.frames_skipped >= 1);
        CHECK(api->surface_resized(surface, 240, 160, 1.0f) == ENGINE_OK);
        REQUIRE(FrameAndRead(api, engine, surface, &pixels) == ENGINE_OK);
        CHECK(pixels.width == 240);
        CHECK(pixels.height == 160);
        engine_buffer_release(&pixels.data);
    }
    SECTION("a surface rebound to a new window")
    {
        os::InputState input2;
        os::PlatformWindowHost second;
        if (!second.Open(os::WindowBackend::Wayland, "engine surface test 2", 160, 100, &input2))
        {
            SKIP("a second window could not be opened");
        }
        for (int i = 0; i < 10; ++i)
        {
            second.Get()->PumpEvents();
        }
        engine_surface_info info2 = InfoOf(*second.Get(), 160, 100);
        REQUIRE(api->surface_rebind(surface, &info2) == ENGINE_OK);
        engine_surface_state state = {};
        state.struct_size = sizeof(state);
        CHECK(api->surface_state(surface, &state) == ENGINE_OK);
        CHECK(state.lost == 0);
        CHECK(state.recoveries == 1);
        REQUIRE(FrameAndRead(api, engine, surface, &pixels) == ENGINE_OK);
        CHECK(pixels.width == 160);
        CHECK(pixels.height == 100);
        CHECK(AllRed(pixels));
        engine_buffer_release(&pixels.data);
        CHECK(api->surface_destroy(surface) == ENGINE_OK);  // before `second` goes
        surface = {};
    }
    if (surface.value != 0)
    {
        CHECK(api->surface_destroy(surface) == ENGINE_OK);
    }
    CHECK(api->destroy(engine) == ENGINE_OK);
}
