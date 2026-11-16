#pragma once

//! Textures the host fills with its own pixels (Phase 24 Days 365 and 366): a video frame, a canvas, a remote screen.
//! A [[engine_c::HostTexture]] keeps the newest pixels on the CPU (the source every device uploads from and rebuilds
//! from after a reset); [[engine_c::TextureHost]] is the table of them behind the handles of the C API. The upload to a
//! device belongs to the surface that draws the texture (`surface_scene.cpp`). Internal to the module `engine_c`.
#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "string/string.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

/// What the textures of an engine did since the last frame took the counts.
///
/// - Thread-safety: thread-safe (atomics).
struct TextureCounters
{
    Atomic<u64> updates_replaced{0};   ///< Updates that arrived before the previous one was uploaded by any device.
    Atomic<u64> textures_uploaded{0};  ///< Uploads made by the surfaces.
    Atomic<u64> bytes_uploaded{0};     ///< Bytes of those uploads.
    Atomic<u64> slots_reverted{0};     ///< Slot bindings that fell back to the slot's own value, one per frame.
};

/// The bounds of a host texture, read from the `assets.host_texture.*` settings when the engine is created.
///
/// - Thread-safety: thread-compatible plain data.
struct HostTextureLimits
{
    u32 max_size = 8192;               ///< The most width or height, in pixels.
    u64 upload_budget = u64{8} << 20;  ///< The most bytes of host textures a surface uploads in one frame.
};

/// One host texture: a size, a colour space and the newest pixels.
///
/// An update copies the host's rows under a short lock into the one buffer kept here; the newest update replaces an
/// older one nobody uploaded, so a slow frame never queues video. Every device reads the buffer through
/// [[engine_c::HostTexture::CopyIfNewer]], keeping the version it saw.
///
/// - Thread-safety: thread-safe; any host thread may update while the frame reads.
/// - Ownership: shared by the table and by every surface that draws it, through [[engine_c::HostTexturePtr]].
class HostTexture
{
public:
    /// Makes a texture of `width` x `height` with no pixels yet.
    ///
    /// - Ownership: borrows `counters`, which must outlive the texture.
    HostTexture(u32 width, u32 height, bool srgb, StringView name, const HostTextureLimits& limits,
                TextureCounters* counters);

    /// Replaces the pixels with `height` rows of `width` RGBA8 pixels read from `pixels` with `stride` bytes between
    /// rows. A `width` or `height` of 0 keeps the texture's own; another size resizes it with the update.
    ///
    /// - Fails: INVALID_ARGUMENT (NULL, a stride below the row, above 16 MiB or not a whole number of pixels is not
    ///   needed), TOO_LARGE (a size above the limit).
    engine_status Update(const void* pixels, usize stride, u32 width, u32 height);

    /// Changes the size; the pixels are dropped until the next update.
    ///
    /// - Fails: INVALID_ARGUMENT (a zero size), TOO_LARGE (above the limit).
    engine_status Resize(u32 width, u32 height);

    /// The size now.
    /// Whether an update gave it pixels since it was made or resized.
    bool HasPixels() const;
    u32 Width() const;
    /// See `Width`.
    u32 Height() const;
    /// Whether the pixels are sRGB.
    bool Srgb() const { return srgb_; }
    /// The version of the newest pixels (0 before the first update; every update and resize raises it).
    u64 Version() const;

    /// Copies the newest pixels into `dst` as rows of `dst_pitch` bytes when their version is above `*seen`, and sets
    /// `*seen`, `*width` and `*height`. Returns false when nothing newer exists, when there are no pixels yet, or when
    /// `dst_capacity` is too small for the size (then `*width` and `*height` still say what is needed).
    ///
    /// - Thread-safety: thread-safe.
    bool CopyIfNewer(u64* seen, u32* width, u32* height, u8* dst, usize dst_capacity, usize dst_pitch);

    /// Copies the newest pixels, tightly packed.
    ///
    /// - Returns: false when there are no pixels yet.
    bool Read(Vector<u8>* out, u32* width, u32* height) const;

private:
    mutable Mutex mutex_;
    u32 width_;
    u32 height_;
    bool srgb_;
    HostTextureLimits limits_;
    TextureCounters* counters_;
    Vector<u8> pixels_;  // width_ * height_ * 4 bytes when `has_pixels_`
    bool has_pixels_ = false;
    bool consumed_ = true;  // some device uploaded the newest pixels (an update over unread pixels is a replace)
    u64 version_ = 0;
};

using HostTexturePtr = SharedPtr<HostTexture>;
using HostTextureConstPtr = SharedPtr<const HostTexture>;

/// The textures of an engine behind their handles.
///
/// - Thread-safety: thread-safe; `Update` of one texture and a frame may run at once.
class TextureHost
{
public:
    /// Fixes the limits (read once when the engine is created).
    void Configure(const HostTextureLimits& limits) { limits_ = limits; }
    /// The limits.
    const HostTextureLimits& Limits() const { return limits_; }
    /// The counters the frame reads and clears.
    TextureCounters& Counters() { return counters_; }

    /// See `texture_create` in `engine_c.h`.
    engine_status Create(const engine_texture_info* info, engine_handle* out);
    /// See `texture_update`.
    engine_status Update(engine_handle texture, const void* pixels, size_t stride, const engine_update_info* info);
    /// See `texture_resize`.
    engine_status Resize(engine_handle texture, u32 width, u32 height);
    /// See `texture_destroy`.
    engine_status Destroy(engine_handle texture);
    /// See `texture_read_pixels`.
    engine_status ReadPixels(engine_handle texture, engine_buffer* out, u32* width, u32* height);
    /// The texture a handle names, for a surface (null for a handle that is stale or not a texture's).
    HostTexturePtr Find(engine_handle texture);

private:
    struct Slot
    {
        HostTexturePtr object;
        u32 generation = 0;
    };
    Mutex mutex_;
    Vector<Slot> slots_;
    HostTextureLimits limits_;
    TextureCounters counters_;
};

}  // namespace engine_placeholder::engine_c

namespace engine_placeholder::settings
{
class Registry;
}

namespace engine_placeholder::engine_c
{

/// Declares the `assets.host_texture.*` settings.
void RegisterHostTextureSettings(settings::Registry* registry);
/// Reads them (the defaults for one that is not declared).
HostTextureLimits ReadHostTextureSettings(const settings::Registry& registry);

// The functions of the table that the texture host serves (engine_c.cpp lists them in the table).
engine_status TextureCreateFn(engine_handle engine, const engine_texture_info* info, engine_handle* out);
engine_status TextureUpdateFn(engine_handle texture, const void* pixels, size_t stride, const engine_update_info* info);
engine_status TextureResizeFn(engine_handle texture, u32 width, u32 height);
engine_status TextureDestroyFn(engine_handle texture);
engine_status TextureReadPixelsFn(engine_handle texture, engine_buffer* out, u32* width, u32* height);

}  // namespace engine_placeholder::engine_c
