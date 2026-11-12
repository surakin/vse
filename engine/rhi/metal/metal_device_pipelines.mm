// MetalDevice: shaders, render and compute pipelines, depth-stencil states and the pipeline cache (Phase 26 Day 319).
//
// A shader is an MTLLibrary made from the minted bytes and the function its blob names, with a copy of the library's
// slot table (Day 318). A render pipeline maps each stage's table against its layouts (MapStageSlots); vertex buffers
// sit at the top of the buffer table (MetalVertexBufferSlot), clear of the shader's own buffers. What Metal sets on the
// encoder rather than in the pipeline state (cull mode, winding, fill mode, the primitive type, the depth-stencil
// state) is kept with the pipeline for the command list. Pipelines are looked up in the binary archive first; a miss is
// compiled and added.

#include "cstr/format.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

namespace
{

MTLVertexFormat VertexFormatOf(VertexFormat f)
{
    switch (f)
    {
    case VertexFormat::Float1: return MTLVertexFormatFloat;
    case VertexFormat::Float2: return MTLVertexFormatFloat2;
    case VertexFormat::Float3: return MTLVertexFormatFloat3;
    case VertexFormat::Float4: return MTLVertexFormatFloat4;
    case VertexFormat::UInt1: return MTLVertexFormatUInt;
    case VertexFormat::UInt2: return MTLVertexFormatUInt2;
    case VertexFormat::UInt3: return MTLVertexFormatUInt3;
    case VertexFormat::UInt4: return MTLVertexFormatUInt4;
    case VertexFormat::UNorm8x4: return MTLVertexFormatUChar4Normalized;
    case VertexFormat::Count: break;
    }
    return MTLVertexFormatInvalid;
}

MTLBlendFactor BlendFactorOf(BlendFactor f)
{
    switch (f)
    {
    case BlendFactor::Zero: return MTLBlendFactorZero;
    case BlendFactor::One: return MTLBlendFactorOne;
    case BlendFactor::SrcColor: return MTLBlendFactorSourceColor;
    case BlendFactor::OneMinusSrcColor: return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::DstColor: return MTLBlendFactorDestinationColor;
    case BlendFactor::OneMinusDstColor: return MTLBlendFactorOneMinusDestinationColor;
    case BlendFactor::SrcAlpha: return MTLBlendFactorSourceAlpha;
    case BlendFactor::OneMinusSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::DstAlpha: return MTLBlendFactorDestinationAlpha;
    case BlendFactor::OneMinusDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorOne;
}

MTLBlendOperation BlendOpOf(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add: return MTLBlendOperationAdd;
    case BlendOp::Subtract: return MTLBlendOperationSubtract;
    case BlendOp::ReverseSubtract: return MTLBlendOperationReverseSubtract;
    case BlendOp::Min: return MTLBlendOperationMin;
    case BlendOp::Max: return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

MTLCompareFunction CompareFunctionOf(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never: return MTLCompareFunctionNever;
    case CompareOp::Less: return MTLCompareFunctionLess;
    case CompareOp::Equal: return MTLCompareFunctionEqual;
    case CompareOp::LessEqual: return MTLCompareFunctionLessEqual;
    case CompareOp::Greater: return MTLCompareFunctionGreater;
    case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
    case CompareOp::GreaterEqual: return MTLCompareFunctionGreaterEqual;
    case CompareOp::Always: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

MTLColorWriteMask WriteMaskOf(ColorWriteMask m)
{
    MTLColorWriteMask out = MTLColorWriteMaskNone;
    if (Has(m, ColorWriteMask::R))
    {
        out |= MTLColorWriteMaskRed;
    }
    if (Has(m, ColorWriteMask::G))
    {
        out |= MTLColorWriteMaskGreen;
    }
    if (Has(m, ColorWriteMask::B))
    {
        out |= MTLColorWriteMaskBlue;
    }
    if (Has(m, ColorWriteMask::A))
    {
        out |= MTLColorWriteMaskAlpha;
    }
    return out;
}

void PrimitiveOf(PrimitiveTopology t, MTLPrimitiveType* type, MTLPrimitiveTopologyClass* cls)
{
    switch (t)
    {
    case PrimitiveTopology::TriangleList:
        *type = MTLPrimitiveTypeTriangle;
        *cls = MTLPrimitiveTopologyClassTriangle;
        return;
    case PrimitiveTopology::TriangleStrip:
        *type = MTLPrimitiveTypeTriangleStrip;
        *cls = MTLPrimitiveTopologyClassTriangle;
        return;
    case PrimitiveTopology::LineList:
        *type = MTLPrimitiveTypeLine;
        *cls = MTLPrimitiveTopologyClassLine;
        return;
    case PrimitiveTopology::LineStrip:
        *type = MTLPrimitiveTypeLineStrip;
        *cls = MTLPrimitiveTopologyClassLine;
        return;
    case PrimitiveTopology::PointList:
        *type = MTLPrimitiveTypePoint;
        *cls = MTLPrimitiveTopologyClassPoint;
        return;
    }
}

const char* ErrorText(NSError* error)
{
    return error != nil && error.localizedDescription != nil ? error.localizedDescription.UTF8String
                                                             : "no reason given";
}

// The highest buffer slot a stage's own resources and push constants use, or -1.
int HighestBufferSlot(const MetalStageSlots& slots)
{
    int high = slots.push_constant_slot != kNoShaderSlot ? static_cast<int>(slots.push_constant_slot) : -1;
    for (usize i = 0; i < slots.refs.Size(); ++i)
    {
        const MetalSlotRef& r = slots.refs[i];
        const bool buffer = r.type == BindingType::UniformBuffer || r.type == BindingType::StorageBuffer ||
                            r.type == BindingType::ReadOnlyStorageBuffer;
        const int top = static_cast<int>(r.slot + r.count) - 1;
        if (buffer && top > high)
        {
            high = top;
        }
    }
    return high;
}

void CopySlots(const MetalStageSlots& from, MetalStageSlots* to)
{
    to->refs.Clear();
    for (usize i = 0; i < from.refs.Size(); ++i)
    {
        to->refs.Push(from.refs[i]);
    }
    to->push_constant_slot = from.push_constant_slot;
}

}  // namespace

MTLVertexDescriptor* MakeMetalVertexDescriptor(const GraphicsPipelineDesc& desc)
{
    MTLVertexDescriptor* v = [MTLVertexDescriptor vertexDescriptor];
    for (usize i = 0; i < desc.vertex_attributes.Size(); ++i)
    {
        const VertexAttributeDesc& a = desc.vertex_attributes[i];
        v.attributes[a.location].format = VertexFormatOf(a.format);
        v.attributes[a.location].offset = a.offset;
        v.attributes[a.location].bufferIndex = MetalVertexBufferSlot(a.binding);
    }
    for (usize b = 0; b < desc.vertex_bindings.Size(); ++b)
    {
        const VertexBindingDesc& vb = desc.vertex_bindings[b];
        MTLVertexBufferLayoutDescriptor* layout = v.layouts[MetalVertexBufferSlot(static_cast<u32>(b))];
        layout.stride = vb.stride;
        layout.stepFunction = vb.per_instance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        layout.stepRate = 1;
    }
    return v;
}

// ---- shaders ----

ShaderHandle MetalDevice::CreateShader(const ShaderDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateShader: %s", why);
        return {};
    }
    if (desc.format != ShaderFormat::MetalLib)
    {
        RhiLog(LogVerbosity::Error, "CreateShader: '%s' is not a Metal library; the Metal device takes MetalLib only",
               desc.debug_name != nullptr ? desc.debug_name : "(unnamed)");
        return {};
    }
    ShaderRes res;
    res.stage = desc.stage;
    @autoreleasepool
    {
        dispatch_data_t data = dispatch_data_create(desc.bytes, desc.size, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NSError* error = nil;
        res.library = [device_ newLibraryWithData:data error:&error];
        if (res.library == nil)
        {
            RhiLog(LogVerbosity::Error, "CreateShader: Metal refused the library '%s': %s",
                   desc.debug_name != nullptr ? desc.debug_name : "(unnamed)", ErrorText(error));
            return {};
        }
        res.function = [res.library newFunctionWithName:[NSString stringWithUTF8String:desc.entry]];
        const MTLFunctionType want = desc.stage == ShaderStage::Vertex     ? MTLFunctionTypeVertex
                                     : desc.stage == ShaderStage::Fragment ? MTLFunctionTypeFragment
                                                                           : MTLFunctionTypeKernel;
        if (res.function == nil || res.function.functionType != want)
        {
            RhiLog(LogVerbosity::Error, "CreateShader: the library '%s' has no %s function named '%s'",
                   desc.debug_name != nullptr ? desc.debug_name : "(unnamed)",
                   want == MTLFunctionTypeVertex     ? "vertex"
                   : want == MTLFunctionTypeFragment ? "fragment"
                                                     : "kernel",
                   desc.entry);
            return {};
        }
        if (desc.debug_name != nullptr)
        {
            res.library.label = [NSString stringWithUTF8String:desc.debug_name];
        }
    }
    if (desc.slots != nullptr)
    {
        res.has_slots = true;
        for (u32 i = 0; i < desc.slots->count; ++i)
        {
            res.slots.Push(desc.slots->slots[i]);
        }
        res.push_constant_slot = desc.slots->push_constant_slot;
        for (int i = 0; i < 3; ++i)
        {
            res.threadgroup[i] = desc.slots->threadgroup_size[i];
        }
    }
    LockGuard<Mutex> lock(resource_mutex_);
    const ShaderHandle handle = shaders_.Allocate(static_cast<ShaderRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateShader: out of shader handles");
    }
    return handle;
}

void MetalDevice::DestroyShader(ShaderHandle shader)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(shaders_.Get(shader) != nullptr, "DestroyShader: handle is invalid or already destroyed");
    if (shaders_.Get(shader) == nullptr)
    {
        return;
    }
    // A pipeline keeps its own compiled state: the shader can go at once, as on the other backends.
    shaders_.Retire(shader, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

bool MetalDevice::ShaderParts(ShaderHandle shader, ShaderStage stage, id<MTLFunction>* function,
                              Vector<ShaderSlot>* slots, ShaderSlotTable* table, bool* has_table) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const ShaderRes* s = shaders_.Get(shader);
    if (s == nullptr || s->stage != stage)
    {
        return false;
    }
    *function = s->function;
    slots->Clear();
    for (usize i = 0; i < s->slots.Size(); ++i)
    {
        slots->Push(s->slots[i]);
    }
    *table = ShaderSlotTable{};
    table->slots = slots->Data();
    table->count = static_cast<u32>(slots->Size());
    table->push_constant_slot = s->push_constant_slot;
    for (int i = 0; i < 3; ++i)
    {
        table->threadgroup_size[i] = s->threadgroup[i];
    }
    *has_table = s->has_slots;
    return true;
}

// ---- depth-stencil states ----

id<MTLDepthStencilState> MetalDevice::DepthStencilState(bool test, bool write, CompareOp compare)
{
    const u32 key = (test ? 1u : 0u) | (write ? 2u : 0u) | (static_cast<u32>(compare) << 2);
    LockGuard<Mutex> lock(resource_mutex_);
    for (usize i = 0; i < depth_states_.Size(); ++i)
    {
        if (depth_states_[i].key == key)
        {
            return depth_states_[i].state;
        }
    }
    DepthStateEntry entry;
    entry.key = key;
    @autoreleasepool
    {
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        // Without the test, depth passes always; Metal writes depth only when depthWriteEnabled.
        d.depthCompareFunction = test ? CompareFunctionOf(compare) : MTLCompareFunctionAlways;
        d.depthWriteEnabled = test && write;
        entry.state = [device_ newDepthStencilStateWithDescriptor:d];
    }
    depth_states_.Push(entry);
    return entry.state;
}

// ---- pipelines ----

PipelineHandle MetalDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: %s", why);
        return {};
    }
    const char* name = desc.debug_name != nullptr ? desc.debug_name : "(unnamed)";
    id<MTLFunction> vertex_fn = nil;
    id<MTLFunction> fragment_fn = nil;
    Vector<ShaderSlot> vertex_slot_list;
    Vector<ShaderSlot> fragment_slot_list;
    ShaderSlotTable vertex_table;
    ShaderSlotTable fragment_table;
    bool vertex_has = false;
    bool fragment_has = false;
    if (!ShaderParts(desc.vertex_shader, ShaderStage::Vertex, &vertex_fn, &vertex_slot_list, &vertex_table,
                     &vertex_has))
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline '%s': the vertex shader is invalid or not a vertex shader",
               name);
        return {};
    }
    const bool has_fragment = desc.fragment_shader.IsValid();
    if (has_fragment && !ShaderParts(desc.fragment_shader, ShaderStage::Fragment, &fragment_fn, &fragment_slot_list,
                                     &fragment_table, &fragment_has))
    {
        RhiLog(LogVerbosity::Error,
               "CreateGraphicsPipeline '%s': the fragment shader is invalid or not a fragment shader", name);
        return {};
    }
    PipelineRes res;
    MetalPipelineInfo& info = res.info;
    char error[256] = {};
    if (!MapStageSlots(*this, ShaderStage::Vertex, vertex_has ? &vertex_table : nullptr, desc.bind_group_layouts,
                       desc.bind_group_layout_count, &info.vertex, error, sizeof(error)) ||
        (has_fragment &&
         !MapStageSlots(*this, ShaderStage::Fragment, fragment_has ? &fragment_table : nullptr, desc.bind_group_layouts,
                        desc.bind_group_layout_count, &info.fragment, error, sizeof(error))))
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline '%s': %s", name, error);
        return {};
    }
    info.vertex_binding_count = static_cast<u32>(desc.vertex_bindings.Size());
    for (usize a = 0; a < desc.vertex_attributes.Size(); ++a)
    {
        // Metal has 31 attribute slots; an attribute's binding must name one of the pipeline's vertex buffers.
        const VertexAttributeDesc& attribute = desc.vertex_attributes[a];
        if (attribute.location >= kMetalBufferSlots || attribute.binding >= info.vertex_binding_count ||
            VertexFormatOf(attribute.format) == MTLVertexFormatInvalid)
        {
            RhiLog(LogVerbosity::Error,
                   "CreateGraphicsPipeline '%s': vertex attribute %zu (location %u, binding %u) is outside Metal's "
                   "31 attributes or the pipeline's bindings, or has no format",
                   name, a, attribute.location, attribute.binding);
            return {};
        }
    }
    if (info.vertex_binding_count != 0 &&
        HighestBufferSlot(info.vertex) >= static_cast<int>(MetalVertexBufferSlot(info.vertex_binding_count - 1)))
    {
        RhiLog(LogVerbosity::Error,
               "CreateGraphicsPipeline '%s': the vertex shader's buffers reach slot %d, where its %u vertex buffers "
               "start (the top of Metal's buffer table)",
               name, HighestBufferSlot(info.vertex), info.vertex_binding_count);
        return {};
    }
    MTLPrimitiveTopologyClass topology_class = MTLPrimitiveTopologyClassTriangle;
    PrimitiveOf(desc.topology, &info.primitive, &topology_class);
    info.cull = desc.cull_mode == CullMode::None    ? MTLCullModeNone
                : desc.cull_mode == CullMode::Front ? MTLCullModeFront
                                                    : MTLCullModeBack;
    // The RHI's winding is in framebuffer space, which the projection's Y flip keeps the same as Metal's.
    info.winding = desc.front_face == FrontFace::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise;
    info.fill = desc.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    info.depth = DepthStencilState(desc.depth_test, desc.depth_write, desc.depth_compare);
    @autoreleasepool
    {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.label = [NSString stringWithUTF8String:name];
        d.vertexFunction = vertex_fn;
        d.fragmentFunction = fragment_fn;
        d.vertexDescriptor = MakeMetalVertexDescriptor(desc);
        d.inputPrimitiveTopology = topology_class;
        d.rasterSampleCount = desc.samples;
        for (u32 c = 0; c < desc.color_target_count; ++c)
        {
            const ColorTargetDesc& t = desc.color_targets[c];
            MTLRenderPipelineColorAttachmentDescriptor* a = d.colorAttachments[c];
            a.pixelFormat = ToMtlPixelFormat(t.format, device_);
            a.writeMask = WriteMaskOf(t.write_mask);
            a.blendingEnabled = t.blend.enable;
            a.sourceRGBBlendFactor = BlendFactorOf(t.blend.src_color);
            a.destinationRGBBlendFactor = BlendFactorOf(t.blend.dst_color);
            a.rgbBlendOperation = BlendOpOf(t.blend.color_op);
            a.sourceAlphaBlendFactor = BlendFactorOf(t.blend.src_alpha);
            a.destinationAlphaBlendFactor = BlendFactorOf(t.blend.dst_alpha);
            a.alphaBlendOperation = BlendOpOf(t.blend.alpha_op);
        }
        if (desc.depth_format != Format::Undefined)
        {
            const MTLPixelFormat depth = ToMtlPixelFormat(desc.depth_format, device_);
            d.depthAttachmentPixelFormat = depth;
            if (FormatInfo(desc.depth_format).has_stencil)
            {
                d.stencilAttachmentPixelFormat = depth;
            }
        }
        NSError* error_object = nil;
        bool hit = false;
        info.render = pipeline_cache_.NewRender(d, &hit, &error_object);
        if (info.render == nil)
        {
            RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline '%s': Metal refused it: %s", name,
                   ErrorText(error_object));
            return {};
        }
        (hit ? cache_hits_ : cache_misses_).FetchAdd(1, MemoryOrder::Relaxed);
    }
    LockGuard<Mutex> lock(resource_mutex_);
    const PipelineHandle handle = pipelines_.Allocate(static_cast<PipelineRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: out of pipeline handles");
    }
    return handle;
}

PipelineHandle MetalDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: %s", why);
        return {};
    }
    const char* name = desc.debug_name != nullptr ? desc.debug_name : "(unnamed)";
    id<MTLFunction> function = nil;
    Vector<ShaderSlot> slot_list;
    ShaderSlotTable table;
    bool has_table = false;
    if (!ShaderParts(desc.shader, ShaderStage::Compute, &function, &slot_list, &table, &has_table))
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline '%s': the shader is invalid or not a compute shader", name);
        return {};
    }
    PipelineRes res;
    MetalPipelineInfo& info = res.info;
    info.compute = true;
    char error[256] = {};
    if (!MapStageSlots(*this, ShaderStage::Compute, has_table ? &table : nullptr, desc.bind_group_layouts,
                       desc.bind_group_layout_count, &info.kernel, error, sizeof(error)))
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline '%s': %s", name, error);
        return {};
    }
    // A Metal dispatch passes the threadgroup size the shader declares ([numthreads], minted with its slots).
    info.threadgroup = MTLSizeMake(table.threadgroup_size[0], table.threadgroup_size[1], table.threadgroup_size[2]);
    if (!has_table || info.threadgroup.width == 0 || info.threadgroup.height == 0 || info.threadgroup.depth == 0)
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline '%s': the shader records no threadgroup size", name);
        return {};
    }
    // The device's limits first, so a kernel that can never run is refused before it is compiled or cached; the
    // pipeline's own limit (lower when it needs many registers) is checked once it exists.
    const MTLSize device_max = device_.maxThreadsPerThreadgroup;
    if (info.threadgroup.width > device_max.width || info.threadgroup.height > device_max.height ||
        info.threadgroup.depth > device_max.depth ||
        info.threadgroup.width * info.threadgroup.height * info.threadgroup.depth > 1024)
    {
        RhiLog(LogVerbosity::Error,
               "CreateComputePipeline '%s': the threadgroup %lux%lux%lu is more than this device takes", name,
               static_cast<unsigned long>(info.threadgroup.width), static_cast<unsigned long>(info.threadgroup.height),
               static_cast<unsigned long>(info.threadgroup.depth));
        return {};
    }
    @autoreleasepool
    {
        MTLComputePipelineDescriptor* d = [[MTLComputePipelineDescriptor alloc] init];
        d.label = [NSString stringWithUTF8String:name];
        d.computeFunction = function;
        NSError* error_object = nil;
        bool hit = false;
        info.compute_state = pipeline_cache_.NewCompute(d, &hit, &error_object);
        if (info.compute_state == nil)
        {
            RhiLog(LogVerbosity::Error, "CreateComputePipeline '%s': Metal refused it: %s", name,
                   ErrorText(error_object));
            return {};
        }
        (hit ? cache_hits_ : cache_misses_).FetchAdd(1, MemoryOrder::Relaxed);
        const NSUInteger threads = info.threadgroup.width * info.threadgroup.height * info.threadgroup.depth;
        if (threads > info.compute_state.maxTotalThreadsPerThreadgroup)
        {
            RhiLog(LogVerbosity::Error,
                   "CreateComputePipeline '%s': the threadgroup of %lu threads is more than this pipeline's %lu", name,
                   static_cast<unsigned long>(threads),
                   static_cast<unsigned long>(info.compute_state.maxTotalThreadsPerThreadgroup));
            return {};
        }
    }
    LockGuard<Mutex> lock(resource_mutex_);
    const PipelineHandle handle = pipelines_.Allocate(static_cast<PipelineRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: out of pipeline handles");
    }
    return handle;
}

void MetalDevice::DestroyPipeline(PipelineHandle pipeline)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(pipelines_.Get(pipeline) != nullptr, "DestroyPipeline: handle is invalid or already destroyed");
    if (pipelines_.Get(pipeline) == nullptr)
    {
        return;
    }
    pipelines_.Retire(pipeline, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

bool MetalDevice::LookupPipeline(PipelineHandle pipeline, MetalPipelineInfo* out) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const PipelineRes* p = pipelines_.Get(pipeline);
    if (p == nullptr)
    {
        return false;
    }
    const MetalPipelineInfo& from = p->info;
    out->compute = from.compute;
    out->render = from.render;
    out->compute_state = from.compute_state;
    out->depth = from.depth;
    out->primitive = from.primitive;
    out->cull = from.cull;
    out->winding = from.winding;
    out->fill = from.fill;
    out->vertex_binding_count = from.vertex_binding_count;
    out->threadgroup = from.threadgroup;
    CopySlots(from.vertex, &out->vertex);
    CopySlots(from.fragment, &out->fragment);
    CopySlots(from.kernel, &out->kernel);
    return true;
}

void MetalDevice::Quiesce()
{
    WaitIdle();
    pipeline_cache_.Save();
}

}  // namespace engine_placeholder::rhi::metal
