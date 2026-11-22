#pragma once
//! The device side of the host textures a surface draws: for each host texture a material slot
//! names, one texture on the surface's device, a ring of three persistently mapped staging buffers, and the version of
//! the pixels last uploaded. Internal to the module `engine_c`.

#include "containers/vector.h"
#include "engine_c/host_texture.h"
#include "rhi/device.h"
#include "types.h"

namespace vse::engine_c
{

/// The host textures on one device.
///
/// A frame goes `BeginFrame`, `Acquire` for every texture a slot names (each copies the newest pixels into the next
/// staging buffer, within the budget), `Flush` (one submission with every copy), `EndFrame` (textures nobody asked
/// for this frame are dropped), then the surface renders with the handles `Acquire` gave.
///
/// - Thread-safety: thread-compatible; used by the frame thread of its surface.
/// - Ownership: borrows the device and the texture host; owns the device objects.
class LiveTextures
{
public:
    LiveTextures() = default;
    ~LiveTextures();
    LiveTextures(const LiveTextures&) = delete;
    LiveTextures& operator=(const LiveTextures&) = delete;

    /// Names the device and the host the textures come from.
    void Attach(rhi::Device* device, TextureHost* host);
    /// Destroys every device object (after the device is idle); the next `Acquire` makes them again from the kept
    /// pixels. Used by a rebind and a device reset.
    void Release();

    /// Starts a frame: nothing is wanted yet, the upload budget is full.
    void BeginFrame();
    /// The texture a slot names, ready to bind, with the newest pixels copied to staging.
    ///
    /// - Param handle: the value of the engine_handle of the host texture.
    /// - Returns: an invalid handle when the texture was destroyed or has no pixels yet (the slot keeps its own
    ///   value), counted in `TextureCounters::slots_reverted` by the caller's `Revert`.
    rhi::TextureHandle Acquire(u64 handle);
    /// Records the copies `Acquire` staged and submits them.
    void Flush();
    /// Drops the textures no `Acquire` asked for since `BeginFrame`.
    void EndFrame();

    /// Uploads made, and their bytes, since the surface was made.
    u64 Uploads() const { return uploads_; }
    u64 UploadedBytes() const { return bytes_; }
    /// Counts a slot that went back to its own value.
    void Revert();

private:
    static constexpr u32 kRing = 3;
    struct Live
    {
        u64 handle = 0;
        HostTexturePtr source;
        rhi::TextureHandle texture;
        rhi::BufferHandle staging[kRing];
        u8* mapped[kRing] = {};
        u32 width = 0;
        u32 height = 0;
        u32 pitch = 0;
        u32 ring = 0;
        u64 seen = 0;  // the version last copied
        bool uploaded = false;
        bool wanted = false;
        bool copied = false;  // a copy waits for Flush
        bool state_shader_read = false;
    };

    bool Make(Live* live, u32 width, u32 height);
    void Destroy(Live* live);

    rhi::Device* device_ = nullptr;
    TextureHost* host_ = nullptr;
    Vector<Live> lives_;
    u64 budget_left_ = 0;
    bool first_this_frame_ = true;
    u64 uploads_ = 0;
    u64 bytes_ = 0;
};

}  // namespace vse::engine_c
