#include "engine_c/live_textures.h"

#include "rhi/command_list.h"

namespace vse::engine_c
{

LiveTextures::~LiveTextures()
{
    Release();
}

void LiveTextures::Attach(rhi::Device* device, TextureHost* host)
{
    device_ = device;
    host_ = host;
}

void LiveTextures::Destroy(Live* live)
{
    if (device_ == nullptr)
    {
        return;
    }
    for (u32 i = 0; i < kRing; ++i)
    {
        if (live->staging[i].IsValid())
        {
            if (live->mapped[i] != nullptr)
            {
                device_->UnmapBuffer(live->staging[i]);
            }
            device_->DestroyBuffer(live->staging[i]);
            live->staging[i] = rhi::BufferHandle{};
            live->mapped[i] = nullptr;
        }
    }
    if (live->texture.IsValid())
    {
        device_->DestroyTexture(live->texture);
        live->texture = rhi::TextureHandle{};
    }
    live->uploaded = false;
    live->copied = false;
    live->state_shader_read = false;
    live->seen = 0;
    live->ring = 0;
}

void LiveTextures::Release()
{
    if (device_ != nullptr && lives_.Size() != 0)
    {
        device_->WaitIdle();
    }
    for (usize i = 0; i < lives_.Size(); ++i)
    {
        Destroy(&lives_[i]);
    }
    lives_.Clear();
}

bool LiveTextures::Make(Live* live, u32 width, u32 height)
{
    const u32 alignment = device_->Caps().buffer_texture_copy_row_pitch_alignment;
    const u32 row = width * 4u;
    const u32 pitch = alignment > 1 ? (row + alignment - 1) / alignment * alignment : row;
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    // A host that sends B, G, R, A gets a BGRA texture when the device samples one: no conversion anywhere.
    const rhi::Format bgra = live->source->Srgb() ? rhi::Format::BGRA8_SRGB : rhi::Format::BGRA8_UNorm;
    live->bgra = live->source->Bgra() && device_->FormatSupport(bgra, td.usage);
    td.format = live->bgra ? bgra : (live->source->Srgb() ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNorm);
    live->texture = device_->CreateTexture(td);
    if (!live->texture.IsValid())
    {
        return false;
    }
    for (u32 i = 0; i < kRing; ++i)
    {
        rhi::BufferDesc bd;
        bd.size = static_cast<u64>(pitch) * height;
        bd.usage = rhi::BufferUsage::CopySrc;
        bd.location = rhi::MemoryLocation::CpuToGpu;
        live->staging[i] = device_->CreateBuffer(bd);
        live->mapped[i] = live->staging[i].IsValid() ? static_cast<u8*>(device_->MapBuffer(live->staging[i])) : nullptr;
        if (live->mapped[i] == nullptr)
        {
            Destroy(live);
            return false;
        }
    }
    live->width = width;
    live->height = height;
    live->pitch = pitch;
    live->ring = 0;
    live->seen = 0;
    live->uploaded = false;
    live->state_shader_read = false;
    return true;
}

void LiveTextures::BeginFrame()
{
    for (usize i = 0; i < lives_.Size(); ++i)
    {
        lives_[i].wanted = false;
        lives_[i].copied = false;
    }
    budget_left_ = host_ != nullptr ? host_->Limits().upload_budget : 0;
    first_this_frame_ = true;
}

rhi::TextureHandle LiveTextures::Acquire(u64 handle)
{
    if (device_ == nullptr || host_ == nullptr)
    {
        return rhi::TextureHandle{};
    }
    Live* live = nullptr;
    for (usize i = 0; i < lives_.Size(); ++i)
    {
        if (lives_[i].handle == handle)
        {
            live = &lives_[i];
            break;
        }
    }
    engine_handle given;
    given.value = handle;
    if (live == nullptr)
    {
        HostTexturePtr source = host_->Find(given);
        if (source.Get() == nullptr)
        {
            return rhi::TextureHandle{};  // destroyed
        }
        Live fresh;
        fresh.handle = handle;
        fresh.source = static_cast<HostTexturePtr&&>(source);
        lives_.Push(static_cast<Live&&>(fresh));
        live = &lives_[lives_.Size() - 1];
    }
    const bool first_touch = !live->wanted;
    live->wanted = true;
    if (host_->Find(given).Get() == nullptr)
    {
        return rhi::TextureHandle{};  // destroyed since the last frame: EndFrame drops it
    }
    // The size is decided once per frame, at the first slot that names the texture: a later slot (or a resize from
    // another thread) never destroys what an earlier slot of this frame was given.
    u32 width = live->width;
    u32 height = live->height;
    if (first_touch)
    {
        width = live->source->Width();
        height = live->source->Height();
        if (width == 0 || height == 0)
        {
            return rhi::TextureHandle{};
        }
        if (live->texture.IsValid() && (live->width != width || live->height != height))
        {
            device_->WaitIdle();  // the old texture may be in use by a frame in flight
            Destroy(live);
        }
    }
    else if (!live->texture.IsValid())
    {
        return rhi::TextureHandle{};  // this frame's first slot found no pixels or no room: so do the others
    }
    const u64 bytes = static_cast<u64>(width) * height * 4u;
    const bool fits = first_this_frame_ || bytes <= budget_left_;
    u64 seen = live->texture.IsValid() ? live->seen : 0;
    if (!live->texture.IsValid())
    {
        if (!live->source->HasPixels() || !fits || !Make(live, width, height))
        {
            return rhi::TextureHandle{};
        }
        seen = 0;
    }
    if (fits && !live->copied)
    {
        u32 w = 0;
        u32 h = 0;
        const u32 slot = live->ring % kRing;
        if (live->source->CopyIfNewer(&seen, &w, &h, live->mapped[slot], static_cast<usize>(live->pitch) * live->height,
                                      live->pitch, live->bgra) &&
            w == live->width && h == live->height)  // a resize between the two reads: the next frame remakes it
        {
            live->seen = seen;
            live->ring = slot + 1;
            live->copied = true;
            budget_left_ = bytes <= budget_left_ ? budget_left_ - bytes : 0;
            first_this_frame_ = false;
            ++uploads_;
            bytes_ += bytes;
            host_->Counters().textures_uploaded.FetchAdd(1, MemoryOrder::Relaxed);
            host_->Counters().bytes_uploaded.FetchAdd(bytes, MemoryOrder::Relaxed);
            device_->FlushMappedBuffer(live->staging[slot], 0, static_cast<u64>(live->pitch) * live->height);
        }
    }
    if (!live->uploaded && !live->copied)
    {
        return rhi::TextureHandle{};  // no pixels have reached the device yet
    }
    return live->texture;
}

void LiveTextures::Flush()
{
    bool any = false;
    for (usize i = 0; i < lives_.Size(); ++i)
    {
        any = any || lives_[i].copied;
    }
    if (!any)
    {
        return;
    }
    device_->BeginFrame();
    rhi::CommandList* list = device_->CreateCommandList();
    list->Begin();
    for (usize i = 0; i < lives_.Size(); ++i)
    {
        Live& live = lives_[i];
        if (!live.copied || !live.texture.IsValid() || !live.staging[(live.ring + kRing - 1) % kRing].IsValid())
        {
            live.copied = false;
            continue;
        }
        const u32 slot = (live.ring + kRing - 1) % kRing;
        list->Barrier(live.texture,
                      live.state_shader_read ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined,
                      rhi::ResourceState::CopyDst);
        rhi::BufferTextureCopy region;
        region.buffer_row_pitch = live.pitch;
        list->CopyBufferToTexture(live.staging[slot], live.texture, region);
        list->Barrier(live.texture, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead);
        live.state_shader_read = true;
        live.uploaded = true;
        live.copied = false;
    }
    list->End();
    rhi::CommandList* lists[] = {list};
    device_->Submit(lists, 1);
    device_->EndFrame();
}

void LiveTextures::EndFrame()
{
    for (usize i = 0; i < lives_.Size();)
    {
        if (!lives_[i].wanted)
        {
            device_->WaitIdle();
            Destroy(&lives_[i]);
            if (i != lives_.Size() - 1)
            {
                lives_[i] = static_cast<Live&&>(lives_[lives_.Size() - 1]);
            }
            lives_.Pop();
        }
        else
        {
            ++i;
        }
    }
}

void LiveTextures::Revert()
{
    if (host_ != nullptr)
    {
        host_->Counters().slots_reverted.FetchAdd(1, MemoryOrder::Relaxed);
    }
}

}  // namespace vse::engine_c
