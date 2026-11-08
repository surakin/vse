#include "renderer/glyph_atlas_texture.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

rhi::TextureHandle GlyphAtlasTexture::Page(u32 page) const {
    ENGINE_ASSERT(page < pages_.Size(), "GlyphAtlasTexture::Page: the page is out of range");
    return pages_[page];
}

bool GlyphAtlasTexture::MakePage(u32 page) {
    ENGINE_ASSERT(page == pages_.Size(), "GlyphAtlasTexture::MakePage: pages are made in order");
    rhi::TextureDesc desc;
    desc.width = atlas_->PageSize();
    desc.height = atlas_->PageSize();
    desc.format = rhi::Format::R8_UNorm;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    desc.debug_name = "glyph atlas page";
    const rhi::TextureHandle texture = device_->CreateTexture(desc);
    if (!texture.IsValid()) {
        RenderLog(LogVerbosity::Error, "GlyphAtlasTexture: the device could not make page %u (%ux%u)", page, desc.width, desc.height);
        return false;
    }
    pages_.Push(texture);
    stale_.Push(1);
    return true;
}

bool GlyphAtlasTexture::Upload(u32 page) {
    text::Rect changed;
    atlas_->TakeDirty(page, &changed); // whole page goes either way; this marks the atlas page clean
    const ByteSpan pixels = atlas_->PagePixels(page);
    if (!device_->UploadTexture(pages_[page], 0, 0, pixels.Data(), 0).ok) {
        RenderLog(LogVerbosity::Error, "GlyphAtlasTexture: the upload of page %u failed", page);
        stale_[page] = 1;
        return false;
    }
    stale_[page] = 0;
    ++uploads_;
    return true;
}

void GlyphAtlasTexture::DestroyPages() {
    if (device_ != nullptr) {
        for (usize i = 0; i < pages_.Size(); ++i) {
            if (pages_[i].IsValid()) {
                device_->DestroyTexture(pages_[i]);
            }
        }
    }
    pages_.Clear();
    stale_.Clear();
}

bool GlyphAtlasTexture::Create(rhi::Device* device, text::GlyphAtlas* atlas, DeviceResources* resources) {
    ENGINE_ASSERT(device != nullptr && atlas != nullptr && device_ == nullptr, "GlyphAtlasTexture::Create: bad arguments or the object exists");
    atlas_ = atlas;
    if (!Recreate(device)) {
        atlas_ = nullptr;
        return false;
    }
    if (resources != nullptr && !registration_.Attach(resources, this, tiers::kTextures)) {
        Destroy();
        return false;
    }
    return true;
}

void GlyphAtlasTexture::Destroy() {
    registration_.Reset();
    Release();
    atlas_ = nullptr;
    uploads_ = 0;
}

void GlyphAtlasTexture::Release() {
    DestroyPages();
    device_ = nullptr;
}

bool GlyphAtlasTexture::Recreate(rhi::Device* device) {
    ENGINE_ASSERT(device_ == nullptr && atlas_ != nullptr, "GlyphAtlasTexture::Recreate: the textures still exist or there is no atlas");
    const rhi::DeviceCaps& caps = device->Caps();
    const u32 size = atlas_->PageSize();
    if (size > caps.max_texture_dimension_2d) {
        RenderLog(LogVerbosity::Error, "GlyphAtlasTexture: the page size %u exceeds the device limit of %u", size, caps.max_texture_dimension_2d);
        return false;
    }
    if (!device->FormatSupport(rhi::Format::R8_UNorm, rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst)) {
        RenderLog(LogVerbosity::Error, "GlyphAtlasTexture: the device cannot sample an 8-bit texture that is copied into");
        return false;
    }
    device_ = device;
    for (u32 page = 0; page < atlas_->PageCount(); ++page) {
        if (!MakePage(page) || !Upload(page)) {
            Release();
            return false;
        }
    }
    return true;
}

bool GlyphAtlasTexture::Sync() {
    ENGINE_ASSERT(device_ != nullptr, "GlyphAtlasTexture::Sync: the object does not hold its textures");
    bool ok = true;
    while (pages_.Size() < atlas_->PageCount()) {
        if (!MakePage(static_cast<u32>(pages_.Size()))) {
            return false;
        }
    }
    for (u32 page = 0; page < atlas_->PageCount(); ++page) {
        text::Rect changed;
        // TakeDirty marks the page clean, so remember a change before trying to send it.
        if (atlas_->TakeDirty(page, &changed)) {
            stale_[page] = 1;
        }
        if (stale_[page] != 0) {
            ok = Upload(page) && ok;
        }
    }
    return ok;
}

} // namespace engine_placeholder::renderer
