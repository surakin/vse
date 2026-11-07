#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "bindings_test.frag.spv.inc"
#include "fullscreen.vert.spv.inc"
#include "gpu_fixture.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/uniform_ring.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

struct QuietLog {
    bool saved = LogRhi.Enabled();
    QuietLog() { LogRhi.SetEnabled(false); }
    ~QuietLog() { LogRhi.SetEnabled(saved); }
};

BindGroupLayoutHandle UniformLayout(Device* device) {
    BindingDesc entry = {0, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(&entry, 1);
    return device->CreateBindGroupLayout(ld);
}

} // namespace

// ---- the uniform ring (null device: no GPU needed) ----

TEST_CASE("UniformRing offsets respect the alignment and never overlap", "[rhi][null][uniform-ring]") {
    NullDevicePtr device = CreateNullDevice();
    const u64 alignment = device->Caps().min_uniform_buffer_offset_alignment;
    REQUIRE(alignment > 1);
    auto ring_owner = std::make_unique<UniformRing>(device.Get(), 64 * 1024);
    UniformRing& ring = *ring_owner;
    REQUIRE(ring.Valid());
    device->BeginFrame();
    ring.BeginFrame();

    struct Span64 {
        u64 begin;
        u64 end;
    };
    std::vector<Span64> spans;
    u32 state = 12345;
    for (int i = 0; i < 200; ++i) {
        state = state * 1664525u + 1013904223u;
        const u64 size = 1 + (state >> 16) % 300;
        UniformAllocation a = ring.Allocate(size);
        if (!a.Valid()) {
            break;
        }
        CHECK(a.offset % alignment == 0);
        CHECK(a.size == size);
        CHECK(a.buffer == ring.Buffer());
        spans.push_back({a.offset, a.offset + size});
        std::memset(a.data, static_cast<int>(i), size); // the whole range is writable
    }
    REQUIRE(spans.size() > 20);
    for (usize i = 0; i < spans.size(); ++i) {
        for (usize j = i + 1; j < spans.size(); ++j) {
            CHECK((spans[i].end <= spans[j].begin || spans[j].end <= spans[i].begin));
        }
    }
    device->EndFrame();
    ring_owner.reset(); // before the device it uses
    device.Reset();
}

TEST_CASE("UniformRing is full at its capacity, resets each frame and rotates regions", "[rhi][null][uniform-ring]") {
    NullDevicePtr device = CreateNullDevice();
    QuietLog quiet;
    const u64 alignment = device->Caps().min_uniform_buffer_offset_alignment;
    auto ring_owner = std::make_unique<UniformRing>(device.Get(), alignment * 4);
    UniformRing& ring = *ring_owner;
    REQUIRE(ring.Valid());
    CHECK(ring.BytesPerFrame() == alignment * 4);

    u64 first_offset[4] = {};
    for (int frame = 0; frame < 4; ++frame) {
        device->BeginFrame();
        ring.BeginFrame();
        CHECK(ring.BytesUsed() == 0);
        for (int i = 0; i < 4; ++i) {
            UniformAllocation a = ring.Allocate(alignment); // exactly fills the region
            REQUIRE(a.Valid());
            if (i == 0) first_offset[frame] = a.offset;
        }
        CHECK_FALSE(ring.Allocate(1).Valid()); // full
        CHECK(ring.Allocate(alignment * 5).data == nullptr); // larger than a region
        device->EndFrame();
    }
    // Three regions, used in rotation: the fourth frame is back where the first was.
    CHECK(first_offset[0] != first_offset[1]);
    CHECK(first_offset[1] != first_offset[2]);
    CHECK(first_offset[0] == first_offset[3]);
    ring_owner.reset(); // before the device it uses
    device.Reset();
}

TEST_CASE("UniformRing allocations from several threads are disjoint", "[rhi][null][uniform-ring]") {
    NullDevicePtr device = CreateNullDevice();
    auto ring_owner = std::make_unique<UniformRing>(device.Get(), 512 * 1024);
    UniformRing& ring = *ring_owner;
    device->BeginFrame();
    ring.BeginFrame();
    constexpr int kThreads = 4;
    constexpr int kPerThread = 300;
    std::vector<UniformAllocation> results(kThreads * kPerThread);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                results[t * kPerThread + i] = ring.Allocate(16 + static_cast<u64>((i * 7 + t) % 100));
            }
        });
    }
    for (std::thread& th : threads) th.join();
    std::vector<unsigned char> seen(ring.BytesPerFrame() * UniformRing::kSlots, 0);
    for (const UniformAllocation& a : results) {
        REQUIRE(a.Valid());
        for (u64 b = 0; b < a.size; ++b) {
            REQUIRE(seen[a.offset + b] == 0); // no byte handed out twice
            seen[a.offset + b] = 1;
        }
    }
    device->EndFrame();
    ring_owner.reset(); // before the device it uses
    device.Reset();
}

TEST_CASE("Transient bind groups end with their frame on the null device", "[rhi][null][bindings]") {
    NullDevicePtr device = CreateNullDevice();
    QuietLog quiet;
    BindingDesc entry = {0, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(&entry, 1);
    const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(ld);
    BufferDesc bd;
    bd.size = 1024;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle buffer = device->CreateBuffer(bd);
    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = buffer;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);

    device->BeginFrame();
    gd.transient = true;
    const BindGroupHandle transient = device->CreateBindGroup(gd);
    gd.transient = false;
    const BindGroupHandle persistent = device->CreateBindGroup(gd);
    REQUIRE(transient.IsValid());
    REQUIRE(persistent.IsValid());
    device->DestroyBindGroup(transient); // not allowed: one error, the group stays
    CHECK(device->ErrorCount() == 1);
    device->ResetErrors();
    device->EndFrame();

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->SetBindGroup(0, persistent);
    CHECK(device->ErrorCount() == 0);
    list->SetBindGroup(0, transient); // released with the frame it was created in
    CHECK(device->ErrorCount() == 1);
    device->ResetErrors();
    list->End();
    device->DestroyBindGroup(persistent);
    device->EndFrame();
    device->DestroyBuffer(buffer);
    device->DestroyBindGroupLayout(layout);
    device.Reset();
}

// ---- bind groups and push constants on a real device ----

namespace {

struct BindDevice {
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device;

    explicit BindDevice(u32 sets_per_pool = 1024) {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.descriptor_sets_per_pool = sets_per_pool;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info);
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
    }
    ~BindDevice() {
        if (device != nullptr) {
            device->WaitIdle();
            device.Reset();
        }
    }
    void Cycle(int frames) {
        for (int i = 0; i < frames; ++i) {
            device->BeginFrame();
            device->EndFrame();
        }
        device->WaitIdle();
    }
};

} // namespace

TEST_CASE("A uniform, texture and sampler bind group and a push constant reach the shader", "[rhi][vulkan][gpu][bindings]") {
    BindDevice t;
    Device* device = t.device.Get();
    t.fx.ExpectErrors(0);
    constexpr u32 kSize = 8;

    // Texture: a constant color, uploaded (which leaves it in ShaderRead).
    TextureDesc td;
    td.width = 4;
    td.height = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    const TextureHandle texture = device->CreateTexture(td);
    REQUIRE(texture.IsValid());
    std::vector<unsigned char> texels(4 * 4 * 4);
    for (usize i = 0; i < texels.size(); i += 4) {
        texels[i] = 200;
        texels[i + 1] = 100;
        texels[i + 2] = 40;
        texels[i + 3] = 255;
    }
    REQUIRE(device->UploadTexture(texture, 0, 0, texels.data(), 0).ok);

    SamplerDesc sd;
    sd.mag_filter = FilterMode::Nearest;
    sd.min_filter = FilterMode::Nearest;
    const SamplerHandle sampler = device->CreateSampler(sd);
    REQUIRE(sampler.IsValid());

    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle ubo = device->CreateBuffer(bd);
    REQUIRE(ubo.IsValid());
    const float scale[4] = {0.5f, 1.0f, 1.0f, 1.0f};
    void* mapped = device->MapBuffer(ubo);
    std::memcpy(mapped, scale, sizeof(scale));
    device->UnmapBuffer(ubo);

    BindingDesc entries[3] = {{0, BindingType::UniformBuffer, ShaderStage::Fragment, 1},
                              {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                              {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(entries, 3);
    const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(ld);
    REQUIRE(layout.IsValid());
    BindGroupEntry ge[3];
    ge[0].binding = 0;
    ge[0].buffer = ubo;
    ge[1].binding = 1;
    ge[1].texture = texture;
    ge[2].binding = 2;
    ge[2].sampler = sampler;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(ge, 3);
    const BindGroupHandle persistent = device->CreateBindGroup(gd);
    REQUIRE(persistent.IsValid());

    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.words = shaders::kFullscreenVertSpv;
    vs.word_count = shaders::kFullscreenVertSpvWordCount;
    ShaderDesc fs;
    fs.stage = ShaderStage::Fragment;
    fs.words = shaders::kBindingsTestFragSpv;
    fs.word_count = shaders::kBindingsTestFragSpvWordCount;
    const ShaderHandle vshader = device->CreateShader(vs);
    const ShaderHandle fshader = device->CreateShader(fs);
    REQUIRE(vshader.IsValid());
    REQUIRE(fshader.IsValid());
    GraphicsPipelineDesc pd;
    pd.vertex_shader = vshader;
    pd.fragment_shader = fshader;
    pd.cull_mode = CullMode::None;
    pd.color_targets[0].format = Format::RGBA8_UNorm;
    pd.color_target_count = 1;
    pd.bind_group_layouts[0] = layout;
    pd.bind_group_layout_count = 1;
    pd.push_constant_bytes = 16;
    pd.push_constant_stages = ShaderStage::Fragment;
    const PipelineHandle pipeline = device->CreateGraphicsPipeline(pd);
    REQUIRE(pipeline.IsValid());

    TextureDesc rt;
    rt.width = kSize;
    rt.height = kSize;
    rt.format = Format::RGBA8_UNorm;
    rt.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(rt);
    REQUIRE(target.IsValid());

    // Two draws into two halves of the target, one with a persistent group and one with a transient one, with different pushes.
    device->BeginFrame();
    gd.transient = true;
    const BindGroupHandle transient = device->CreateBindGroup(gd);
    REQUIRE(transient.IsValid());
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target;
    list->BeginRendering(pass);
    list->SetPipeline(pipeline);
    list->SetBindGroup(0, persistent); // set before the push constants and the viewport: order must not matter
    const float bias_left[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    list->SetPushConstants(0, bias_left, 16);
    list->SetViewport(Viewport{0, 0, 4, 8, 0, 1});
    list->SetScissor(ScissorRect{0, 0, 4, 8});
    list->Draw(3, 1, 0, 0);
    const float bias_right[4] = {0.0f, 0.0f, 0.2f, 0.0f};
    list->SetBindGroup(0, transient);
    list->SetPushConstants(0, bias_right, 16);
    list->SetViewport(Viewport{4, 0, 4, 8, 0, 1});
    list->SetScissor(ScissorRect{4, 0, 4, 8});
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();

    std::vector<unsigned char> pixels(kSize * kSize * 4);
    REQUIRE(device->ReadbackTexture(target, 0, 0, pixels.data(), pixels.size(), ResourceState::ColorAttachment));
    auto near = [](int a, int b) { return a - b <= 2 && b - a <= 2; };
    const unsigned char* left = &pixels[(4 * kSize + 1) * 4];
    const unsigned char* right = &pixels[(4 * kSize + 6) * 4];
    CHECK(near(left[0], 100));  // 200 * 0.5 from the uniform buffer
    CHECK(near(left[1], 100));  // 100 * 1.0
    CHECK(near(left[2], 40));   // 40 + no bias
    CHECK(near(right[0], 100)); // the transient group binds the same resources
    CHECK(near(right[2], 40 + 51)); // push constant 0.2 * 255
    CHECK(near(right[3], 255));

    // The transient group is gone a few frames later; the persistent one stays until destroyed.
    t.Cycle(4);
    device->BeginFrame();
    CHECK(t.device->Counts().bind_groups == 1);
    device->EndFrame();

    device->DestroyBindGroup(persistent);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vshader);
    device->DestroyShader(fshader);
    device->DestroyBindGroupLayout(layout);
    device->DestroyBuffer(ubo);
    device->DestroySampler(sampler);
    device->DestroyTexture(texture);
    device->DestroyTexture(target);
    t.Cycle(4);
    device->BeginFrame();
    CHECK(t.device->Counts().bind_groups == 0);
    device->EndFrame();
}

TEST_CASE("Heavy transient descriptor use grows pools instead of failing", "[rhi][vulkan][gpu][bindings]") {
    BindDevice t(/*sets_per_pool=*/64);
    t.fx.ExpectErrors(0);
    Device* device = t.device.Get();
    const BindGroupLayoutHandle layout = UniformLayout(device);
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle ubo = device->CreateBuffer(bd);
    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = ubo;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);
    gd.transient = true;

    usize pools_after_first = 0;
    for (int frame = 0; frame < 4; ++frame) {
        device->BeginFrame();
        for (int i = 0; i < 10000; ++i) {
            const BindGroupHandle g = device->CreateBindGroup(gd);
            REQUIRE(g.IsValid());
        }
        if (frame == 0) {
            pools_after_first = t.device->Counts().descriptor_pools;
            CHECK(pools_after_first > 100); // 10000 / 64 pools, grown on demand
            CHECK(t.device->Counts().bind_groups == 10000);
        }
        device->EndFrame();
    }
    // Pools are reused by later frames rather than grown without bound: three frame slots, so at most about three times
    // the first frame's pools.
    CHECK(t.device->Counts().descriptor_pools <= pools_after_first * 3 + 3);
    t.Cycle(4);
    device->BeginFrame();
    CHECK(t.device->Counts().bind_groups == 0); // every transient group of the earlier frames was released
    device->EndFrame();
    device->DestroyBuffer(ubo);
    device->DestroyBindGroupLayout(layout);
}

TEST_CASE("Persistent bind groups are recycled and a large array gets a pool of its own", "[rhi][vulkan][gpu][bindings]") {
    BindDevice t(/*sets_per_pool=*/16);
    t.fx.ExpectErrors(0);
    Device* device = t.device.Get();
    const BindGroupLayoutHandle layout = UniformLayout(device);
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle ubo = device->CreateBuffer(bd);
    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = ubo;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);

    // Create and destroy far more groups over time than one pool holds: freed sets are reused.
    usize peak_pools = 0;
    for (int round = 0; round < 20; ++round) {
        std::vector<BindGroupHandle> groups;
        for (int i = 0; i < 40; ++i) {
            groups.push_back(device->CreateBindGroup(gd));
            REQUIRE(groups.back().IsValid());
        }
        const usize pools = t.device->Counts().descriptor_pools;
        if (pools > peak_pools) peak_pools = pools;
        for (BindGroupHandle g : groups) device->DestroyBindGroup(g);
        t.Cycle(3);
    }
    CHECK(peak_pools <= 4);

    // 200 samplers in one binding do not fit a 16-set pool sized by the default rule: they get an exact pool.
    BindingDesc big = {0, BindingType::Sampler, ShaderStage::Fragment, 200};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(&big, 1);
    const BindGroupLayoutHandle big_layout = device->CreateBindGroupLayout(ld);
    REQUIRE(big_layout.IsValid());
    const SamplerHandle sampler = device->CreateSampler(SamplerDesc{});
    std::vector<BindGroupEntry> entries(200);
    for (u32 i = 0; i < 200; ++i) {
        entries[i].binding = 0;
        entries[i].array_element = i;
        entries[i].sampler = sampler;
    }
    BindGroupDesc bg;
    bg.layout = big_layout;
    bg.entries = rhi::Span<BindGroupEntry>(entries.data(), entries.size());
    const BindGroupHandle big_group = device->CreateBindGroup(bg);
    CHECK(big_group.IsValid());
    device->DestroyBindGroup(big_group);
    t.Cycle(3);

    // Large groups made and destroyed again and again reuse their pool instead of making one each time.
    const usize pools_before = t.device->Counts().descriptor_pools;
    for (int i = 0; i < 20; ++i) {
        const BindGroupHandle again = device->CreateBindGroup(bg);
        REQUIRE(again.IsValid());
        device->DestroyBindGroup(again);
        t.Cycle(3);
    }
    CHECK(t.device->Counts().descriptor_pools == pools_before);

    // The same for a transient large group made every frame.
    bg.transient = true;
    for (int i = 0; i < 10; ++i) {
        device->BeginFrame();
        REQUIRE(device->CreateBindGroup(bg).IsValid());
        device->EndFrame();
    }
    CHECK(t.device->Counts().descriptor_pools <= pools_before + 3); // at most one pool per frame slot

    device->DestroySampler(sampler);
    device->DestroyBindGroupLayout(big_layout);
    device->DestroyBuffer(ubo);
    device->DestroyBindGroupLayout(layout);
}

TEST_CASE("Bind group misuse is refused before the driver sees it", "[rhi][vulkan][gpu][bindings]") {
    BindDevice t;
    t.fx.ExpectErrors(0);
    Device* device = t.device.Get();
    QuietLog quiet;
    const BindGroupLayoutHandle layout = UniformLayout(device);
    BufferDesc bd;
    bd.size = 1024;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle ubo = device->CreateBuffer(bd);
    BufferDesc wrong = bd;
    wrong.usage = BufferUsage::Vertex;
    const BufferHandle vertex_buffer = device->CreateBuffer(wrong);

    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = ubo;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);
    SECTION("a buffer without Uniform usage") {
        ge.buffer = vertex_buffer;
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("an offset that is not a multiple of the alignment") {
        // The alignment is the driver's (4 on AMD, 64 on NVIDIA); 1 is misaligned for any alignment above 1.
        if (device->Caps().min_uniform_buffer_offset_alignment <= 1) {
            SKIP("the device accepts any uniform offset");
        }
        ge.offset = 1;
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("a range outside the buffer") {
        ge.size = 4096;
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("no entry for a binding") {
        gd.entries = rhi::Span<BindGroupEntry>();
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("a destroyed buffer") {
        const BufferHandle gone = device->CreateBuffer(bd);
        device->DestroyBuffer(gone);
        ge.buffer = gone;
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("a destroyed layout") {
        const BindGroupLayoutHandle other = UniformLayout(device);
        device->DestroyBindGroupLayout(other);
        gd.layout = other;
        CHECK_FALSE(device->CreateBindGroup(gd).IsValid());
    }
    SECTION("a group whose resource is destroyed later reports it") {
        const BufferHandle shortlived = device->CreateBuffer(bd);
        ge.buffer = shortlived;
        const BindGroupHandle g = device->CreateBindGroup(gd);
        REQUIRE(g.IsValid());
        VulkanDevice::BindGroupInfo info;
        REQUIRE(t.device->LookupBindGroup(g, &info));
        CHECK(info.resources_live);
        device->DestroyBuffer(shortlived);
        REQUIRE(t.device->LookupBindGroup(g, &info));
        CHECK_FALSE(info.resources_live);
        device->DestroyBindGroup(g);
    }
    device->DestroyBuffer(ubo);
    device->DestroyBuffer(vertex_buffer);
    device->DestroyBindGroupLayout(layout);
}
