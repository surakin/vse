// Bind group layouts, graphics pipelines and the on-disk pipeline cache of the Vulkan device.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "io/file.h"
#include "io/file_system.h"
#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vk_pipeline_policy.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

bool SameBindings(const BindingDesc* a, u32 a_count, const BindingDesc* b, u32 b_count) {
    if (a_count != b_count) {
        return false;
    }
    for (u32 i = 0; i < a_count; ++i) {
        if (a[i].binding != b[i].binding || a[i].type != b[i].type || a[i].stages != b[i].stages || a[i].count != b[i].count) {
            return false;
        }
    }
    return true;
}

// Reads a whole cache file into `out`; false if it does not exist, is not a regular file (a directory or a pipe must not stall
// start-up), is empty or too large, or cannot be read.
bool ReadCacheFile(const char* path, Vector<unsigned char>* out) {
    io::Path file_path;
    io::FileInfo info;
    if (!io::Path::Parse(StringView(path), io::NativePathStyle(), &file_path) || io::Stat(file_path, &info).Failed() ||
        info.type != io::FileType::Regular || info.size == 0 || info.size > (u64{256} << 20)) {
        return false;
    }
    return io::ReadFile(file_path, out, u64{256} << 20).Ok();
}

// Creates the cache directory and writes the file whole, atomically: a reader sees the old cache or the new one.
bool SaveCacheFile(const char* path, const void* data, usize size) {
    io::Path file_path;
    if (!io::Path::Parse(StringView(path), io::NativePathStyle(), &file_path)) {
        return false;
    }
    const io::Path dir = file_path.Parent();
    return (dir.IsEmpty() || io::CreateDirectories(dir).Ok()) && io::WriteFileAtomic(file_path, data, size, 0644).Ok(); // readable by others, as a cache shared through ENGINE_RHI_CACHE_DIR was before
}

} // namespace

// ---- pipeline cache ----

void VulkanDevice::InitializePipelineCache(const DeviceCreateInfo& info) {
    cache_path_[0] = '\0';
    PipelineCacheIdentity identity;
    identity.vendor_id = properties_.vendorID;
    identity.device_id = properties_.deviceID;
    std::memcpy(identity.uuid, properties_.pipelineCacheUUID, 16);

    Vector<unsigned char> blob;
    if (info.pipeline_cache) {
        // An explicit directory, then ENGINE_RHI_CACHE_DIR, then the user's cache directory for the application.
        char dir[400];
        const PipelineCacheDirChoice choice = ChoosePipelineCacheDir(info.pipeline_cache_dir, std::getenv("ENGINE_RHI_CACHE_DIR"), dir, sizeof(dir));
        bool have_dir = choice == PipelineCacheDirChoice::Chosen;
        if (choice == PipelineCacheDirChoice::NotConfigured) {
            char app[200];
            io::Path user_cache;
            const io::Status s = SanitizePipelineCacheAppName(info.application_name, app, sizeof(app)) ? io::UserCacheDirectory(StringView(app), &user_cache)
                                                                                                    : io::Status::Of(io::Error::Invalid);
            have_dir = s.Ok() && user_cache.ToNative(dir, sizeof(dir));
        }
        if (have_dir && BuildPipelineCachePath(dir, identity, cache_path_, sizeof(cache_path_))) {
            cache_stats_.enabled = true;
            ReadCacheFile(cache_path_, &blob);
            if (blob.Size() != 0) {
                const char* why = CheckPipelineCacheHeader(blob.Data(), blob.Size(), identity);
                if (why != nullptr) {
                    RhiLog(LogVerbosity::Warn, "Vulkan: ignoring %s: %s", cache_path_, why);
                    cache_stats_.rejected = true;
                    blob.Reset();
                }
            }
        } else {
            cache_path_[0] = '\0';
            RhiLog(LogVerbosity::Info, "Vulkan: no directory for the pipeline cache; running without it");
        }
    }

    VkPipelineCacheCreateInfo cache_info{};
    cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cache_info.initialDataSize = blob.Size();
    cache_info.pInitialData = blob.Data();
    VkResult result = fns_.CreatePipelineCache(device_, &cache_info, nullptr, &pipeline_cache_);
    if (result != VK_SUCCESS && blob.Size() != 0) {
        // The driver refused the blob (its own validation is stricter than ours): start empty.
        RhiLog(LogVerbosity::Warn, "Vulkan: the driver rejected %s; starting with an empty pipeline cache", cache_path_);
        cache_stats_.rejected = true;
        cache_info.initialDataSize = 0;
        cache_info.pInitialData = nullptr;
        result = fns_.CreatePipelineCache(device_, &cache_info, nullptr, &pipeline_cache_);
    }
    blob.Reset();
    if (result != VK_SUCCESS) {
        RhiLog(LogVerbosity::Warn, "Vulkan: cannot create a pipeline cache (%s); pipelines compile without one", VkResultName(result));
        pipeline_cache_ = nullptr;
        cache_stats_.enabled = false;
        cache_path_[0] = '\0';
        return;
    }
    cache_stats_.loaded_bytes = cache_info.initialDataSize;
    if (cache_stats_.loaded_bytes != 0) {
        RhiLog(LogVerbosity::Info, "Vulkan: loaded %llu bytes of pipeline cache from %s",
               static_cast<unsigned long long>(cache_stats_.loaded_bytes), cache_path_);
    }
}

VulkanDevice::PipelineCacheStats VulkanDevice::PipelineCache() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return cache_stats_;
}

bool VulkanDevice::SavePipelineCache() {
    if (pipeline_cache_ == nullptr || cache_path_[0] == '\0') {
        return false;
    }
    std::lock_guard<std::mutex> save_lock(cache_save_mutex_);
    usize size = 0;
    if (fns_.GetPipelineCacheData(device_, pipeline_cache_, &size, nullptr) != VK_SUCCESS || size == 0) {
        return false;
    }
    Vector<unsigned char> data;
    if (!data.TryResizeUninitialized(size)) {
        return false;
    }
    bool saved = false;
    if (fns_.GetPipelineCacheData(device_, pipeline_cache_, &size, data.Data()) == VK_SUCCESS) {
        if (SaveCacheFile(cache_path_, data.Data(), size)) {
            std::lock_guard<std::mutex> lock(resource_mutex_);
            cache_stats_.saved_bytes = size;
            saved = true;
        } else {
            RhiLog(LogVerbosity::Warn, "Vulkan: cannot save the pipeline cache to %s", cache_path_);
        }
    }
    return saved;
}

void VulkanDevice::ShutdownPipelines() {
    if (pipeline_cache_ != nullptr) {
        SavePipelineCache();
        fns_.DestroyPipelineCache(device_, pipeline_cache_, nullptr);
        pipeline_cache_ = nullptr;
    }
    for (usize i = 0; i < kMaxPipelineLayouts; ++i) {
        if (pipeline_layouts_[i].layout != nullptr) {
            fns_.DestroyPipelineLayout(device_, pipeline_layouts_[i].layout, nullptr);
            pipeline_layouts_[i] = PipelineLayoutEntry{};
        }
    }
    for (usize i = 0; i < kMaxSetLayouts; ++i) {
        if (set_layouts_[i].layout != nullptr) {
            fns_.DestroyDescriptorSetLayout(device_, set_layouts_[i].layout, nullptr);
            set_layouts_[i].layout = nullptr;
        }
    }
}

// ---- bind group layouts ----

BindGroupLayoutHandle VulkanDevice::CreateBindGroupLayout(const BindGroupLayoutDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: %s", why);
        return {};
    }
    BindGroupLayoutRes res;
    res.binding_count = static_cast<u32>(desc.entries.count);
    // Sorted by binding index, so layouts that differ only in the order of their entries are equal and share one object.
    for (u32 i = 0; i < res.binding_count; ++i) {
        u32 at = i;
        while (at > 0 && res.bindings[at - 1].binding > desc.entries[i].binding) {
            res.bindings[at] = res.bindings[at - 1];
            --at;
        }
        res.bindings[at] = desc.entries[i];
    }

    std::lock_guard<std::mutex> lock(resource_mutex_);
    u32 slot = static_cast<u32>(kMaxSetLayouts);
    u32 free_slot = static_cast<u32>(kMaxSetLayouts);
    for (u32 i = 0; i < kMaxSetLayouts; ++i) {
        SetLayoutEntry& e = set_layouts_[i];
        if (e.layout == nullptr) {
            if (free_slot == kMaxSetLayouts) free_slot = i;
        } else if (SameBindings(e.bindings, e.binding_count, res.bindings, res.binding_count)) {
            slot = i;
            break;
        }
    }
    if (slot == kMaxSetLayouts) {
        if (free_slot == kMaxSetLayouts) {
            RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: too many distinct layouts");
            return {};
        }
        VkDescriptorSetLayoutBinding vk_bindings[kMaxBindingsPerLayout];
        for (u32 i = 0; i < res.binding_count; ++i) {
            vk_bindings[i] = {};
            vk_bindings[i].binding = res.bindings[i].binding;
            vk_bindings[i].descriptorType = ToVkDescriptorType(res.bindings[i].type);
            vk_bindings[i].descriptorCount = res.bindings[i].count;
            vk_bindings[i].stageFlags = ToVkShaderStages(res.bindings[i].stages);
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = res.binding_count;
        info.pBindings = vk_bindings;
        VkDescriptorSetLayout vk_layout = nullptr;
        const VkResult result = fns_.CreateDescriptorSetLayout(device_, &info, nullptr, &vk_layout);
        if (result != VK_SUCCESS) {
            NoteDeviceLost(result);
            RhiLog(LogVerbosity::Error, "Vulkan: cannot create a descriptor set layout (%s)", VkResultName(result));
            return {};
        }
        SetDebugName(HandleToU64(vk_layout), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, desc.debug_name);
        SetLayoutEntry& e = set_layouts_[free_slot];
        e.layout = vk_layout;
        e.id = next_set_layout_id_++;
        e.binding_count = res.binding_count;
        for (u32 i = 0; i < res.binding_count; ++i) {
            e.bindings[i] = res.bindings[i];
        }
        e.refs = 0;
        slot = free_slot;
    }
    SetLayoutEntry& shared = set_layouts_[slot];
    res.set_slot = slot;
    res.id = shared.id;
    BindGroupLayoutHandle handle = bind_group_layouts_.Allocate(BindGroupLayoutRes(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: out of layout handles");
        if (shared.refs == 0) {
            ReleaseSetLayoutLocked(slot);
        }
        return {};
    }
    ++shared.refs;
    return handle;
}

void VulkanDevice::ReleaseSetLayoutLocked(u32 slot) {
    SetLayoutEntry& e = set_layouts_[slot];
    if (e.refs > 0) {
        --e.refs;
    }
    if (e.refs == 0 && e.layout != nullptr) {
        fns_.DestroyDescriptorSetLayout(device_, e.layout, nullptr);
        e.layout = nullptr;
    }
}

void VulkanDevice::DestroyBindGroupLayout(BindGroupLayoutHandle layout) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ENGINE_ASSERT(bind_group_layouts_.Get(layout) != nullptr, "DestroyBindGroupLayout: handle is invalid or already destroyed");
    if (bind_group_layouts_.Get(layout) == nullptr) {
        return;
    }
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    bind_group_layouts_.Retire(layout, frame_open_ ? current : current + 1);
}

// ---- pipeline layouts ----

VkPipelineLayout VulkanDevice::PipelineLayoutLocked(const u32* set_slots, const u64* set_ids, u32 count, u32 push_bytes,
                                                    ShaderStage push_stages, u32* slot) {
    usize free_slot = kMaxPipelineLayouts;
    for (usize i = 0; i < kMaxPipelineLayouts; ++i) {
        const PipelineLayoutEntry& e = pipeline_layouts_[i];
        if (e.layout == nullptr) {
            if (free_slot == kMaxPipelineLayouts) free_slot = i;
            continue;
        }
        if (e.set_count != count || e.push_bytes != push_bytes || (push_bytes != 0 && e.push_stages != push_stages)) {
            continue;
        }
        bool same = true;
        for (u32 s = 0; s < count && same; ++s) {
            same = e.set_ids[s] == set_ids[s];
        }
        if (same) {
            ++pipeline_layouts_[i].refs;
            *slot = static_cast<u32>(i);
            return e.layout;
        }
    }
    if (free_slot == kMaxPipelineLayouts) {
        RhiLog(LogVerbosity::Error, "Vulkan: too many distinct pipeline layouts");
        return nullptr;
    }
    VkDescriptorSetLayout sets[kMaxBindGroups];
    for (u32 s = 0; s < count; ++s) {
        sets[s] = set_layouts_[set_slots[s]].layout;
    }
    VkPushConstantRange range{};
    range.stageFlags = ToVkShaderStages(push_stages);
    range.offset = 0;
    range.size = push_bytes;
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = count;
    info.pSetLayouts = sets;
    info.pushConstantRangeCount = push_bytes != 0 ? 1 : 0;
    info.pPushConstantRanges = push_bytes != 0 ? &range : nullptr;
    VkPipelineLayout layout = nullptr;
    const VkResult result = fns_.CreatePipelineLayout(device_, &info, nullptr, &layout);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create a pipeline layout (%s)", VkResultName(result));
        return nullptr;
    }
    PipelineLayoutEntry& e = pipeline_layouts_[free_slot];
    *slot = static_cast<u32>(free_slot);
    e.refs = 1;
    e.layout = layout;
    e.set_count = count;
    for (u32 s = 0; s < count; ++s) {
        e.set_ids[s] = set_ids[s];
    }
    e.push_bytes = push_bytes;
    e.push_stages = push_stages;
    return layout;
}

void VulkanDevice::ReleasePipelineLayoutLocked(u32 slot) {
    PipelineLayoutEntry& e = pipeline_layouts_[slot];
    if (e.refs > 0 && --e.refs == 0) {
        fns_.DestroyPipelineLayout(device_, e.layout, nullptr);
        e = PipelineLayoutEntry{};
    }
}

// ---- graphics pipelines ----

PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: %s", why);
        return {};
    }
    if (desc.color_target_count > caps_.max_color_attachments || desc.bind_group_layout_count > caps_.max_bind_groups ||
        desc.push_constant_bytes > caps_.max_push_constant_bytes) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: exceeds a device limit (color targets, bind groups or push constants)");
        return {};
    }
    if (desc.wireframe && !fill_mode_non_solid_) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: this device cannot draw wireframe");
        return {};
    }
    ShaderInfo vs;
    if (!LookupShader(desc.vertex_shader, &vs) || vs.stage != ShaderStage::Vertex) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: vertex shader is invalid, destroyed or not a vertex stage");
        return {};
    }
    ShaderInfo fs;
    if (desc.fragment_shader.IsValid() && (!LookupShader(desc.fragment_shader, &fs) || fs.stage != ShaderStage::Fragment)) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: fragment shader is invalid, destroyed or not a fragment stage");
        return {};
    }
    for (u32 i = 0; i < desc.color_target_count; ++i) {
        if (!FormatSupport(desc.color_targets[i].format, TextureUsage::ColorAttachment)) {
            RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: color target %u (%s) cannot be rendered to on this device", i,
                   FormatInfo(desc.color_targets[i].format).name);
            return {};
        }
    }
    if (desc.depth_format != Format::Undefined && !FormatSupport(desc.depth_format, TextureUsage::DepthAttachment)) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: depth format %s cannot be rendered to on this device",
               FormatInfo(desc.depth_format).name);
        return {};
    }

    PipelineRes res;
    VkPipelineLayout layout = nullptr;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        u32 slots[kMaxBindGroups];
        for (u32 i = 0; i < desc.bind_group_layout_count; ++i) {
            const BindGroupLayoutRes* l = bind_group_layouts_.Get(desc.bind_group_layouts[i]);
            if (l == nullptr) {
                RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: bind group layout %u is invalid or destroyed", i);
                return {};
            }
            slots[i] = l->set_slot;
            res.layout_ids[i] = l->id;
        }
        res.layout_count = desc.bind_group_layout_count;
        res.push_bytes = desc.push_constant_bytes;
        res.push_stages = desc.push_constant_bytes != 0 ? desc.push_constant_stages : ShaderStage::None;
        layout = PipelineLayoutLocked(slots, res.layout_ids, res.layout_count, res.push_bytes, res.push_stages, &res.layout_slot);
        if (layout == nullptr) {
            return {};
        }
    }
    res.layout = layout;

    VkPipelineShaderStageCreateInfo stages[2]{};
    u32 stage_count = 0;
    stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[stage_count].module = vs.module;
    stages[stage_count].pName = vs.entry;
    ++stage_count;
    if (desc.fragment_shader.IsValid()) {
        stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage_count].module = fs.module;
        stages[stage_count].pName = fs.entry;
        ++stage_count;
    }

    VkVertexInputBindingDescription bindings[kMaxVertexBindings];
    for (usize i = 0; i < desc.vertex_bindings.count; ++i) {
        bindings[i] = {static_cast<u32>(i), desc.vertex_bindings[i].stride,
                       desc.vertex_bindings[i].per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    VkVertexInputAttributeDescription attributes[kMaxVertexAttributes];
    for (usize i = 0; i < desc.vertex_attributes.count; ++i) {
        const VertexAttributeDesc& a = desc.vertex_attributes[i];
        attributes[i] = {a.location, a.binding, ToVkVertexFormat(a.format), a.offset};
    }
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = static_cast<u32>(desc.vertex_bindings.count);
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = static_cast<u32>(desc.vertex_attributes.count);
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = ToVkTopology(desc.topology);
    assembly.primitiveRestartEnable = desc.primitive_restart ? VK_TRUE : VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = desc.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode = ToVkCullMode(desc.cull_mode);
    raster.frontFace = ToVkFrontFace(desc.front_face);
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = ToVkCompareOp(desc.depth_compare);
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blend_attachments[kMaxColorAttachments]{};
    VkFormat color_formats[kMaxColorAttachments];
    for (u32 i = 0; i < desc.color_target_count; ++i) {
        const ColorTargetDesc& t = desc.color_targets[i];
        VkPipelineColorBlendAttachmentState& b = blend_attachments[i];
        b.blendEnable = t.blend.enable ? VK_TRUE : VK_FALSE;
        b.srcColorBlendFactor = ToVkBlendFactor(t.blend.src_color);
        b.dstColorBlendFactor = ToVkBlendFactor(t.blend.dst_color);
        b.colorBlendOp = ToVkBlendOp(t.blend.color_op);
        b.srcAlphaBlendFactor = ToVkBlendFactor(t.blend.src_alpha);
        b.dstAlphaBlendFactor = ToVkBlendFactor(t.blend.dst_alpha);
        b.alphaBlendOp = ToVkBlendOp(t.blend.alpha_op);
        b.colorWriteMask = static_cast<VkColorComponentFlags>(t.write_mask);
        color_formats[i] = ToVkFormat(t.format);
        res.color_formats[i] = t.format;
    }
    res.color_count = desc.color_target_count;
    res.depth_format = desc.depth_format;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = desc.color_target_count;
    blend.pAttachments = blend_attachments;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = desc.color_target_count;
    rendering.pColorAttachmentFormats = color_formats;
    if (desc.depth_format != Format::Undefined) {
        rendering.depthAttachmentFormat = ToVkFormat(desc.depth_format);
        if (FormatInfo(desc.depth_format).has_stencil) {
            rendering.stencilAttachmentFormat = rendering.depthAttachmentFormat;
        }
    }

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.basePipelineIndex = -1;
    const VkResult result = fns_.CreateGraphicsPipelines(device_, pipeline_cache_, 1, &info, nullptr, &res.pipeline);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create the graphics pipeline '%s' (%s)", desc.debug_name != nullptr ? desc.debug_name : "",
               VkResultName(result));
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ReleasePipelineLayoutLocked(res.layout_slot);
        return {};
    }
    SetDebugName(HandleToU64(res.pipeline), VK_OBJECT_TYPE_PIPELINE, desc.debug_name);

    PipelineHandle handle;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        handle = pipelines_.Allocate(PipelineRes(res));
    }
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateGraphicsPipeline: out of pipeline handles");
        fns_.DestroyPipeline(device_, res.pipeline, nullptr);
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ReleasePipelineLayoutLocked(res.layout_slot);
    }
    return handle;
}

PipelineHandle VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: %s", why);
        return {};
    }
    if (desc.bind_group_layout_count > caps_.max_bind_groups || desc.push_constant_bytes > caps_.max_push_constant_bytes) {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: exceeds a device limit (bind groups or push constants)");
        return {};
    }
    ShaderInfo cs;
    if (!LookupShader(desc.shader, &cs) || cs.stage != ShaderStage::Compute) {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: shader is invalid, destroyed or not a compute stage");
        return {};
    }

    PipelineRes res;
    res.compute = true;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        u32 slots[kMaxBindGroups];
        for (u32 i = 0; i < desc.bind_group_layout_count; ++i) {
            const BindGroupLayoutRes* l = bind_group_layouts_.Get(desc.bind_group_layouts[i]);
            if (l == nullptr) {
                RhiLog(LogVerbosity::Error, "CreateComputePipeline: bind group layout %u is invalid or destroyed", i);
                return {};
            }
            slots[i] = l->set_slot;
            res.layout_ids[i] = l->id;
        }
        res.layout_count = desc.bind_group_layout_count;
        res.push_bytes = desc.push_constant_bytes;
        res.push_stages = desc.push_constant_bytes != 0 ? ShaderStage::Compute : ShaderStage::None;
        res.layout = PipelineLayoutLocked(slots, res.layout_ids, res.layout_count, res.push_bytes, res.push_stages, &res.layout_slot);
        if (res.layout == nullptr) {
            return {};
        }
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = cs.module;
    info.stage.pName = cs.entry;
    info.layout = res.layout;
    info.basePipelineIndex = -1;
    const VkResult result = fns_.CreateComputePipelines(device_, pipeline_cache_, 1, &info, nullptr, &res.pipeline);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create the compute pipeline '%s' (%s)", desc.debug_name != nullptr ? desc.debug_name : "",
               VkResultName(result));
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ReleasePipelineLayoutLocked(res.layout_slot);
        return {};
    }
    SetDebugName(HandleToU64(res.pipeline), VK_OBJECT_TYPE_PIPELINE, desc.debug_name);

    PipelineHandle handle;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        handle = pipelines_.Allocate(PipelineRes(res));
    }
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateComputePipeline: out of pipeline handles");
        fns_.DestroyPipeline(device_, res.pipeline, nullptr);
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ReleasePipelineLayoutLocked(res.layout_slot);
    }
    return handle;
}

void VulkanDevice::DestroyPipeline(PipelineHandle pipeline) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ENGINE_ASSERT(pipelines_.Get(pipeline) != nullptr, "DestroyPipeline: handle is invalid or already destroyed");
    if (pipelines_.Get(pipeline) == nullptr) {
        return;
    }
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    pipelines_.Retire(pipeline, frame_open_ ? current : current + 1);
}

bool VulkanDevice::LookupPipeline(PipelineHandle pipeline, PipelineInfo* info) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const PipelineRes* p = pipelines_.Get(pipeline);
    if (p == nullptr) {
        return false;
    }
    info->pipeline = p->pipeline;
    info->layout = p->layout;
    info->compute = p->compute;
    info->layout_count = p->layout_count;
    for (u32 i = 0; i < kMaxBindGroups; ++i) {
        info->layout_ids[i] = p->layout_ids[i];
    }
    info->push_bytes = p->push_bytes;
    info->push_stages = p->push_stages;
    info->color_count = p->color_count;
    for (u32 i = 0; i < kMaxColorAttachments; ++i) {
        info->color_formats[i] = p->color_formats[i];
    }
    info->depth_format = p->depth_format;
    return true;
}

} // namespace engine_placeholder::rhi::vulkan
