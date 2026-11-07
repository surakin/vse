// Descriptor pools and bind groups of the Vulkan device.

#include <cstring>

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_pipeline_policy.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

bool PoolIsFull(VkResult r) { return r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL; }

} // namespace

// A pool for `max_sets` sets. `exact` (when given) sizes the pool for exactly one set of that layout, for layouts whose
// arrays do not fit the default sizing; otherwise every descriptor type gets `per_type` descriptors per set.
VkDescriptorPool VulkanDevice::MakeDescriptorPoolLocked(u32 max_sets, u32 per_type, bool free_individual, const BindingDesc* exact,
                                                        u32 exact_count) {
    VkDescriptorPoolSize sizes[5];
    u32 counts[5] = {};
    const VkDescriptorType types[5] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                       VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    if (exact != nullptr) {
        for (u32 i = 0; i < exact_count; ++i) {
            counts[static_cast<usize>(exact[i].type)] += exact[i].count;
        }
    } else {
        for (u32& c : counts) {
            c = max_sets * per_type;
        }
    }
    u32 size_count = 0;
    for (u32 t = 0; t < 5; ++t) {
        if (counts[t] != 0) {
            sizes[size_count++] = {types[t], counts[t]};
        }
    }
    if (size_count == 0) {
        sizes[size_count++] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}; // an empty layout still needs a valid pool
    }
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = free_individual ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    info.maxSets = max_sets;
    info.poolSizeCount = size_count;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = nullptr;
    const VkResult result = fns_.CreateDescriptorPool(device_, &info, nullptr, &pool);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create a descriptor pool (%s)", VkResultName(result));
        return nullptr;
    }
    return pool;
}

// Allocates one set, growing the pool list instead of failing when a pool is full.
bool VulkanDevice::AllocateSetLocked(DescriptorPools& pools, bool persistent, VkDescriptorSetLayout layout, const BindingDesc* bindings,
                                     u32 binding_count, VkDescriptorSet* set, VkDescriptorPool* pool_out) {
    VkDescriptorSetAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    bool hard_failure = false; // anything but "this pool is full": no other pool would do better
    auto try_pool = [&](VkDescriptorPool pool) {
        info.descriptorPool = pool;
        const VkResult r = fns_.AllocateDescriptorSets(device_, &info, set);
        if (r == VK_SUCCESS) {
            *pool_out = pool;
            return true;
        }
        if (!PoolIsFull(r)) {
            hard_failure = true;
            NoteDeviceLost(r);
            RhiLog(LogVerbosity::Error, "Vulkan: cannot allocate a descriptor set (%s)", VkResultName(r));
        }
        return false;
    };

    // A layout needing more descriptors of a type than a default pool holds goes straight to a pool of its own, so the
    // driver (and the validation layer) is never asked for something that cannot fit.
    u32 needed[5] = {};
    for (u32 i = 0; i < binding_count; ++i) {
        needed[static_cast<usize>(bindings[i].type)] += bindings[i].count;
    }
    bool fits_default = true;
    for (u32 n : needed) {
        fits_default = fits_default && n <= descriptor_sets_per_pool_ * 4;
    }
    // Pools made for one large layout are reused by later groups that fit them (a freed or reset set leaves one free).
    auto allocate_exact = [&]() {
        for (usize i = 0; i < pools.exact.Size() && !hard_failure; ++i) {
            bool covers = true;
            for (u32 t = 0; t < 5; ++t) {
                covers = covers && pools.exact[i].counts[t] >= needed[t];
            }
            if (covers && try_pool(pools.exact[i].pool)) {
                return true;
            }
        }
        if (hard_failure) {
            return false;
        }
        VkDescriptorPool exact = MakeDescriptorPoolLocked(1, 0, persistent, bindings, binding_count);
        if (exact == nullptr) {
            return false;
        }
        ExactPool entry{exact, {}};
        for (u32 t = 0; t < 5; ++t) {
            entry.counts[t] = needed[t];
        }
        pools.exact.Push(entry);
        return try_pool(exact);
    };
    if (!fits_default) {
        return allocate_exact();
    }

    const usize existing = pools.pools.Size();
    if (persistent) {
        // Freed sets leave room in earlier pools: look from the current one around the whole list.
        for (usize n = 0; n < existing && !hard_failure; ++n) {
            const usize i = (pools.current + n) % existing;
            if (try_pool(pools.pools[i])) {
                pools.current = i;
                return true;
            }
        }
    } else {
        // A per-frame pool is only filled, never freed into, so move forward.
        while (pools.current < existing && !hard_failure) {
            if (try_pool(pools.pools[pools.current])) {
                return true;
            }
            ++pools.current;
        }
    }
    if (hard_failure) {
        return false;
    }
    VkDescriptorPool fresh = MakeDescriptorPoolLocked(descriptor_sets_per_pool_, 4, persistent, nullptr, 0);
    if (fresh == nullptr) {
        return false;
    }
    pools.pools.Push(fresh);
    const usize fresh_index = pools.pools.Size() - 1;
    if (try_pool(fresh)) {
        pools.current = fresh_index;
        return true;
    }
    return !hard_failure && allocate_exact(); // the layout does not fit the default sizing after all
}

void VulkanDevice::ResetFrameDescriptorsLocked(usize frame_slot) {
    DescriptorPools& pools = frame_pools_[frame_slot];
    for (usize i = 0; i < pools.pools.Size(); ++i) {
        fns_.ResetDescriptorPool(device_, pools.pools[i], 0);
    }
    for (usize i = 0; i < pools.exact.Size(); ++i) {
        fns_.ResetDescriptorPool(device_, pools.exact[i].pool, 0);
    }
    pools.current = 0;
    // The frame's transient groups ended with it: invalidate their handles. The stamp is the open frame, which no
    // earlier destroy exceeds, so the pool's retirement order holds.
    rhi::detail::GrowArray<BindGroupHandle>& groups = frame_groups_[frame_slot];
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    for (usize i = 0; i < groups.Size(); ++i) {
        bind_groups_.Retire(groups[i], current);
    }
    while (!groups.Empty()) {
        groups.Erase(groups.Size() - 1);
    }
}

void VulkanDevice::ReleaseBindGroupLocked(BindGroupRes* group) {
    if (!group->transient && group->set != nullptr && group->pool != nullptr) {
        fns_.FreeDescriptorSets(device_, group->pool, 1, &group->set);
    }
    group->refs.Reset();
    group->set = nullptr;
}

void VulkanDevice::ShutdownDescriptors() {
    auto destroy = [&](DescriptorPools& pools) {
        for (usize i = 0; i < pools.pools.Size(); ++i) {
            fns_.DestroyDescriptorPool(device_, pools.pools[i], nullptr);
        }
        for (usize i = 0; i < pools.exact.Size(); ++i) {
            fns_.DestroyDescriptorPool(device_, pools.exact[i].pool, nullptr);
        }
    };
    destroy(persistent_pools_);
    for (DescriptorPools& frame : frame_pools_) {
        destroy(frame);
    }
}

BindGroupHandle VulkanDevice::CreateBindGroup(const BindGroupDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: %s", why);
        return {};
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (desc.transient) {
        ENGINE_ASSERT(frame_open_, "CreateBindGroup: a transient group can only be created inside a frame");
    }
    const BindGroupLayoutRes* layout = bind_group_layouts_.Get(desc.layout);
    if (layout == nullptr) {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: layout handle is invalid or destroyed");
        return {};
    }
    u32 total = 0;
    for (u32 b = 0; b < layout->binding_count; ++b) {
        total += layout->bindings[b].count;
    }
    if (desc.entries.Size() != total) {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: %u entries given but the layout has %u binding elements",
               static_cast<u32>(desc.entries.Size()), total);
        return {};
    }

    BindGroupRes res;
    res.layout_id = layout->id;
    res.transient = desc.transient;
    res.frame = frame_number_.load(std::memory_order_relaxed);
    const usize slots = total == 0 ? 1 : total;
    Vector<VkWriteDescriptorSet> write_storage;
    Vector<VkDescriptorBufferInfo> buffer_info_storage;
    Vector<VkDescriptorImageInfo> image_info_storage;
    if (!res.refs.TryResize(slots) || !write_storage.TryResize(slots) || !buffer_info_storage.TryResize(slots) || !image_info_storage.TryResize(slots)) {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: out of memory");
        return {};
    }
    VkWriteDescriptorSet* writes = write_storage.Data();
    VkDescriptorBufferInfo* buffer_infos = buffer_info_storage.Data();
    VkDescriptorImageInfo* image_infos = image_info_storage.Data();
    auto fail = [&]() { return BindGroupHandle{}; };

    const VkDescriptorSetLayout set_layout = set_layouts_[layout->set_slot].layout;
    for (u32 b = 0; b < layout->binding_count; ++b) {
        const BindingDesc& bd = layout->bindings[b];
        for (u32 e = 0; e < bd.count; ++e) {
            const BindGroupEntry* found = nullptr;
            for (usize k = 0; k < desc.entries.Size(); ++k) {
                if (desc.entries[k].binding == bd.binding && desc.entries[k].array_element == e) {
                    found = &desc.entries[k];
                    break;
                }
            }
            if (found == nullptr) {
                RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u element %u has no entry", bd.binding, e);
                return fail();
            }
            GroupRef& ref = res.refs[res.ref_count];
            ref.type = bd.type;
            VkWriteDescriptorSet& w = writes[res.ref_count];
            w = {};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstBinding = bd.binding;
            w.dstArrayElement = e;
            w.descriptorCount = 1;
            w.descriptorType = ToVkDescriptorType(bd.type);
            switch (bd.type) {
            case BindingType::UniformBuffer:
            case BindingType::StorageBuffer: {
                const bool uniform = bd.type == BindingType::UniformBuffer;
                VkBuffer vk_buffer = nullptr;
                BufferDesc bdesc;
                // LookupBuffer takes the resource mutex we hold, so read the pool directly.
                const BufferRes* buf = buffers_.Get(found->buffer);
                if (buf == nullptr) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer is invalid or destroyed", bd.binding);
                    return fail();
                }
                vk_buffer = buf->raw.buffer;
                bdesc = buf->desc;
                if (!Has(bdesc.usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage)) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer lacks %s usage", bd.binding, uniform ? "Uniform" : "Storage");
                    return fail();
                }
                const u32 align = uniform ? caps_.min_uniform_buffer_offset_alignment : caps_.min_storage_buffer_offset_alignment;
                if (align != 0 && found->offset % align != 0) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u offset is not a multiple of %u", bd.binding, align);
                    return fail();
                }
                if (found->offset >= bdesc.size || (found->size != 0 && found->size > bdesc.size - found->offset)) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u range lies outside the buffer", bd.binding);
                    return fail();
                }
                const u64 range = found->size != 0 ? found->size : bdesc.size - found->offset;
                const u64 max_range = uniform ? properties_.limits.maxUniformBufferRange : properties_.limits.maxStorageBufferRange;
                if (range > max_range) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u range of %llu bytes exceeds the device limit of %llu", bd.binding,
                           static_cast<unsigned long long>(range), static_cast<unsigned long long>(max_range));
                    return fail();
                }
                VkDescriptorBufferInfo& bi = buffer_infos[res.ref_count];
                bi.buffer = vk_buffer;
                bi.offset = found->offset;
                bi.range = found->size != 0 ? found->size : VK_WHOLE_SIZE;
                w.pBufferInfo = &bi;
                ref.buffer = found->buffer;
                break;
            }
            case BindingType::SampledTexture:
            case BindingType::StorageTexture: {
                const bool sampled = bd.type == BindingType::SampledTexture;
                const TextureRes* tex = textures_.Get(found->texture);
                if (tex == nullptr) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture is invalid or destroyed", bd.binding);
                    return fail();
                }
                if (!Has(tex->desc.usage, sampled ? TextureUsage::Sampled : TextureUsage::Storage)) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture lacks %s usage", bd.binding, sampled ? "Sampled" : "Storage");
                    return fail();
                }
                VkDescriptorImageInfo& ii = image_infos[res.ref_count];
                ii.sampler = nullptr;
                ii.imageView = tex->view;
                ii.imageLayout = sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                w.pImageInfo = &ii;
                ref.texture = found->texture;
                break;
            }
            case BindingType::Sampler: {
                const SamplerRes* smp = samplers_.Get(found->sampler);
                if (smp == nullptr) {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u sampler is invalid or destroyed", bd.binding);
                    return fail();
                }
                VkDescriptorImageInfo& ii = image_infos[res.ref_count];
                ii.sampler = smp->sampler;
                ii.imageView = nullptr;
                ii.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                w.pImageInfo = &ii;
                ref.sampler = found->sampler;
                break;
            }
            case BindingType::Count:
                break;
            }
            ++res.ref_count;
        }
    }

    const usize frame_slot = static_cast<usize>(res.frame % kFramePools);
    DescriptorPools& pools = desc.transient ? frame_pools_[frame_slot] : persistent_pools_;
    if (!AllocateSetLocked(pools, !desc.transient, set_layout, layout->bindings, layout->binding_count, &res.set, &res.pool)) {
        return fail();
    }
    for (u32 i = 0; i < res.ref_count; ++i) {
        writes[i].dstSet = res.set;
    }
    if (res.ref_count > 0) {
        fns_.UpdateDescriptorSets(device_, res.ref_count, writes, 0, nullptr);
    }
    SetDebugName(HandleToU64(res.set), VK_OBJECT_TYPE_DESCRIPTOR_SET, desc.debug_name);

    BindGroupHandle handle = bind_groups_.Allocate(static_cast<BindGroupRes&&>(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: out of bind group handles");
        if (!res.transient) {
            fns_.FreeDescriptorSets(device_, res.pool, 1, &res.set);
        }
        return {};
    }
    if (desc.transient) {
        frame_groups_[frame_slot].Push(handle);
    }
    return handle;
}

void VulkanDevice::DestroyBindGroup(BindGroupHandle group) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const BindGroupRes* g = bind_groups_.Get(group);
    ENGINE_ASSERT(g != nullptr, "DestroyBindGroup: handle is invalid or already destroyed");
    if (g == nullptr) {
        return;
    }
    ENGINE_ASSERT(!g->transient, "DestroyBindGroup: a transient group is released by the device, not destroyed by the application");
    if (g->transient) {
        return;
    }
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    bind_groups_.Retire(group, frame_open_ ? current : current + 1);
}

bool VulkanDevice::LookupBindGroup(BindGroupHandle group, BindGroupInfo* info) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const BindGroupRes* g = bind_groups_.Get(group);
    if (g == nullptr) {
        return false;
    }
    info->set = g->set;
    info->layout_id = g->layout_id;
    info->transient = g->transient;
    info->frame = g->frame;
    info->resources_live = true;
    for (u32 i = 0; i < g->ref_count; ++i) {
        const GroupRef& ref = g->refs[i];
        const bool live = ref.type == BindingType::UniformBuffer || ref.type == BindingType::StorageBuffer ? buffers_.Get(ref.buffer) != nullptr
                          : ref.type == BindingType::Sampler                                              ? samplers_.Get(ref.sampler) != nullptr
                                                                                                          : textures_.Get(ref.texture) != nullptr;
        if (!live) {
            info->resources_live = false;
            break;
        }
    }
    return true;
}

bool VulkanDevice::LookupSampler(SamplerHandle sampler, VkSampler* vk_sampler) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const SamplerRes* s = samplers_.Get(sampler);
    if (s == nullptr) {
        return false;
    }
    *vk_sampler = s->sampler;
    return true;
}

} // namespace engine_placeholder::rhi::vulkan
