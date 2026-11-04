#include "rhi/null/null_device.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#include "log/assert.h"
#include "rhi/handle_pool.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi {

namespace {

constexpr u32 kMaxGroupElements = 64;

constexpr u32 Bit(ResourceState s) {
    return 1u << static_cast<u32>(s);
}

const char* StateName(ResourceState s) {
    switch (s) {
    case ResourceState::Undefined: return "Undefined";
    case ResourceState::VertexBuffer: return "VertexBuffer";
    case ResourceState::IndexBuffer: return "IndexBuffer";
    case ResourceState::UniformBuffer: return "UniformBuffer";
    case ResourceState::ShaderRead: return "ShaderRead";
    case ResourceState::ShaderWrite: return "ShaderWrite";
    case ResourceState::CopySrc: return "CopySrc";
    case ResourceState::CopyDst: return "CopyDst";
    case ResourceState::ColorAttachment: return "ColorAttachment";
    case ResourceState::DepthAttachment: return "DepthAttachment";
    case ResourceState::DepthRead: return "DepthRead";
    case ResourceState::Present: return "Present";
    case ResourceState::Count: break;
    }
    return "?";
}

// Resources the device validates by state. CPU-visible buffers have a fixed state and are exempt.
bool BufferIsTracked(const BufferDesc& d) {
    return d.location == MemoryLocation::GpuOnly;
}

struct BufferRes {
    BufferDesc desc;
    u8* data = nullptr;
    ResourceState state = ResourceState::Undefined;
    bool mapped = false;

    BufferRes() = default;
    BufferRes(const BufferRes&) = delete;
    BufferRes& operator=(const BufferRes&) = delete;
    BufferRes(BufferRes&& o) noexcept : desc(o.desc), data(o.data), state(o.state), mapped(o.mapped) {
        o.data = nullptr;
    }
    ~BufferRes() { std::free(data); }
};

struct TextureRes {
    TextureDesc desc;
    ResourceState state = ResourceState::Undefined;
};

struct SamplerRes {
    SamplerDesc desc;
};

struct ShaderRes {
    ShaderStage stage = ShaderStage::None;
};

struct LayoutRes {
    u32 count = 0;
    BindingDesc entries[kMaxBindingsPerLayout];
};

struct GroupRef {
    BindingType type = BindingType::UniformBuffer;
    BufferHandle buffer;
    TextureHandle texture;
    SamplerHandle sampler;
};

struct GroupRes {
    BindGroupLayoutHandle layout;
    u32 count = 0;
    GroupRef refs[kMaxGroupElements];
};

struct PipelineRes {
    bool compute = false;
    u32 layout_count = 0;
    BindGroupLayoutHandle layouts[kMaxBindGroups];
    u32 push_bytes = 0;
    u32 vertex_binding_count = 0;
    u32 color_count = 0;
    Format color_formats[kMaxColorAttachments] = {};
    Format depth_format = Format::Undefined;
};

// One recorded operation that matters at submit time: a state requirement, a barrier, or a copy to perform.
enum class OpKind : u8 { Require, BarrierBuffer, BarrierTexture, CopyBuffer };

struct Op {
    OpKind kind = OpKind::Require;
    BufferHandle buffer;
    BufferHandle buffer2;
    TextureHandle texture;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    u32 allowed = 0;
    u64 src_offset = 0;
    u64 dst_offset = 0;
    u64 size = 0;
    const char* what = "";
};

// Minimal growable array for trivially copyable records (the engine containers cannot free).
template <typename T>
class Vec {
public:
    Vec() = default;
    Vec(const Vec&) = delete;
    Vec& operator=(const Vec&) = delete;
    ~Vec() { std::free(data_); }
    void Clear() { size_ = 0; }
    void Push(const T& v) {
        if (size_ == cap_) {
            usize ncap = cap_ == 0 ? 32 : cap_ * 2;
            void* p = std::realloc(data_, ncap * sizeof(T));
            ENGINE_ASSERT(p != nullptr, "NullDevice: out of host memory recording a command");
            data_ = static_cast<T*>(p);
            cap_ = ncap;
        }
        data_[size_++] = v;
    }
    usize Size() const { return size_; }
    const T& operator[](usize i) const { return data_[i]; }

private:
    T* data_ = nullptr;
    usize size_ = 0;
    usize cap_ = 0;
};

bool BufferStateAllowed(ResourceState s, BufferUsage u) {
    switch (s) {
    case ResourceState::Undefined: return false;
    case ResourceState::VertexBuffer: return Has(u, BufferUsage::Vertex);
    case ResourceState::IndexBuffer: return Has(u, BufferUsage::Index);
    case ResourceState::UniformBuffer: return Has(u, BufferUsage::Uniform);
    case ResourceState::ShaderRead:
    case ResourceState::ShaderWrite: return Has(u, BufferUsage::Storage);
    case ResourceState::CopySrc: return Has(u, BufferUsage::CopySrc);
    case ResourceState::CopyDst: return Has(u, BufferUsage::CopyDst);
    default: return false;
    }
}

bool TextureStateAllowed(ResourceState s, TextureUsage u) {
    switch (s) {
    case ResourceState::Undefined: return false;
    case ResourceState::ShaderRead: return Has(u, TextureUsage::Sampled);
    case ResourceState::ShaderWrite: return Has(u, TextureUsage::Storage);
    case ResourceState::CopySrc: return Has(u, TextureUsage::CopySrc);
    case ResourceState::CopyDst: return Has(u, TextureUsage::CopyDst);
    case ResourceState::ColorAttachment:
    case ResourceState::Present: return Has(u, TextureUsage::ColorAttachment);
    case ResourceState::DepthAttachment:
    case ResourceState::DepthRead: return Has(u, TextureUsage::DepthAttachment);
    default: return false;
    }
}

u32 MipExtent(u32 base, u32 mip) {
    u32 e = base >> mip;
    return e == 0 ? 1 : e;
}

} // namespace

namespace detail {

class NullCommandList;

struct NullDeviceState {
    NullDeviceOptions options;
    mutable std::mutex mu;

    HandlePool<BufferRes, BufferTag> buffers;
    HandlePool<TextureRes, TextureTag> textures;
    HandlePool<SamplerRes, SamplerTag> samplers;
    HandlePool<ShaderRes, ShaderTag> shaders;
    HandlePool<LayoutRes, BindGroupLayoutTag> layouts;
    HandlePool<GroupRes, BindGroupTag> groups;
    HandlePool<PipelineRes, PipelineTag> pipelines;

    u64 frame = 0;
    bool in_frame = false;
    FenceValue submitted = 0;
    bool lost = false;

    std::atomic<u32> errors{0};
    mutable std::mutex err_mu;
    char last_error[256] = {};

    // Command lists are pooled and recycled at BeginFrame, never freed before the device: a pointer kept
    // across frames therefore stays dereferenceable (it reports "not ended" if submitted) instead of dangling.
    Swapchain* swapchain = nullptr;
    NullCommandList** lists = nullptr;
    usize list_count = 0;     // lists allocated
    usize list_capacity = 0;
    usize lists_in_use = 0;   // handed out this frame

    explicit NullDeviceState(const NullDeviceOptions& o) : options(o) {}
    ~NullDeviceState();

    void Error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    u64 CompletedFrame() const {
        if (frame == 0) {
            return 0;
        }
        u64 behind = 1 + static_cast<u64>(options.simulated_latency_frames);
        return frame >= behind ? frame - behind : 0;
    }
    void Collect(u64 completed);

    BufferRes* Buf(BufferHandle h) { return buffers.Get(h); }
    TextureRes* Tex(TextureHandle h) { return textures.Get(h); }
};

void NullDeviceState::Error(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    errors.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(err_mu);
        usize n = strnlen(buf, sizeof(buf) - 1);
        std::memcpy(last_error, buf, n);
        last_error[n] = '\0';
    }
    RhiLog(LogVerbosity::Error, "NullDevice: %s", buf);
    if (options.abort_on_error) {
        AssertFail(__FILE__, __LINE__, buf);
    }
}

void NullDeviceState::Collect(u64 completed) {
    // Buffer storage is released by BufferRes's destructor; nothing else owns host memory.
    buffers.CollectRetired(completed, [](BufferRes&) {});
    textures.CollectRetired(completed, [](TextureRes&) {});
    samplers.CollectRetired(completed, [](SamplerRes&) {});
    shaders.CollectRetired(completed, [](ShaderRes&) {});
    layouts.CollectRetired(completed, [](LayoutRes&) {});
    groups.CollectRetired(completed, [](GroupRes&) {});
    pipelines.CollectRetired(completed, [](PipelineRes&) {});
}

// ---------------------------------------------------------------------------------------------------------
// Command list

class NullCommandList final : public CommandList {
public:
    enum class Phase : u8 { Idle, Recording, Ended, Submitted };

    NullCommandList(NullDeviceState* s) : s_(s) {}

    void Recycle(u64 frame) {
        phase_ = Phase::Idle;
        frame_ = frame;
        ops_.Clear();
        ResetBindings();
    }

    Phase GetPhase() const { return phase_; }
    u64 Frame() const { return frame_; }
    void MarkSubmitted() { phase_ = Phase::Submitted; }
    const Vec<Op>& Ops() const { return ops_; }
    NullDeviceState* Owner() const { return s_; }

    void Begin() override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (phase_ != Phase::Idle) {
            s_->Error("Begin: the list is already recording, ended or submitted");
            return;
        }
        phase_ = Phase::Recording;
        ops_.Clear();
        ResetBindings();
    }

    void End() override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("End")) {
            return;
        }
        if (in_rendering_) {
            s_->Error("End: a rendering pass is still open");
        }
        if (label_depth_ != 0) {
            s_->Error("End: %u debug label(s) were not popped", label_depth_);
        }
        phase_ = Phase::Ended;
    }

    void CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("CopyBuffer") || !OutsidePass("CopyBuffer")) {
            return;
        }
        BufferRes* s = s_->Buf(src);
        BufferRes* d = s_->Buf(dst);
        if (s == nullptr || d == nullptr) {
            s_->Error("CopyBuffer: %s buffer handle is invalid or destroyed", s == nullptr ? "source" : "destination");
            return;
        }
        if (!Has(s->desc.usage, BufferUsage::CopySrc) || !Has(d->desc.usage, BufferUsage::CopyDst)) {
            s_->Error("CopyBuffer: source needs CopySrc usage and destination needs CopyDst usage");
            return;
        }
        if (size > s->desc.size || src_offset > s->desc.size - size || size > d->desc.size ||
            dst_offset > d->desc.size - size) {
            s_->Error("CopyBuffer: range lies outside a buffer");
            return;
        }
        if (src == dst) {
            s_->Error("CopyBuffer: source and destination are the same buffer");
            return;
        }
        RequireBuffer(src, CopySrcMask(), "CopyBuffer source");
        RequireBuffer(dst, Bit(ResourceState::CopyDst), "CopyBuffer destination");
        Op op;
        op.kind = OpKind::CopyBuffer;
        op.buffer = src;
        op.buffer2 = dst;
        op.src_offset = src_offset;
        op.dst_offset = dst_offset;
        op.size = size;
        ops_.Push(op);
    }

    void CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("CopyBufferToTexture") || !OutsidePass("CopyBufferToTexture")) {
            return;
        }
        BufferRes* b = s_->Buf(src);
        TextureRes* t = s_->Tex(dst);
        if (b == nullptr || t == nullptr) {
            s_->Error("CopyBufferToTexture: %s handle is invalid or destroyed", b == nullptr ? "buffer" : "texture");
            return;
        }
        if (!Has(b->desc.usage, BufferUsage::CopySrc) || !Has(t->desc.usage, TextureUsage::CopyDst)) {
            s_->Error("CopyBufferToTexture: buffer needs CopySrc usage and texture needs CopyDst usage");
            return;
        }
        if (!CheckTextureCopy(*b, *t, region, "CopyBufferToTexture")) {
            return;
        }
        RequireBuffer(src, CopySrcMask(), "CopyBufferToTexture source");
        RequireTexture(dst, Bit(ResourceState::CopyDst), "CopyBufferToTexture destination");
    }

    void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("CopyTextureToBuffer") || !OutsidePass("CopyTextureToBuffer")) {
            return;
        }
        TextureRes* t = s_->Tex(src);
        BufferRes* b = s_->Buf(dst);
        if (b == nullptr || t == nullptr) {
            s_->Error("CopyTextureToBuffer: %s handle is invalid or destroyed", t == nullptr ? "texture" : "buffer");
            return;
        }
        if (!Has(t->desc.usage, TextureUsage::CopySrc) || !Has(b->desc.usage, BufferUsage::CopyDst)) {
            s_->Error("CopyTextureToBuffer: texture needs CopySrc usage and buffer needs CopyDst usage");
            return;
        }
        if (!CheckTextureCopy(*b, *t, region, "CopyTextureToBuffer")) {
            return;
        }
        RequireTexture(src, Bit(ResourceState::CopySrc), "CopyTextureToBuffer source");
        RequireBuffer(dst, Bit(ResourceState::CopyDst), "CopyTextureToBuffer destination");
    }

    void Barrier(BufferHandle buffer, ResourceState before, ResourceState after) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("Barrier") || !OutsidePass("Barrier")) {
            return;
        }
        BufferRes* b = s_->Buf(buffer);
        if (b == nullptr) {
            s_->Error("Barrier: buffer handle is invalid or destroyed");
            return;
        }
        if (!BufferIsTracked(b->desc)) {
            s_->Error("Barrier: CPU-visible buffers have a fixed state and take no barriers");
            return;
        }
        if (!BufferStateAllowed(after, b->desc.usage)) {
            s_->Error("Barrier: buffer cannot enter state %s (wrong state for its type or missing usage flag)",
                      StateName(after));
            return;
        }
        Op op;
        op.kind = OpKind::BarrierBuffer;
        op.buffer = buffer;
        op.before = before;
        op.after = after;
        ops_.Push(op);
    }

    void Barrier(TextureHandle texture, ResourceState before, ResourceState after) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("Barrier") || !OutsidePass("Barrier")) {
            return;
        }
        TextureRes* t = s_->Tex(texture);
        if (t == nullptr) {
            s_->Error("Barrier: texture handle is invalid or destroyed");
            return;
        }
        if (!TextureStateAllowed(after, t->desc.usage)) {
            s_->Error("Barrier: texture cannot enter state %s (wrong state for its type or missing usage flag)",
                      StateName(after));
            return;
        }
        Op op;
        op.kind = OpKind::BarrierTexture;
        op.texture = texture;
        op.before = before;
        op.after = after;
        ops_.Push(op);
    }

    void GenerateMips(TextureHandle texture) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("GenerateMips") || !OutsidePass("GenerateMips")) {
            return;
        }
        TextureRes* t = s_->Tex(texture);
        if (t == nullptr) {
            s_->Error("GenerateMips: texture handle is invalid or destroyed");
            return;
        }
        const FormatProperties& f = FormatInfo(t->desc.format);
        if (!Has(t->desc.usage, TextureUsage::Sampled) || !Has(t->desc.usage, TextureUsage::CopySrc) ||
            !Has(t->desc.usage, TextureUsage::CopyDst) || t->desc.mip_levels < 2 || f.is_depth || f.is_compressed) {
            s_->Error("GenerateMips: needs Sampled, CopySrc and CopyDst usage, more than one mip level and a color format that blits");
            return;
        }
        RequireTexture(texture, Bit(ResourceState::ShaderRead), "GenerateMips");
    }

    void BeginRendering(const RenderingDesc& desc) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("BeginRendering")) {
            return;
        }
        if (in_rendering_) {
            s_->Error("BeginRendering: a rendering pass is already open");
            return;
        }
        bool has_depth = desc.depth.texture.IsValid();
        if (desc.color_count == 0 && !has_depth) {
            s_->Error("BeginRendering: no attachments");
            return;
        }
        if (desc.color_count > kMaxColorAttachments || desc.color_count > s_->options.caps.max_color_attachments) {
            s_->Error("BeginRendering: more color attachments than the device supports");
            return;
        }
        u32 w = 0;
        u32 h = 0;
        Format formats[kMaxColorAttachments] = {};
        Format depth_format = Format::Undefined;
        for (u32 i = 0; i < desc.color_count; ++i) {
            TextureRes* t = s_->Tex(desc.color[i].texture);
            if (t == nullptr) {
                s_->Error("BeginRendering: color attachment %u is invalid or destroyed", i);
                return;
            }
            if (!Has(t->desc.usage, TextureUsage::ColorAttachment)) {
                s_->Error("BeginRendering: color attachment %u lacks ColorAttachment usage", i);
                return;
            }
            if (i == 0) {
                w = t->desc.width;
                h = t->desc.height;
            } else if (t->desc.width != w || t->desc.height != h) {
                s_->Error("BeginRendering: attachment extents differ");
                return;
            }
            formats[i] = t->desc.format;
        }
        if (has_depth) {
            TextureRes* t = s_->Tex(desc.depth.texture);
            if (t == nullptr) {
                s_->Error("BeginRendering: depth attachment is invalid or destroyed");
                return;
            }
            if (!Has(t->desc.usage, TextureUsage::DepthAttachment)) {
                s_->Error("BeginRendering: depth attachment lacks DepthAttachment usage");
                return;
            }
            if (desc.color_count == 0) {
                w = t->desc.width;
                h = t->desc.height;
            } else if (t->desc.width != w || t->desc.height != h) {
                s_->Error("BeginRendering: attachment extents differ");
                return;
            }
            depth_format = t->desc.format;
        }
        for (u32 i = 0; i < desc.color_count; ++i) {
            RequireTexture(desc.color[i].texture, Bit(ResourceState::ColorAttachment), "BeginRendering color attachment");
        }
        if (has_depth) {
            RequireTexture(desc.depth.texture, Bit(ResourceState::DepthAttachment), "BeginRendering depth attachment");
        }
        in_rendering_ = true;
        ForgetGraphicsPipeline();  // a pipeline never carries over from an earlier pass
        render_color_count_ = desc.color_count;
        for (u32 i = 0; i < kMaxColorAttachments; ++i) {
            render_formats_[i] = formats[i];
        }
        render_depth_format_ = depth_format;
    }

    void EndRendering() override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("EndRendering")) {
            return;
        }
        if (!in_rendering_) {
            s_->Error("EndRendering: no rendering pass is open");
            return;
        }
        in_rendering_ = false;
        ForgetGraphicsPipeline();
    }

    void SetPipeline(PipelineHandle pipeline) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetPipeline")) {
            return;
        }
        PipelineRes* p = s_->pipelines.Get(pipeline);
        if (p == nullptr) {
            s_->Error("SetPipeline: pipeline handle is invalid or destroyed");
            return;
        }
        if (p->compute) {
            if (in_rendering_) {
                s_->Error("SetPipeline: a compute pipeline cannot be bound inside a rendering pass");
                return;
            }
        } else {
            if (!in_rendering_) {
                s_->Error("SetPipeline: a graphics pipeline can only be bound inside a rendering pass");
                return;
            }
            bool match = p->color_count == render_color_count_ && p->depth_format == render_depth_format_;
            for (u32 i = 0; match && i < p->color_count; ++i) {
                match = p->color_formats[i] == render_formats_[i];
            }
            if (!match) {
                s_->Error("SetPipeline: pipeline attachment formats do not match the rendering pass");
                return;
            }
        }
        pipeline_ = pipeline;
        pipeline_info_ = *p;
    }

    void SetBindGroup(u32 slot, BindGroupHandle group) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetBindGroup")) {
            return;
        }
        if (slot >= s_->options.caps.max_bind_groups || slot >= kMaxBindGroups) {
            s_->Error("SetBindGroup: slot %u is outside the device limit", slot);
            return;
        }
        if (s_->groups.Get(group) == nullptr) {
            s_->Error("SetBindGroup: group handle is invalid or destroyed");
            return;
        }
        groups_[slot] = group;
    }

    void SetPushConstants(u32 offset, const void* data, u32 size) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetPushConstants")) {
            return;
        }
        if (!pipeline_.IsValid()) {
            s_->Error("SetPushConstants: no pipeline is bound");
            return;
        }
        if (offset % 4 != 0 || size % 4 != 0 || (size > 0 && data == nullptr)) {
            s_->Error("SetPushConstants: offset and size must be multiples of 4 and data must not be null");
            return;
        }
        if (static_cast<u64>(offset) + size > pipeline_info_.push_bytes) {
            s_->Error("SetPushConstants: range exceeds the pipeline's push-constant range");
            return;
        }
    }

    void SetViewport(const Viewport& viewport) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetViewport")) {
            return;
        }
        if (!(viewport.width > 0.0f) || !(viewport.height > 0.0f)) {
            s_->Error("SetViewport: width and height must be positive");
        }
    }

    void SetScissor(const ScissorRect&) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        Recording("SetScissor");
    }

    void SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetVertexBuffer")) {
            return;
        }
        if (slot >= kMaxVertexBindings) {
            s_->Error("SetVertexBuffer: slot %u is out of range", slot);
            return;
        }
        BufferRes* b = s_->Buf(buffer);
        if (b == nullptr) {
            s_->Error("SetVertexBuffer: buffer handle is invalid or destroyed");
            return;
        }
        if (!Has(b->desc.usage, BufferUsage::Vertex)) {
            s_->Error("SetVertexBuffer: buffer lacks Vertex usage");
            return;
        }
        if (offset > b->desc.size) {
            s_->Error("SetVertexBuffer: offset is past the end of the buffer");
            return;
        }
        vertex_buffers_[slot] = buffer;
    }

    void SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("SetIndexBuffer")) {
            return;
        }
        BufferRes* b = s_->Buf(buffer);
        if (b == nullptr) {
            s_->Error("SetIndexBuffer: buffer handle is invalid or destroyed");
            return;
        }
        if (!Has(b->desc.usage, BufferUsage::Index)) {
            s_->Error("SetIndexBuffer: buffer lacks Index usage");
            return;
        }
        u64 width = type == IndexType::UInt16 ? 2 : 4;
        if (offset % width != 0 || offset > b->desc.size) {
            s_->Error("SetIndexBuffer: offset is misaligned or past the end of the buffer");
            return;
        }
        index_buffer_ = buffer;
    }

    void Draw(u32, u32, u32, u32) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("Draw")) {
            return;
        }
        CheckDraw("Draw", false);
    }

    void DrawIndexed(u32, u32, u32, i32, u32) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("DrawIndexed")) {
            return;
        }
        CheckDraw("DrawIndexed", true);
    }

    void Dispatch(u32 x, u32 y, u32 z) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("Dispatch")) {
            return;
        }
        if (in_rendering_) {
            s_->Error("Dispatch: not allowed inside a rendering pass");
            return;
        }
        if (!pipeline_.IsValid() || !pipeline_info_.compute) {
            s_->Error("Dispatch: no compute pipeline is bound");
            return;
        }
        if (s_->pipelines.Get(pipeline_) == nullptr) {
            s_->Error("Dispatch: the bound pipeline was destroyed");
            return;
        }
        const u32* limit = s_->options.caps.max_compute_work_group_count;
        if (x > limit[0] || y > limit[1] || z > limit[2]) {
            s_->Error("Dispatch: workgroup count exceeds the device limit");
            return;
        }
        CheckBindings("Dispatch");
    }

    void PushDebugLabel(const char* name) override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("PushDebugLabel")) {
            return;
        }
        if (name == nullptr) {
            s_->Error("PushDebugLabel: name is null");
            return;
        }
        ++label_depth_;
    }

    void PopDebugLabel() override {
        std::lock_guard<std::mutex> lock(s_->mu);
        if (!Recording("PopDebugLabel")) {
            return;
        }
        if (label_depth_ == 0) {
            s_->Error("PopDebugLabel: no debug label is open");
            return;
        }
        --label_depth_;
    }

private:
    static u32 CopySrcMask() { return Bit(ResourceState::CopySrc); }

    void ForgetGraphicsPipeline() {
        if (pipeline_.IsValid() && !pipeline_info_.compute) {
            pipeline_ = PipelineHandle{};
            pipeline_info_ = PipelineRes{};
        }
    }

    void ResetBindings() {
        in_rendering_ = false;
        label_depth_ = 0;
        pipeline_ = PipelineHandle{};
        pipeline_info_ = PipelineRes{};
        for (BindGroupHandle& g : groups_) {
            g = BindGroupHandle{};
        }
        for (BufferHandle& b : vertex_buffers_) {
            b = BufferHandle{};
        }
        index_buffer_ = BufferHandle{};
        render_color_count_ = 0;
        render_depth_format_ = Format::Undefined;
    }

    bool Recording(const char* what) {
        if (phase_ != Phase::Recording) {
            s_->Error("%s: the command list is not recording (call Begin first, and do not record after End or Submit)", what);
            return false;
        }
        return true;
    }

    bool OutsidePass(const char* what) {
        if (in_rendering_) {
            s_->Error("%s: not allowed inside a rendering pass", what);
            return false;
        }
        return true;
    }

    void RequireBuffer(BufferHandle h, u32 allowed, const char* what) {
        BufferRes* b = s_->Buf(h);
        if (b == nullptr || !BufferIsTracked(b->desc)) {
            return;
        }
        Op op;
        op.kind = OpKind::Require;
        op.buffer = h;
        op.allowed = allowed;
        op.what = what;
        ops_.Push(op);
    }

    void RequireTexture(TextureHandle h, u32 allowed, const char* what) {
        Op op;
        op.kind = OpKind::Require;
        op.texture = h;
        op.allowed = allowed;
        op.what = what;
        ops_.Push(op);
    }

    bool CheckTextureCopy(const BufferRes& b, const TextureRes& t, const BufferTextureCopy& r, const char* what) {
        const TextureDesc& d = t.desc;
        if (r.mip_level >= d.mip_levels || r.array_layer >= d.array_layers) {
            s_->Error("%s: mip level or array layer is out of range", what);
            return false;
        }
        const FormatProperties& fp = FormatInfo(d.format);
        if (fp.has_stencil) {
            s_->Error("%s: combined depth-stencil formats cannot be copied", what);
            return false;
        }
        u32 mw = MipExtent(d.width, r.mip_level);
        u32 mh = MipExtent(d.height, r.mip_level);
        u32 md = d.dimension == TextureDimension::Tex3D ? MipExtent(d.depth, r.mip_level) : 1;
        u64 row_bytes = FormatRowBytes(d.format, mw);
        u64 pitch = r.buffer_row_pitch != 0 ? r.buffer_row_pitch : row_bytes;
        // Vulkan requires the offset to be a multiple of the texel (block) size, and the row length to be a whole
        // number of texels; the other backends have similar rules, so the contract states them.
        if (r.buffer_offset % fp.bytes_per_block != 0 || pitch % fp.bytes_per_block != 0) {
            s_->Error("%s: buffer_offset and buffer_row_pitch must be multiples of the texel size (%u bytes)", what,
                      static_cast<u32>(fp.bytes_per_block));
            return false;
        }
        if (pitch < row_bytes) {
            s_->Error("%s: buffer_row_pitch is smaller than one row of the level", what);
            return false;
        }
        u64 rows = (static_cast<u64>(mh) + fp.block_height - 1) / fp.block_height;
        u64 needed = (static_cast<u64>(md) * rows - 1) * pitch + row_bytes;
        if (r.buffer_offset > b.desc.size || needed > b.desc.size - r.buffer_offset) {
            s_->Error("%s: the buffer is too small for the texture level", what);
            return false;
        }
        return true;
    }

    // Validates bound groups against the pipeline and records the state requirements of every bound resource.
    void CheckBindings(const char* what) {
        for (u32 slot = 0; slot < pipeline_info_.layout_count; ++slot) {
            GroupRes* g = s_->groups.Get(groups_[slot]);
            if (!groups_[slot].IsValid()) {
                s_->Error("%s: bind group %u required by the pipeline is not set", what, slot);
                return;
            }
            if (g == nullptr) {
                s_->Error("%s: bind group %u was destroyed", what, slot);
                return;
            }
            if (g->layout != pipeline_info_.layouts[slot]) {
                s_->Error("%s: bind group %u was created from a different layout than the pipeline declares", what, slot);
                return;
            }
            for (u32 i = 0; i < g->count; ++i) {
                const GroupRef& ref = g->refs[i];
                switch (ref.type) {
                case BindingType::UniformBuffer:
                    if (!CheckGroupBuffer(ref.buffer, Bit(ResourceState::UniformBuffer), what)) return;
                    break;
                case BindingType::StorageBuffer:
                    if (!CheckGroupBuffer(ref.buffer, Bit(ResourceState::ShaderRead) | Bit(ResourceState::ShaderWrite), what)) return;
                    break;
                case BindingType::SampledTexture:
                    if (!CheckGroupTexture(ref.texture, Bit(ResourceState::ShaderRead) | Bit(ResourceState::DepthRead), what)) return;
                    break;
                case BindingType::StorageTexture:
                    if (!CheckGroupTexture(ref.texture, Bit(ResourceState::ShaderWrite), what)) return;
                    break;
                case BindingType::Sampler:
                    if (s_->samplers.Get(ref.sampler) == nullptr) {
                        s_->Error("%s: a bound sampler was destroyed", what);
                        return;
                    }
                    break;
                case BindingType::Count:
                    break;
                }
            }
        }
    }

    bool CheckGroupBuffer(BufferHandle h, u32 allowed, const char* what) {
        if (s_->Buf(h) == nullptr) {
            s_->Error("%s: a buffer in a bound bind group was destroyed", what);
            return false;
        }
        RequireBuffer(h, allowed, "bound buffer");
        return true;
    }

    bool CheckGroupTexture(TextureHandle h, u32 allowed, const char* what) {
        if (s_->Tex(h) == nullptr) {
            s_->Error("%s: a texture in a bound bind group was destroyed", what);
            return false;
        }
        RequireTexture(h, allowed, "bound texture");
        return true;
    }

    void CheckDraw(const char* what, bool indexed) {
        if (!in_rendering_) {
            s_->Error("%s: only allowed inside a rendering pass", what);
            return;
        }
        if (!pipeline_.IsValid() || pipeline_info_.compute) {
            s_->Error("%s: no graphics pipeline is bound", what);
            return;
        }
        if (s_->pipelines.Get(pipeline_) == nullptr) {
            s_->Error("%s: the bound pipeline was destroyed", what);
            return;
        }
        for (u32 i = 0; i < pipeline_info_.vertex_binding_count; ++i) {
            if (!vertex_buffers_[i].IsValid()) {
                s_->Error("%s: vertex buffer %u required by the pipeline is not set", what, i);
                return;
            }
            if (s_->Buf(vertex_buffers_[i]) == nullptr) {
                s_->Error("%s: vertex buffer %u was destroyed", what, i);
                return;
            }
            RequireBuffer(vertex_buffers_[i], Bit(ResourceState::VertexBuffer), "bound vertex buffer");
        }
        if (indexed) {
            if (!index_buffer_.IsValid()) {
                s_->Error("%s: no index buffer is bound", what);
                return;
            }
            if (s_->Buf(index_buffer_) == nullptr) {
                s_->Error("%s: the index buffer was destroyed", what);
                return;
            }
            RequireBuffer(index_buffer_, Bit(ResourceState::IndexBuffer), "bound index buffer");
        }
        CheckBindings(what);
    }

    NullDeviceState* s_;
    Phase phase_ = Phase::Idle;
    u64 frame_ = 0;
    Vec<Op> ops_;

    bool in_rendering_ = false;
    u32 label_depth_ = 0;
    PipelineHandle pipeline_;
    PipelineRes pipeline_info_;
    BindGroupHandle groups_[kMaxBindGroups];
    BufferHandle vertex_buffers_[kMaxVertexBindings];
    BufferHandle index_buffer_;
    u32 render_color_count_ = 0;
    Format render_formats_[kMaxColorAttachments] = {};
    Format render_depth_format_ = Format::Undefined;
};

NullDeviceState::~NullDeviceState() {
    for (usize i = 0; i < list_count; ++i) {
        delete lists[i];
    }
    std::free(lists);
}

} // namespace detail

using State = detail::NullDeviceState;
using detail::NullCommandList;

// ---------------------------------------------------------------------------------------------------------
// Device

NullDevice::NullDevice(State* state) : state_(state) {}

NullDevice::~NullDevice() {
    if (state_->swapchain != nullptr) {
        state_->Error("NullDevice: the swapchain was still alive when the device was destroyed");
        DestroySwapchain(state_->swapchain);
    }
    delete state_;
}

NullDevice* CreateNullDevice(const NullDeviceOptions& options) {
    return new NullDevice(new State(options));
}

void DestroyDevice(Device* device) {
    if (device == nullptr) {
        return;
    }
    device->WaitIdle();
    delete device;
}

const char* NullDevice::BackendName() const {
    return "null";
}

const DeviceCaps& NullDevice::Caps() const {
    return state_->options.caps;
}

BufferHandle NullDevice::CreateBuffer(const BufferDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateBuffer: %s", why);
        return BufferHandle{};
    }
    if (desc.size > s.options.max_buffer_bytes) {
        s.Error("CreateBuffer: %llu bytes exceeds the null device's buffer limit", static_cast<unsigned long long>(desc.size));
        return BufferHandle{};
    }
    BufferRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    res.data = static_cast<u8*>(std::calloc(static_cast<usize>(desc.size), 1));
    if (res.data == nullptr) {
        s.Error("CreateBuffer: out of host memory");
        return BufferHandle{};
    }
    std::lock_guard<std::mutex> lock(s.mu);
    BufferHandle h = s.buffers.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateBuffer: out of buffer slots");
    }
    return h;
}

void NullDevice::DestroyBuffer(BufferHandle buffer) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    BufferRes* b = s.Buf(buffer);
    if (b == nullptr) {
        s.Error("DestroyBuffer: handle is invalid or already destroyed");
        return;
    }
    if (b->mapped) {
        s.Error("DestroyBuffer: the buffer is still mapped");
        return;
    }
    s.buffers.Retire(buffer, s.frame);
}

TextureHandle NullDevice::CreateTexture(const TextureDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateTexture: %s", why);
        return TextureHandle{};
    }
    const DeviceCaps& caps = s.options.caps;
    u32 limit = desc.dimension == TextureDimension::Tex3D ? caps.max_texture_dimension_3d : caps.max_texture_dimension_2d;
    u32 biggest = desc.width > desc.height ? desc.width : desc.height;
    if (desc.dimension == TextureDimension::Tex3D && desc.depth > biggest) {
        biggest = desc.depth;
    }
    if (biggest > limit || desc.array_layers > caps.max_texture_array_layers) {
        s.Error("CreateTexture: extent or layer count exceeds the device limit");
        return TextureHandle{};
    }
    TextureRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    std::lock_guard<std::mutex> lock(s.mu);
    TextureHandle h = s.textures.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateTexture: out of texture slots");
    }
    return h;
}

void NullDevice::DestroyTexture(TextureHandle texture) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.textures.Retire(texture, s.frame)) {
        s.Error("DestroyTexture: handle is invalid or already destroyed");
    }
}

SamplerHandle NullDevice::CreateSampler(const SamplerDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateSampler: %s", why);
        return SamplerHandle{};
    }
    SamplerRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    std::lock_guard<std::mutex> lock(s.mu);
    SamplerHandle h = s.samplers.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateSampler: out of sampler slots");
    }
    return h;
}

void NullDevice::DestroySampler(SamplerHandle sampler) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.samplers.Retire(sampler, s.frame)) {
        s.Error("DestroySampler: handle is invalid or already destroyed");
    }
}

ShaderHandle NullDevice::CreateShader(const ShaderDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateShader: %s", why);
        return ShaderHandle{};
    }
    ShaderRes res;
    res.stage = desc.stage;
    std::lock_guard<std::mutex> lock(s.mu);
    ShaderHandle h = s.shaders.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateShader: out of shader slots");
    }
    return h;
}

void NullDevice::DestroyShader(ShaderHandle shader) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.shaders.Retire(shader, s.frame)) {
        s.Error("DestroyShader: handle is invalid or already destroyed");
    }
}

BindGroupLayoutHandle NullDevice::CreateBindGroupLayout(const BindGroupLayoutDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateBindGroupLayout: %s", why);
        return BindGroupLayoutHandle{};
    }
    LayoutRes res;
    res.count = static_cast<u32>(desc.entries.count);
    u64 elements = 0;  // 64-bit: counts are caller-supplied and a 32-bit sum could wrap past the check
    for (u32 i = 0; i < res.count; ++i) {
        res.entries[i] = desc.entries[i];
        elements += desc.entries[i].count;
    }
    if (elements > kMaxGroupElements) {
        s.Error("CreateBindGroupLayout: more than %u binding elements", kMaxGroupElements);
        return BindGroupLayoutHandle{};
    }
    std::lock_guard<std::mutex> lock(s.mu);
    BindGroupLayoutHandle h = s.layouts.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateBindGroupLayout: out of layout slots");
    }
    return h;
}

void NullDevice::DestroyBindGroupLayout(BindGroupLayoutHandle layout) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.layouts.Retire(layout, s.frame)) {
        s.Error("DestroyBindGroupLayout: handle is invalid or already destroyed");
    }
}

BindGroupHandle NullDevice::CreateBindGroup(const BindGroupDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateBindGroup: %s", why);
        return BindGroupHandle{};
    }
    std::lock_guard<std::mutex> lock(s.mu);
    LayoutRes* layout = s.layouts.Get(desc.layout);
    if (layout == nullptr) {
        s.Error("CreateBindGroup: layout handle is invalid or destroyed");
        return BindGroupHandle{};
    }
    GroupRes res;
    res.layout = desc.layout;
    u32 expected = 0;
    for (u32 b = 0; b < layout->count; ++b) {
        const BindingDesc& bd = layout->entries[b];
        for (u32 e = 0; e < bd.count; ++e) {
            const BindGroupEntry* found = nullptr;
            for (usize k = 0; k < desc.entries.count; ++k) {
                if (desc.entries[k].binding == bd.binding && desc.entries[k].array_element == e) {
                    found = &desc.entries[k];
                    break;
                }
            }
            if (found == nullptr) {
                s.Error("CreateBindGroup: binding %u element %u has no entry", bd.binding, e);
                return BindGroupHandle{};
            }
            GroupRef& ref = res.refs[res.count++];
            ref.type = bd.type;
            switch (bd.type) {
            case BindingType::UniformBuffer:
            case BindingType::StorageBuffer: {
                BufferRes* buf = s.Buf(found->buffer);
                bool uniform = bd.type == BindingType::UniformBuffer;
                if (buf == nullptr) {
                    s.Error("CreateBindGroup: binding %u buffer is invalid or destroyed", bd.binding);
                    return BindGroupHandle{};
                }
                if (!Has(buf->desc.usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage)) {
                    s.Error("CreateBindGroup: binding %u buffer lacks %s usage", bd.binding, uniform ? "Uniform" : "Storage");
                    return BindGroupHandle{};
                }
                u32 align = uniform ? s.options.caps.min_uniform_buffer_offset_alignment
                                    : s.options.caps.min_storage_buffer_offset_alignment;
                if (align != 0 && found->offset % align != 0) {
                    s.Error("CreateBindGroup: binding %u offset is not a multiple of %u", bd.binding, align);
                    return BindGroupHandle{};
                }
                if (found->offset >= buf->desc.size || (found->size != 0 && found->size > buf->desc.size - found->offset)) {
                    s.Error("CreateBindGroup: binding %u range lies outside the buffer", bd.binding);
                    return BindGroupHandle{};
                }
                ref.buffer = found->buffer;
                break;
            }
            case BindingType::SampledTexture:
            case BindingType::StorageTexture: {
                TextureRes* tex = s.Tex(found->texture);
                bool sampled = bd.type == BindingType::SampledTexture;
                if (tex == nullptr) {
                    s.Error("CreateBindGroup: binding %u texture is invalid or destroyed", bd.binding);
                    return BindGroupHandle{};
                }
                if (!Has(tex->desc.usage, sampled ? TextureUsage::Sampled : TextureUsage::Storage)) {
                    s.Error("CreateBindGroup: binding %u texture lacks %s usage", bd.binding, sampled ? "Sampled" : "Storage");
                    return BindGroupHandle{};
                }
                ref.texture = found->texture;
                break;
            }
            case BindingType::Sampler:
                if (s.samplers.Get(found->sampler) == nullptr) {
                    s.Error("CreateBindGroup: binding %u sampler is invalid or destroyed", bd.binding);
                    return BindGroupHandle{};
                }
                ref.sampler = found->sampler;
                break;
            case BindingType::Count:
                break;
            }
            ++expected;
        }
    }
    if (desc.entries.count != expected) {
        s.Error("CreateBindGroup: %u entries given but the layout has %u binding elements",
                static_cast<u32>(desc.entries.count), expected);
        return BindGroupHandle{};
    }
    BindGroupHandle h = s.groups.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateBindGroup: out of bind group slots");
    }
    return h;
}

void NullDevice::DestroyBindGroup(BindGroupHandle group) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.groups.Retire(group, s.frame)) {
        s.Error("DestroyBindGroup: handle is invalid or already destroyed");
    }
}

PipelineHandle NullDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateGraphicsPipeline: %s", why);
        return PipelineHandle{};
    }
    const DeviceCaps& caps = s.options.caps;
    if (desc.color_target_count > caps.max_color_attachments || desc.bind_group_layout_count > caps.max_bind_groups ||
        desc.push_constant_bytes > caps.max_push_constant_bytes) {
        s.Error("CreateGraphicsPipeline: exceeds a device limit (color targets, bind groups or push constants)");
        return PipelineHandle{};
    }
    std::lock_guard<std::mutex> lock(s.mu);
    ShaderRes* vs = s.shaders.Get(desc.vertex_shader);
    if (vs == nullptr || vs->stage != ShaderStage::Vertex) {
        s.Error("CreateGraphicsPipeline: vertex shader is invalid, destroyed or not a vertex stage");
        return PipelineHandle{};
    }
    if (desc.fragment_shader.IsValid()) {
        ShaderRes* fs = s.shaders.Get(desc.fragment_shader);
        if (fs == nullptr || fs->stage != ShaderStage::Fragment) {
            s.Error("CreateGraphicsPipeline: fragment shader is invalid, destroyed or not a fragment stage");
            return PipelineHandle{};
        }
    }
    PipelineRes res;
    res.compute = false;
    res.layout_count = desc.bind_group_layout_count;
    for (u32 i = 0; i < desc.bind_group_layout_count; ++i) {
        if (s.layouts.Get(desc.bind_group_layouts[i]) == nullptr) {
            s.Error("CreateGraphicsPipeline: bind group layout %u is invalid or destroyed", i);
            return PipelineHandle{};
        }
        res.layouts[i] = desc.bind_group_layouts[i];
    }
    res.push_bytes = desc.push_constant_bytes;
    res.vertex_binding_count = static_cast<u32>(desc.vertex_bindings.count);
    res.color_count = desc.color_target_count;
    for (u32 i = 0; i < desc.color_target_count; ++i) {
        res.color_formats[i] = desc.color_targets[i].format;
    }
    res.depth_format = desc.depth_format;
    PipelineHandle h = s.pipelines.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateGraphicsPipeline: out of pipeline slots");
    }
    return h;
}

PipelineHandle NullDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    State& s = *state_;
    const char* why = desc.Validate();
    if (why != nullptr) {
        s.Error("CreateComputePipeline: %s", why);
        return PipelineHandle{};
    }
    const DeviceCaps& caps = s.options.caps;
    if (desc.bind_group_layout_count > caps.max_bind_groups || desc.push_constant_bytes > caps.max_push_constant_bytes) {
        s.Error("CreateComputePipeline: exceeds a device limit (bind groups or push constants)");
        return PipelineHandle{};
    }
    std::lock_guard<std::mutex> lock(s.mu);
    ShaderRes* cs = s.shaders.Get(desc.shader);
    if (cs == nullptr || cs->stage != ShaderStage::Compute) {
        s.Error("CreateComputePipeline: shader is invalid, destroyed or not a compute stage");
        return PipelineHandle{};
    }
    PipelineRes res;
    res.compute = true;
    res.layout_count = desc.bind_group_layout_count;
    for (u32 i = 0; i < desc.bind_group_layout_count; ++i) {
        if (s.layouts.Get(desc.bind_group_layouts[i]) == nullptr) {
            s.Error("CreateComputePipeline: bind group layout %u is invalid or destroyed", i);
            return PipelineHandle{};
        }
        res.layouts[i] = desc.bind_group_layouts[i];
    }
    res.push_bytes = desc.push_constant_bytes;
    PipelineHandle h = s.pipelines.Allocate(std::move(res));
    if (!h.IsValid()) {
        s.Error("CreateComputePipeline: out of pipeline slots");
    }
    return h;
}

void NullDevice::DestroyPipeline(PipelineHandle pipeline) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.pipelines.Retire(pipeline, s.frame)) {
        s.Error("DestroyPipeline: handle is invalid or already destroyed");
    }
}

void* NullDevice::MapBuffer(BufferHandle buffer) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    BufferRes* b = s.Buf(buffer);
    if (b == nullptr) {
        s.Error("MapBuffer: handle is invalid or destroyed");
        return nullptr;
    }
    if (b->desc.location == MemoryLocation::GpuOnly) {
        s.Error("MapBuffer: GpuOnly buffers are not CPU-visible");
        return nullptr;
    }
    if (b->mapped) {
        s.Error("MapBuffer: the buffer is already mapped");
        return nullptr;
    }
    b->mapped = true;
    return b->data;
}

UploadTicket NullDevice::UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    BufferRes* b = s.Buf(dst);
    if (b == nullptr) {
        s.Error("UploadBuffer: handle is invalid or already destroyed");
        return UploadTicket{0, false};
    }
    if (!Has(b->desc.usage, BufferUsage::CopyDst)) {
        s.Error("UploadBuffer: the buffer was not created with CopyDst usage");
        return UploadTicket{0, false};
    }
    if (data == nullptr || size > b->desc.size || offset > b->desc.size - size) {
        s.Error("UploadBuffer: null data or a range outside the buffer");
        return UploadTicket{0, false};
    }
    if (s.lost) {
        return UploadTicket{0, false};
    }
    std::memcpy(b->data + offset, data, static_cast<usize>(size));
    b->state = ResourceState::CopyDst;
    return {};
}

namespace {

// A swapchain with no window: three textures that rotate, for testing the frame loop without a GPU. A size of 0
// in the description means 640x480.
class NullSwapchain final : public Swapchain {
public:
    NullSwapchain(NullDevice* device, const SwapchainDesc& desc)
        : device_(device), width_(desc.width != 0 ? desc.width : 640), height_(desc.height != 0 ? desc.height : 480) {
        Make();
    }
    ~NullSwapchain() override { Drop(); }

    SwapchainStatus Acquire() override {
        ENGINE_ASSERT(!acquired_, "Swapchain::Acquire: the previous image was not presented");
        if (width_ == 0 || height_ == 0) {
            return SwapchainStatus::Minimized;
        }
        current_ = (current_ + 1) % kImages;
        acquired_ = true;
        return SwapchainStatus::Ok;
    }
    SwapchainStatus Present() override {
        ENGINE_ASSERT(acquired_, "Swapchain::Present: no image has been acquired");
        acquired_ = false;
        return device_->DeviceLost() ? SwapchainStatus::Lost : SwapchainStatus::Ok;
    }
    SwapchainStatus Resize(u32 width, u32 height) override {
        Drop();
        width_ = width;
        height_ = height;
        if (width == 0 || height == 0) {
            return SwapchainStatus::Minimized;
        }
        Make();
        return SwapchainStatus::Ok;
    }
    Format ImageFormat() const override { return Format::BGRA8_SRGB; }
    u32 Width() const override { return width_; }
    u32 Height() const override { return height_; }
    TextureHandle CurrentTexture() const override { return acquired_ ? images_[current_] : TextureHandle{}; }

private:
    static constexpr u32 kImages = 3;
    void Make() {
        TextureDesc desc;
        desc.width = width_;
        desc.height = height_;
        desc.format = Format::BGRA8_SRGB;
        desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        for (u32 i = 0; i < kImages; ++i) {
            images_[i] = device_->CreateTexture(desc);
        }
        current_ = kImages - 1;
    }
    void Drop() {
        for (u32 i = 0; i < kImages; ++i) {
            if (images_[i].IsValid()) {
                device_->DestroyTexture(images_[i]);
                images_[i] = TextureHandle{};
            }
        }
        acquired_ = false;
    }

    NullDevice* device_;
    u32 width_;
    u32 height_;
    TextureHandle images_[kImages];
    u32 current_ = 0;
    bool acquired_ = false;
};

} // namespace

Swapchain* NullDevice::CreateSwapchain(const SwapchainDesc& desc) {
    State& s = *state_;
    if (s.swapchain != nullptr) {
        s.Error("CreateSwapchain: the device already has a swapchain");
        return nullptr;
    }
    s.swapchain = new NullSwapchain(this, desc);
    return s.swapchain;
}

void NullDevice::DestroySwapchain(Swapchain* swapchain) {
    State& s = *state_;
    if (swapchain == nullptr) {
        return;
    }
    if (swapchain != s.swapchain) {
        s.Error("DestroySwapchain: not this device's swapchain");
        return;
    }
    delete s.swapchain;
    s.swapchain = nullptr;
}

bool NullDevice::FormatSupport(Format format, TextureUsage usage) const {
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count)) {
        return false;
    }
    const FormatProperties& f = FormatInfo(format);
    if (f.is_depth && (Has(usage, TextureUsage::ColorAttachment) || Has(usage, TextureUsage::Storage))) {
        return false;
    }
    if (!f.is_depth && Has(usage, TextureUsage::DepthAttachment)) {
        return false;
    }
    if (f.is_compressed && (Has(usage, TextureUsage::ColorAttachment) || Has(usage, TextureUsage::Storage))) {
        return false;
    }
    return true;
}

bool NullDevice::CanGenerateMips(Format format) const {
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count)) {
        return false;
    }
    const FormatProperties& f = FormatInfo(format);
    return !f.is_depth && !f.is_compressed;
}

UploadTicket NullDevice::UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    TextureRes* t = s.Tex(dst);
    if (t == nullptr) {
        s.Error("UploadTexture: handle is invalid or already destroyed");
        return UploadTicket{0, false};
    }
    if (!Has(t->desc.usage, TextureUsage::CopyDst) || !Has(t->desc.usage, TextureUsage::Sampled)) {
        s.Error("UploadTexture: the texture needs Sampled and CopyDst usage");
        return UploadTicket{0, false};
    }
    if (mip >= t->desc.mip_levels || layer >= t->desc.array_layers || data == nullptr) {
        s.Error("UploadTexture: mip or layer out of range, or data is null");
        return UploadTicket{0, false};
    }
    const FormatProperties& f = FormatInfo(t->desc.format);
    const u32 width = t->desc.width >> mip > 0 ? t->desc.width >> mip : 1;
    const u64 row_bytes = FormatRowBytes(t->desc.format, width);
    if (row_pitch != 0 && (row_pitch < row_bytes || row_pitch % f.bytes_per_block != 0)) {
        s.Error("UploadTexture: row_pitch is smaller than a row or not a multiple of the texel size");
        return UploadTicket{0, false};
    }
    if (t->state != ResourceState::Undefined && t->state != ResourceState::ShaderRead) {
        s.Error("UploadTexture: the texture is in state %s; it must be Undefined or ShaderRead", StateName(t->state));
        return UploadTicket{0, false};
    }
    if (s.lost) {
        return UploadTicket{0, false};
    }
    t->state = ResourceState::ShaderRead;
    return {};
}

bool NullDevice::ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    TextureRes* t = s.Tex(src);
    if (t == nullptr) {
        s.Error("ReadbackTexture: handle is invalid or already destroyed");
        return false;
    }
    if (!Has(t->desc.usage, TextureUsage::CopySrc)) {
        s.Error("ReadbackTexture: the texture was not created with CopySrc usage");
        return false;
    }
    if (mip >= t->desc.mip_levels || layer >= t->desc.array_layers || dst == nullptr) {
        s.Error("ReadbackTexture: mip or layer out of range, or dst is null");
        return false;
    }
    const u32 width = t->desc.width >> mip > 0 ? t->desc.width >> mip : 1;
    const u32 height = t->desc.height >> mip > 0 ? t->desc.height >> mip : 1;
    const u32 depth = t->desc.depth >> mip > 0 ? t->desc.depth >> mip : 1;
    const u64 bytes = FormatSliceBytes(t->desc.format, width, height) * depth;
    if (dst_size < bytes) {
        s.Error("ReadbackTexture: dst is smaller than the level");
        return false;
    }
    if (state == ResourceState::Undefined || t->state != state) {
        s.Error("ReadbackTexture: the texture is in state %s but the call says %s", StateName(t->state), StateName(state));
        return false;
    }
    if (s.lost) {
        return false;
    }
    std::memset(dst, 0, static_cast<usize>(bytes)); // the null device keeps no texel data
    return true;
}

bool NullDevice::ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    BufferRes* b = s.Buf(src);
    if (b == nullptr) {
        s.Error("ReadbackBuffer: handle is invalid or already destroyed");
        return false;
    }
    if (!Has(b->desc.usage, BufferUsage::CopySrc) && b->desc.location != MemoryLocation::GpuToCpu) {
        s.Error("ReadbackBuffer: the buffer was not created with CopySrc usage");
        return false;
    }
    if (dst == nullptr || size > b->desc.size || offset > b->desc.size - size) {
        s.Error("ReadbackBuffer: null destination or a range outside the buffer");
        return false;
    }
    if (s.lost) {
        return false;
    }
    std::memcpy(dst, b->data + offset, static_cast<usize>(size));
    return true;
}

void NullDevice::UnmapBuffer(BufferHandle buffer) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    BufferRes* b = s.Buf(buffer);
    if (b == nullptr) {
        s.Error("UnmapBuffer: handle is invalid or destroyed");
        return;
    }
    if (!b->mapped) {
        s.Error("UnmapBuffer: the buffer is not mapped");
        return;
    }
    b->mapped = false;
}

CommandList* NullDevice::CreateCommandList() {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.in_frame) {
        s.Error("CreateCommandList: no frame is open (call BeginFrame first)");
        return nullptr;
    }
    if (s.lost) {
        return nullptr;
    }
    if (s.lists_in_use == s.list_count) {
        if (s.list_count == s.list_capacity) {
            usize ncap = s.list_capacity == 0 ? 8 : s.list_capacity * 2;
            void* p = std::realloc(s.lists, ncap * sizeof(NullCommandList*));
            ENGINE_ASSERT(p != nullptr, "NullDevice: out of host memory for command lists");
            s.lists = static_cast<NullCommandList**>(p);
            s.list_capacity = ncap;
        }
        s.lists[s.list_count++] = new NullCommandList(&s);
    }
    NullCommandList* list = s.lists[s.lists_in_use++];
    list->Recycle(s.frame);
    return list;
}

FenceValue NullDevice::Submit(CommandList* const* lists, usize count) {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.lost) {
        return 0;
    }
    if (!s.in_frame) {
        s.Error("Submit: no frame is open (call BeginFrame first)");
        return 0;
    }
    if (count > 0 && lists == nullptr) {
        s.Error("Submit: lists is null but count is %u", static_cast<u32>(count));
        return 0;
    }
    // Validate the whole batch first so a bad list does not leave the batch half-executed.
    bool ok = true;
    for (usize i = 0; i < count; ++i) {
        auto* l = static_cast<NullCommandList*>(lists[i]);
        if (l == nullptr || l->Owner() != state_) {
            s.Error("Submit: list %u is null or belongs to another device", static_cast<u32>(i));
            ok = false;
            continue;
        }
        if (l->GetPhase() != NullCommandList::Phase::Ended) {
            s.Error("Submit: list %u was not ended, was already submitted, or is from an earlier frame", static_cast<u32>(i));
            ok = false;
        }
        for (usize j = 0; j < i; ++j) {
            if (lists[j] == lists[i]) {
                s.Error("Submit: list %u appears more than once", static_cast<u32>(i));
                ok = false;
            }
        }
    }
    if (!ok) {
        return 0;
    }
    for (usize i = 0; i < count; ++i) {
        auto* l = static_cast<NullCommandList*>(lists[i]);
        const Vec<Op>& ops = l->Ops();
        for (usize k = 0; k < ops.Size(); ++k) {
            const Op& op = ops[k];
            switch (op.kind) {
            case OpKind::Require: {
                if (op.texture.IsValid()) {
                    TextureRes* t = s.Tex(op.texture);
                    if (t == nullptr) {
                        s.Error("Submit: %s uses a destroyed texture", op.what);
                    } else if ((op.allowed & Bit(t->state)) == 0) {
                        s.Error("Submit: %s: texture is in state %s but the use needs a different state", op.what, StateName(t->state));
                    }
                } else {
                    BufferRes* b = s.Buf(op.buffer);
                    if (b == nullptr) {
                        s.Error("Submit: %s uses a destroyed buffer", op.what);
                    } else if ((op.allowed & Bit(b->state)) == 0) {
                        s.Error("Submit: %s: buffer is in state %s but the use needs a different state", op.what, StateName(b->state));
                    }
                }
                break;
            }
            case OpKind::BarrierBuffer: {
                BufferRes* b = s.Buf(op.buffer);
                if (b == nullptr) {
                    s.Error("Submit: Barrier on a destroyed buffer");
                    break;
                }
                if (b->state != op.before && op.before != ResourceState::Undefined) {
                    s.Error("Submit: Barrier says the buffer is in state %s but it is in state %s", StateName(op.before),
                            StateName(b->state));
                }
                b->state = op.after;
                break;
            }
            case OpKind::BarrierTexture: {
                TextureRes* t = s.Tex(op.texture);
                if (t == nullptr) {
                    s.Error("Submit: Barrier on a destroyed texture");
                    break;
                }
                if (t->state != op.before && op.before != ResourceState::Undefined) {
                    s.Error("Submit: Barrier says the texture is in state %s but it is in state %s", StateName(op.before),
                            StateName(t->state));
                }
                t->state = op.after;
                break;
            }
            case OpKind::CopyBuffer: {
                BufferRes* src = s.Buf(op.buffer);
                BufferRes* dst = s.Buf(op.buffer2);
                if (src == nullptr || dst == nullptr) {
                    s.Error("Submit: CopyBuffer uses a destroyed buffer");
                    break;
                }
                std::memmove(dst->data + op.dst_offset, src->data + op.src_offset, static_cast<usize>(op.size));
                break;
            }
            }
        }
        l->MarkSubmitted();
    }
    return ++s.submitted;
}

bool NullDevice::WaitFence(FenceValue value, u64) {
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->lost && value <= state_->submitted;
}

FenceValue NullDevice::CompletedFence() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->submitted;
}

void NullDevice::BeginFrame() {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.in_frame) {
        s.Error("BeginFrame: the previous frame was not ended");
    }
    ++s.frame;
    s.in_frame = true;
    for (usize i = 0; i < s.lists_in_use; ++i) {
        s.lists[i]->Recycle(s.frame);
    }
    s.lists_in_use = 0;
    s.Collect(s.CompletedFrame());
}

void NullDevice::EndFrame() {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.in_frame) {
        s.Error("EndFrame: no frame is open");
        return;
    }
    s.in_frame = false;
}

u64 NullDevice::FrameNumber() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->frame;
}

void NullDevice::WaitIdle() {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    s.Collect(~0ull);
}

bool NullDevice::DeviceLost() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->lost;
}

void NullDevice::SimulateDeviceLoss() {
    std::lock_guard<std::mutex> lock(state_->mu);
    if (!state_->lost) {
        state_->lost = true;
        RhiLog(LogVerbosity::Error, "NullDevice: device lost (simulated)");
    }
}

u32 NullDevice::ErrorCount() const {
    return state_->errors.load(std::memory_order_relaxed);
}

usize NullDevice::LastError(char* out, usize capacity) const {
    if (capacity == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(state_->err_mu);
    usize len = std::strlen(state_->last_error);
    if (len >= capacity) {
        len = capacity - 1;
    }
    std::memcpy(out, state_->last_error, len);
    out[len] = '\0';
    return len;
}

void NullDevice::ResetErrors() {
    std::lock_guard<std::mutex> lock(state_->err_mu);
    state_->errors.store(0, std::memory_order_relaxed);
    state_->last_error[0] = '\0';
}

NullDeviceStats NullDevice::Statistics() const {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    NullDeviceStats st;
    st.buffers = s.buffers.LiveCount();
    st.textures = s.textures.LiveCount();
    st.samplers = s.samplers.LiveCount();
    st.shaders = s.shaders.LiveCount();
    st.bind_group_layouts = s.layouts.LiveCount();
    st.bind_groups = s.groups.LiveCount();
    st.pipelines = s.pipelines.LiveCount();
    st.pending_destruction = s.buffers.RetiredCount() + s.textures.RetiredCount() + s.samplers.RetiredCount() +
                             s.shaders.RetiredCount() + s.layouts.RetiredCount() + s.groups.RetiredCount() +
                             s.pipelines.RetiredCount();
    st.command_lists = s.lists_in_use;
    return st;
}

bool NullDevice::PeekBuffer(BufferHandle buffer, u64 offset, void* out, u64 size) const {
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    const BufferRes* b = s.buffers.Get(buffer);
    if (b == nullptr || size > b->desc.size || offset > b->desc.size - size) {
        return false;
    }
    std::memcpy(out, b->data + offset, static_cast<usize>(size));
    return true;
}

ResourceState NullDevice::StateOf(BufferHandle buffer) const {
    std::lock_guard<std::mutex> lock(state_->mu);
    const BufferRes* b = state_->buffers.Get(buffer);
    return b != nullptr ? b->state : ResourceState::Undefined;
}

ResourceState NullDevice::StateOf(TextureHandle texture) const {
    std::lock_guard<std::mutex> lock(state_->mu);
    const TextureRes* t = state_->textures.Get(texture);
    return t != nullptr ? t->state : ResourceState::Undefined;
}

} // namespace engine_placeholder::rhi
