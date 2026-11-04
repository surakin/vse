// VulkanDevice: textures, samplers, format support, texture upload and readback.

#include <cstring>

#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_barrier.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

constexpr u64 kTextureChunkBytes = 4ull << 20;
constexpr u64 kStagingWaitNs = 10ull * 1000 * 1000 * 1000;

u32 MipExtent(u32 base, u32 mip) {
    const u32 v = base >> mip;
    return v > 0 ? v : 1;
}

VkImageUsageFlags ToVkImageUsage(TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    if (Has(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (Has(usage, TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (Has(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (Has(usage, TextureUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (Has(usage, TextureUsage::CopySrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (Has(usage, TextureUsage::CopyDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkFormatFeatureFlags RequiredFeatures(TextureUsage usage) {
    VkFormatFeatureFlags flags = 0;
    if (Has(usage, TextureUsage::Sampled)) flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (Has(usage, TextureUsage::Storage)) flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (Has(usage, TextureUsage::ColorAttachment)) flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (Has(usage, TextureUsage::DepthAttachment)) flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (Has(usage, TextureUsage::CopySrc)) flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if (Has(usage, TextureUsage::CopyDst)) flags |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return flags;
}

VkFilter ToVkFilter(FilterMode m) { return m == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

VkSamplerAddressMode ToVkAddress(AddressMode m) {
    switch (m) {
    case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirrorRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkBorderColor ToVkBorder(BorderColor c) {
    switch (c) {
    case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case BorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case BorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

VkCompareOp ToVkCompare(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

bool SameSampler(const SamplerDesc& a, const SamplerDesc& b) {
    return a.mag_filter == b.mag_filter && a.min_filter == b.min_filter && a.mip_filter == b.mip_filter && a.address_u == b.address_u &&
           a.address_v == b.address_v && a.address_w == b.address_w && a.border_color == b.border_color &&
           a.max_anisotropy == b.max_anisotropy && a.compare_enable == b.compare_enable && a.compare_op == b.compare_op &&
           a.min_lod == b.min_lod && a.max_lod == b.max_lod;
}

} // namespace

void VulkanDevice::InitializeFormats() {
    for (usize i = 1; i < static_cast<usize>(Format::Count); ++i) {
        VkFormatProperties props{};
        instance_.Fns().GetPhysicalDeviceFormatProperties(physical_, ToVkFormat(static_cast<Format>(i)), &props);
        format_features_[i] = props.optimalTilingFeatures;
    }
}

bool VulkanDevice::FormatSupport(Format format, TextureUsage usage) const {
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count)) {
        return false;
    }
    const VkFormatFeatureFlags features = format_features_[static_cast<usize>(format)];
    const VkFormatFeatureFlags required = RequiredFeatures(usage);
    return required == 0 ? features != 0 : (features & required) == required;
}

bool VulkanDevice::CanGenerateMips(Format format) const {
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count)) {
        return false;
    }
    const FormatProperties& f = FormatInfo(format);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return !f.is_depth && !f.is_compressed && (format_features_[static_cast<usize>(format)] & need) == need;
}

// ---- textures ----

TextureHandle VulkanDevice::CreateTexture(const TextureDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateTexture: %s", why);
        return {};
    }
    if (desc.dimension == TextureDimension::Cube) {
        RhiLog(LogVerbosity::Error, "CreateTexture: cube textures are not supported yet");
        return {};
    }
    const bool is3d = desc.dimension == TextureDimension::Tex3D;
    if (desc.width > (is3d ? caps_.max_texture_dimension_3d : caps_.max_texture_dimension_2d) ||
        desc.height > (is3d ? caps_.max_texture_dimension_3d : caps_.max_texture_dimension_2d) ||
        (is3d && desc.depth > caps_.max_texture_dimension_3d) || desc.array_layers > caps_.max_texture_array_layers) {
        RhiLog(LogVerbosity::Error, "CreateTexture: the size exceeds the device limits");
        return {};
    }
    if (!FormatSupport(desc.format, desc.usage)) {
        RhiLog(LogVerbosity::Error, "CreateTexture: format %s does not support the requested usage on this device", FormatInfo(desc.format).name);
        return {};
    }

    {
        VkImageFormatProperties limits{};
        const VkResult q = instance_.Fns().GetPhysicalDeviceImageFormatProperties(
            physical_, ToVkFormat(desc.format), is3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, ToVkImageUsage(desc.usage), 0, &limits);
        if (q != VK_SUCCESS || desc.width > limits.maxExtent.width || desc.height > limits.maxExtent.height ||
            (is3d && desc.depth > limits.maxExtent.depth) || desc.mip_levels > limits.maxMipLevels || desc.array_layers > limits.maxArrayLayers) {
            RhiLog(LogVerbosity::Error, "CreateTexture: format %s with this usage and size is beyond what the device supports",
                   FormatInfo(desc.format).name);
            return {};
        }
    }
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = is3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = ToVkFormat(desc.format);
    info.extent = {desc.width, desc.height, is3d ? desc.depth : 1};
    info.mipLevels = desc.mip_levels;
    info.arrayLayers = desc.array_layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = ToVkImageUsage(desc.usage);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = nullptr;
    VkResult result = fns_.CreateImage(device_, &info, nullptr, &image);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create a %ux%u texture (%s)", desc.width, desc.height, VkResultName(result));
        return {};
    }

    VkMemoryDedicatedRequirements dedicated_req{};
    dedicated_req.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 req{};
    req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    req.pNext = &dedicated_req;
    VkImageMemoryRequirementsInfo2 req_info{};
    req_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    req_info.image = image;
    fns_.GetImageMemoryRequirements2(device_, &req_info, &req);

    MemoryRequest request;
    request.size = req.memoryRequirements.size;
    request.alignment = req.memoryRequirements.alignment;
    request.memory_type_bits = req.memoryRequirements.memoryTypeBits;
    request.location = MemoryLocation::GpuOnly;
    request.linear = false;
    request.dedicated = dedicated_req.requiresDedicatedAllocation == VK_TRUE || dedicated_req.prefersDedicatedAllocation == VK_TRUE;
    request.dedicated_image = request.dedicated ? HandleToU64(image) : 0;
    MemoryResult memory_result = MemoryResult::Ok;
    TextureRes res;
    res.memory = memory_->Allocate(request, &memory_result);
    if (!res.memory.Valid()) {
        fns_.DestroyImage(device_, image, nullptr);
        RhiLog(LogVerbosity::Error, "Vulkan: no memory for a %ux%u texture", desc.width, desc.height);
        return {};
    }
    result = fns_.BindImageMemory(device_, image, HandleFromU64<VkDeviceMemory>(res.memory.memory), res.memory.offset);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        ENGINE_VK_CHECK(result);
        memory_->Free(res.memory);
        fns_.DestroyImage(device_, image, nullptr);
        return {};
    }
    res.image = image;
    raw_image_count_.fetch_add(1, std::memory_order_relaxed);
    res.desc = desc;
    res.desc.debug_name = nullptr;

    // Views. The sampling view covers every mip and layer; a depth-stencil texture is sampled as depth only.
    const VkImageAspectFlags aspects = AspectsOf(desc.format);
    VkImageAspectFlags view_aspects = aspects;
    if (Has(desc.usage, TextureUsage::Sampled) && (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) {
        view_aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = is3d ? VK_IMAGE_VIEW_TYPE_3D : (desc.array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    view_info.format = info.format;
    view_info.subresourceRange = {view_aspects, 0, desc.mip_levels, 0, desc.array_layers};
    bool ok = true;
    if (Has(desc.usage, TextureUsage::Sampled) || Has(desc.usage, TextureUsage::Storage)) {
        result = fns_.CreateImageView(device_, &view_info, nullptr, &res.view);
        ok = result == VK_SUCCESS;
    }
    if (ok && (Has(desc.usage, TextureUsage::ColorAttachment) || Has(desc.usage, TextureUsage::DepthAttachment))) {
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.subresourceRange = {aspects, 0, 1, 0, 1};
        result = fns_.CreateImageView(device_, &view_info, nullptr, &res.attachment_view);
        ok = result == VK_SUCCESS;
    }
    if (!ok) {
        NoteDeviceLost(result);
        ENGINE_VK_CHECK(result);
        DestroyTextureRes(&res);
        return {};
    }
    SetDebugName(HandleToU64(image), VK_OBJECT_TYPE_IMAGE, desc.debug_name);

    TextureHandle handle;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        handle = textures_.Allocate(TextureRes(res));
    }
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateTexture: out of texture handles");
        DestroyTextureRes(&res);
    }
    return handle;
}

void VulkanDevice::DestroyTextureRes(TextureRes* t) {
    if (t->external) {
        if (t->attachment_view != nullptr) {
            fns_.DestroyImageView(device_, t->attachment_view, nullptr);
        }
        *t = TextureRes{};
        return;
    }
    if (t->attachment_view != nullptr) {
        fns_.DestroyImageView(device_, t->attachment_view, nullptr);
    }
    if (t->view != nullptr) {
        fns_.DestroyImageView(device_, t->view, nullptr);
    }
    if (t->image != nullptr) {
        fns_.DestroyImage(device_, t->image, nullptr);
        raw_image_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    if (t->memory.Valid()) {
        memory_->Free(t->memory);
    }
    *t = TextureRes{};
}

void VulkanDevice::DestroyTexture(TextureHandle texture) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    TextureRes* t = textures_.Get(texture);
    ENGINE_ASSERT(t != nullptr, "DestroyTexture: handle is invalid or already destroyed");
    if (t == nullptr) {
        return;
    }
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    textures_.Retire(texture, frame_open_ ? current : current + 1);
}

bool VulkanDevice::LookupTexture(TextureHandle texture, TextureInfo* info) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    if (t == nullptr) {
        return false;
    }
    info->image = t->image;
    info->view = t->view;
    info->attachment_view = t->attachment_view;
    info->desc = t->desc;
    info->aspects = AspectsOf(t->desc.format);
    return true;
}

// ---- samplers ----

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateSampler: %s", why);
        return {};
    }
    SamplerDesc normalized = desc;
    normalized.debug_name = nullptr;
    if (normalized.max_anisotropy > caps_.max_sampler_anisotropy) {
        normalized.max_anisotropy = caps_.max_sampler_anisotropy;
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);
    SamplerHandle shared;
    samplers_.ForEachLive([&](SamplerHandle h, SamplerRes& s) {
        if (!shared.IsValid() && SameSampler(s.desc, normalized)) {
            ++s.refs;
            shared = h;
        }
    });
    if (shared.IsValid()) {
        return shared;
    }

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = ToVkFilter(normalized.mag_filter);
    info.minFilter = ToVkFilter(normalized.min_filter);
    info.mipmapMode = normalized.mip_filter == FilterMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = ToVkAddress(normalized.address_u);
    info.addressModeV = ToVkAddress(normalized.address_v);
    info.addressModeW = ToVkAddress(normalized.address_w);
    info.anisotropyEnable = normalized.max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = normalized.max_anisotropy;
    info.compareEnable = normalized.compare_enable ? VK_TRUE : VK_FALSE;
    info.compareOp = ToVkCompare(normalized.compare_op);
    info.minLod = normalized.min_lod;
    info.maxLod = normalized.max_lod;
    info.borderColor = ToVkBorder(normalized.border_color);
    SamplerRes res;
    const VkResult result = fns_.CreateSampler(device_, &info, nullptr, &res.sampler);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create a sampler (%s)", VkResultName(result));
        return {};
    }
    SetDebugName(HandleToU64(res.sampler), VK_OBJECT_TYPE_SAMPLER, desc.debug_name);
    res.desc = normalized;
    res.refs = 1;
    SamplerHandle handle = samplers_.Allocate(SamplerRes(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateSampler: out of sampler handles");
        fns_.DestroySampler(device_, res.sampler, nullptr);
    }
    return handle;
}

void VulkanDevice::DestroySampler(SamplerHandle sampler) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    SamplerRes* s = samplers_.Get(sampler);
    ENGINE_ASSERT(s != nullptr, "DestroySampler: handle is invalid or already destroyed");
    if (s == nullptr) {
        return;
    }
    if (--s->refs > 0) {
        return;
    }
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    samplers_.Retire(sampler, frame_open_ ? current : current + 1);
}

// ---- upload and readback ----

// Finds `alloc_bytes` in the staging ring, waiting for the GPU to free older chunks. Holds submit_mutex_.
bool VulkanDevice::StageChunkLocked(u64 alloc_bytes, u64 align, u64* ring_offset) {
    staging_ring_->Reclaim(CompletedTimeline());
    while (!staging_ring_->Allocate(alloc_bytes, align, ring_offset)) {
        const u64 pending = staging_ring_->OldestPendingFence();
        if (pending == 0 || !WaitTimeline(pending, kStagingWaitNs)) {
            RhiLog(LogVerbosity::Error, "UploadTexture: the staging ring is exhausted and the GPU is not freeing it");
            return false;
        }
        staging_ring_->Reclaim(CompletedTimeline());
    }
    return true;
}

UploadTicket VulkanDevice::UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) {
    TextureRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        TextureRes* t = textures_.Get(dst);
        ENGINE_ASSERT(t != nullptr, "UploadTexture: handle is invalid or already destroyed");
        if (t == nullptr) {
            return UploadTicket{0, false};
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopyDst) && Has(t->desc.usage, TextureUsage::Sampled),
                      "UploadTexture: the texture needs Sampled and CopyDst usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers, "UploadTexture: mip or layer out of range");
        ENGINE_ASSERT(data != nullptr, "UploadTexture: data is null");
        ENGINE_ASSERT(t->state == ResourceState::Undefined || t->state == ResourceState::ShaderRead,
                      "UploadTexture: the texture must be in state Undefined or ShaderRead");
        snapshot = *t;
    }
    const Format format = snapshot.desc.format;
    const FormatProperties& fp = FormatInfo(format);
    const u32 width = MipExtent(snapshot.desc.width, mip);
    const u32 height = MipExtent(snapshot.desc.height, mip);
    const u32 depth = snapshot.desc.dimension == TextureDimension::Tex3D ? MipExtent(snapshot.desc.depth, mip) : 1;
    const u64 row_bytes = FormatRowBytes(format, width);
    const u64 pitch = row_pitch != 0 ? row_pitch : row_bytes;
    ENGINE_ASSERT(pitch >= row_bytes && pitch % fp.bytes_per_block == 0, "UploadTexture: row_pitch is smaller than a row or not a multiple of the texel size");
    const u64 block_rows = (static_cast<u64>(height) + fp.block_height - 1) / fp.block_height;
    if (pitch > kTextureChunkBytes) {
        RhiLog(LogVerbosity::Error, "UploadTexture: a row of %llu bytes does not fit the staging chunk", static_cast<unsigned long long>(pitch));
        return UploadTicket{0, false};
    }
    const u64 rows_per_chunk = kTextureChunkBytes / pitch < block_rows ? kTextureChunkBytes / pitch : block_rows;
    const u32 buffer_row_length = static_cast<u32>(pitch / fp.bytes_per_block * fp.block_width);
    const u64 slice_stride = pitch * block_rows;
    const VkImageAspectFlags aspects = AspectsOf(format);
    const VkImageAspectFlags copy_aspect = CopyAspectOf(format);
    ENGINE_ASSERT(copy_aspect != 0, "UploadTexture: depth-stencil formats with a stencil aspect cannot be uploaded");
    const u64 total_chunks_per_slice = (block_rows + rows_per_chunk - 1) / rows_per_chunk;
    const u64 total_chunks = total_chunks_per_slice * depth;

    UploadTicket ticket;
    u64 chunk_index = 0;
    for (u32 z = 0; z < depth; ++z) {
        for (u64 row = 0; row < block_rows; row += rows_per_chunk, ++chunk_index) {
            const u64 rows = block_rows - row < rows_per_chunk ? block_rows - row : rows_per_chunk;
            const bool last_row_of_slice = row + rows == block_rows;
            // Bytes the caller's buffer holds for this band: the final row of the level has no padding after it.
            const u64 src_bytes = (last_row_of_slice ? (rows - 1) * pitch + row_bytes : rows * pitch);
            const u64 src_offset = z * slice_stride + row * pitch;
            const bool first = chunk_index == 0;
            const bool last = chunk_index + 1 == total_chunks;

            std::lock_guard<std::mutex> lock(submit_mutex_);
            // A failure after the first chunk left the image in TRANSFER_DST: put it in ShaderRead anyway, so the
            // texture is in the state the caller expects (with the level only partly written), then report failure.
            auto fail = [&]() {
                if (chunk_index > 0) {
                    SubmitInternalLocked([&](VkCommandBuffer cb) {
                        CmdImageBarrier(fns_, cb, snapshot.image, aspects, 0, snapshot.desc.mip_levels, 0, snapshot.desc.array_layers,
                                        ResourceState::CopyDst, ResourceState::ShaderRead);
                    });
                    std::lock_guard<std::mutex> state_lock(resource_mutex_);
                    if (TextureRes* t = textures_.Get(dst)) {
                        t->state = ResourceState::ShaderRead;
                    }
                }
                ticket.ok = false;
                return ticket;
            };
            if (device_lost_.load(std::memory_order_relaxed)) {
                return fail();
            }
            u64 ring_offset = 0;
            if (!StageChunkLocked(rows * pitch, 64, &ring_offset)) {
                return fail();
            }
            std::memcpy(static_cast<char*>(staging_.memory.mapped) + ring_offset, static_cast<const char*>(data) + src_offset,
                        static_cast<usize>(src_bytes));
            FlushRange(staging_.memory, ring_offset, rows * pitch);

            const u64 value = SubmitInternalLocked([&](VkCommandBuffer cb) {
                if (first) {
                    CmdImageBarrier(fns_, cb, snapshot.image, aspects, 0, snapshot.desc.mip_levels, 0, snapshot.desc.array_layers,
                                    snapshot.state, ResourceState::CopyDst);
                }
                VkBufferImageCopy region{};
                region.bufferOffset = ring_offset;
                region.bufferRowLength = buffer_row_length;
                region.bufferImageHeight = 0;
                region.imageSubresource = {copy_aspect, mip, layer, 1};
                const u32 y = static_cast<u32>(row * fp.block_height);
                const u32 band_height = static_cast<u32>(rows * fp.block_height);
                region.imageOffset = {0, static_cast<i32>(y), static_cast<i32>(z)};
                region.imageExtent = {width, y + band_height > height ? height - y : band_height, 1};
                fns_.CmdCopyBufferToImage(cb, staging_.buffer, snapshot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                if (last) {
                    CmdImageBarrier(fns_, cb, snapshot.image, aspects, 0, snapshot.desc.mip_levels, 0, snapshot.desc.array_layers,
                                    ResourceState::CopyDst, ResourceState::ShaderRead);
                }
            });
            if (value == 0) {
                staging_ring_->Retire(last_submit_value_);
                return fail();
            }
            staging_ring_->Retire(value);
            ticket.fence = value;
        }
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(dst)) {
        t->state = ResourceState::ShaderRead;
    }
    return ticket;
}

bool VulkanDevice::ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) {
    TextureRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        TextureRes* t = textures_.Get(src);
        ENGINE_ASSERT(t != nullptr, "ReadbackTexture: handle is invalid or already destroyed");
        if (t == nullptr) {
            return false;
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopySrc), "ReadbackTexture: the texture was not created with CopySrc usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers, "ReadbackTexture: mip or layer out of range");
        ENGINE_ASSERT(dst != nullptr, "ReadbackTexture: dst is null");
        ENGINE_ASSERT(state != ResourceState::Undefined && state != ResourceState::Count, "ReadbackTexture: state must be the texture's current state");
        ENGINE_ASSERT(CopyAspectOf(t->desc.format) != 0, "ReadbackTexture: depth-stencil formats with a stencil aspect cannot be read back");
        snapshot = *t;
    }
    const Format format = snapshot.desc.format;
    const u32 width = MipExtent(snapshot.desc.width, mip);
    const u32 height = MipExtent(snapshot.desc.height, mip);
    const u32 depth = snapshot.desc.dimension == TextureDimension::Tex3D ? MipExtent(snapshot.desc.depth, mip) : 1;
    const u64 bytes = FormatSliceBytes(format, width, height) * depth;
    ENGINE_ASSERT(dst_size >= bytes, "ReadbackTexture: dst is smaller than the level");
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }
    RawBuffer temp;
    if (!CreateRawBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryLocation::GpuToCpu, "texture readback", &temp) || temp.memory.mapped == nullptr) {
        DestroyRawBuffer(&temp);
        return false;
    }
    const VkImageAspectFlags aspects = AspectsOf(format);
    u64 value = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        value = SubmitInternalLocked([&](VkCommandBuffer cb) {
            if (state != ResourceState::CopySrc) {
                CmdImageBarrier(fns_, cb, snapshot.image, aspects, 0, snapshot.desc.mip_levels, 0, snapshot.desc.array_layers, state,
                                ResourceState::CopySrc);
            }
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = {CopyAspectOf(format), mip, layer, 1};
            region.imageExtent = {width, height, depth};
            fns_.CmdCopyImageToBuffer(cb, snapshot.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, temp.buffer, 1, &region);
            if (state != ResourceState::CopySrc) {
                CmdImageBarrier(fns_, cb, snapshot.image, aspects, 0, snapshot.desc.mip_levels, 0, snapshot.desc.array_layers,
                                ResourceState::CopySrc, state);
            }
            VkMemoryBarrier2 host{};
            host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &host;
            fns_.CmdPipelineBarrier2(cb, &dep);
        });
    }
    if (value == 0 || !WaitTimeline(value, ~0ull)) {
        DestroyRawBuffer(&temp);
        return false;
    }
    InvalidateRange(temp.memory, 0, bytes);
    std::memcpy(dst, temp.memory.mapped, static_cast<usize>(bytes));
    DestroyRawBuffer(&temp);
    return true;
}

} // namespace engine_placeholder::rhi::vulkan
