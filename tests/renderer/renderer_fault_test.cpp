// Phase 4 Day 83: failure injection. Each case breaks one stage of a frame (the device is lost before the frame, while the frame
// callback builds the graph, while the first pass records, while the last pass records; a transient texture cannot be made; a
// material's texture is missing; an object's pipeline cannot be built) and checks the same three things: the frame ends with a
// defined status, the cause is in the log or in LastError, and the renderer and the device can still be destroyed with nothing left
// alive.
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "demo_frame.h"
#include "demo_scene.h"
#include "math/transform.h"
#include "renderer/renderer.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

enum class Stage { BetweenFrames, InBuild, InFirstPass, InLastPass };

rhi::TextureDesc ColorTex(u32 w, u32 h) {
    rhi::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = rhi::Format::RGBA8_UNorm;
    d.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    return d;
}

// A two-pass frame whose passes and callback can pull the plug on the device.
struct Faulty {
    rhi::NullDevice* device = nullptr;
    GraphResource mid, target;
    bool lose_in_build = false;
    bool lose_in_first = false;
    bool lose_in_last = false;
    u32 passes_run = 0;
};

void RunPass(const PassExecuteContext& context, void* user, bool lose) {
    Faulty* f = static_cast<Faulty*>(user);
    ++f->passes_run;
    if (lose) f->device->SimulateDeviceLoss();
    rhi::RenderingDesc rd;
    rd.color[0].texture = context.graph->ResolvedTexture(f->passes_run == 1 ? f->mid : f->target);
    rd.color_count = 1;
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}
void FirstPass(const PassExecuteContext& context, void* user) { RunPass(context, user, static_cast<Faulty*>(user)->lose_in_first); }
void LastPass(const PassExecuteContext& context, void* user) { RunPass(context, user, static_cast<Faulty*>(user)->lose_in_last); }

bool BuildFaulty(const FrameContext& context, void* user) {
    Faulty* f = static_cast<Faulty*>(user);
    f->passes_run = 0;
    if (f->lose_in_build) f->device->SimulateDeviceLoss();
    f->target = context.target;
    f->mid = context.graph->CreateTexture("mid", ColorTex(context.width / 2, context.height / 2));
    context.graph->AddPass("first", &FirstPass, f).Write(f->mid, GraphUsage::ColorAttachment);
    context.graph->AddPass("last", &LastPass, f).Read(f->mid, GraphUsage::Sampled).Write(f->target, GraphUsage::ColorAttachment);
    return true;
}

// Command lists are not counted: the device recycles them at the next BeginFrame and frees them with itself.
bool NothingAlive(const rhi::NullDeviceStats& s) {
    UNSCOPED_INFO("buffers " << s.buffers << " textures " << s.textures << " samplers " << s.samplers << " shaders " << s.shaders << " layouts "
                    << s.bind_group_layouts << " groups " << s.bind_groups << " pipelines " << s.pipelines << " pending "
                    << s.pending_destruction << " lists " << s.command_lists);
    return s.buffers == 0 && s.textures == 0 && s.samplers == 0 && s.shaders == 0 && s.bind_group_layouts == 0 && s.bind_groups == 0 &&
           s.pipelines == 0 && s.pending_destruction == 0;
}

} // namespace

TEST_CASE("Losing the device at any stage of a frame gives DeviceLost and leaves a destroyable renderer", "[renderer][fault]") {
    const Stage stage = GENERATE(Stage::BetweenFrames, Stage::InBuild, Stage::InFirstPass, Stage::InLastPass);
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    rhi::NullDevice* null_device = device.Get();
    {
        rhi::SwapchainDesc sd;
        sd.width = 64;
        sd.height = 48;
        rhi::Swapchain* swapchain = device->CreateSwapchain(sd);
        REQUIRE(swapchain != nullptr);
        Faulty faulty;
        faulty.device = null_device;
        {
            RendererDesc rd;
            rd.device = device.Get();
            rd.swapchain = swapchain;
            rd.build = &BuildFaulty;
            rd.build_user = &faulty;
            Renderer renderer(rd);
            for (int i = 0; i < 2; ++i) REQUIRE(renderer.RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::Ok);
            switch (stage) {
            case Stage::BetweenFrames: null_device->SimulateDeviceLoss(); break;
            case Stage::InBuild: faulty.lose_in_build = true; break;
            case Stage::InFirstPass: faulty.lose_in_first = true; break;
            case Stage::InLastPass: faulty.lose_in_last = true; break;
            }
            CHECK(renderer.RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::DeviceLost);
            CHECK(device->DeviceLost());
            // Later frames keep answering DeviceLost; none of them crashes or hangs.
            for (int i = 0; i < 3; ++i) CHECK(renderer.RenderFrame(nullptr, nullptr, nullptr) == FrameStatus::DeviceLost);
            CHECK(renderer.Stats().frames_lost == 4);
            CHECK(renderer.Stats().frames_rendered == 2);
        }
        device->DestroySwapchain(swapchain);
    }
    device->WaitIdle();
    CHECK(NothingAlive(null_device->Statistics()));
}

TEST_CASE("A transient texture the device cannot make fails the frame, names it, and the pool stays whole", "[renderer][fault]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        Faulty faulty;
        faulty.device = device.Get();
        RendererDesc rd;
        rd.device = device.Get();
        rd.build = &BuildFaulty;
        rd.build_user = &faulty;
        Renderer renderer(rd);
        rhi::TextureDesc td = ColorTex(64, 48);
        td.format = rhi::Format::Undefined; // the device refuses a texture without a format
        const rhi::TextureHandle target = device->CreateTexture(td);
        CHECK_FALSE(target.IsValid());
        device->ResetErrors();
        // The mid texture is half the target size: a 1 x 1 target makes it 0 x 0, which the device refuses.
        const rhi::TextureHandle one = device->CreateTexture(ColorTex(1, 1));
        FrameTarget ft;
        ft.texture = one;
        ft.width = 1;
        ft.height = 1;
        ft.final_state = rhi::ResourceState::CopySrc;
        CHECK(renderer.RenderFrame(nullptr, nullptr, &ft) == FrameStatus::Failed);
        CHECK(std::string(renderer.LastError()) == "transient texture 'mid' could not be allocated");
        CHECK(renderer.Pool().Stats().textures_idle == renderer.Pool().Stats().textures_live);
        device->ResetErrors();
        device->DestroyTexture(one);
    }
    device->WaitIdle();
    CHECK(NothingAlive(device->Statistics()));
}

namespace {

// The whole deferred frame on the null device: a small demo scene rendered to an offscreen target.
struct DeferredRig {
    rhi::NullDevicePtr device;
    rhi::TextureHandle target;
    renderer_demo::DemoFrame frame;
    renderer_demo::DemoScene scene;
    Camera camera;

    bool Create(u32 lose_at_frame, const char* asset_dir, const rhi::NullDeviceOptions* base = nullptr) {
        rhi::NullDeviceOptions options = base != nullptr ? *base : rhi::NullDeviceOptions{};
        options.lose_device_at_frame = lose_at_frame;
        device = rhi::CreateNullDevice(options);
        rhi::TextureDesc td;
        td.width = 96;
        td.height = 64;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        target = device->CreateTexture(td);
        renderer_demo::FrameSetup fs;
        fs.device = device.Get();
        fs.max_objects = 128;
        fs.max_lights = 32;
        if (!frame.Create(fs)) return false;
        renderer_demo::SceneOptions so;
        so.objects = 64;
        so.lights = 8;
        so.asset_dir = asset_dir;
        if (!scene.Create(device.Get(), frame.Materials(), frame.Templates(), so)) return false;
        scene.Animate(0.0f);
        camera = scene.OverviewCamera(96.0f / 64.0f);
        FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
        return true;
    }
    FrameStatus Render() {
        FrameTarget ft;
        ft.texture = target;
        ft.width = 96;
        ft.height = 64;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.final_state = rhi::ResourceState::CopySrc;
        return frame.Render(&scene.Scene(), &camera, &ft);
    }
    // Tears everything down in the order a program would and reports whether the device holds nothing afterwards.
    bool TearDown() {
        device->WaitIdle();
        scene.Destroy();
        frame.Destroy();
        device->DestroyTexture(target);
        device->WaitIdle();
        return NothingAlive(device->Statistics());
    }
};

} // namespace

TEST_CASE("Losing the device in the deferred pipeline's frames ends each frame as DeviceLost and frees everything", "[renderer][fault]") {
    // Frame numbers count from the mesh uploads: the scene creation itself takes the first frames, so the loss lands at
    // different stages of the sweep (before the G-buffer, in it, in lighting, in the tonemap).
    const u32 lose_at = GENERATE(range(3u, 9u));
    DeferredRig rig;
    if (!rig.Create(lose_at, nullptr)) {
        // The device was lost while the scene's textures and meshes were being made: creation fails cleanly and frees what it made.
        CHECK(rig.device->DeviceLost());
        CHECK(rig.TearDown());
        return;
    }
    bool lost = false;
    for (int i = 0; i < 8; ++i) {
        const FrameStatus status = rig.Render();
        if (status == FrameStatus::DeviceLost) lost = true;
        // Once lost, always lost; before that, every frame is fine.
        CHECK((status == FrameStatus::DeviceLost) == lost);
    }
    CHECK(lost);
    CHECK(rig.TearDown());
}

TEST_CASE("A material whose texture file is missing falls back to a procedural one and the frame renders", "[renderer][fault]") {
    DeferredRig rig;
    REQUIRE(rig.Create(0, "/nonexistent/asset/dir"));
    CHECK(rig.Render() == FrameStatus::Ok);
    CHECK(rig.frame.LastGBufferOk());
    CHECK(rig.device->ErrorCount() == 0);
    CHECK(rig.TearDown());
}

TEST_CASE("An object whose pipeline cannot be built is named and left out; the rest of the frame still draws", "[renderer][fault]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        renderer_demo::DemoFrame frame;
        renderer_demo::FrameSetup fs;
        fs.device = device.Get();
        fs.max_objects = 8;
        fs.max_lights = 4;
        REQUIRE(frame.Create(fs));
        MeshData plain, colored;
        REQUIRE(MakeCubeMesh(1.0f, &plain) == nullptr);
        const u8 grey[4] = {128, 128, 128, 255};
        REQUIRE(MakeUniformColoredMesh(plain, grey, &colored) == nullptr);
        GpuMesh cube, colored_cube;
        REQUIRE(cube.Create(device.Get(), plain, "cube"));
        REQUIRE(colored_cube.Create(device.Get(), colored, "colored cube"));
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        cube.RecordPrepare(list);
        colored_cube.RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        cube.MarkReady();
        colored_cube.MarkReady();
        const MaterialId material = CreateGBufferMaterial(frame.Materials(), frame.Templates(), GBufferOptions{}, "plain");
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.material = material.index;
        od.mesh = &cube;
        scene.AddObject(od);
        od.mesh = &colored_cube; // a vertex-colored mesh under a material that does not read colors: no pipeline fits
        Transform placed;
        placed.position = Vec3(1.5f, 0, 0);
        od.transform = placed.ToMatrix();
        scene.AddObject(od);

        rhi::TextureDesc td;
        td.width = 64;
        td.height = 48;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        const rhi::TextureHandle target = device->CreateTexture(td);
        FrameTarget ft;
        ft.texture = target;
        ft.width = 64;
        ft.height = 48;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.final_state = rhi::ResourceState::CopySrc;
        Camera camera = MakeLookAtCamera(Vec3(0, 6, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
        camera.aspect = 64.0f / 48.0f;
        CHECK(frame.Render(&scene, &camera, &ft) == FrameStatus::Ok);
        CHECK_FALSE(frame.LastGBufferOk());
        CHECK(frame.GBufferStatistics().rejected == 1);
        CHECK(frame.GBufferStatistics().drawn_objects == 1);
        // The next frame fails the same way and is just as harmless.
        CHECK(frame.Render(&scene, &camera, &ft) == FrameStatus::Ok);
        CHECK(device->ErrorCount() == 0);
        device->WaitIdle();
        frame.Destroy();
        device->DestroyTexture(target);
    }
    device->WaitIdle();
    CHECK(NothingAlive(device->Statistics()));
}

TEST_CASE("A device that cannot build the G-buffer pipelines leaves every object out and the frame still ends Ok", "[renderer][fault]") {
    rhi::NullDeviceOptions options;
    options.caps.max_bind_groups = 2; // the material pipelines use sets 0, 1 and 2; the lighting pass needs two groups
    DeferredRig rig;
    REQUIRE(rig.Create(0, nullptr, &options));
    rig.device->ResetErrors();
    CHECK(rig.Render() == FrameStatus::Ok);
    CHECK_FALSE(rig.frame.LastGBufferOk());
    CHECK(rig.frame.GBufferStatistics().drawn_objects == 0);
    CHECK(rig.frame.GBufferStatistics().rejected == rig.frame.GBufferStatistics().visible);
    CHECK(rig.frame.GBufferStatistics().visible > 0);
    CHECK(std::string(rig.frame.LastError()).empty()); // the frame itself did not fail; the objects were named in the pass's error
    CHECK(rig.TearDown());
}

TEST_CASE("A device that cannot build the lighting pipeline refuses the frame setup cleanly", "[renderer][fault]") {
    rhi::NullDeviceOptions options;
    options.caps.max_bind_groups = 1; // the lighting pass binds the frame block and the G-buffer inputs: two groups
    DeferredRig rig;
    CHECK_FALSE(rig.Create(0, nullptr, &options)); // DemoFrame::Create reports the pass that is not valid
    CHECK(rig.TearDown());
}

TEST_CASE("A device with too few color attachments is refused before any pass is built", "[renderer][fault]") {
    rhi::NullDeviceOptions options;
    options.caps.max_color_attachments = 2;
    DeferredRig rig;
    CHECK_FALSE(rig.Create(0, nullptr, &options));
    CHECK(rig.TearDown());
}
