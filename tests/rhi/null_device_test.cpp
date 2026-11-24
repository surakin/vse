#include "sync/thread.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "spirv_builder.h"

using namespace vse;
using namespace vse::rhi;

namespace
{

// The null device logs each violation at error level; silence the channel so expected errors do not flood the
// test output, and restore it afterwards.
struct QuietLog
{
    bool saved = LogRhi.Enabled();
    QuietLog() { LogRhi.SetEnabled(false); }
    ~QuietLog() { LogRhi.SetEnabled(saved); }
};

struct Fixture
{
    QuietLog quiet;
    NullDevicePtr dev;
    Fixture(const NullDeviceOptions& options = NullDeviceOptions())
    {
        dev = CreateNullDevice(options);
        dev->BeginFrame();
    }

    BufferHandle Buffer(u64 size, BufferUsage usage, MemoryLocation loc = MemoryLocation::GpuOnly)
    {
        BufferDesc d;
        d.size = size;
        d.usage = usage;
        d.location = loc;
        return dev->CreateBuffer(d);
    }
    TextureHandle Texture(u32 w, u32 h, Format f, TextureUsage usage)
    {
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = f;
        d.usage = usage;
        return dev->CreateTexture(d);
    }
    ShaderHandle Shader(ShaderStage stage)
    {
        ShaderDesc d;
        d.stage = stage;
        const Vector<u32> words = spirv_test::MinimalModule(stage);
        d.SetCode(SpirvBlob(words.Data(), words.Size()));
        return dev->CreateShader(d);
    }
    CommandList* List()
    {
        CommandList* l = dev->CreateCommandList();
        REQUIRE(l != nullptr);
        l->Begin();
        return l;
    }
    void Submit(CommandList* l)
    {
        l->End();
        dev->Submit(&l, 1);
    }
    u32 Errors() const { return dev->ErrorCount(); }
    // Asserts that exactly one error has been reported since the last check, then clears it.
    void ExpectOneError()
    {
        CHECK(dev->ErrorCount() == 1);
        dev->ResetErrors();
    }
};

// A complete set of resources for a draw with one vertex buffer, one uniform buffer, a texture and a sampler.
struct DrawScene
{
    Fixture& f;
    TextureHandle target;
    BufferHandle vertices;
    BufferHandle uniforms;
    TextureHandle texture;
    SamplerHandle sampler;
    BindGroupLayoutHandle layout;
    BindGroupHandle group;
    ShaderHandle vs;
    ShaderHandle fs;
    PipelineHandle pipeline;
    VertexBindingDesc binding{12, false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};

    explicit DrawScene(Fixture& fx) : f(fx)
    {
        target = f.Texture(64, 64, Format::RGBA8_UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySrc);
        vertices = f.Buffer(1024, BufferUsage::Vertex | BufferUsage::CopyDst);
        uniforms = f.Buffer(256, BufferUsage::Uniform, MemoryLocation::CpuToGpu);
        texture = f.Texture(16, 16, Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::CopyDst);
        SamplerDesc sd;
        sampler = f.dev->CreateSampler(sd);

        BindingDesc entries[3] = {
            {0, BindingType::UniformBuffer, ShaderStage::Vertex, 1},
            {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
            {2, BindingType::Sampler, ShaderStage::Fragment, 1},
        };
        BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<BindingDesc>(entries);
        layout = f.dev->CreateBindGroupLayout(ld);

        BindGroupEntry ge[3];
        ge[0].binding = 0;
        ge[0].buffer = uniforms;
        ge[1].binding = 1;
        ge[1].texture = texture;
        ge[2].binding = 2;
        ge[2].sampler = sampler;
        BindGroupDesc gd;
        gd.layout = layout;
        gd.entries = rhi::Span<BindGroupEntry>(ge);
        group = f.dev->CreateBindGroup(gd);

        vs = f.Shader(ShaderStage::Vertex);
        fs = f.Shader(ShaderStage::Fragment);
        GraphicsPipelineDesc pd;
        pd.vertex_shader = vs;
        pd.fragment_shader = fs;
        pd.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
        pd.vertex_attributes = rhi::Span<VertexAttributeDesc>(&attr, 1);
        pd.color_targets[0].format = Format::RGBA8_UNorm;
        pd.color_target_count = 1;
        pd.bind_group_layouts[0] = layout;
        pd.bind_group_layout_count = 1;
        pd.push_constant_bytes = 16;
        pd.push_constant_stages = ShaderStage::Vertex;
        pipeline = f.dev->CreateGraphicsPipeline(pd);
    }

    // Moves every resource into the state the draw needs. Records into `l`.
    void Prepare(CommandList* l)
    {
        l->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
        l->Barrier(texture, ResourceState::Undefined, ResourceState::ShaderRead);
        l->Barrier(vertices, ResourceState::Undefined, ResourceState::VertexBuffer);
    }
    RenderingDesc Pass() const
    {
        RenderingDesc r;
        r.color[0].texture = target;
        r.color_count = 1;
        return r;
    }
    void BindAll(CommandList* l)
    {
        l->SetPipeline(pipeline);
        l->SetBindGroup(0, group);
        l->SetVertexBuffer(0, vertices, 0);
    }
};

}  // namespace

TEST_CASE("The null device reports its backend and caps", "[rhi][null]")
{
    Fixture f;
    CHECK(StringView(f.dev->BackendName()) == StringView("null"));
    CHECK(f.dev->Caps().max_bind_groups >= 4);
    CHECK(f.dev->FrameNumber() == 1);
    CHECK_FALSE(f.dev->DeviceLost());
}

TEST_CASE("Upload by mapping then copy to a GPU-only buffer round-trips", "[rhi][null]")
{
    Fixture f;
    BufferHandle staging = f.Buffer(64, BufferUsage::CopySrc, MemoryLocation::CpuToGpu);
    BufferHandle dst = f.Buffer(64, BufferUsage::CopyDst | BufferUsage::Vertex);
    REQUIRE(staging.IsValid());
    REQUIRE(dst.IsValid());

    u8* p = static_cast<u8*>(f.dev->MapBuffer(staging));
    REQUIRE(p != nullptr);
    for (u32 i = 0; i < 64; ++i)
    {
        p[i] = static_cast<u8>(i * 3);
    }
    f.dev->UnmapBuffer(staging);

    CommandList* l = f.List();
    l->Barrier(dst, ResourceState::Undefined, ResourceState::CopyDst);
    l->CopyBuffer(staging, dst, 0, 0, 64);
    l->Barrier(dst, ResourceState::CopyDst, ResourceState::VertexBuffer);
    f.Submit(l);

    CHECK(f.Errors() == 0);
    CHECK(f.dev->StateOf(dst) == ResourceState::VertexBuffer);
    u8 out[64];
    REQUIRE(f.dev->PeekBuffer(dst, 0, out, 64));
    for (u32 i = 0; i < 64; ++i)
    {
        CHECK(out[i] == static_cast<u8>(i * 3));
    }
}

TEST_CASE("A complete legal draw sequence produces no errors", "[rhi][null]")
{
    Fixture f;
    DrawScene s(f);
    REQUIRE(s.pipeline.IsValid());
    REQUIRE(s.group.IsValid());

    CommandList* l = f.List();
    s.Prepare(l);
    l->PushDebugLabel("frame");
    l->BeginRendering(s.Pass());
    s.BindAll(l);
    Viewport vp;
    vp.width = 64;
    vp.height = 64;
    l->SetViewport(vp);
    ScissorRect sc;
    sc.width = 64;
    sc.height = 64;
    l->SetScissor(sc);
    u32 push[4] = {1, 2, 3, 4};
    l->SetPushConstants(0, push, sizeof(push));
    l->Draw(3, 1, 0, 0);
    l->EndRendering();
    l->PopDebugLabel();
    f.Submit(l);

    CHECK(f.Errors() == 0);
    CHECK(f.dev->StateOf(s.target) == ResourceState::ColorAttachment);
    f.dev->EndFrame();
    f.dev->BeginFrame();
    CHECK(f.Errors() == 0);
}

TEST_CASE("A legal indexed draw and a legal compute dispatch produce no errors", "[rhi][null]")
{
    Fixture f;
    DrawScene s(f);
    BufferHandle indices = f.Buffer(64, BufferUsage::Index | BufferUsage::CopyDst);

    CommandList* l = f.List();
    s.Prepare(l);
    l->Barrier(indices, ResourceState::Undefined, ResourceState::IndexBuffer);
    l->BeginRendering(s.Pass());
    s.BindAll(l);
    l->SetIndexBuffer(indices, 0, IndexType::UInt16);
    l->DrawIndexed(6, 1, 0, 0, 0);
    l->EndRendering();
    f.Submit(l);
    CHECK(f.Errors() == 0);

    // Compute with one storage buffer.
    BindingDesc cb{0, BindingType::StorageBuffer, ShaderStage::Compute, 1};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(&cb, 1);
    BindGroupLayoutHandle layout = f.dev->CreateBindGroupLayout(ld);
    BufferHandle storage = f.Buffer(1024, BufferUsage::Storage);
    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = storage;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);
    BindGroupHandle group = f.dev->CreateBindGroup(gd);
    ComputePipelineDesc cd;
    cd.shader = f.Shader(ShaderStage::Compute);
    cd.bind_group_layouts[0] = layout;
    cd.bind_group_layout_count = 1;
    PipelineHandle cp = f.dev->CreateComputePipeline(cd);
    REQUIRE(cp.IsValid());

    f.dev->EndFrame();
    f.dev->BeginFrame();
    l = f.List();
    l->Barrier(storage, ResourceState::Undefined, ResourceState::ShaderWrite);
    l->SetPipeline(cp);
    l->SetBindGroup(0, group);
    l->Dispatch(16, 1, 1);
    l->Barrier(storage, ResourceState::ShaderWrite, ResourceState::ShaderRead);
    l->Dispatch(16, 1, 1);
    f.Submit(l);
    CHECK(f.Errors() == 0);
}

TEST_CASE("Invalid descriptors fail creation with one counted error each", "[rhi][null][misuse]")
{
    Fixture f;
    BufferDesc bd;  // size zero
    CHECK_FALSE(f.dev->CreateBuffer(bd).IsValid());
    f.ExpectOneError();
    TextureDesc td;  // zero extent
    CHECK_FALSE(f.dev->CreateTexture(td).IsValid());
    f.ExpectOneError();
    SamplerDesc sd;
    sd.max_anisotropy = 99.0f;
    CHECK_FALSE(f.dev->CreateSampler(sd).IsValid());
    f.ExpectOneError();
    ShaderDesc shd;  // no stage
    CHECK_FALSE(f.dev->CreateShader(shd).IsValid());
    f.ExpectOneError();
    GraphicsPipelineDesc gd;  // no vertex shader
    CHECK_FALSE(f.dev->CreateGraphicsPipeline(gd).IsValid());
    f.ExpectOneError();
    ComputePipelineDesc cd;
    CHECK_FALSE(f.dev->CreateComputePipeline(cd).IsValid());
    f.ExpectOneError();
}

TEST_CASE("Huge binding array counts cannot overflow the element budget", "[rhi][null][misuse]")
{
    Fixture f;
    // Two bindings of 0x80000000 elements would sum to zero in 32 bits.
    BindingDesc big[2] = {
        {0, BindingType::SampledTexture, ShaderStage::Fragment, 0x80000000u},
        {1, BindingType::Sampler, ShaderStage::Fragment, 0x80000000u},
    };
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(big);
    CHECK_FALSE(f.dev->CreateBindGroupLayout(ld).IsValid());
    f.ExpectOneError();

    // Several arrays that are individually fine but together exceed the device's element budget.
    BindingDesc many[3] = {
        {0, BindingType::SampledTexture, ShaderStage::Fragment, 40},
        {1, BindingType::Sampler, ShaderStage::Fragment, 40},
        {2, BindingType::Sampler, ShaderStage::Fragment, 1},
    };
    ld.entries = rhi::Span<BindingDesc>(many);
    CHECK_FALSE(f.dev->CreateBindGroupLayout(ld).IsValid());
    f.ExpectOneError();
}

TEST_CASE("The caller's limits are honored, including a single-sample anisotropy limit", "[rhi][null]")
{
    NullDeviceOptions opts;
    CHECK(opts.caps.max_sampler_anisotropy == 16.0f);  // the default for a null device
    opts.caps.max_sampler_anisotropy = 1.0f;
    Fixture f(opts);
    CHECK(f.dev->Caps().max_sampler_anisotropy == 1.0f);
}

TEST_CASE("Resources beyond the device limits are refused", "[rhi][null][misuse]")
{
    Fixture f;
    CHECK_FALSE(f.Texture(8192, 8, Format::RGBA8_UNorm, TextureUsage::Sampled).IsValid());
    f.ExpectOneError();
    CHECK_FALSE(f.Buffer(1ull << 40, BufferUsage::Vertex).IsValid());
    f.ExpectOneError();
}

TEST_CASE("A cube face beyond the device's cube limit is refused though a 2D texture of that size is not",
          "[rhi][null][misuse]")
{
    NullDeviceOptions opts;
    opts.caps.max_texture_dimension_2d = 4096;
    opts.caps.max_texture_dimension_cube = 1024;
    Fixture f(opts);
    TextureDesc d;
    d.dimension = TextureDimension::Cube;
    d.width = d.height = 2048;
    d.array_layers = 6;
    d.format = Format::RGBA8_UNorm;
    d.usage = TextureUsage::Sampled;
    CHECK_FALSE(f.dev->CreateTexture(d).IsValid());
    f.ExpectOneError();
    CHECK(f.Texture(2048, 8, Format::RGBA8_UNorm, TextureUsage::Sampled).IsValid());
    d.width = d.height = 1024;
    CHECK(f.dev->CreateTexture(d).IsValid());
}

TEST_CASE("Stale handles are rejected on every destroy", "[rhi][null][misuse]")
{
    Fixture f;
    BufferHandle b = f.Buffer(16, BufferUsage::Vertex);
    f.dev->DestroyBuffer(b);
    CHECK(f.Errors() == 0);
    f.dev->DestroyBuffer(b);  // double destroy
    f.ExpectOneError();
    f.dev->DestroyTexture(TextureHandle{});
    f.ExpectOneError();
    f.dev->DestroySampler(SamplerHandle{3, 3});
    f.ExpectOneError();
    f.dev->DestroyShader(ShaderHandle{});
    f.ExpectOneError();
    f.dev->DestroyPipeline(PipelineHandle{});
    f.ExpectOneError();
    f.dev->DestroyBindGroup(BindGroupHandle{});
    f.ExpectOneError();
    f.dev->DestroyBindGroupLayout(BindGroupLayoutHandle{});
    f.ExpectOneError();
}

TEST_CASE("Mapping rules", "[rhi][null][misuse]")
{
    Fixture f;
    BufferHandle gpu = f.Buffer(16, BufferUsage::Vertex);
    CHECK(f.dev->MapBuffer(gpu) == nullptr);  // GPU-only is not CPU-visible
    f.ExpectOneError();

    BufferHandle cpu = f.Buffer(16, BufferUsage::CopySrc, MemoryLocation::CpuToGpu);
    CHECK(f.dev->MapBuffer(cpu) != nullptr);
    CHECK(f.dev->MapBuffer(cpu) == nullptr);  // already mapped
    f.ExpectOneError();
    f.dev->DestroyBuffer(cpu);  // still mapped
    f.ExpectOneError();
    f.dev->UnmapBuffer(cpu);
    f.dev->UnmapBuffer(cpu);  // not mapped any more
    f.ExpectOneError();
    f.dev->DestroyBuffer(cpu);
    CHECK(f.Errors() == 0);
    f.dev->UnmapBuffer(cpu);  // destroyed
    f.ExpectOneError();
}

TEST_CASE("Recording outside Begin/End and unbalanced scopes are errors", "[rhi][null][misuse]")
{
    Fixture f;
    BufferHandle b = f.Buffer(16, BufferUsage::CopySrc | BufferUsage::CopyDst);
    CommandList* l = f.dev->CreateCommandList();
    l->CopyBuffer(b, b, 0, 0, 4);  // before Begin
    f.ExpectOneError();
    l->Begin();
    l->Begin();  // twice
    f.ExpectOneError();
    l->PopDebugLabel();  // nothing to pop
    f.ExpectOneError();
    l->PushDebugLabel("a");
    l->End();  // label left open
    f.ExpectOneError();
    l->PushDebugLabel("late");  // after End
    f.ExpectOneError();
}

TEST_CASE("Rendering pass scoping", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);
    l->EndRendering();  // none open
    f.ExpectOneError();
    l->BeginRendering(s.Pass());
    l->BeginRendering(s.Pass());  // nested
    f.ExpectOneError();
    l->Barrier(s.vertices, ResourceState::VertexBuffer, ResourceState::CopySrc);  // not allowed inside a pass
    f.ExpectOneError();
    l->End();  // pass still open
    f.ExpectOneError();
    RenderingDesc none;
    CommandList* l2 = f.List();
    l2->BeginRendering(none);  // no attachments
    f.ExpectOneError();
}

TEST_CASE("A barrier from Undefined is always accepted, for buffers and textures alike", "[rhi][null]")
{
    Fixture f;
    BufferHandle b = f.Buffer(16, BufferUsage::CopySrc | BufferUsage::CopyDst);
    TextureHandle t = f.Texture(4, 4, Format::RGBA8_UNorm, TextureUsage::CopySrc | TextureUsage::CopyDst);
    CommandList* l = f.List();
    l->Barrier(b, ResourceState::Undefined, ResourceState::CopyDst);
    l->Barrier(t, ResourceState::Undefined, ResourceState::CopyDst);
    l->Barrier(b, ResourceState::Undefined, ResourceState::CopySrc);  // discard and re-enter another state
    l->Barrier(t, ResourceState::Undefined, ResourceState::CopySrc);
    f.Submit(l);
    CHECK(f.Errors() == 0);
    CHECK(f.dev->StateOf(b) == ResourceState::CopySrc);
    CHECK(f.dev->StateOf(t) == ResourceState::CopySrc);
}

TEST_CASE("Barrier state mismatches are caught at submit", "[rhi][null][misuse]")
{
    Fixture f;
    TextureHandle t = f.Texture(8, 8, Format::RGBA8_UNorm, TextureUsage::Sampled | TextureUsage::CopyDst);
    CommandList* l = f.List();
    l->Barrier(t, ResourceState::Undefined, ResourceState::CopyDst);
    l->Barrier(t, ResourceState::ShaderRead, ResourceState::CopySrc);  // texture is in CopyDst, not ShaderRead
    f.Submit(l);
    f.ExpectOneError();

    // A state the resource's usage cannot enter is rejected when recorded.
    l = nullptr;
    f.dev->EndFrame();
    f.dev->BeginFrame();
    l = f.List();
    l->Barrier(t, ResourceState::CopyDst, ResourceState::ColorAttachment);  // no ColorAttachment usage
    f.ExpectOneError();
    BufferHandle cpu = f.Buffer(16, BufferUsage::CopySrc, MemoryLocation::CpuToGpu);
    l->Barrier(cpu, ResourceState::Undefined, ResourceState::CopySrc);  // CPU-visible buffers take no barriers
    f.ExpectOneError();
}

TEST_CASE("Copy misuse", "[rhi][null][misuse]")
{
    Fixture f;
    BufferHandle a = f.Buffer(32, BufferUsage::CopySrc | BufferUsage::CopyDst);
    BufferHandle b = f.Buffer(32, BufferUsage::CopyDst);
    BufferHandle v = f.Buffer(32, BufferUsage::Vertex);
    CommandList* l = f.List();
    l->CopyBuffer(a, b, 0, 0, 64);  // too big
    f.ExpectOneError();
    l->CopyBuffer(a, b, 8, 0, 32);  // source range overflows
    f.ExpectOneError();
    l->CopyBuffer(b, a, 0, 0, 8);  // b lacks CopySrc
    f.ExpectOneError();
    l->CopyBuffer(a, v, 0, 0, 8);  // v lacks CopyDst
    f.ExpectOneError();
    l->CopyBuffer(a, a, 0, 0, 8);  // same buffer
    f.ExpectOneError();
    f.dev->DestroyBuffer(b);
    l->CopyBuffer(a, b, 0, 0, 8);  // destroyed
    f.ExpectOneError();

    // Wrong state at submit: the destination was never moved to CopyDst.
    BufferHandle d = f.Buffer(32, BufferUsage::CopyDst);
    CommandList* l2 = f.List();
    l2->Barrier(a, ResourceState::Undefined, ResourceState::CopySrc);
    l2->CopyBuffer(a, d, 0, 0, 8);
    f.Submit(l2);
    f.ExpectOneError();
}

// A device whose copies need only texel alignment (Vulkan, Metal), so these tests exercise the texel rules alone.
NullDeviceOptions UnitCopyAlignment()
{
    NullDeviceOptions options;
    options.caps.buffer_texture_copy_offset_alignment = 1;
    options.caps.buffer_texture_copy_row_pitch_alignment = 1;
    return options;
}

TEST_CASE("Texture copies need texel-aligned offsets and pitches", "[rhi][null][misuse]")
{
    Fixture f(UnitCopyAlignment());
    TextureHandle t = f.Texture(16, 16, Format::RGBA8_UNorm, TextureUsage::CopyDst);
    BufferHandle b = f.Buffer(4096, BufferUsage::CopySrc);
    CommandList* l = f.List();
    BufferTextureCopy r;
    r.buffer_offset = 2;  // not a multiple of 4
    l->CopyBufferToTexture(b, t, r);
    f.ExpectOneError();
    r.buffer_offset = 0;
    r.buffer_row_pitch = 70;  // not a multiple of 4
    l->CopyBufferToTexture(b, t, r);
    f.ExpectOneError();
    r.buffer_row_pitch = 128;  // padded rows are fine
    l->CopyBufferToTexture(b, t, r);
    CHECK(f.Errors() == 0);
}

TEST_CASE("Texture copy range checks", "[rhi][null][misuse]")
{
    Fixture f(UnitCopyAlignment());
    TextureHandle t = f.Texture(16, 16, Format::RGBA8_UNorm, TextureUsage::CopyDst | TextureUsage::CopySrc);
    BufferHandle small = f.Buffer(100, BufferUsage::CopySrc | BufferUsage::CopyDst);
    BufferHandle big = f.Buffer(16 * 16 * 4, BufferUsage::CopySrc | BufferUsage::CopyDst);
    CommandList* l = f.List();
    BufferTextureCopy r;
    l->CopyBufferToTexture(small, t, r);  // 1024 bytes needed
    f.ExpectOneError();
    r.mip_level = 5;  // texture has one mip
    l->CopyBufferToTexture(big, t, r);
    f.ExpectOneError();
    r.mip_level = 0;
    r.buffer_row_pitch = 8;  // less than one row (64 bytes)
    l->CopyTextureToBuffer(t, big, r);
    f.ExpectOneError();
    r.buffer_row_pitch = 0;
    r.buffer_offset = 4;  // pushes the end past the buffer
    l->CopyBufferToTexture(big, t, r);
    f.ExpectOneError();
    r.buffer_offset = 0;
    l->Barrier(t, ResourceState::Undefined, ResourceState::CopyDst);
    l->Barrier(big, ResourceState::Undefined, ResourceState::CopySrc);
    l->CopyBufferToTexture(big, t, r);  // legal
    f.Submit(l);
    CHECK(f.Errors() == 0);
}

TEST_CASE("Draw preconditions", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);

    l->Draw(3, 1, 0, 0);  // outside a pass
    f.ExpectOneError();
    l->BeginRendering(s.Pass());
    l->Draw(3, 1, 0, 0);  // no pipeline
    f.ExpectOneError();
    l->SetPipeline(s.pipeline);
    l->Draw(3, 1, 0, 0);  // no bind group
    f.ExpectOneError();
    l->SetBindGroup(0, s.group);
    l->Draw(3, 1, 0, 0);  // no vertex buffer
    f.ExpectOneError();
    l->SetVertexBuffer(0, s.vertices, 0);
    l->DrawIndexed(3, 1, 0, 0, 0);  // no index buffer
    f.ExpectOneError();
    l->Draw(3, 1, 0, 0);  // now legal
    CHECK(f.Errors() == 0);
    l->EndRendering();
    f.Submit(l);
    CHECK(f.Errors() == 0);
}

TEST_CASE("A graphics pipeline does not carry over to the next rendering pass", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);
    l->BeginRendering(s.Pass());
    s.BindAll(l);
    l->Draw(3, 1, 0, 0);
    CHECK(f.Errors() == 0);
    l->EndRendering();

    l->BeginRendering(s.Pass());
    l->SetBindGroup(0, s.group);
    l->SetVertexBuffer(0, s.vertices, 0);
    l->Draw(3, 1, 0, 0);  // the pipeline from the previous pass is gone
    f.ExpectOneError();
    l->EndRendering();
}

TEST_CASE("Binding misuse", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);

    l->SetBindGroup(7, s.group);  // beyond the device limit
    f.ExpectOneError();
    l->SetBindGroup(0, BindGroupHandle{});
    f.ExpectOneError();
    l->SetVertexBuffer(99, s.vertices, 0);
    f.ExpectOneError();
    l->SetVertexBuffer(0, s.uniforms, 0);  // lacks Vertex usage
    f.ExpectOneError();
    l->SetVertexBuffer(0, s.vertices, 4096);  // past the end
    f.ExpectOneError();
    l->SetIndexBuffer(s.vertices, 0, IndexType::UInt16);  // lacks Index usage
    f.ExpectOneError();

    // A group made from a different layout than the pipeline declares.
    BindingDesc other{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(&other, 1);
    BindGroupLayoutHandle other_layout = f.dev->CreateBindGroupLayout(ld);
    BindGroupEntry ge;
    ge.binding = 0;
    ge.buffer = s.uniforms;
    BindGroupDesc gd;
    gd.layout = other_layout;
    gd.entries = rhi::Span<BindGroupEntry>(&ge, 1);
    BindGroupHandle wrong = f.dev->CreateBindGroup(gd);
    REQUIRE(wrong.IsValid());

    l->BeginRendering(s.Pass());
    l->SetPipeline(s.pipeline);
    l->SetBindGroup(0, wrong);
    l->SetVertexBuffer(0, s.vertices, 0);
    l->Draw(3, 1, 0, 0);
    f.ExpectOneError();
    l->EndRendering();
}

TEST_CASE("Pipeline and attachment mismatches", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);
    l->SetPipeline(s.pipeline);  // graphics pipeline outside a pass
    f.ExpectOneError();

    // A pass with a different color format than the pipeline.
    TextureHandle bgra = f.Texture(64, 64, Format::BGRA8_UNorm, TextureUsage::ColorAttachment);
    l->Barrier(bgra, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc r;
    r.color[0].texture = bgra;
    r.color_count = 1;
    l->BeginRendering(r);
    l->SetPipeline(s.pipeline);
    f.ExpectOneError();
    l->EndRendering();

    // Attachments of different sizes.
    TextureHandle small = f.Texture(32, 32, Format::RGBA8_UNorm, TextureUsage::ColorAttachment);
    RenderingDesc two = s.Pass();
    two.color[1].texture = small;
    two.color_count = 2;
    l->BeginRendering(two);
    f.ExpectOneError();

    // A destroyed attachment.
    f.dev->DestroyTexture(small);
    RenderingDesc dead;
    dead.color[0].texture = small;
    dead.color_count = 1;
    l->BeginRendering(dead);
    f.ExpectOneError();
}

TEST_CASE("Resources in the wrong state at submit are caught", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    // The vertex buffer is left in CopyDst instead of VertexBuffer.
    CommandList* l = f.List();
    l->Barrier(s.target, ResourceState::Undefined, ResourceState::ColorAttachment);
    l->Barrier(s.texture, ResourceState::Undefined, ResourceState::ShaderRead);
    l->Barrier(s.vertices, ResourceState::Undefined, ResourceState::CopyDst);
    l->BeginRendering(s.Pass());
    s.BindAll(l);
    l->Draw(3, 1, 0, 0);
    l->EndRendering();
    f.Submit(l);
    f.ExpectOneError();

    // The sampled texture is still Undefined.
    f.dev->EndFrame();
    f.dev->BeginFrame();
    DrawScene s2(f);
    l = f.List();
    l->Barrier(s2.target, ResourceState::Undefined, ResourceState::ColorAttachment);
    l->Barrier(s2.vertices, ResourceState::Undefined, ResourceState::VertexBuffer);
    l->BeginRendering(s2.Pass());
    s2.BindAll(l);
    l->Draw(3, 1, 0, 0);
    l->EndRendering();
    f.Submit(l);
    f.ExpectOneError();

    // The color target was never moved to ColorAttachment.
    f.dev->EndFrame();
    f.dev->BeginFrame();
    DrawScene s3(f);
    l = f.List();
    l->BeginRendering(s3.Pass());
    l->EndRendering();
    f.Submit(l);
    f.ExpectOneError();
}

TEST_CASE("A destroyed resource used by a recorded draw is caught", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    s.Prepare(l);
    l->BeginRendering(s.Pass());
    s.BindAll(l);
    f.dev->DestroyTexture(s.texture);  // referenced by the bound group
    l->Draw(3, 1, 0, 0);
    f.ExpectOneError();
    l->EndRendering();
}

TEST_CASE("Push constant range checks", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    CommandList* l = f.List();
    u32 data[8] = {};
    l->SetPushConstants(0, data, 4);  // no pipeline yet
    f.ExpectOneError();
    s.Prepare(l);
    l->BeginRendering(s.Pass());
    l->SetPipeline(s.pipeline);        // push range is 16 bytes
    l->SetPushConstants(8, data, 16);  // 8 + 16 > 16
    f.ExpectOneError();
    l->SetPushConstants(2, data, 4);  // misaligned
    f.ExpectOneError();
    l->SetPushConstants(0, nullptr, 4);
    f.ExpectOneError();
    l->SetPushConstants(0, data, 16);  // fits
    CHECK(f.Errors() == 0);
    l->EndRendering();
}

TEST_CASE("Compute misuse", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    ComputePipelineDesc cd;
    cd.shader = f.Shader(ShaderStage::Compute);
    PipelineHandle cp = f.dev->CreateComputePipeline(cd);
    CommandList* l = f.List();
    l->Dispatch(1, 1, 1);  // no pipeline
    f.ExpectOneError();
    l->SetPipeline(cp);
    l->Dispatch(70000, 1, 1);  // beyond maxComputeWorkGroupCount
    f.ExpectOneError();
    s.Prepare(l);
    l->BeginRendering(s.Pass());
    l->SetPipeline(cp);  // compute pipeline inside a pass
    f.ExpectOneError();
    l->Dispatch(1, 1, 1);  // dispatch inside a pass
    f.ExpectOneError();
    l->EndRendering();
}

TEST_CASE("Viewport must have positive size", "[rhi][null][misuse]")
{
    Fixture f;
    CommandList* l = f.List();
    Viewport vp;
    l->SetViewport(vp);  // 0 x 0
    f.ExpectOneError();
    vp.width = 10;
    vp.height = 10;
    l->SetViewport(vp);
    CHECK(f.Errors() == 0);
}

TEST_CASE("Bind groups must match their layout", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    BindGroupDesc gd;
    gd.layout = s.layout;
    BindGroupEntry ge[3];
    ge[0].binding = 0;
    ge[0].buffer = s.uniforms;
    ge[1].binding = 1;
    ge[1].texture = s.texture;
    ge[2].binding = 2;
    ge[2].sampler = s.sampler;

    gd.entries = rhi::Span<BindGroupEntry>(ge, 2);  // missing the sampler
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();

    gd.entries = rhi::Span<BindGroupEntry>(ge);
    BindGroupEntry extra[4] = {ge[0], ge[1], ge[2], ge[0]};
    extra[3].binding = 9;  // no such binding
    gd.entries = rhi::Span<BindGroupEntry>(extra);
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();

    BindGroupEntry wrong_buffer[3] = {ge[0], ge[1], ge[2]};
    wrong_buffer[0].buffer = s.vertices;  // lacks Uniform usage
    gd.entries = rhi::Span<BindGroupEntry>(wrong_buffer);
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();

    BindGroupEntry unaligned[3] = {ge[0], ge[1], ge[2]};
    unaligned[0].offset = 4;
    gd.entries = rhi::Span<BindGroupEntry>(unaligned);
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();

    BindGroupEntry wrong_texture[3] = {ge[0], ge[1], ge[2]};
    wrong_texture[1].texture = s.target;  // lacks Sampled usage
    gd.entries = rhi::Span<BindGroupEntry>(wrong_texture);
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();

    gd.layout = BindGroupLayoutHandle{};
    CHECK_FALSE(f.dev->CreateBindGroup(gd).IsValid());
    f.ExpectOneError();
}

TEST_CASE("Pipelines need live shaders of the right stage", "[rhi][null][misuse]")
{
    Fixture f;
    DrawScene s(f);
    GraphicsPipelineDesc pd;
    pd.vertex_shader = s.fs;  // a fragment shader as the vertex stage
    pd.fragment_shader = s.fs;
    pd.color_targets[0].format = Format::RGBA8_UNorm;
    pd.color_target_count = 1;
    CHECK_FALSE(f.dev->CreateGraphicsPipeline(pd).IsValid());
    f.ExpectOneError();

    pd.vertex_shader = s.vs;
    f.dev->DestroyShader(s.fs);
    CHECK_FALSE(f.dev->CreateGraphicsPipeline(pd).IsValid());  // destroyed fragment shader
    f.ExpectOneError();

    pd.fragment_shader = ShaderHandle{};
    pd.color_target_count = 0;
    pd.depth_format = Format::D32_Float;
    pd.depth_test = true;
    pd.depth_write = true;
    CHECK(f.dev->CreateGraphicsPipeline(pd).IsValid());  // depth-only
    CHECK(f.Errors() == 0);
}

TEST_CASE("Submit validation", "[rhi][null][misuse]")
{
    Fixture f;
    CommandList* l = f.List();
    CommandList* ptr = l;
    CHECK(f.dev->Submit(&ptr, 1) == 0);  // not ended
    f.ExpectOneError();

    l->End();
    FenceValue v = f.dev->Submit(&ptr, 1);
    CHECK(v == 1);
    CHECK(f.dev->WaitFence(v, 0));
    CHECK(f.dev->CompletedFence() == v);
    CHECK(f.dev->Submit(&ptr, 1) == 0);  // already submitted
    f.ExpectOneError();

    CommandList* dup[2];
    dup[0] = f.List();
    dup[0]->End();
    dup[1] = dup[0];
    CHECK(f.dev->Submit(dup, 2) == 0);  // same list twice
    f.ExpectOneError();

    CommandList* stale = f.List();
    stale->End();
    f.dev->EndFrame();
    f.dev->BeginFrame();
    // The previous frame's lists were recycled. A pointer kept across the frame is reported, not dereferenced
    // as freed memory (ASan would flag that).
    CHECK(f.dev->Submit(&stale, 1) == 0);
    f.ExpectOneError();
    CommandList* fresh = f.List();
    fresh->End();
    CHECK(f.dev->Submit(&fresh, 1) == 2);
    CHECK(f.Errors() == 0);

    CHECK(f.dev->Submit(nullptr, 0) == 3);  // an empty batch is legal and still signals
    CommandList* none = nullptr;
    CHECK(f.dev->Submit(&none, 1) == 0);
    f.ExpectOneError();
}

TEST_CASE("Submit needs an open frame and a real array", "[rhi][null][misuse]")
{
    Fixture f;
    CommandList* l = f.List();
    l->End();
    CHECK(f.dev->Submit(nullptr, 1) == 0);  // count without an array
    f.ExpectOneError();
    f.dev->EndFrame();
    CHECK(f.dev->Submit(&l, 1) == 0);  // between frames
    f.ExpectOneError();
}

TEST_CASE("Frames must be begun and ended in pairs", "[rhi][null][misuse]")
{
    QuietLog quiet;
    NullDevicePtr dev = CreateNullDevice();
    dev->EndFrame();  // no frame
    CHECK(dev->ErrorCount() == 1);
    dev->BeginFrame();
    dev->BeginFrame();  // previous not ended
    CHECK(dev->ErrorCount() == 2);
    CHECK(dev->FrameNumber() == 2);
    dev.Reset();
}

TEST_CASE("Destruction is deferred until the GPU has finished the frame", "[rhi][null][lifetime]")
{
    NullDeviceOptions opts;
    opts.simulated_latency_frames = 2;
    Fixture f(opts);  // frame 1
    BufferHandle b = f.Buffer(16, BufferUsage::Vertex);
    f.dev->DestroyBuffer(b);
    CHECK(f.dev->Statistics().buffers == 0);
    CHECK(f.dev->Statistics().pending_destruction == 1);

    // Destroyed in frame 1; with a latency of 2 frames it is released when frame 4 begins.
    for (int i = 0; i < 2; ++i)
    {
        f.dev->EndFrame();
        f.dev->BeginFrame();  // frames 2, 3
        CHECK(f.dev->Statistics().pending_destruction == 1);
    }
    f.dev->EndFrame();
    f.dev->BeginFrame();  // frame 4
    CHECK(f.dev->Statistics().pending_destruction == 0);
    CHECK(f.Errors() == 0);
}

TEST_CASE("With no simulated latency a resource is released at the next frame", "[rhi][null][lifetime]")
{
    Fixture f;
    TextureHandle t = f.Texture(4, 4, Format::RGBA8_UNorm, TextureUsage::Sampled);
    f.dev->DestroyTexture(t);
    CHECK(f.dev->Statistics().pending_destruction == 1);
    f.dev->EndFrame();
    f.dev->BeginFrame();
    CHECK(f.dev->Statistics().pending_destruction == 0);
}

TEST_CASE("WaitIdle releases every deferred destruction", "[rhi][null][lifetime]")
{
    NullDeviceOptions opts;
    opts.simulated_latency_frames = 5;
    Fixture f(opts);
    for (int i = 0; i < 20; ++i)
    {
        f.dev->DestroyBuffer(f.Buffer(16, BufferUsage::Vertex));
    }
    CHECK(f.dev->Statistics().pending_destruction == 20);
    f.dev->WaitIdle();
    CHECK(f.dev->Statistics().pending_destruction == 0);
}

TEST_CASE("A destroyed handle never resolves, even after its slot is reused", "[rhi][null][lifetime]")
{
    Fixture f;
    BufferHandle old = f.Buffer(16, BufferUsage::CopySrc, MemoryLocation::CpuToGpu);
    f.dev->DestroyBuffer(old);
    f.dev->EndFrame();
    f.dev->BeginFrame();  // slot is recycled now
    BufferHandle fresh = f.Buffer(16, BufferUsage::CopySrc, MemoryLocation::CpuToGpu);
    CHECK(fresh.index == old.index);
    CHECK(fresh != old);
    CHECK(f.dev->MapBuffer(old) == nullptr);  // the stale handle does not reach the new buffer
    f.ExpectOneError();
    CHECK(f.dev->MapBuffer(fresh) != nullptr);
}

TEST_CASE("Statistics count live resources", "[rhi][null][lifetime]")
{
    Fixture f;
    DrawScene s(f);
    NullDeviceStats st = f.dev->Statistics();
    CHECK(st.buffers == 2);
    CHECK(st.textures == 2);
    CHECK(st.samplers == 1);
    CHECK(st.shaders == 2);
    CHECK(st.bind_group_layouts == 1);
    CHECK(st.bind_groups == 1);
    CHECK(st.pipelines == 1);
    f.dev->CreateCommandList();
    CHECK(f.dev->Statistics().command_lists == 1);
    f.dev->EndFrame();
    f.dev->BeginFrame();
    CHECK(f.dev->Statistics().command_lists == 0);
}

TEST_CASE("A lost device refuses work", "[rhi][null]")
{
    Fixture f;
    CommandList* l = f.List();
    l->End();
    f.dev->SimulateDeviceLoss();
    CHECK(f.dev->DeviceLost());
    CHECK(f.dev->Submit(&l, 1) == 0);
    CHECK(f.dev->CreateCommandList() == nullptr);
    CHECK(f.Errors() == 0);  // device loss is a state, not a contract violation
}

TEST_CASE("The last error message is retrievable and truncates safely", "[rhi][null]")
{
    Fixture f;
    char buf[256];
    CHECK(f.dev->LastError(buf, sizeof(buf)) == 0);
    f.dev->DestroyBuffer(BufferHandle{});
    usize n = f.dev->LastError(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(StringView(buf).Contains(StringView("DestroyBuffer")));
    char tiny[8];
    CHECK(f.dev->LastError(tiny, sizeof(tiny)) == 7);
    CHECK(tiny[7] == '\0');
    CHECK(f.dev->LastError(tiny, 0) == 0);
    f.dev->ResetErrors();
    CHECK(f.dev->ErrorCount() == 0);
    CHECK(f.dev->LastError(buf, sizeof(buf)) == 0);
}

TEST_CASE("Concurrent creation, destruction and recording is safe", "[rhi][null][threads]")
{
    Fixture f;
    constexpr int kThreads = 4;
    constexpr int kIterations = 200;
    Vector<Thread> threads;
    CommandList* lists[kThreads] = {};
    for (int t = 0; t < kThreads; ++t)
    {
        threads.Push(Thread(
            [&, t]()
            {
                for (int i = 0; i < kIterations; ++i)
                {
                    BufferDesc d;
                    d.size = 64;
                    d.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
                    d.location = MemoryLocation::GpuOnly;
                    BufferHandle a = f.dev->CreateBuffer(d);
                    BufferHandle b = f.dev->CreateBuffer(d);
                    f.dev->DestroyBuffer(a);
                    f.dev->DestroyBuffer(b);
                }
                CommandList* l = f.dev->CreateCommandList();
                l->Begin();
                BufferDesc d;
                d.size = 64;
                d.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
                BufferHandle src = f.dev->CreateBuffer(d);
                BufferHandle dst = f.dev->CreateBuffer(d);
                l->Barrier(src, ResourceState::Undefined, ResourceState::CopySrc);
                l->Barrier(dst, ResourceState::Undefined, ResourceState::CopyDst);
                l->CopyBuffer(src, dst, 0, 0, 64);
                l->End();
                lists[t] = l;
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i].Join();
    }
    FenceValue v = f.dev->Submit(lists, kThreads);
    CHECK(v == 1);
    CHECK(f.Errors() == 0);
}

TEST_CASE("The null device honors an injected device loss", "[rhi][null][fault]")
{
    DeviceCreateInfo info;
    info.backend = Backend::Null;
    info.faults.lose_device_at_frame = 2;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device != nullptr);
    device->BeginFrame();
    CHECK_FALSE(device->DeviceLost());
    device->EndFrame();
    device->BeginFrame();
    CHECK(device->DeviceLost());
    device->EndFrame();
    device.Reset();
}

TEST_CASE("The default device enforces Direct3D 12's copy alignment, so portable code pads its rows",
          "[rhi][null][misuse]")
{
    Fixture f;  // default caps: offset 512, row pitch 256
    TextureHandle t = f.Texture(16, 16, Format::RGBA8_UNorm, TextureUsage::CopyDst | TextureUsage::CopySrc);
    BufferHandle b = f.Buffer(16 * 256 + 1024, BufferUsage::CopySrc | BufferUsage::CopyDst);
    CommandList* l = f.List();
    l->Barrier(t, ResourceState::Undefined, ResourceState::CopyDst);
    l->Barrier(b, ResourceState::Undefined, ResourceState::CopySrc);
    BufferTextureCopy r;  // tightly packed rows of 64 bytes
    l->CopyBufferToTexture(b, t, r);
    f.ExpectOneError();
    r.buffer_row_pitch = 256;
    r.buffer_offset = 4;  // texel-aligned, but not 512-aligned
    l->CopyBufferToTexture(b, t, r);
    f.ExpectOneError();
    r.buffer_offset = 512;
    l->CopyBufferToTexture(b, t, r);  // legal
    f.Submit(l);
    CHECK(f.Errors() == 0);
}
