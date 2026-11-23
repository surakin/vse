#include "renderer/terrain_pass.h"

#include "containers/shared_bytes.h"
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
    rhi::BindingDesc bindings[3] = {{0, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Vertex, 1},
                                    {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Vertex, 1},
                                    {2, rhi::BindingType::Sampler, rhi::ShaderStage::Vertex, 1}};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 3);
    layout_ = device->CreateBindGroupLayout(ld);
    rhi::SamplerDesc sd;
    sd.address_u = sd.address_v = sd.address_w = rhi::AddressMode::ClampToEdge;
    sd.debug_name = "terrain height";
    sampler_ = device->CreateSampler(sd);
    rhi::BufferDesc bd;
    bd.size = kTileRegionBytes * FrameResources::kSlots;
    bd.usage = rhi::BufferUsage::Storage;
    bd.location = rhi::MemoryLocation::CpuToGpu;
    bd.debug_name = "terrain tiles";
    tile_buffer_ = device->CreateBuffer(bd);
    if (!vertex_.IsValid() || !fragment_.IsValid() || !layout_.IsValid() || !sampler_.IsValid() ||
        !tile_buffer_.IsValid())
    {
        RenderLog(LogVerbosity::Warn,
                  "TerrainPass: creating the shaders, layout, sampler or buffer failed; no terrain is drawn");
        return;
    }
    tile_mapped_ = static_cast<byte*>(device->MapBuffer(tile_buffer_));
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
    pipeline_ = {};
    sampler_ = {};
    layout_ = {};
    fragment_ = {};
    vertex_ = {};
    frame_group_ = {};
    draw_ = false;
}

bool TerrainPass::Recreate(rhi::Device* device)
{
    device_ = device;
    Build();
    if (!pipeline_.IsValid())
    {
        Release();
    }
    return true;
}

void TerrainPass::Clear()
{
    draw_ = false;
    tile_count_ = 0;
    height_.Reset();
}

void TerrainPass::Prepare(const TerrainDraw& draw, Span<const TerrainTile> tiles, u64 frame_index)
{
    Clear();
    if (!pipeline_.IsValid() || tile_mapped_ == nullptr || !draw.height || !draw.height->Valid() ||
        draw.height->Desc().format != rhi::Format::R16_UNorm ||
        draw.height->Desc().dimension != rhi::TextureDimension::Tex2D || tiles.Size() == 0 || draw.samples_x < 2 ||
        draw.samples_z < 2 || !(draw.cell_size > 0.0f))
    {
        return;
    }
    u32 count = static_cast<u32>(tiles.Size());
    if (count > kMaxTerrainDrawTiles)
    {
        RenderLog(LogVerbosity::Warn, "TerrainPass: more tiles than the frame holds; the farthest are not drawn");
        count = kMaxTerrainDrawTiles;
    }
    slot_ = static_cast<u32>(frame_index % FrameResources::kSlots);
    byte* base = tile_mapped_ + kTileRegionBytes * slot_;
    for (u32 i = 0; i < count; ++i)
    {
        const f32 size = static_cast<f32>(kTerrainTileCells << tiles[i].level) * draw.cell_size;
        const f32 origin_x = static_cast<f32>(tiles[i].x * (kTerrainTileCells << tiles[i].level)) * draw.cell_size;
        const f32 origin_z = static_cast<f32>(tiles[i].z * (kTerrainTileCells << tiles[i].level)) * draw.cell_size;
        const f32 instance[8] = {origin_x, origin_z, size, static_cast<f32>(tiles[i].level), 0.0f, 0.0f, 0.0f, 0.0f};
        MemCopy(base + static_cast<usize>(i) * kTileInstanceBytes, instance, sizeof(instance));
    }
    device_->FlushMappedBuffer(tile_buffer_, kTileRegionBytes * slot_, static_cast<u64>(count) * kTileInstanceBytes);
    tile_count_ = count;
    height_ = draw.height;
    for (u32 i = 0; i < 16; ++i)
    {
        push_.world[i] = draw.world.col[i / 4][i % 4];
    }
    push_.grid[0] = draw.cell_size;
    push_.grid[1] = draw.range_min;
    push_.grid[2] = draw.range_max - draw.range_min;
    push_.grid[3] = static_cast<f32>(draw.height->Desc().mip_levels);
    push_.texture[0] = static_cast<f32>(draw.samples_x);
    push_.texture[1] = static_cast<f32>(draw.samples_z);
    push_.texture[2] = draw.select.first_range;
    push_.texture[3] = draw.select.morph_fraction;
    push_.spare[0] = push_.spare[1] = push_.spare[2] = push_.spare[3] = 0.0f;
    draw_ = true;
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
}

bool TerrainPass::AddToGraph(RenderGraph* graph, const GBufferTargets& targets)
{
    ENGINE_ASSERT(graph != nullptr, "TerrainPass::AddToGraph: graph is null");
    if (!draw_ || tile_count_ == 0)
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
    if (!self->pipeline_.IsValid() || !self->frame_group_.IsValid() || !self->height_ || !self->height_->Valid())
    {
        RenderLog(LogVerbosity::Error, "TerrainPass: the pipeline, the frame group or the height texture is missing");
        return;
    }
    const rhi::TextureDesc& desc = context.graph->TextureDescOf(self->targets_.albedo);
    rhi::BindGroupEntry e[3];
    e[0].binding = 0;
    e[0].buffer = self->tile_buffer_;
    e[0].offset = kTileRegionBytes * self->slot_;
    e[0].size = kTileRegionBytes;
    e[1].binding = 1;
    e[1].texture = self->height_->Handle();  // read now: a reset may have remade it
    e[2].binding = 2;
    e[2].sampler = self->sampler_;
    rhi::BindGroupDesc gd;
    gd.layout = self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 3);
    gd.transient = true;
    const rhi::BindGroupHandle group = self->device_->CreateBindGroup(gd);
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
    list->SetBindGroup(1, group);
    list->SetPushConstants(0, &self->push_, sizeof(Push));
    list->Draw(kTerrainTileCells * kTerrainTileCells * 6, self->tile_count_, 0, 0);
    list->EndRendering();
}

}  // namespace vse::renderer
