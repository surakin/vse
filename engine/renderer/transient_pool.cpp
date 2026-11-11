#include "renderer/transient_pool.h"

#include "lang/new.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer
{

namespace
{

// Two descriptions match when every field but the debug name is equal. Keep these in step with rhi::TextureDesc and
// rhi::BufferDesc: a field added there and left out here would let the pool hand out a resource built differently.
bool SameTexture(const rhi::TextureDesc& a, const rhi::TextureDesc& b)
{
    return a.dimension == b.dimension && a.width == b.width && a.height == b.height && a.depth == b.depth &&
           a.mip_levels == b.mip_levels && a.array_layers == b.array_layers && a.format == b.format &&
           a.usage == b.usage && a.samples == b.samples;
}

bool SameBuffer(const rhi::BufferDesc& a, const rhi::BufferDesc& b)
{
    return a.size == b.size && a.usage == b.usage && a.location == b.location;
}

}  // namespace

struct TransientPool::TextureEntry
{
    rhi::TextureDesc desc;
    rhi::TextureHandle handle;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    bool in_use = false;
    u64 last_used = 0;
};

struct TransientPool::BufferEntry
{
    rhi::BufferDesc desc;
    rhi::BufferHandle handle;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    bool in_use = false;
    u64 last_used = 0;
};

TransientPool::TransientPool(rhi::Device* device, const TransientPoolDesc& desc) : device_(device), desc_(desc)
{
    ENGINE_ASSERT(device != nullptr, "TransientPool: the device is null");
    ENGINE_ASSERT(desc.max_textures > 0 && desc.max_buffers > 0, "TransientPool: a capacity is 0");
    textures_.Resize(desc.max_textures);
    buffers_.Resize(desc.max_buffers);
}

TransientPool::~TransientPool()
{
    registration_.Reset();
    for (u32 i = 0; i < texture_count_; ++i)
    {
        device_->DestroyTexture(textures_[i].handle);
    }
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        device_->DestroyBuffer(buffers_[i].handle);
    }
}

void TransientPool::Release()
{
    if (released_)
    {
        return;
    }
    for (u32 i = 0; i < texture_count_; ++i)
    {
        ENGINE_ASSERT(!textures_[i].in_use, "TransientPool::Release: a texture is still in use");
        device_->DestroyTexture(textures_[i].handle);
        textures_[i] = TextureEntry{};
    }
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        ENGINE_ASSERT(!buffers_[i].in_use, "TransientPool::Release: a buffer is still in use");
        device_->DestroyBuffer(buffers_[i].handle);
        buffers_[i] = BufferEntry{};
    }
    texture_count_ = 0;
    buffer_count_ = 0;
    stats_.textures_live = stats_.textures_idle = 0;
    stats_.buffers_live = stats_.buffers_idle = 0;
    ++stats_.resets;
    released_ = true;
}

bool TransientPool::Recreate(rhi::Device* device)
{
    device_ = device;
    released_ = false;
    return true;
}

void TransientPool::BeginFrame(u64 frame_index)
{
    frame_ = frame_index;
}

void TransientPool::EndFrame()
{
    for (u32 i = 0; i < texture_count_;)
    {
        TextureEntry& t = textures_[i];
        if (!t.in_use && frame_ > t.last_used && frame_ - t.last_used > desc_.max_idle_frames)
        {
            device_->DestroyTexture(t.handle);
            t = textures_[--texture_count_];
            textures_[texture_count_] = TextureEntry{};
            ++stats_.textures_destroyed;
        }
        else
        {
            ++i;
        }
    }
    for (u32 i = 0; i < buffer_count_;)
    {
        BufferEntry& b = buffers_[i];
        if (!b.in_use && frame_ > b.last_used && frame_ - b.last_used > desc_.max_idle_frames)
        {
            device_->DestroyBuffer(b.handle);
            b = buffers_[--buffer_count_];
            buffers_[buffer_count_] = BufferEntry{};
            ++stats_.buffers_destroyed;
        }
        else
        {
            ++i;
        }
    }
}

bool TransientPool::AcquireTexture(const rhi::TextureDesc& desc, PooledTexture* out)
{
    ENGINE_ASSERT(out != nullptr, "TransientPool::AcquireTexture: out is null");
    *out = PooledTexture{};
    for (u32 i = 0; i < texture_count_; ++i)
    {
        TextureEntry& t = textures_[i];
        if (!t.in_use && SameTexture(t.desc, desc))
        {
            t.in_use = true;
            t.last_used = frame_;
            ++stats_.texture_reuses;
            out->handle = t.handle;
            out->state = t.state;
            return true;
        }
    }
    if (texture_count_ == desc_.max_textures)
    {
        // Full: make room by destroying the idle texture unused for longest.
        u32 oldest = texture_count_;
        for (u32 i = 0; i < texture_count_; ++i)
        {
            if (!textures_[i].in_use &&
                (oldest == texture_count_ || textures_[i].last_used < textures_[oldest].last_used))
            {
                oldest = i;
            }
        }
        if (oldest == texture_count_)
        {
            RenderLog(LogVerbosity::Error, "TransientPool: no room for another texture (max_textures %u)",
                      desc_.max_textures);
            return false;
        }
        device_->DestroyTexture(textures_[oldest].handle);
        textures_[oldest] = textures_[--texture_count_];
        textures_[texture_count_] = TextureEntry{};
        ++stats_.textures_destroyed;
    }
    const rhi::TextureHandle handle = device_->CreateTexture(desc);
    if (!handle.IsValid())
    {
        RenderLog(LogVerbosity::Error, "TransientPool: the device could not create texture '%s'",
                  desc.debug_name != nullptr ? desc.debug_name : "");
        return false;
    }
    TextureEntry& t = textures_[texture_count_++];
    t.desc = desc;
    t.desc.debug_name = nullptr;
    t.handle = handle;
    t.state = rhi::ResourceState::Undefined;
    t.in_use = true;
    t.last_used = frame_;
    ++stats_.textures_created;
    out->handle = handle;
    out->state = t.state;
    return true;
}

void TransientPool::ReleaseTexture(rhi::TextureHandle texture, rhi::ResourceState state)
{
    for (u32 i = 0; i < texture_count_; ++i)
    {
        TextureEntry& t = textures_[i];
        if (t.handle == texture)
        {
            ENGINE_ASSERT(t.in_use, "TransientPool::ReleaseTexture: the texture is not in use");
            t.in_use = false;
            t.state = state;
            t.last_used = frame_;
            return;
        }
    }
    ENGINE_ASSERT(false, "TransientPool::ReleaseTexture: the texture does not belong to this pool");
}

bool TransientPool::AcquireBuffer(const rhi::BufferDesc& desc, PooledBuffer* out)
{
    ENGINE_ASSERT(out != nullptr, "TransientPool::AcquireBuffer: out is null");
    *out = PooledBuffer{};
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        BufferEntry& b = buffers_[i];
        if (!b.in_use && SameBuffer(b.desc, desc))
        {
            b.in_use = true;
            b.last_used = frame_;
            ++stats_.buffer_reuses;
            out->handle = b.handle;
            out->state = b.state;
            return true;
        }
    }
    if (buffer_count_ == desc_.max_buffers)
    {
        u32 oldest = buffer_count_;
        for (u32 i = 0; i < buffer_count_; ++i)
        {
            if (!buffers_[i].in_use && (oldest == buffer_count_ || buffers_[i].last_used < buffers_[oldest].last_used))
            {
                oldest = i;
            }
        }
        if (oldest == buffer_count_)
        {
            RenderLog(LogVerbosity::Error, "TransientPool: no room for another buffer (max_buffers %u)",
                      desc_.max_buffers);
            return false;
        }
        device_->DestroyBuffer(buffers_[oldest].handle);
        buffers_[oldest] = buffers_[--buffer_count_];
        buffers_[buffer_count_] = BufferEntry{};
        ++stats_.buffers_destroyed;
    }
    const rhi::BufferHandle handle = device_->CreateBuffer(desc);
    if (!handle.IsValid())
    {
        RenderLog(LogVerbosity::Error, "TransientPool: the device could not create buffer '%s'",
                  desc.debug_name != nullptr ? desc.debug_name : "");
        return false;
    }
    BufferEntry& b = buffers_[buffer_count_++];
    b.desc = desc;
    b.desc.debug_name = nullptr;
    b.handle = handle;
    b.state = rhi::ResourceState::Undefined;
    b.in_use = true;
    b.last_used = frame_;
    ++stats_.buffers_created;
    out->handle = handle;
    out->state = b.state;
    return true;
}

void TransientPool::ReleaseBuffer(rhi::BufferHandle buffer, rhi::ResourceState state)
{
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        BufferEntry& b = buffers_[i];
        if (b.handle == buffer)
        {
            ENGINE_ASSERT(b.in_use, "TransientPool::ReleaseBuffer: the buffer is not in use");
            b.in_use = false;
            b.state = state;
            b.last_used = frame_;
            return;
        }
    }
    ENGINE_ASSERT(false, "TransientPool::ReleaseBuffer: the buffer does not belong to this pool");
}

void TransientPool::SetTextureState(rhi::TextureHandle texture, rhi::ResourceState state)
{
    for (u32 i = 0; i < texture_count_; ++i)
    {
        if (textures_[i].handle == texture && !textures_[i].in_use)
        {
            textures_[i].state = state;
        }
    }
}

void TransientPool::SetBufferState(rhi::BufferHandle buffer, rhi::ResourceState state)
{
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        if (buffers_[i].handle == buffer && !buffers_[i].in_use)
        {
            buffers_[i].state = state;
        }
    }
}

TransientPoolStats TransientPool::Stats() const
{
    TransientPoolStats s = stats_;
    s.textures_live = texture_count_;
    s.buffers_live = buffer_count_;
    s.textures_idle = 0;
    s.buffers_idle = 0;
    for (u32 i = 0; i < texture_count_; ++i)
    {
        s.textures_idle += textures_[i].in_use ? 0 : 1;
    }
    for (u32 i = 0; i < buffer_count_; ++i)
    {
        s.buffers_idle += buffers_[i].in_use ? 0 : 1;
    }
    return s;
}

}  // namespace engine_placeholder::renderer
