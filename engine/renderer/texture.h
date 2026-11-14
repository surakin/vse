#pragma once

#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "renderer/device_resources.h"
#include "rhi/rhi.h"
#include "types.h"

//! Textures and samplers that keep their source, so they can be made again after the device is replaced.

namespace engine_placeholder::renderer
{

/// The CPU side of a texture: its description and its pixels.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the pixels.
struct TextureImage
{
    /// The description of the finished texture. `usage` gets `CopyDst` added; `debug_name` is ignored (the owner's name
    /// is used).
    rhi::TextureDesc desc;
    /// The pixels of the mip levels, tightly packed, level 0 first. Either all `desc.mip_levels` levels or only level
    /// 0.
    Vector<u8> pixels;
    /// Whether to build the levels that `pixels` does not hold on the GPU (needs `desc.mip_levels` above 1 and a format
    /// and device that can; otherwise the texture has one level).
    bool generate_mips = false;
};

/// One mip level of one layer of a texture whose texels are in a region of shared bytes: the entry of
/// [[renderer::GpuTexture::CreateFromSharedLevels]]. It names the level by where it is, not by a pointer.
///
/// - Thread-safety: thread-compatible (a plain value).
struct TextureLevelSpan
{
    u64 offset = 0;     ///< Where the level's rows start in the region.
    u64 bytes = 0;      ///< How many bytes the level occupies (`row_pitch` times its rows, the last padded too).
    u32 row_pitch = 0;  ///< Bytes between rows (0: tightly packed); a multiple of the texel size.
    u32 mip = 0;        ///< The mip level.
    u32 layer = 0;      ///< The array layer.
};

/// A function that produces a texture's image again, called at every (re)creation; for textures backed by an asset
/// file.
///
/// It receives the pointer given with the loader and an image to fill in, and returns false on failure.
using TextureLoader = bool (*)(void* user, TextureImage* out);

/// A sampled texture that survives a device reset.
///
/// It holds either its pixels (`Create`) or a loader (`CreateFromLoader`), registers with a
/// [[renderer::DeviceResources]] at the texture tier, and rebuilds itself in `Recreate`. The handle changes after a
/// reset: read `Handle()` each frame, never keep it.
///
/// - Thread-safety: owner-thread-only. Move-only.
/// - Ownership: owns the texture and the pixels. The device and the registry must outlive it.
class GpuTexture final : public Recreatable
{
public:
    GpuTexture() = default;
    /// Releases everything this object owns.
    ~GpuTexture() override { Destroy(); }
    /// Takes over `other`, which is left empty.
    GpuTexture(GpuTexture&& other) noexcept;
    /// Destroys this texture, then takes over `other`, which is left empty.
    GpuTexture& operator=(GpuTexture&& other) noexcept;
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    /// Creates the texture from `image` and keeps `image` as the source.
    ///
    /// - Param device: the device.
    /// - Param image: the description and pixels; moved in.
    /// - Param resources: the registry, or null for a texture that does not need to survive a reset (the pixels are
    /// then not kept).
    /// - Param debug_name: a label; copied, may be null.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if the description is invalid, exceeds the device limits, the format
    /// is
    ///   not supported, or the upload fails.
    /// - Aborts: the texture already exists.
    /// - Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool Create(rhi::Device* device, TextureImage&& image, DeviceResources* resources, const char* debug_name);

    /// Creates the texture from every level of every layer, uploaded straight from a region of shared bytes (the mip
    /// sections of an asset blob): the staging copy into device memory is the only copy, and no mip is generated. The
    /// texture keeps a share of the region and the offsets of the levels, no pointer into it, so a reset uploads from
    /// the same bytes for as long as the texture lives, whoever else lets go of them.
    ///
    /// - Param device: the device.
    /// - Param desc: the finished texture; `usage` gets `CopyDst` added; `debug_name` is ignored.
    /// - Param bytes: the region the levels are in.
    /// - Param levels: exactly one entry for each of `desc.mip_levels * desc.array_layers` subresources.
    /// - Param resources: the registry, or null for a texture that does not need to survive a reset.
    /// - Param debug_name: a label; copied, may be null.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, for no region, an invalid description, a missing, repeated or
    ///   out-of-range level, a level that is not inside the region, a size or format the device cannot hold, or a
    ///   failed upload.
    /// - Aborts: the texture already exists.
    /// - Ownership: shares `bytes`; `levels` is read during the call and copied.
    bool CreateFromSharedLevels(rhi::Device* device, const rhi::TextureDesc& desc, SharedBytesPtr bytes,
                                Span<const TextureLevelSpan> levels, DeviceResources* resources,
                                const char* debug_name);

    /// Creates the texture from `loader` and keeps only the loader; the pixels are read again at every recreate.
    ///
    /// - Param device: the device.
    /// - Param loader: produces the image.
    /// - Param user: passed to `loader`; borrowed, must outlive the texture.
    /// - Param resources: the registry, or null.
    /// - Param debug_name: a label; copied, may be null.
    /// - Returns: true on success.
    /// - Fails: as for [[renderer::GpuTexture::Create]], and when the loader fails.
    /// - Aborts: the texture already exists.
    /// - Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool CreateFromLoader(rhi::Device* device, TextureLoader loader, void* user, DeviceResources* resources,
                          const char* debug_name);

    /// Destroys the texture and forgets the source and the registration.
    void Destroy();

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return handle_.IsValid(); }
    const char* RecreatableName() const override { return name_; }

    /// Returns whether the texture exists on a device.
    /// - Fails: never.
    bool Valid() const { return handle_.IsValid(); }
    /// Returns the current handle; changes after a reset, so read it each frame.
    rhi::TextureHandle Handle() const { return handle_; }
    /// Returns the description of the finished texture (with the levels it really has).
    const rhi::TextureDesc& Desc() const { return desc_; }

private:
    bool Make(rhi::Device* device, const TextureImage& image);
    bool MakeFromSharedLevels(rhi::Device* device);
    void MoveFrom(GpuTexture& other);

    TextureImage image_;
    SharedBytesPtr shared_bytes_;             // the region of CreateFromSharedLevels (image_.desc is its description)
    Vector<TextureLevelSpan> shared_levels_;  // where its levels are (image_.desc is its description)
    TextureLoader loader_ = nullptr;
    void* loader_user_ = nullptr;
    bool has_source_ = false;
    char name_[48] = "texture";
    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    rhi::TextureDesc desc_;
    rhi::TextureHandle handle_;
};

/// How a [[renderer::GpuTexture]] is held: shared, never pointed at.
using GpuTexturePtr = SharedPtr<GpuTexture>;
/// A read-only share of a [[renderer::GpuTexture]].
using GpuTextureConstPtr = SharedPtr<const GpuTexture>;

/// A sampler that survives a device reset.
///
/// - Thread-safety: owner-thread-only. Move-only.
/// - Ownership: owns the sampler. The device and the registry must outlive it.
class GpuSampler final : public Recreatable
{
public:
    GpuSampler() = default;
    /// Releases everything this object owns.
    ~GpuSampler() override { Destroy(); }
    /// Takes over `other`, which is left empty.
    GpuSampler(GpuSampler&& other) noexcept;
    /// Destroys this sampler, then takes over `other`, which is left empty.
    GpuSampler& operator=(GpuSampler&& other) noexcept;
    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;

    /// Creates the sampler.
    ///
    /// - Param device: the device.
    /// - Param desc: the description; kept.
    /// - Param resources: the registry, or null.
    /// - Param debug_name: a label; copied, may be null.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if the device refuses the description.
    /// - Aborts: the sampler already exists.
    /// - Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool Create(rhi::Device* device, const rhi::SamplerDesc& desc, DeviceResources* resources, const char* debug_name);

    /// Destroys the sampler and forgets the registration.
    void Destroy();

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return handle_.IsValid(); }
    const char* RecreatableName() const override { return name_; }

    /// Returns whether the sampler exists on a device.
    /// - Fails: never.
    bool Valid() const { return handle_.IsValid(); }
    /// Returns the current handle; changes after a reset.
    rhi::SamplerHandle Handle() const { return handle_; }

private:
    void MoveFrom(GpuSampler& other);

    rhi::SamplerDesc desc_;
    bool has_source_ = false;
    char name_[48] = "sampler";
    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    rhi::SamplerHandle handle_;
};

/// How a [[renderer::GpuSampler]] is held: shared, never pointed at.
using GpuSamplerPtr = SharedPtr<GpuSampler>;
/// A read-only share of a [[renderer::GpuSampler]].
using GpuSamplerConstPtr = SharedPtr<const GpuSampler>;

}  // namespace engine_placeholder::renderer
