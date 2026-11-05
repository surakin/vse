#pragma once

//! The transient pool: textures and buffers a frame's render graph needs, kept between frames and reused.
//!
//! A graph declares a transient resource by description only. When the graph runs it takes a real texture or buffer with
//! an equal description from this pool, or creates one, and gives it back afterwards, so a steady-state frame creates
//! nothing. A pooled resource also remembers the state it was left in, because the next user starts from there. One that
//! nobody asks for during a number of frames is destroyed.

#include "containers/vector.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// Capacities and aging of a [[renderer::TransientPool]].
///
/// Thread-safety: thread-compatible plain data.
struct TransientPoolDesc {
    /// Most textures the pool holds, in use or idle.
    u32 max_textures = 128;
    /// Most buffers the pool holds, in use or idle.
    u32 max_buffers = 64;
    /// Frames an idle resource may go unused before it is destroyed.
    u32 max_idle_frames = 3;
};

/// A texture taken from the pool and the state it is in.
///
/// Thread-safety: thread-compatible plain data.
struct PooledTexture {
    /// The texture; invalid if the request failed.
    rhi::TextureHandle handle;
    /// The state the texture is in now; a fresh texture is `Undefined`.
    rhi::ResourceState state = rhi::ResourceState::Undefined;
};

/// A buffer taken from the pool and the state it is in.
///
/// Thread-safety: thread-compatible plain data.
struct PooledBuffer {
    /// The buffer; invalid if the request failed.
    rhi::BufferHandle handle;
    /// The state the buffer is in now; a fresh buffer is `Undefined`.
    rhi::ResourceState state = rhi::ResourceState::Undefined;
};

/// What the pool has done, for tests and tools.
///
/// Thread-safety: thread-compatible plain data.
struct TransientPoolStats {
    /// Textures created over the pool's life.
    u32 textures_created = 0;
    /// Textures handed out from the idle list instead of created.
    u32 texture_reuses = 0;
    /// Textures destroyed because they went unused.
    u32 textures_destroyed = 0;
    /// Textures held now (in use plus idle).
    u32 textures_live = 0;
    /// Textures idle now.
    u32 textures_idle = 0;
    /// Buffers created over the pool's life.
    u32 buffers_created = 0;
    /// Buffers handed out from the idle list instead of created.
    u32 buffer_reuses = 0;
    /// Buffers destroyed because they went unused.
    u32 buffers_destroyed = 0;
    /// Buffers held now.
    u32 buffers_live = 0;
    /// Buffers idle now.
    u32 buffers_idle = 0;
};

/// Pools transient textures and buffers by description.
///
/// Two descriptions are equal when every field but the debug name matches. Use per frame is `BeginFrame`, any number of
/// acquire and release calls (the render graph makes them), `EndFrame`.
///
/// Thread-safety: thread-compatible.
/// Ownership: borrows the device, which must outlive the pool; owns the resources it created and destroys them through
///   the device (so the GPU may still be using them) when it is destroyed or they age out.
class TransientPool {
public:
    /// Creates an empty pool.
    ///
    /// Param device: the device; must not be null.
    /// Param desc: capacities, each at least 1.
    /// Aborts: `device` is null or a capacity is 0.
    /// Ownership: `device` is borrowed.
    TransientPool(rhi::Device* device, const TransientPoolDesc& desc);
    /// Destroys every pooled resource.
    ~TransientPool();
    TransientPool(const TransientPool&) = delete;
    TransientPool& operator=(const TransientPool&) = delete;

    /// Starts a frame; resources idle for more than `max_idle_frames` frames are destroyed at `EndFrame`.
    ///
    /// Param frame_index: the frame number, increasing.
    void BeginFrame(u64 frame_index);

    /// Destroys the idle resources that went unused for too long.
    void EndFrame();

    /// Takes a texture with the given description: an idle one if there is one, otherwise a new one.
    ///
    /// When the pool is full, the idle texture unused for longest is destroyed to make room.
    ///
    /// Param desc: the description; its `debug_name` is used only when a texture is created.
    /// Param out: receives the texture and its state.
    /// Returns: true on success.
    /// Fails: the pool is full or the device could not create the texture; logged, and `out` is left invalid.
    /// Aborts: `out` is null.
    /// Ownership: the texture stays owned by the pool; hand it back with `ReleaseTexture`, never destroy it.
    bool AcquireTexture(const rhi::TextureDesc& desc, PooledTexture* out);

    /// Gives a texture back, remembering the state it was left in.
    ///
    /// Param texture: a texture acquired from this pool.
    /// Param state: the state it is in now.
    /// Aborts: the texture is not in use from this pool.
    void ReleaseTexture(rhi::TextureHandle texture, rhi::ResourceState state);

    /// Takes a buffer with the given description; see [[renderer::TransientPool::AcquireTexture]].
    ///
    /// Param desc: the description.
    /// Param out: receives the buffer and its state.
    /// Returns: true on success.
    /// Fails: the pool is full or the device could not create the buffer; logged.
    /// Aborts: `out` is null.
    /// Ownership: the buffer stays owned by the pool; hand it back with `ReleaseBuffer`, never destroy it.
    bool AcquireBuffer(const rhi::BufferDesc& desc, PooledBuffer* out);

    /// Gives a buffer back, remembering the state it was left in.
    ///
    /// Param buffer: a buffer acquired from this pool.
    /// Param state: the state it is in now.
    /// Aborts: the buffer is not in use from this pool.
    void ReleaseBuffer(rhi::BufferHandle buffer, rhi::ResourceState state);

    /// Overwrites the state remembered for an idle texture; used to undo a recording that was never submitted.
    ///
    /// Param texture: a texture of this pool; ignored if it is in use or unknown.
    /// Param state: the state to remember.
    /// Fails: never.
    void SetTextureState(rhi::TextureHandle texture, rhi::ResourceState state);
    /// Overwrites the state remembered for an idle buffer; see [[renderer::TransientPool::SetTextureState]].
    ///
    /// Param buffer: a buffer of this pool; ignored if it is in use or unknown.
    /// Param state: the state to remember.
    /// Fails: never.
    void SetBufferState(rhi::BufferHandle buffer, rhi::ResourceState state);

    /// Returns the counters.
    /// Fails: never.
    TransientPoolStats Stats() const;

private:
    struct TextureEntry;
    struct BufferEntry;

    rhi::Device* device_ = nullptr;
    TransientPoolDesc desc_;
    Vector<TextureEntry> textures_;
    Vector<BufferEntry> buffers_;
    u32 texture_count_ = 0;
    u32 buffer_count_ = 0;
    u64 frame_ = 0;
    TransientPoolStats stats_;
};

} // namespace engine_placeholder::renderer
