// Dx12Device: shaders, graphics and compute pipelines, and the on-disk pipeline cache (Phase 13 Day 167).
//
// A shader keeps a copy of its DXIL container; a pipeline is made from the shaders, the root signature of its layouts
// (dx12_device_bindings.cpp) and its fixed state. The cache is an `ID3D12PipelineLibrary` serialized to one file per
// adapter and driver under the pipeline cache directory (rhi/pipeline_cache_file.h): each pipeline is stored under a
// name hashed from everything that makes it, so a later run loads it instead of compiling. The file is checked (header,
// identity, size, hash) before D3D12 reads any of it, and anything D3D12 still refuses is discarded with a log line.

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/dx12/dx12_pipeline_cache.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/pipeline_cache_file.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12
{

namespace
{

const char* NameOr(const char* name)
{
    return name != nullptr ? name : "";
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(PrimitiveTopology t)
{
    switch (t)
    {
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

D3D_PRIMITIVE_TOPOLOGY Topology(PrimitiveTopology t)
{
    switch (t)
    {
    case PrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

bool IsStrip(PrimitiveTopology t)
{
    return t == PrimitiveTopology::TriangleStrip || t == PrimitiveTopology::LineStrip;
}

D3D12_CULL_MODE Cull(CullMode m)
{
    switch (m)
    {
    case CullMode::None: return D3D12_CULL_MODE_NONE;
    case CullMode::Front: return D3D12_CULL_MODE_FRONT;
    case CullMode::Back: return D3D12_CULL_MODE_BACK;
    }
    return D3D12_CULL_MODE_BACK;
}

D3D12_COMPARISON_FUNC Compare(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
    case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS;
}

// D3D12 refuses the color factors in an alpha blend; on the alpha channel each means its alpha counterpart, as in
// Vulkan.
D3D12_BLEND Blend(BlendFactor f, bool alpha)
{
    switch (f)
    {
    case BlendFactor::Zero: return D3D12_BLEND_ZERO;
    case BlendFactor::One: return D3D12_BLEND_ONE;
    case BlendFactor::SrcColor: return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::DstColor: return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDstColor: return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    }
    return D3D12_BLEND_ONE;
}

D3D12_BLEND_OP BlendOperation(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add: return D3D12_BLEND_OP_ADD;
    case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min: return D3D12_BLEND_OP_MIN;
    case BlendOp::Max: return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

template <typename T> u64 HashValue(const T& value, u64 seed)
{
    return Fnv1a(&value, sizeof(value), seed);
}

// The cache key of a graphics pipeline: everything in its description, with the shaders and the root signature by
// content. `desc` was zero-filled before it was set, so the padding inside its plain parts is zero too.
u64 GraphicsKey(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, u64 vs, u64 ps, u64 root)
{
    u64 h = HashValue(u32{1}, 0xcbf29ce484222325ull);  // the key's own version
    h = HashValue(vs, h);
    h = HashValue(ps, h);
    h = HashValue(root, h);
    h = HashValue(d.BlendState, h);
    h = HashValue(d.SampleMask, h);
    h = HashValue(d.RasterizerState, h);
    h = HashValue(d.DepthStencilState, h);
    for (u32 i = 0; i < d.InputLayout.NumElements; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC& e = d.InputLayout.pInputElementDescs[i];
        const u32 parts[6] = {e.SemanticIndex,     static_cast<u32>(e.Format),         e.InputSlot,
                              e.AlignedByteOffset, static_cast<u32>(e.InputSlotClass), e.InstanceDataStepRate};
        h = Fnv1a(e.SemanticName, CStrLength(e.SemanticName), h);
        h = HashValue(parts, h);
    }
    h = HashValue(d.IBStripCutValue, h);
    h = HashValue(d.PrimitiveTopologyType, h);
    h = HashValue(d.NumRenderTargets, h);
    h = HashValue(d.RTVFormats, h);
    h = HashValue(d.DSVFormat, h);
    h = HashValue(d.SampleDesc, h);
    return HashValue(d.Flags, h);
}

}  // namespace

// ---- the pipeline cache ----

void Dx12Device::InitializePipelineCache(const DeviceCreateInfo& info)
{
    shaders_.SeedGeneration(GenerationSeed(epoch_));
    pipelines_.SeedGeneration(GenerationSeed(epoch_));
    LockGuard<Mutex> lock(library_mutex_);
    cache_path_[0] = '\0';
    if (!info.pipeline_cache)
    {
        return;
    }
    char dir[400];
    if (!ResolvePipelineCacheDir(info, dir, sizeof(dir)) ||
        !BuildPipelineLibraryPath(dir, adapter_.id, adapter_.driver, cache_path_, sizeof(cache_path_)))
    {
        cache_path_[0] = '\0';
        RhiLog(LogVerbosity::Info, "D3D12: no directory for the pipeline cache; running without it");
        return;
    }
    ComRef<ID3D12Device1> device1;
    if (FAILED(device_->QueryInterface(__uuidof(ID3D12Device1), device1.PutVoid())))
    {
        RhiLog(LogVerbosity::Info, "D3D12: no ID3D12Device1, so no pipeline library; running without a pipeline cache");
        cache_path_[0] = '\0';
        return;
    }
    const u64 identity = PipelineLibraryIdentity(adapter_.id, adapter_.driver);
    PipelineLibraryParts parts;
    if (ReadPipelineCacheFile(cache_path_, &library_file_))
    {
        if (const char* why = CheckPipelineLibraryFile(library_file_.Data(), library_file_.Size(), identity, &parts))
        {
            RhiLog(LogVerbosity::Warn, "D3D12: ignoring %s: %s", cache_path_, why);
            cache_stats_.rejected = true;
            library_file_.Reset();
        }
    }
    HRESULT hr = E_FAIL;
    if (library_file_.Size() != 0)
    {
        hr = device1->CreatePipelineLibrary(parts.library, parts.library_size, __uuidof(ID3D12PipelineLibrary),
                                            library_.PutVoid());
        if (FAILED(hr))
        {
            // D3D12_ERROR_DRIVER_VERSION_MISMATCH, D3D12_ERROR_ADAPTER_NOT_FOUND or a library it cannot read: start
            // empty.
            RhiLog(LogVerbosity::Warn, "D3D12: the runtime refused %s (%s); starting with an empty pipeline cache",
                   cache_path_, HresultName(hr));
            cache_stats_.rejected = true;
            library_file_.Reset();
        }
        else
        {
            for (u32 i = 0; i < parts.key_count; ++i)
            {
                InsertSortedKey(&library_keys_, PipelineLibraryKey(parts, i));
            }
            cache_stats_.loaded_bytes = library_file_.Size();
            RhiLog(LogVerbosity::Info, "D3D12: loaded %llu bytes of pipeline cache (%u pipelines) from %s",
                   static_cast<unsigned long long>(library_file_.Size()), parts.key_count, cache_path_);
        }
    }
    if (!library_)
    {
        hr = device1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), library_.PutVoid());
        if (FAILED(hr))
        {
            // GPU-based validation, for one, does not allow pipeline libraries.
            RhiLog(LogVerbosity::Info, "D3D12: cannot create a pipeline library (%s); running without a pipeline cache",
                   HresultName(hr));
            cache_path_[0] = '\0';
            return;
        }
    }
    library_->SetName(L"engine pipeline library");
    cache_stats_.enabled = true;
}

Dx12Device::PipelineCacheStats Dx12Device::PipelineCache() const
{
    LockGuard<Mutex> lock(library_mutex_);
    return cache_stats_;
}

bool Dx12Device::SavePipelineCache()
{
    LockGuard<Mutex> lock(library_mutex_);
    if (!library_ || cache_path_[0] == '\0' || !library_dirty_)
    {
        return false;
    }
    const usize library_bytes = library_->GetSerializedSize();
    const usize key_bytes = library_keys_.Size() * 8;
    Vector<byte> file;
    if (library_bytes == 0 || !file.TryResizeUninitialized(kPipelineLibraryHeaderBytes + key_bytes + library_bytes))
    {
        return false;
    }
    byte* keys = file.Data() + kPipelineLibraryHeaderBytes;
    for (usize i = 0; i < library_keys_.Size(); ++i)
    {
        for (u32 b = 0; b < 8; ++b)
        {
            keys[i * 8 + b] = static_cast<byte>(library_keys_[i] >> (8 * b));
        }
    }
    if (!Check(library_->Serialize(keys + key_bytes, library_bytes), "ID3D12PipelineLibrary::Serialize"))
    {
        return false;
    }
    WritePipelineLibraryHeader(file.Data(), PipelineLibraryIdentity(adapter_.id, adapter_.driver),
                               static_cast<u32>(library_keys_.Size()), keys, key_bytes + library_bytes);
    if (!WritePipelineCacheFile(cache_path_, file.Data(), file.Size()))
    {
        RhiLog(LogVerbosity::Warn, "D3D12: cannot save the pipeline cache to %s", cache_path_);
        return false;
    }
    cache_stats_.saved_bytes = file.Size();
    library_dirty_ = false;
    return true;
}

void Dx12Device::StoreInLibrary(u64 key, ID3D12PipelineState* state)
{
    LockGuard<Mutex> lock(library_mutex_);
    if (!library_)
    {
        return;
    }
    wchar_t name[18];
    PipelineLibraryName(key, name);
    // E_INVALIDARG: another thread stored the same pipeline meanwhile. Anything else only costs the cache an entry.
    if (SUCCEEDED(library_->StorePipeline(name, state)))
    {
        InsertSortedKey(&library_keys_, key);
        library_dirty_ = true;
    }
}

ComRef<ID3D12PipelineState> Dx12Device::CachedGraphicsState(u64 key, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                            const char* name)
{
    ComRef<ID3D12PipelineState> state;
    {
        LockGuard<Mutex> lock(library_mutex_);
        if (library_ && HasSortedKey(library_keys_, key))
        {
            wchar_t lib_name[18];
            PipelineLibraryName(key, lib_name);
            if (SUCCEEDED(
                    library_->LoadGraphicsPipeline(lib_name, &desc, __uuidof(ID3D12PipelineState), state.PutVoid())))
            {
                ++cache_stats_.hits;
                return state;
            }
        }
        if (library_)
        {
            ++cache_stats_.misses;
        }
    }
    const HRESULT hr = device_->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), state.PutVoid());
    if (FAILED(hr))
    {
        if (IsDeviceLostResult(hr))
        {
            NoteRemoved("CreateGraphicsPipelineState");
        }
        RhiLog(LogVerbosity::Error,
               "D3D12: cannot create the graphics pipeline '%s' (%s; the debug layer's message, if on, is above)",
               NameOr(name), HresultName(hr));
        return {};
    }
    StoreInLibrary(key, state.Get());
    return state;
}

ComRef<ID3D12PipelineState> Dx12Device::CachedComputeState(u64 key, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,
                                                           const char* name)
{
    ComRef<ID3D12PipelineState> state;
    {
        LockGuard<Mutex> lock(library_mutex_);
        if (library_ && HasSortedKey(library_keys_, key))
        {
            wchar_t lib_name[18];
            PipelineLibraryName(key, lib_name);
            if (SUCCEEDED(
                    library_->LoadComputePipeline(lib_name, &desc, __uuidof(ID3D12PipelineState), state.PutVoid())))
            {
                ++cache_stats_.hits;
                return state;
            }
        }
        if (library_)
        {
            ++cache_stats_.misses;
        }
    }
    const HRESULT hr = device_->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), state.PutVoid());
    if (FAILED(hr))
    {
        if (IsDeviceLostResult(hr))
        {
            NoteRemoved("CreateComputePipelineState");
        }
        RhiLog(LogVerbosity::Error,
               "D3D12: cannot create the compute pipeline '%s' (%s; the debug layer's message, if on, is above)",
               NameOr(name), HresultName(hr));
        return {};
    }
    StoreInLibrary(key, state.Get());
    return state;
}

void Dx12Device::ShutdownPipelines()
{
    pipelines_.ForEachLive(
        [](PipelineHandle, PipelineRes& p)
        {
            p.state.Reset();
            p.state_cut16.Reset();
        });
    SavePipelineCache();
    LockGuard<Mutex> lock(library_mutex_);
    library_.Reset();
    library_file_.Reset();
    library_keys_.Reset();
}

// ---- shaders ----

ShaderHandle Dx12Device::CreateShader(const ShaderDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateShader: %s", why);
        return {};
    }
    if (desc.format != ShaderFormat::Dxil)
    {
        RhiLog(
            LogVerbosity::Error,
            "CreateShader: '%s' is %s, but a Direct3D 12 device runs DXIL (pick the blob with ShaderBlobSet::For(Caps().shader_format))",
            NameOr(desc.debug_name), desc.format == ShaderFormat::Spirv ? "SPIR-V" : "MSL");
        return {};
    }
    ShaderRes res;
    res.code = MakeShared<ShaderCode>();
    if (!res.code || !res.code->bytes.TryResizeUninitialized(desc.size))
    {
        RhiLog(LogVerbosity::Error, "CreateShader: out of memory for '%s' (%zu bytes)", NameOr(desc.debug_name),
               desc.size);
        return {};
    }
    MemCopy(res.code->bytes.Data(), desc.bytes, desc.size);
    res.stage = desc.stage;
    res.hash = Fnv1a(desc.bytes, desc.size);
    LockGuard<Mutex> lock(resource_mutex_);
    const ShaderHandle handle = shaders_.Allocate(static_cast<ShaderRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateShader: out of shader handles");
    }
    return handle;
}

void Dx12Device::DestroyShader(ShaderHandle shader)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(shaders_.Get(shader) != nullptr, "DestroyShader: handle is invalid or already destroyed");
    if (shaders_.Get(shader) == nullptr)
    {
        return;
    }
    // The GPU never reads a shader: its bytes go at once, or when a pipeline creation still holding them finishes.
    shaders_.Free(shader);
}

// ---- pipelines ----

PipelineHandle Dx12Device::AddPipeline(PipelineRes&& res, const char* what)
{
    LockGuard<Mutex> lock(resource_mutex_);
    const PipelineHandle handle = pipelines_.Allocate(static_cast<PipelineRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "%s: out of pipeline handles", what);
    }
    return handle;
}

PipelineHandle Dx12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: %s", why);
        return {};
    }
    if (desc.color_target_count > caps_.max_color_attachments || desc.bind_group_layout_count > caps_.max_bind_groups ||
        desc.push_constant_bytes > caps_.max_push_constant_bytes)
    {
        RhiLog(LogVerbosity::Error,
               "CreateGraphicsPipeline: exceeds a device limit (color targets, bind groups or push constants)");
        return {};
    }
    for (u32 i = 0; i < desc.color_target_count; ++i)
    {
        if (!FormatSupport(desc.color_targets[i].format, TextureUsage::ColorAttachment))
        {
            RhiLog(LogVerbosity::Error,
                   "CreateGraphicsPipeline: color target %u (%s) cannot be rendered to on this device", i,
                   FormatInfo(desc.color_targets[i].format).name);
            return {};
        }
    }
    if (desc.depth_format != Format::Undefined && !FormatSupport(desc.depth_format, TextureUsage::DepthAttachment))
    {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: depth format %s cannot be rendered to on this device",
               FormatInfo(desc.depth_format).name);
        return {};
    }
    PipelineRes res;
    PipelineInfo& info = res.info;
    const ShaderStage push_stages = desc.push_constant_bytes != 0 ? desc.push_constant_stages : ShaderStage::None;
    if (!RootSignatureFor(desc.bind_group_layouts, desc.bind_group_layout_count, desc.push_constant_bytes, push_stages,
                          false, &info.root))
    {
        return {};
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d;
    MemSet(&d, 0, sizeof(d));  // the cache key hashes the plain parts whole, padding included
    D3D12_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes];
    MemSet(elements, 0, sizeof(elements));
    u64 vs_hash = 0, ps_hash = 0;
    SharedPtr<ShaderCode> vs_code, ps_code;  // held while the pipeline compiles, outside the lock
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const ShaderRes* vs = shaders_.Get(desc.vertex_shader);
        if (vs == nullptr || vs->stage != ShaderStage::Vertex)
        {
            RhiLog(LogVerbosity::Error,
                   "CreateGraphicsPipeline: vertex shader is invalid, destroyed or not a vertex stage");
            return {};
        }
        vs_code = vs->code;
        vs_hash = vs->hash;
        if (desc.fragment_shader.IsValid())
        {
            const ShaderRes* fs = shaders_.Get(desc.fragment_shader);
            if (fs == nullptr || fs->stage != ShaderStage::Fragment)
            {
                RhiLog(LogVerbosity::Error,
                       "CreateGraphicsPipeline: fragment shader is invalid, destroyed or not a fragment stage");
                return {};
            }
            ps_code = fs->code;
            ps_hash = fs->hash;
        }
    }
    ComRef<ID3D12PipelineState> state, cut16;
    {
        d.pRootSignature = info.root.signature;
        d.VS = {vs_code->bytes.Data(), vs_code->bytes.Size()};
        if (ps_code)
        {
            d.PS = {ps_code->bytes.Data(), ps_code->bytes.Size()};
        }
        // Inputs are TEXCOORD<location> (shaders/target_common.slang; shader_mint checks every vertex shader).
        for (usize i = 0; i < desc.vertex_attributes.Size(); ++i)
        {
            const VertexAttributeDesc& a = desc.vertex_attributes[i];
            D3D12_INPUT_ELEMENT_DESC& e = elements[i];
            const bool instance = desc.vertex_bindings[a.binding].per_instance;
            e.SemanticName = "TEXCOORD";
            e.SemanticIndex = a.location;
            e.Format = ToDxgiVertex(a.format);
            e.InputSlot = a.binding;
            e.AlignedByteOffset = a.offset;
            e.InputSlotClass = instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                        : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            e.InstanceDataStepRate = instance ? 1 : 0;
        }
        d.InputLayout = {elements, static_cast<UINT>(desc.vertex_attributes.Size())};
        // The clip-space Y flip (ENGINE_CLIP in the D3D12 build) keeps every vertex where Vulkan puts it on the render
        // target, so a triangle's winding there is the same and the front face maps through unchanged.
        d.RasterizerState.FillMode = desc.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = Cull(desc.cull_mode);
        d.RasterizerState.FrontCounterClockwise = desc.front_face == FrontFace::CounterClockwise ? TRUE : FALSE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.BlendState.IndependentBlendEnable = desc.color_target_count > 1 ? TRUE : FALSE;
        for (u32 i = 0; i < desc.color_target_count; ++i)
        {
            const ColorTargetDesc& t = desc.color_targets[i];
            D3D12_RENDER_TARGET_BLEND_DESC& b = d.BlendState.RenderTarget[i];
            b.BlendEnable = t.blend.enable ? TRUE : FALSE;
            b.SrcBlend = Blend(t.blend.src_color, false);
            b.DestBlend = Blend(t.blend.dst_color, false);
            b.BlendOp = BlendOperation(t.blend.color_op);
            b.SrcBlendAlpha = Blend(t.blend.src_alpha, true);
            b.DestBlendAlpha = Blend(t.blend.dst_alpha, true);
            b.BlendOpAlpha = BlendOperation(t.blend.alpha_op);
            b.LogicOp = D3D12_LOGIC_OP_NOOP;
            b.RenderTargetWriteMask = static_cast<UINT8>(static_cast<u32>(t.write_mask) &
                                                         0xF);  // R, G, B, A: the same bits as D3D12's
            d.RTVFormats[i] = ToDxgi(t.format);
            info.color_formats[i] = t.format;
        }
        d.NumRenderTargets = desc.color_target_count;
        info.color_count = desc.color_target_count;
        // Without a depth test D3D12 writes no depth either, as in Vulkan.
        d.DepthStencilState.DepthEnable = desc.depth_test && desc.depth_format != Format::Undefined ? TRUE : FALSE;
        d.DepthStencilState.DepthWriteMask = desc.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                                                              : D3D12_DEPTH_WRITE_MASK_ZERO;
        d.DepthStencilState.DepthFunc = Compare(desc.depth_compare);
        d.DSVFormat = desc.depth_format != Format::Undefined ? ToDxgi(desc.depth_format) : DXGI_FORMAT_UNKNOWN;
        info.depth_format = desc.depth_format;
        d.SampleMask = UINT_MAX;
        d.SampleDesc.Count = 1;
        d.PrimitiveTopologyType = TopologyType(desc.topology);
        const bool cut = desc.primitive_restart && IsStrip(desc.topology);
        d.IBStripCutValue = cut ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF
                                : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

        state = CachedGraphicsState(GraphicsKey(d, vs_hash, ps_hash, info.root.content_hash), d, desc.debug_name);
        if (!state)
        {
            return {};
        }
        if (cut)
        {
            // D3D12 fixes the cut value in the pipeline, the index size at draw time: a second state for 16-bit
            // indices.
            d.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
            cut16 = CachedGraphicsState(GraphicsKey(d, vs_hash, ps_hash, info.root.content_hash), d, desc.debug_name);
            if (!cut16)
            {
                return {};
            }
        }
    }
    SetDebugName(state.Get(), desc.debug_name);
    if (cut16)
    {
        SetDebugName(cut16.Get(), desc.debug_name);
    }
    info.topology = Topology(desc.topology);
    info.vertex_binding_count = static_cast<u32>(desc.vertex_bindings.Size());
    for (u32 i = 0; i < info.vertex_binding_count; ++i)
    {
        info.vertex_strides[i] = desc.vertex_bindings[i].stride;
    }
    info.push_bytes = desc.push_constant_bytes;
    res.state = static_cast<ComRef<ID3D12PipelineState>&&>(state);
    res.state_cut16 = static_cast<ComRef<ID3D12PipelineState>&&>(cut16);
    return AddPipeline(static_cast<PipelineRes&&>(res), "CreateGraphicsPipeline");
}

PipelineHandle Dx12Device::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: %s", why);
        return {};
    }
    if (desc.bind_group_layout_count > caps_.max_bind_groups ||
        desc.push_constant_bytes > caps_.max_push_constant_bytes)
    {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: exceeds a device limit (bind groups or push constants)");
        return {};
    }
    PipelineRes res;
    res.info.compute = true;
    const ShaderStage push_stages = desc.push_constant_bytes != 0 ? ShaderStage::Compute : ShaderStage::None;
    if (!RootSignatureFor(desc.bind_group_layouts, desc.bind_group_layout_count, desc.push_constant_bytes, push_stages,
                          true, &res.info.root))
    {
        return {};
    }
    SharedPtr<ShaderCode> code;
    u64 key = HashValue(u32{2}, 0xcbf29ce484222325ull);  // the key's own version
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const ShaderRes* cs = shaders_.Get(desc.shader);
        if (cs == nullptr || cs->stage != ShaderStage::Compute)
        {
            RhiLog(LogVerbosity::Error, "CreateComputePipeline: shader is invalid, destroyed or not a compute stage");
            return {};
        }
        code = cs->code;
        key = HashValue(cs->hash, key);
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC d;
    MemSet(&d, 0, sizeof(d));
    d.pRootSignature = res.info.root.signature;
    d.CS = {code->bytes.Data(), code->bytes.Size()};
    key = HashValue(res.info.root.content_hash, key);
    ComRef<ID3D12PipelineState> state = CachedComputeState(key, d, desc.debug_name);
    if (!state)
    {
        return {};
    }
    SetDebugName(state.Get(), desc.debug_name);
    res.info.push_bytes = desc.push_constant_bytes;
    res.state = static_cast<ComRef<ID3D12PipelineState>&&>(state);
    return AddPipeline(static_cast<PipelineRes&&>(res), "CreateComputePipeline");
}

void Dx12Device::DestroyPipeline(PipelineHandle pipeline)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(pipelines_.Get(pipeline) != nullptr, "DestroyPipeline: handle is invalid or already destroyed");
    if (pipelines_.Get(pipeline) == nullptr)
    {
        return;
    }
    pipelines_.Retire(pipeline, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

bool Dx12Device::LookupPipeline(PipelineHandle pipeline, PipelineInfo* out) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const PipelineRes* p = pipelines_.Get(pipeline);
    if (p == nullptr)
    {
        return false;
    }
    *out = p->info;
    out->state = p->state.Get();
    out->state_cut16 = p->state_cut16.Get();
    return true;
}

}  // namespace engine_placeholder::rhi::dx12
