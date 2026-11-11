#pragma once

#include "containers/vector.h"
#include "renderer/device_resources.h"
#include "rhi/rhi.h"
#include "text/glyph_atlas.h"
#include "types.h"

//! The GPU copy of a glyph atlas: one 8-bit coverage texture per CPU page, kept up to date and rebuilt after a device reset.

namespace engine_placeholder::renderer {

/// One R8 texture for each page of a [[text::GlyphAtlas]].
///
/// The atlas (CPU side) is the source of truth: [[renderer::GlyphAtlasTexture::Sync]] creates a texture for each page the atlas has gained and re-sends a page when the atlas
/// reports that it changed. A changed page is sent whole, because the RHI uploads a full mip level at a time (a sub-rectangle upload is on `KNOWN-ISSUES.md`); with the default
/// 1024-pixel pages that is one megabyte per changed page per frame at most, and nothing for a page that did not change. The object registers with a
/// [[renderer::DeviceResources]] at the texture tier; after a reset `Recreate` makes every page again from the atlas, so no glyph is lost. The handles change then:
/// read [[renderer::GlyphAtlasTexture::Page]] each frame, never keep one.
///
/// - Thread-safety: owner-thread-only, and not safe to use while another thread adds glyphs to the atlas.
/// - Ownership: borrows the atlas, which must outlive this object; owns the textures. The device and the registry must outlive it.
class GlyphAtlasTexture final : public Recreatable {
public:
    GlyphAtlasTexture() = default;
    /// Releases everything this object owns.
    ~GlyphAtlasTexture() override { Destroy(); }
    GlyphAtlasTexture(const GlyphAtlasTexture&) = delete;
    GlyphAtlasTexture& operator=(const GlyphAtlasTexture&) = delete;

    /// Creates the textures for the pages the atlas has now (none for an atlas that has no page yet) and sends their pixels.
    ///
    /// - Param device: the device.
    /// - Param atlas: the CPU atlas; borrowed, must outlive this object.
    /// - Param resources: the registry, or null for an object that does not need to survive a reset.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if the device does not support an 8-bit sampled texture of the page size, the page size exceeds the device limit, or an upload
    ///   fails; nothing stays allocated.
    /// - Aborts: the object already exists, or `device` or `atlas` is null.
    /// - Ownership: the device and the atlas are borrowed; nothing is kept beyond that.
    bool Create(rhi::Device* device, text::GlyphAtlas* atlas, DeviceResources* resources);

    /// Destroys the textures and forgets the atlas and the registration.
    void Destroy();

    /// Brings the GPU pages up to date: creates textures for new pages and sends every page that changed since the last call.
    ///
    /// Call it before recording the draw that reads the pages (it uploads outside the command lists, in order with earlier submitted work).
    ///
    /// - Returns: true when every page is current.
    /// - Fails: returns false, logging the reason, if a texture cannot be made or an upload fails; the page stays marked as stale and the next call retries it.
    /// - Aborts: the object does not hold its textures (it was never created, or the device was released and not yet recreated).
    bool Sync();

    /// Returns the number of pages on the GPU.
    /// - Fails: never.
    u32 PageCount() const { return static_cast<u32>(pages_.Size()); }

    /// Returns the texture of a page.
    ///
    /// - Param page: below [[renderer::GlyphAtlasTexture::PageCount]].
    /// - Returns: the current handle; it changes after a reset, so read it each frame.
    /// - Aborts: `page` is out of range.
    rhi::TextureHandle Page(u32 page) const;

    /// Returns how many page uploads `Sync` and `Recreate` have made so far.
    /// - Fails: never.
    u64 UploadCount() const { return uploads_; }

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr; }
    const char* RecreatableName() const override { return "glyph atlas texture"; }

private:
    bool MakePage(u32 page);
    bool Upload(u32 page);
    void DestroyPages();

    text::GlyphAtlas* atlas_ = nullptr;
    rhi::Device* device_ = nullptr;
    Vector<rhi::TextureHandle> pages_;
    Vector<u8> stale_; // 1: the page's pixels must be sent whole (new page, failed upload, device recreated)
    u64 uploads_ = 0;
    ScopedRegistration registration_;
};

} // namespace engine_placeholder::renderer
