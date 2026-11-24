#include "engine_c/host_texture.h"

#include "cstr/mem_ops.h"
#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/runtime.h"
#include "settings/desc.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "sync/mutex.h"

namespace vse::engine_c
{

namespace
{

constexpr usize kMaxStride = usize{16} << 20;
constexpr usize kMaxTextures = 1u << 12;

}  // namespace

HostTexture::HostTexture(u32 width, u32 height, bool srgb, StringView, const HostTextureLimits& limits,
                         TextureCounters* counters)
    : width_(width), height_(height), srgb_(srgb), limits_(limits), counters_(counters)
{
}

bool HostTexture::HasPixels() const
{
    LockGuard<Mutex> lock(mutex_);
    return has_pixels_;
}

u32 HostTexture::Width() const
{
    LockGuard<Mutex> lock(mutex_);
    return width_;
}

u32 HostTexture::Height() const
{
    LockGuard<Mutex> lock(mutex_);
    return height_;
}

u64 HostTexture::Version() const
{
    LockGuard<Mutex> lock(mutex_);
    return version_;
}

engine_status HostTexture::Update(const void* pixels, usize stride, u32 width, u32 height, u32 layout)
{
    if (pixels == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`pixels` is NULL");
    }
    if ((layout & ~(ENGINE_UPDATE_BGRA | ENGINE_UPDATE_IGNORE_ALPHA)) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_update_info.flags has an unknown bit");
    }
    LockGuard<Mutex> lock(mutex_);
    const u32 w = width != 0 ? width : width_;
    const u32 h = height != 0 ? height : height_;
    if (w > limits_.max_size || h > limits_.max_size)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the texture is larger than assets.host_texture.max_size");
    }
    const usize row = static_cast<usize>(w) * 4u;
    if (stride < row)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the stride is smaller than a row of pixels");
    }
    if (stride > kMaxStride)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the stride is above 16 MiB");
    }
    const usize bytes = row * h;
    if (pixels_.Size() != bytes)
    {
        pixels_.Resize(bytes);
    }
    const u8* source = static_cast<const u8*>(pixels);
    const bool opaque = (layout & ENGINE_UPDATE_IGNORE_ALPHA) != 0;
    for (u32 y = 0; y < h; ++y)
    {
        u8* dst = pixels_.Data() + static_cast<usize>(y) * row;
        const u8* src = source + static_cast<usize>(y) * stride;
        MemCopy(dst, src, row);  // the bytes stay in the order the host sent
        if (opaque)
        {
            for (u32 x = 0; x < w; ++x)
            {
                dst[static_cast<usize>(x) * 4u + 3u] = 255;
            }
        }
    }
    if (has_pixels_ && !consumed_ && counters_ != nullptr)
    {
        counters_->updates_replaced.FetchAdd(1, MemoryOrder::Relaxed);
    }
    width_ = w;
    height_ = h;
    bgra_ = (layout & ENGINE_UPDATE_BGRA) != 0;
    has_pixels_ = true;
    consumed_ = false;
    ++version_;
    return ENGINE_OK;
}

engine_status HostTexture::Resize(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a texture has a width and a height");
    }
    LockGuard<Mutex> lock(mutex_);
    if (width > limits_.max_size || height > limits_.max_size)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the texture is larger than assets.host_texture.max_size");
    }
    width_ = width;
    height_ = height;
    pixels_.Clear();
    has_pixels_ = false;
    consumed_ = true;
    ++version_;
    return ENGINE_OK;
}

bool HostTexture::Bgra() const
{
    LockGuard<Mutex> lock(mutex_);
    return bgra_;
}

bool HostTexture::CopyIfNewer(u64* seen, u32* width, u32* height, u8* dst, usize dst_capacity, usize dst_pitch,
                              bool want_bgra)
{
    LockGuard<Mutex> lock(mutex_);
    *width = width_;
    *height = height_;
    if (version_ == *seen || !has_pixels_)
    {
        return false;
    }
    const usize row = static_cast<usize>(width_) * 4u;
    if (dst_pitch < row || dst_capacity < dst_pitch * height_)
    {
        return false;
    }
    const bool swap = bgra_ != want_bgra;
    for (u32 y = 0; y < height_; ++y)
    {
        u8* out = dst + static_cast<usize>(y) * dst_pitch;
        const u8* in = pixels_.Data() + static_cast<usize>(y) * row;
        if (!swap)
        {
            MemCopy(out, in, row);
            continue;
        }
        for (u32 x = 0; x < width_; ++x)  // the device texture is in the other order: swap while copying
        {
            const usize k = static_cast<usize>(x) * 4u;
            out[k + 0] = in[k + 2];
            out[k + 1] = in[k + 1];
            out[k + 2] = in[k + 0];
            out[k + 3] = in[k + 3];
        }
    }
    *seen = version_;
    consumed_ = true;
    return true;
}

bool HostTexture::Read(Vector<u8>* out, u32* width, u32* height) const
{
    LockGuard<Mutex> lock(mutex_);
    *width = width_;
    *height = height_;
    if (!has_pixels_)
    {
        return false;
    }
    out->Resize(pixels_.Size());
    MemCopy(out->Data(), pixels_.Data(), pixels_.Size());
    if (bgra_)
    {
        for (usize k = 0; k + 3 < out->Size(); k += 4)  // the read-back is always R, G, B, A
        {
            const u8 b = (*out)[k];
            (*out)[k] = (*out)[k + 2];
            (*out)[k + 2] = b;
        }
    }
    return true;
}

// ---- the table

engine_status TextureHost::Create(const engine_texture_info* given, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = engine_handle{};
    if (given == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    if (StructSizeOf(given) < sizeof(engine_texture_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_texture_info.struct_size is smaller than the struct");
    }
    if ((given->flags & ~ENGINE_TEXTURE_SRGB) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_texture_info.flags has an unknown bit");
    }
    if (given->width == 0 || given->height == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a texture has a width and a height");
    }
    if (given->width > limits_.max_size || given->height > limits_.max_size)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the texture is larger than assets.host_texture.max_size");
    }
    if (given->name.data == nullptr && given->name.length != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    HostTexturePtr texture = MakeShared<HostTexture>(
        given->width, given->height, (given->flags & ENGINE_TEXTURE_SRGB) != 0,
        StringView(given->name.data, given->name.length), limits_, &counters_);
    LockGuard<Mutex> lock(mutex_);
    usize slot = 0;
    for (; slot < slots_.Size(); ++slot)
    {
        if (slots_[slot].object.Get() == nullptr)
        {
            break;
        }
    }
    if (slot == slots_.Size())
    {
        if (slot >= kMaxTextures)
        {
            EngineObjectRelease();
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many textures");
        }
        slots_.Push(Slot());
    }
    slots_[slot].generation = NextGeneration();
    slots_[slot].object = static_cast<HostTexturePtr&&>(texture);
    *out = MakeObjectHandle(kKindTexture, slots_[slot].generation, slot);
    return ENGINE_OK;
}

HostTexturePtr TextureHost::Find(engine_handle handle)
{
    LockGuard<Mutex> lock(mutex_);
    Slot* slot = nullptr;
    if (ResolveSlot(slots_, handle, kKindTexture, "texture", &slot) != ENGINE_OK)
    {
        return HostTexturePtr();
    }
    return slot->object;
}

engine_status TextureHost::Update(engine_handle texture, const void* pixels, size_t stride,
                                  const engine_update_info* info)
{
    u32 width = 0;
    u32 height = 0;
    u32 layout = 0;
    if (info != nullptr)
    {
        if (StructSizeOf(info) < sizeof(engine_update_info))
        {
            return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_update_info.struct_size is smaller than the struct");
        }
        layout = info->flags;
        width = info->width;
        height = info->height;
    }
    HostTexturePtr found;
    {
        LockGuard<Mutex> lock(mutex_);
        Slot* slot = nullptr;
        const engine_status status = ResolveSlot(slots_, texture, kKindTexture, "texture", &slot);
        if (status != ENGINE_OK)
        {
            return status;
        }
        found = slot->object;
    }
    return found->Update(pixels, stride, width, height, layout);  // outside the table's lock: the copy can take a while
}

engine_status TextureHost::Resize(engine_handle texture, u32 width, u32 height)
{
    HostTexturePtr found = Find(texture);
    if (found.Get() == nullptr)
    {
        LockGuard<Mutex> lock(mutex_);
        Slot* slot = nullptr;
        return ResolveSlot(slots_, texture, kKindTexture, "texture", &slot);  // the failure and its text
    }
    return found->Resize(width, height);
}

engine_status TextureHost::Destroy(engine_handle texture)
{
    HostTexturePtr dying;
    {
        LockGuard<Mutex> lock(mutex_);
        Slot* slot = nullptr;
        const engine_status status = ResolveSlot(slots_, texture, kKindTexture, "texture", &slot);
        if (status != ENGINE_OK)
        {
            return status;
        }
        dying = static_cast<HostTexturePtr&&>(slot->object);
        slot->object = HostTexturePtr();
    }
    dying = HostTexturePtr();  // a surface that draws it still holds it until its next frame
    EngineObjectRelease();
    return ENGINE_OK;
}

engine_status TextureHost::ReadPixels(engine_handle texture, engine_buffer* out, u32* width, u32* height)
{
    if (out == nullptr || width == nullptr || height == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out`, `width` or `height` is NULL");
    }
    out->data = nullptr;
    out->size = 0;
    out->owner = nullptr;
    out->release = nullptr;
    *width = 0;
    *height = 0;
    HostTexturePtr found = Find(texture);
    if (found.Get() == nullptr)
    {
        LockGuard<Mutex> lock(mutex_);
        Slot* slot = nullptr;
        return ResolveSlot(slots_, texture, kKindTexture, "texture", &slot);
    }
    Vector<u8> copy;
    if (!found->Read(&copy, width, height))
    {
        return Fail(ENGINE_ERROR_NOT_READY, "the texture has no pixels yet");
    }
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(copy.Data()), copy.Size()), out);
    return ENGINE_OK;
}

// ---- settings and the table's functions

namespace
{

void Declare(settings::Registry* registry, const char* key, i64 value, i64 minimum, i64 maximum, const char* env,
             const char* flag, const char* label, const char* description)
{
    if (registry->Find(StringView(key)).IsValid())
    {
        return;
    }
    settings::SettingDesc d = settings::MakeInt(key, value, minimum, maximum);
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "assets";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

i64 Read(const settings::Registry& registry, const char* key, i64 fallback)
{
    if (!registry.Find(StringView(key)).IsValid())
    {
        return fallback;
    }
    return settings::Setting<i64>::Find(registry, StringView(key)).Get();
}

}  // namespace

void RegisterHostTextureSettings(settings::Registry* registry)
{
    const HostTextureLimits d;
    Declare(registry, "assets.host_texture.max_size", d.max_size, 1, 65536, "ENGINE_ASSETS_HOST_TEXTURE_MAX_SIZE",
            "assets-host-texture-max-size", "Host texture: largest width or height",
            "the most pixels a host texture has along one side");
    Declare(registry, "assets.host_texture.upload_budget", static_cast<i64>(d.upload_budget), 1 << 16, i64(1) << 32,
            "ENGINE_ASSETS_HOST_TEXTURE_UPLOAD_BUDGET", "assets-host-texture-upload-budget",
            "Host texture: bytes uploaded per frame",
            "the most bytes of host textures one surface uploads in one frame (a texture that does not fit waits)");
}

HostTextureLimits ReadHostTextureSettings(const settings::Registry& registry)
{
    HostTextureLimits limits;
    limits.max_size = static_cast<u32>(Read(registry, "assets.host_texture.max_size", limits.max_size));
    limits.upload_budget = static_cast<u64>(
        Read(registry, "assets.host_texture.upload_budget", static_cast<i64>(limits.upload_budget)));
    return limits;
}

engine_status TextureCreateFn(engine_handle engine, const engine_texture_info* info, engine_handle* out)
{
    Runtime* runtime = nullptr;
    if (out != nullptr)
    {
        *out = engine_handle{};
    }
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    return runtime->textures.Create(info, out);
}

namespace
{

TextureHost* CurrentTextures()
{
    Runtime* runtime = CurrentRuntime();
    return runtime == nullptr ? nullptr : &runtime->textures;
}

}  // namespace

engine_status TextureUpdateFn(engine_handle texture, const void* pixels, size_t stride, const engine_update_info* info)
{
    TextureHost* textures = CurrentTextures();
    return textures == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists")
                               : textures->Update(texture, pixels, stride, info);
}

engine_status TextureFormatSupportFn(engine_handle engine, u32 flags, u32* out_support)
{
    Runtime* runtime = nullptr;
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    if (out_support == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_support` is NULL");
    }
    if ((flags & ~(ENGINE_UPDATE_BGRA | ENGINE_UPDATE_IGNORE_ALPHA)) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an unknown layout flag");
    }
    // The device texture is made RGBA8 or BGRA8 as the update's bytes are, and Vulkan, Direct3D 12 and Metal sample
    // both as they are: those two layouts are native. Ignoring the alpha byte is a pass over the rows in the update
    // call (the engine writes 255), so that costs a little.
    *out_support = (flags & ENGINE_UPDATE_IGNORE_ALPHA) != 0 ? ENGINE_FORMAT_CONVERTED : ENGINE_FORMAT_NATIVE;
    return ENGINE_OK;
}

engine_status TextureResizeFn(engine_handle texture, u32 width, u32 height)
{
    TextureHost* textures = CurrentTextures();
    return textures == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists")
                               : textures->Resize(texture, width, height);
}

engine_status TextureDestroyFn(engine_handle texture)
{
    TextureHost* textures = CurrentTextures();
    return textures == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists") : textures->Destroy(texture);
}

engine_status TextureReadPixelsFn(engine_handle texture, engine_buffer* out, u32* width, u32* height)
{
    TextureHost* textures = CurrentTextures();
    return textures == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists")
                               : textures->ReadPixels(texture, out, width, height);
}

}  // namespace vse::engine_c
