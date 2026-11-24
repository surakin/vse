#include "renderer/terrain_pass.h"

#include "containers/shared_bytes.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "terrain.frag.dxil.inc"
#include "terrain.vert.dxil.inc"

namespace vse::renderer
{

namespace
{

constexpr u32 kTileInstanceBytes = 32;
constexpr u64 kTileRegionBytes = static_cast<u64>(kMaxTerrainDrawTiles) * kTileInstanceBytes;
constexpr u64 kParamsRegionBytes = 256;  // the uniform offset alignment

GpuTexturePtr MakeFallback(rhi::Device* device, rhi::Format format, u32 layers, u8 r, u8 g, u8 b, u8 a,
                           const char* name)
{
    rhi::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.array_layers = layers;  // a texture of one layer is a plain 2D view, which the array binding refuses
    desc.format = format;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<byte> bytes;
    bytes.Resize(4 * layers);
    Vector<TextureLevelSpan> spans;
    for (u32 i = 0; i < layers; ++i)
    {
        bytes[i * 4 + 0] = static_cast<byte>(r);
        bytes[i * 4 + 1] = static_cast<byte>(g);
        bytes[i * 4 + 2] = static_cast<byte>(b);
        bytes[i * 4 + 3] = static_cast<byte>(a);
        TextureLevelSpan span;
        span.offset = i * 4;
        span.bytes = 4;
        span.layer = i;
        spans.Push(span);
    }
    GpuTexturePtr texture = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    if (!texture->CreateFromSharedLevels(device, desc, region,
                                         vse::Span<const TextureLevelSpan>(spans.Data(), spans.Size()), nullptr, name))
    {
        return GpuTexturePtr();
    }
    return texture;
}

}  // namespace

TerrainPass::TerrainPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame)
    : device_(device), materials_(materials), frame_(frame)
{
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr,
                  "TerrainPass: device, materials or frame is null");
    Build();
}

TerrainPass::~TerrainPass()
{
    registration_.Reset();
    Release();
}

void TerrainPass::Build()
{
    rhi::Device* device = device_;
    vertex_ = rhi::CreateShaderFromSet(*device, shaders::kTerrainVertShader, rhi::ShaderStage::Vertex);
    fragment_ = rhi::CreateShaderFromSet(*device, shaders::kTerrainFragShader, rhi::ShaderStage::Fragment);
    // StructuredBuffer<TileInstance> tiles (terrain.vert): read-only, with its element size, which Direct3D 12's
    // structured view needs.
    rhi::BindingDesc bindings[8] = {
        {0, rhi::BindingType::ReadOnlyStorageBuffer, rhi::ShaderStage::Vertex, 1, kTileInstanceBytes},
        {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Vertex, 1},
        {2, rhi::BindingType::Sampler, rhi::ShaderStage::Vertex, 1},
        {3, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Fragment, 1},
        {4, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
        {5, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
        {6, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1},
        {7, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1}};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 8);
    layout_ = device->CreateBindGroupLayout(ld);
    rhi::SamplerDesc sd;
    sd.address_u = sd.address_v = sd.address_w = rhi::AddressMode::ClampToEdge;
    sd.debug_name = "terrain height";
    sampler_ = device->CreateSampler(sd);
    rhi::SamplerDesc rs;
    rs.max_anisotropy = 4.0f;
    rs.debug_name = "terrain layers";
    repeat_sampler_ = device->CreateSampler(rs);
    rhi::SamplerDesc cs;
    cs.address_u = cs.address_v = cs.address_w = rhi::AddressMode::ClampToEdge;
    cs.debug_name = "terrain splat";
    clamp_sampler_ = device->CreateSampler(cs);
    rhi::BufferDesc pb;
    pb.size = kParamsRegionBytes * kMaxTerrainsPerFrame * FrameResources::kSlots;
    pb.usage = rhi::BufferUsage::Uniform;
    pb.location = rhi::MemoryLocation::CpuToGpu;
    pb.debug_name = "terrain material";
    params_buffer_ = device->CreateBuffer(pb);
    {
        // sRGB 188 is 0.5 linear and 230 is a roughness of 0.9: the grey the terrain had before it had a material
        fallback_layers_ = MakeFallback(device, rhi::Format::RGBA8_SRGB, 2, 188, 188, 188, 230,
                                        "terrain fallback layers");
        fallback_splat_ = MakeFallback(device, rhi::Format::RGBA8_UNorm, 1, 255, 0, 0, 0, "terrain fallback splat");
    }
    rhi::BufferDesc bd;
    bd.size = kTileRegionBytes * FrameResources::kSlots;
    bd.usage = rhi::BufferUsage::Storage;
    bd.location = rhi::MemoryLocation::CpuToGpu;
    bd.debug_name = "terrain tiles";
    tile_buffer_ = device->CreateBuffer(bd);
    if (!vertex_.IsValid() || !fragment_.IsValid() || !layout_.IsValid() || !sampler_.IsValid() ||
        !tile_buffer_.IsValid() || !params_buffer_.IsValid() || !repeat_sampler_.IsValid() ||
        !clamp_sampler_.IsValid() || !fallback_layers_ || !fallback_splat_)
    {
        RenderLog(LogVerbosity::Warn,
                  "TerrainPass: creating the shaders, layout, sampler or buffer failed; no terrain is drawn");
        Release();  // a pass that cannot draw holds nothing, so Live() (false) is true to what it holds
        return;
    }
    tile_mapped_ = static_cast<byte*>(device->MapBuffer(tile_buffer_));
    params_mapped_ = static_cast<byte*>(device->MapBuffer(params_buffer_));
    rhi::GraphicsPipelineDesc gp;
    gp.vertex_shader = vertex_;
    gp.fragment_shader = fragment_;
    gp.cull_mode = rhi::CullMode::None;
    const PipelineTarget target = GBufferPipelineTarget();
    for (u32 i = 0; i < target.color_count; ++i)
    {
        gp.color_targets[i].format = target.color_formats[i];
    }
    gp.color_target_count = target.color_count;
    gp.depth_format = target.depth_format;
    gp.depth_test = true;
    gp.depth_write = true;
    gp.depth_compare = rhi::CompareOp::Less;
    gp.bind_group_layouts[0] = materials_->FrameLayout();
    gp.bind_group_layouts[1] = layout_;
    gp.bind_group_layout_count = 2;
    gp.push_constant_bytes = sizeof(Push);
    gp.push_constant_stages = rhi::ShaderStage::Vertex;
    gp.debug_name = "terrain";
    pipeline_ = device->CreateGraphicsPipeline(gp);
    if (!pipeline_.IsValid())
    {
        RenderLog(LogVerbosity::Warn, "TerrainPass: creating the pipeline failed; no terrain is drawn");
        Release();
        return;
    }
}

void TerrainPass::Release()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (tile_buffer_.IsValid())
    {
        if (tile_mapped_ != nullptr)
        {
            device_->UnmapBuffer(tile_buffer_);
        }
        device_->DestroyBuffer(tile_buffer_);
    }
    if (params_buffer_.IsValid())
    {
        if (params_mapped_ != nullptr)
        {
            device_->UnmapBuffer(params_buffer_);
        }
        device_->DestroyBuffer(params_buffer_);
    }
    if (repeat_sampler_.IsValid())
    {
        device_->DestroySampler(repeat_sampler_);
    }
    if (clamp_sampler_.IsValid())
    {
        device_->DestroySampler(clamp_sampler_);
    }
    fallback_layers_.Reset();
    fallback_splat_.Reset();
    entries_.Clear();
    if (pipeline_.IsValid())
    {
        device_->DestroyPipeline(pipeline_);
    }
    if (sampler_.IsValid())
    {
        device_->DestroySampler(sampler_);
    }
    if (layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(layout_);
    }
    if (fragment_.IsValid())
    {
        device_->DestroyShader(fragment_);
    }
    if (vertex_.IsValid())
    {
        device_->DestroyShader(vertex_);
    }
    tile_buffer_ = {};
    tile_mapped_ = nullptr;
    params_buffer_ = {};
    params_mapped_ = nullptr;
    repeat_sampler_ = {};
    clamp_sampler_ = {};
    pipeline_ = {};
    sampler_ = {};
    layout_ = {};
    fragment_ = {};
    vertex_ = {};
    frame_group_ = {};
}

bool TerrainPass::Recreate(rhi::Device* device)
{
    device_ = device;
    Build();  // releases what it made when it fails
    return true;
}

void TerrainPass::Clear()
{
    entries_.Clear();
    tile_cursor_ = 0;
    tile_count_ = 0;
}

void TerrainPass::BeginFrame(u64 frame_index)
{
    Clear();
    slot_ = static_cast<u32>(frame_index % FrameResources::kSlots);
}

void TerrainPass::Prepare(const TerrainDraw& draw, Span<const TerrainTile> tiles, u64 frame_index)
{
    BeginFrame(frame_index);
    Add(draw, tiles);
}

bool TerrainPass::Add(const TerrainDraw& draw, Span<const TerrainTile> tiles)
{
    if (!pipeline_.IsValid() || tile_mapped_ == nullptr || !draw.height || !draw.height->Valid() ||
        draw.height->Desc().format != rhi::Format::R16_UNorm ||
        draw.height->Desc().dimension != rhi::TextureDimension::Tex2D || tiles.Size() == 0 || draw.samples_x < 2 ||
        draw.samples_z < 2 || !(draw.cell_size > 0.0f) || entries_.Size() >= kMaxTerrainsPerFrame)
    {
        return false;
    }
    if (tile_cursor_ >= kMaxTerrainDrawTiles)
    {
        RenderLog(LogVerbosity::Warn, "TerrainPass: more tiles than the frame holds; the farthest are not drawn");
        return false;
    }
    u32 count = static_cast<u32>(tiles.Size());
    if (count > kMaxTerrainDrawTiles - tile_cursor_)
    {
        RenderLog(LogVerbosity::Warn, "TerrainPass: more tiles than the frame holds; the farthest are not drawn");
        count = kMaxTerrainDrawTiles - tile_cursor_;
    }
    const char* invalid = TerrainMaterialInvalid(draw.material);
    if (invalid != nullptr)
    {
        RenderLog(LogVerbosity::Warn, "TerrainPass: the terrain material is invalid; the default material is drawn");
    }
    const TerrainMaterial fallback_material;
    const TerrainMaterial& mat = invalid == nullptr ? draw.material : fallback_material;
    Entry entry;
    entry.height = draw.height;
    entry.layers = mat.layer_array ? mat.layer_array : fallback_layers_;
    entry.splat = mat.splat ? mat.splat : fallback_splat_;
    if (!entry.layers->Valid() || !entry.splat->Valid())
    {
        return false;
    }
    entry.first_tile = tile_cursor_;
    entry.tile_count = count;
    byte* base = tile_mapped_ + kTileRegionBytes * slot_ + static_cast<usize>(tile_cursor_) * kTileInstanceBytes;
    for (u32 i = 0; i < count; ++i)
    {
        const f32 size = static_cast<f32>(kTerrainTileCells << tiles[i].level) * draw.cell_size;
        const f32 origin_x = static_cast<f32>(tiles[i].x * (kTerrainTileCells << tiles[i].level)) * draw.cell_size;
        const f32 origin_z = static_cast<f32>(tiles[i].z * (kTerrainTileCells << tiles[i].level)) * draw.cell_size;
        const f32 instance[8] = {origin_x, origin_z, size, static_cast<f32>(tiles[i].level), 0.0f, 0.0f, 0.0f, 0.0f};
        MemCopy(base + static_cast<usize>(i) * kTileInstanceBytes, instance, sizeof(instance));
    }
    device_->FlushMappedBuffer(tile_buffer_,
                               kTileRegionBytes * slot_ + static_cast<u64>(tile_cursor_) * kTileInstanceBytes,
                               static_cast<u64>(count) * kTileInstanceBytes);
    // the next terrain's tiles start at a multiple of 8 instances: a storage buffer range starts at an aligned offset
    tile_cursor_ += (count + 7u) & ~7u;
    tile_count_ += count;
    for (u32 i = 0; i < 16; ++i)
    {
        entry.push.world[i] = draw.world.col[i / 4][i % 4];
    }
    entry.push.grid[0] = draw.cell_size;
    entry.push.grid[1] = draw.range_min;
    entry.push.grid[2] = draw.range_max - draw.range_min;
    entry.push.grid[3] = static_cast<f32>(draw.height->Desc().mip_levels);
    entry.push.texture[0] = static_cast<f32>(draw.samples_x);
    entry.push.texture[1] = static_cast<f32>(draw.samples_z);
    entry.push.texture[2] = draw.select.first_range;
    entry.push.texture[3] = draw.select.morph_fraction;
    entry.push.spare[0] = entry.push.spare[1] = entry.push.spare[2] = entry.push.spare[3] = 0.0f;
    f32 block[32] = {};
    for (u32 i = 0; i < kMaxTerrainLayers; ++i)
    {
        block[i] = mat.layers[i].tiling;
        block[4 + i * 4 + 0] = mat.layers[i].tint.x;
        block[4 + i * 4 + 1] = mat.layers[i].tint.y;
        block[4 + i * 4 + 2] = mat.layers[i].tint.z;
        block[4 + i * 4 + 3] = mat.layers[i].roughness;
        block[20 + i] = mat.layers[i].metallic;
    }
    block[24] = static_cast<f32>(mat.slope_layer);
    block[25] = mat.slope_start;
    block[26] = mat.slope_end;
    block[27] = static_cast<f32>(mat.layer_count);
    block[28] = mat.fade_start;
    block[29] = mat.fade_end;
    block[30] = static_cast<f32>(entry.layers->Desc().mip_levels - 1);
    block[31] = static_cast<f32>(entry.layers->Desc().array_layers - 1);
    entry.params_offset = kParamsRegionBytes * (static_cast<u64>(slot_) * kMaxTerrainsPerFrame + entries_.Size());
    MemCopy(params_mapped_ + entry.params_offset, block, sizeof(block));
    device_->FlushMappedBuffer(params_buffer_, entry.params_offset, sizeof(block));
    entries_.Push(Move(entry));
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
    return true;
}

bool TerrainPass::AddToGraph(RenderGraph* graph, const GBufferTargets& targets)
{
    ENGINE_ASSERT(graph != nullptr, "TerrainPass::AddToGraph: graph is null");
    if (entries_.Size() == 0)
    {
        return false;
    }
    targets_ = targets;
    graph->AddPass("terrain", &TerrainPass::Record, this)
        .ReadWrite(targets.albedo, GraphUsage::ColorAttachment)
        .ReadWrite(targets.normal, GraphUsage::ColorAttachment)
        .ReadWrite(targets.material, GraphUsage::ColorAttachment)
        .ReadWrite(targets.emissive, GraphUsage::ColorAttachment)
        .ReadWrite(targets.depth, GraphUsage::DepthAttachment);
    return true;
}

void TerrainPass::Record(const PassExecuteContext& context, void* user)
{
    TerrainPass* self = static_cast<TerrainPass*>(user);
    if (!self->pipeline_.IsValid() || !self->frame_group_.IsValid())
    {
        RenderLog(LogVerbosity::Error, "TerrainPass: the pipeline or the frame group is missing");
        return;
    }
    const rhi::TextureDesc& desc = context.graph->TextureDescOf(self->targets_.albedo);
    rhi::RenderingDesc rd;
    rd.color_count = 4;
    rd.color[0].texture = context.graph->ResolvedTexture(self->targets_.albedo);
    rd.color[1].texture = context.graph->ResolvedTexture(self->targets_.normal);
    rd.color[2].texture = context.graph->ResolvedTexture(self->targets_.material);
    rd.color[3].texture = context.graph->ResolvedTexture(self->targets_.emissive);
    for (u32 i = 0; i < 4; ++i)
    {
        rd.color[i].load = rhi::LoadOp::Load;
        rd.color[i].store = rhi::StoreOp::Store;
    }
    rd.depth.texture = context.graph->ResolvedTexture(self->targets_.depth);
    rd.depth.load = rhi::LoadOp::Load;
    rd.depth.store = rhi::StoreOp::Store;
    rhi::CommandList* list = context.list;
    list->BeginRendering(rd);
    list->SetPipeline(self->pipeline_);
    list->SetViewport(
        rhi::Viewport{0.0f, 0.0f, static_cast<f32>(desc.width), static_cast<f32>(desc.height), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, desc.width, desc.height});
    list->SetBindGroup(kFrameSet, self->frame_group_);
    for (const Entry& entry : self->entries_)
    {
        if (!entry.height->Valid() || !entry.layers->Valid() || !entry.splat->Valid())
        {
            RenderLog(LogVerbosity::Error, "TerrainPass: a terrain texture is missing; the terrain is not drawn");
            continue;
        }
        rhi::BindGroupEntry e[8];
        e[0].binding = 0;
        e[0].buffer = self->tile_buffer_;
        e[0].offset = kTileRegionBytes * self->slot_ + static_cast<u64>(entry.first_tile) * kTileInstanceBytes;
        e[0].size = static_cast<u64>(entry.tile_count) * kTileInstanceBytes;
        e[1].binding = 1;
        e[1].texture = entry.height->Handle();  // read now: a reset may have remade it
        e[2].binding = 2;
        e[2].sampler = self->sampler_;
        e[3].binding = 3;
        e[3].buffer = self->params_buffer_;
        e[3].offset = entry.params_offset;
        e[3].size = kParamsRegionBytes;
        e[4].binding = 4;
        e[4].texture = entry.splat->Handle();
        e[5].binding = 5;
        e[5].texture = entry.layers->Handle();
        e[6].binding = 6;
        e[6].sampler = self->repeat_sampler_;
        e[7].binding = 7;
        e[7].sampler = self->clamp_sampler_;
        rhi::BindGroupDesc gd;
        gd.layout = self->layout_;
        gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 8);
        gd.transient = true;
        const rhi::BindGroupHandle group = self->device_->CreateBindGroup(gd);
        list->SetBindGroup(1, group);
        list->SetPushConstants(0, &entry.push, sizeof(Push));
        list->Draw(kTerrainTileCells * kTerrainTileCells * 6, entry.tile_count, 0, 0);
    }
    list->EndRendering();
}

}  // namespace vse::renderer
