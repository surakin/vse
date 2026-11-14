#include "renderer/texture.h"

#include "cstr/format.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer
{

namespace
{

u32 LevelExtent(u32 base, u32 level)
{
    const u32 v = base >> level;
    return v == 0 ? 1 : v;
}

u64 LevelBytes(const rhi::TextureDesc& d, u32 level)
{
    const rhi::FormatProperties& f = rhi::FormatInfo(d.format);
    const u64 bw = (LevelExtent(d.width, level) + f.block_width - 1) / f.block_width;
    const u64 bh = (LevelExtent(d.height, level) + f.block_height - 1) / f.block_height;
    return bw * bh * f.bytes_per_block;
}

void CopyName(char* dst, usize cap, const char* name, const char* fallback)
{
    FormatTo(dst, cap, "%s", name != nullptr ? name : fallback);
}

}  // namespace

// ---- GpuTexture -------------------------------------------------------------------------------------------------

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
{
    MoveFrom(other);
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

void GpuTexture::MoveFrom(GpuTexture& other)
{
    image_ = static_cast<TextureImage&&>(other.image_);
    levels_ = static_cast<Vector<TextureLevelRef>&&>(other.levels_);
    loader_ = other.loader_;
    loader_user_ = other.loader_user_;
    has_source_ = other.has_source_;
    MemCopy(name_, other.name_, sizeof(name_));
    registration_.Adopt(&other.registration_, this);
    device_ = other.device_;
    desc_ = other.desc_;
    desc_.debug_name = name_;
    handle_ = other.handle_;
    other.loader_ = nullptr;
    other.has_source_ = false;
    other.device_ = nullptr;
    other.handle_ = {};
}

bool GpuTexture::Make(rhi::Device* device, const TextureImage& image)
{
    rhi::TextureDesc desc = image.desc;
    desc.usage = desc.usage | rhi::TextureUsage::CopyDst;
    desc.debug_name = name_;
    const char* why = desc.Validate();
    if (why != nullptr)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': %s", name_, why);
        return false;
    }
    const rhi::DeviceCaps& caps = device->Caps();
    if (desc.width > caps.max_texture_dimension_2d || desc.height > caps.max_texture_dimension_2d)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': %ux%u exceeds the device limit of %u", name_, desc.width,
                  desc.height, caps.max_texture_dimension_2d);
        return false;
    }
    // The levels the source holds, and whether the GPU builds the rest.
    const u32 wanted = desc.mip_levels;
    u32 provided = 0;
    u64 offset = 0;
    while (provided < wanted && offset + LevelBytes(desc, provided) <= image.pixels.Size())
    {
        offset += LevelBytes(desc, provided);
        ++provided;
    }
    if (provided == 0)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': the pixels do not cover level 0", name_);
        return false;
    }
    bool generate = false;
    if (provided < wanted)
    {
        generate = image.generate_mips && provided == 1 && wanted == rhi::MaxMipLevels(desc.width, desc.height) &&
                   device->CanGenerateMips(desc.format);
        if (generate)
        {
            desc.usage = desc.usage | rhi::TextureUsage::CopySrc;
        }
        else
        {
            desc.mip_levels = provided;  // fewer levels than asked: documented fallback
        }
    }
    if (!device->FormatSupport(desc.format, desc.usage))
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': the device does not support this format and usage", name_);
        return false;
    }
    const rhi::TextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid())
    {
        return false;
    }
    offset = 0;
    for (u32 level = 0; level < provided; ++level)
    {
        if (!device->UploadTexture(texture, level, 0, image.pixels.Data() + offset, 0).ok)
        {
            device->DestroyTexture(texture);
            return false;
        }
        offset += LevelBytes(desc, level);
    }
    if (generate)
    {
        // Inside an open frame (a caller streaming a texture between BeginFrame and EndFrame) the work joins that frame
        // and is waited for by its fence; otherwise it gets a frame of its own.
        const bool own_frame = !device->InFrame();
        if (own_frame)
        {
            device->BeginFrame();
        }
        rhi::CommandList* list = device->CreateCommandList();
        bool submitted = false;
        rhi::FenceValue fence = 0;
        if (list != nullptr)
        {
            list->Begin();
            list->GenerateMips(texture);
            list->End();
            rhi::CommandList* lists[] = {list};
            fence = device->Submit(lists, 1);
            submitted = fence != 0;
        }
        if (own_frame)
        {
            device->EndFrame();
        }
        if (own_frame)
        {
            device->WaitIdle();
        }
        else if (submitted)
        {
            device->WaitFence(fence, ~0ull);
        }
        if (!submitted)
        {
            device->DestroyTexture(texture);
            return false;
        }
    }
    device_ = device;
    desc_ = desc;
    handle_ = texture;
    return true;
}

bool GpuTexture::MakeFromLevels(rhi::Device* device)
{
    rhi::TextureDesc desc = image_.desc;
    desc.usage = desc.usage | rhi::TextureUsage::CopyDst;
    desc.debug_name = name_;
    const char* why = desc.Validate();
    if (why != nullptr)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': %s", name_, why);
        return false;
    }
    const rhi::DeviceCaps& caps = device->Caps();
    if (desc.width > caps.max_texture_dimension_2d || desc.height > caps.max_texture_dimension_2d)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': %ux%u exceeds the device limit of %u", name_, desc.width,
                  desc.height, caps.max_texture_dimension_2d);
        return false;
    }
    if (!device->FormatSupport(desc.format, desc.usage))
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': the device does not support this format and usage", name_);
        return false;
    }
    const rhi::TextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid())
    {
        return false;
    }
    for (usize i = 0; i < levels_.Size(); ++i)
    {
        const TextureLevelRef& level = levels_[i];
        if (!device->UploadTexture(texture, level.mip, level.layer, level.data, level.row_pitch).ok)
        {
            device->DestroyTexture(texture);
            return false;
        }
    }
    device_ = device;
    desc_ = desc;
    handle_ = texture;
    return true;
}

bool GpuTexture::CreateFromLevels(rhi::Device* device, const rhi::TextureDesc& desc, Span<const TextureLevelRef> levels,
                                  DeviceResources* resources, const char* debug_name)
{
    ENGINE_ASSERT(!Valid() && device != nullptr,
                  "GpuTexture::CreateFromLevels: the texture exists or the device is null");
    CopyName(name_, sizeof(name_), debug_name, "texture");
    const u64 expected = static_cast<u64>(desc.mip_levels) * desc.array_layers;
    if (desc.mip_levels == 0 || desc.array_layers == 0 || levels.Size() != expected)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': %u levels given for %u mips of %u layers", name_,
                  static_cast<unsigned>(levels.Size()), desc.mip_levels, desc.array_layers);
        return false;
    }
    // Every (mip, layer) exactly once.
    Vector<u8> seen;
    if (!seen.TryResize(static_cast<usize>(expected)) || !levels_.TryReserve(levels.Size()))
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': out of memory", name_);
        return false;
    }
    MemSet(seen.Data(), 0, seen.Size());
    for (usize i = 0; i < levels.Size(); ++i)
    {
        const TextureLevelRef& level = levels[i];
        if (level.data == nullptr || level.mip >= desc.mip_levels || level.layer >= desc.array_layers ||
            seen[static_cast<usize>(level.layer) * desc.mip_levels + level.mip] != 0)
        {
            RenderLog(LogVerbosity::Error, "GpuTexture '%s': level %u has no data, is out of range or repeats", name_,
                      static_cast<unsigned>(i));
            levels_.Reset();
            return false;
        }
        seen[static_cast<usize>(level.layer) * desc.mip_levels + level.mip] = 1;
        levels_.Push(level);
    }
    image_ = TextureImage{};
    image_.desc = desc;
    if (!MakeFromLevels(device))
    {
        levels_.Reset();
        image_ = TextureImage{};
        return false;
    }
    has_source_ = true;
    if (resources != nullptr && !registration_.Attach(resources, this, tiers::kTextures))
    {
        Destroy();
        return false;
    }
    return true;
}

bool GpuTexture::Create(rhi::Device* device, TextureImage&& image, DeviceResources* resources, const char* debug_name)
{
    ENGINE_ASSERT(!Valid() && device != nullptr, "GpuTexture::Create: the texture exists or the device is null");
    CopyName(name_, sizeof(name_), debug_name, "texture");
    if (!Make(device, image))
    {
        return false;
    }
    if (resources != nullptr)
    {
        image_ = static_cast<TextureImage&&>(image);  // the pixels are kept only when something can use them
        has_source_ = true;
        if (!registration_.Attach(resources, this, tiers::kTextures))
        {
            Destroy();
            return false;
        }
    }
    return true;
}

bool GpuTexture::CreateFromLoader(rhi::Device* device, TextureLoader loader, void* user, DeviceResources* resources,
                                  const char* debug_name)
{
    ENGINE_ASSERT(!Valid() && device != nullptr && loader != nullptr, "GpuTexture::CreateFromLoader: bad arguments");
    CopyName(name_, sizeof(name_), debug_name, "texture");
    TextureImage image;
    if (!loader(user, &image) || !Make(device, image))
    {
        return false;
    }
    loader_ = loader;
    loader_user_ = user;
    has_source_ = true;
    if (!registration_.Attach(resources, this, tiers::kTextures))
    {
        Destroy();
        return false;
    }
    return true;
}

void GpuTexture::Release()
{
    if (device_ != nullptr && handle_.IsValid())
    {
        device_->DestroyTexture(handle_);
    }
    handle_ = {};
    device_ = nullptr;
}

bool GpuTexture::Recreate(rhi::Device* device)
{
    ENGINE_ASSERT(!Valid(), "GpuTexture::Recreate: the texture still exists");
    if (!has_source_)
    {
        RenderLog(LogVerbosity::Error, "GpuTexture '%s': no source to recreate it from", name_);
        return false;
    }
    if (levels_.Size() != 0)
    {
        return MakeFromLevels(device);
    }
    if (loader_ != nullptr)
    {
        TextureImage image;
        if (!loader_(loader_user_, &image))
        {
            RenderLog(LogVerbosity::Error, "GpuTexture '%s': the loader failed", name_);
            return false;
        }
        return Make(device, image);
    }
    return Make(device, image_);
}

void GpuTexture::Destroy()
{
    registration_.Reset();
    Release();
    image_ = TextureImage{};
    levels_.Reset();
    loader_ = nullptr;
    loader_user_ = nullptr;
    has_source_ = false;
}

// ---- GpuSampler -------------------------------------------------------------------------------------------------

GpuSampler::GpuSampler(GpuSampler&& other) noexcept
{
    MoveFrom(other);
}

GpuSampler& GpuSampler::operator=(GpuSampler&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

void GpuSampler::MoveFrom(GpuSampler& other)
{
    desc_ = other.desc_;
    desc_.debug_name = name_;
    has_source_ = other.has_source_;
    MemCopy(name_, other.name_, sizeof(name_));
    registration_.Adopt(&other.registration_, this);
    device_ = other.device_;
    handle_ = other.handle_;
    other.has_source_ = false;
    other.device_ = nullptr;
    other.handle_ = {};
}

bool GpuSampler::Create(rhi::Device* device, const rhi::SamplerDesc& desc, DeviceResources* resources,
                        const char* debug_name)
{
    ENGINE_ASSERT(!Valid() && device != nullptr, "GpuSampler::Create: the sampler exists or the device is null");
    CopyName(name_, sizeof(name_), debug_name, "sampler");
    desc_ = desc;
    desc_.debug_name = name_;
    const rhi::SamplerHandle sampler = device->CreateSampler(desc_);
    if (!sampler.IsValid())
    {
        return false;
    }
    handle_ = sampler;
    device_ = device;
    has_source_ = true;
    if (!registration_.Attach(resources, this, tiers::kTextures))
    {
        Destroy();
        return false;
    }
    return true;
}

void GpuSampler::Release()
{
    if (device_ != nullptr && handle_.IsValid())
    {
        device_->DestroySampler(handle_);
    }
    handle_ = {};
    device_ = nullptr;
}

bool GpuSampler::Recreate(rhi::Device* device)
{
    ENGINE_ASSERT(!Valid(), "GpuSampler::Recreate: the sampler still exists");
    if (!has_source_)
    {
        return false;
    }
    handle_ = device->CreateSampler(desc_);
    if (!handle_.IsValid())
    {
        return false;
    }
    device_ = device;
    return true;
}

void GpuSampler::Destroy()
{
    registration_.Reset();
    Release();
    has_source_ = false;
}

}  // namespace engine_placeholder::renderer
