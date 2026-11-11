// The Direct3D 12 backend's samplers, bind group layouts, bind groups and root signatures (Phase 13 Day 166): sampler
// sharing, the structured-view stride rules, a group of every binding type, shared sampler tables, the transient groups
// and the shader-visible ring, and the root signature cache. Windows only.
#include <catch2/catch_test_macros.hpp>

#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::dx12;

namespace dx12_bindings_test
{

// Fails the test on any debug-layer warning or error while it lives.
struct BindingsSilence
{
    BindingsSilence() { ResetDebugMessageCounts(); }
    ~BindingsSilence()
    {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

Dx12DevicePtr MakeBindingsDevice()
{
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-bindings-test";
    info.validation = ValidationSetting::On;
    return Dx12Device::Create(info);
}

BindGroupLayoutHandle Layout(Dx12Device& device, const BindingDesc* bindings, usize count)
{
    BindGroupLayoutDesc d;
    d.entries = rhi::Span<BindingDesc>(const_cast<BindingDesc*>(bindings), count);
    return device.CreateBindGroupLayout(d);
}

BindGroupHandle Group(Dx12Device& device, BindGroupLayoutHandle layout, const BindGroupEntry* entries, usize count,
                      bool transient = false)
{
    BindGroupDesc d;
    d.layout = layout;
    d.transient = transient;
    d.entries = rhi::Span<BindGroupEntry>(const_cast<BindGroupEntry*>(entries), count);
    return device.CreateBindGroup(d);
}

BufferHandle Buffer(Dx12Device& device, u64 size, BufferUsage usage, MemoryLocation location)
{
    BufferDesc d;
    d.size = size;
    d.usage = usage;
    d.location = location;
    return device.CreateBuffer(d);
}

SamplerDesc Linear()
{
    return SamplerDesc{};
}

SamplerDesc Nearest()
{
    SamplerDesc s;
    s.mag_filter = s.min_filter = s.mip_filter = FilterMode::Nearest;
    s.address_u = s.address_v = s.address_w = AddressMode::ClampToEdge;
    return s;
}

BindGroupEntry SamplerEntry(u32 binding, SamplerHandle sampler)
{
    BindGroupEntry e;
    e.binding = binding;
    e.sampler = sampler;
    return e;
}

}  // namespace dx12_bindings_test

using namespace dx12_bindings_test;

TEST_CASE("DX12 samplers with equal descriptions are one sampler, counted once and destroyed with the last reference",
          "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const SamplerHandle a = device->CreateSampler(Linear());
    SamplerDesc named = Linear();
    named.debug_name = "the same, named";
    const SamplerHandle b = device->CreateSampler(named);
    const SamplerHandle c = device->CreateSampler(Nearest());
    SamplerDesc aniso = Linear();
    aniso.max_anisotropy = 16.0f;
    SamplerDesc compare = Linear();
    compare.compare_enable = true;
    compare.compare_op = CompareOp::LessEqual;
    compare.border_color = BorderColor::OpaqueWhite;
    compare.address_u = compare.address_v = AddressMode::ClampToBorder;
    const SamplerHandle d = device->CreateSampler(aniso);
    const SamplerHandle e = device->CreateSampler(compare);
    REQUIRE(a.IsValid());
    REQUIRE(c.IsValid());
    REQUIRE(d.IsValid());
    REQUIRE(e.IsValid());
    CHECK(a == b);
    CHECK(a != c);
    CHECK(device->Resources().samplers == 4);
    device->DestroySampler(a);
    CHECK(device->Resources().samplers == 4);  // b still holds it
    device->DestroySampler(b);
    device->DestroySampler(c);
    device->DestroySampler(d);
    device->DestroySampler(e);
    CHECK(device->Resources().samplers == 0);
    device->Quiesce();
}

TEST_CASE("A DX12 storage buffer binding needs a stride, and a bound range starts on an element", "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc no_stride[] = {{0, BindingType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1, 0}};
    CHECK_FALSE(Layout(*device, no_stride, 1).IsValid());
    const BindingDesc writable_no_stride[] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1, 0}};
    CHECK_FALSE(Layout(*device, writable_no_stride, 1).IsValid());

    const BindingDesc lights[] = {{0, BindingType::ReadOnlyStorageBuffer, ShaderStage::AllGraphics, 1, 48}};
    const BindGroupLayoutHandle layout = Layout(*device, lights, 1);
    REQUIRE(layout.IsValid());
    const BufferHandle buf = Buffer(*device, 48 * 64, BufferUsage::Storage, MemoryLocation::CpuToGpu);
    REQUIRE(buf.IsValid());
    BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = buf;
    entry.offset = 256;  // 256-aligned, but not a multiple of 48
    entry.size = 48 * 8;
    CHECK_FALSE(Group(*device, layout, &entry, 1).IsValid());
    entry.offset = 768;  // both: lcm(256, 48)
    const BindGroupHandle ok = Group(*device, layout, &entry, 1);
    CHECK(ok.IsValid());

    // A writable storage buffer must be in GPU memory: an upload heap cannot be an unordered access view.
    const BindingDesc rw[] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1, 4}};
    const BindGroupLayoutHandle rw_layout = Layout(*device, rw, 1);
    REQUIRE(rw_layout.IsValid());
    BindGroupEntry rw_entry;
    rw_entry.binding = 0;
    rw_entry.buffer = buf;
    CHECK_FALSE(Group(*device, rw_layout, &rw_entry, 1).IsValid());
    const BufferHandle gpu = Buffer(*device, 4096, BufferUsage::Storage, MemoryLocation::GpuOnly);
    rw_entry.buffer = gpu;
    const BindGroupHandle rw_ok = Group(*device, rw_layout, &rw_entry, 1);
    CHECK(rw_ok.IsValid());

    device->DestroyBindGroup(ok);
    device->DestroyBindGroup(rw_ok);
    device->DestroyBindGroupLayout(layout);
    device->DestroyBindGroupLayout(rw_layout);
    device->DestroyBuffer(buf);
    device->DestroyBuffer(gpu);
    device->Quiesce();
}

TEST_CASE(
    "DX12 refuses a uniform range above 64 KiB, a storage range shorter than one element, and over 256 samplers in a group",
    "[rhi][dx12][gpu]")
{
    // Review findings (Day 166): the first two reached D3D12 as invalid views, the third failed only when a group was
    // made.
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc ubo_binding[] = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1, 0}};
    const BindingDesc ro_binding[] = {{0, BindingType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1, 64}};
    const BindGroupLayoutHandle ubo_layout = Layout(*device, ubo_binding, 1);
    const BindGroupLayoutHandle ro_layout = Layout(*device, ro_binding, 1);
    const BufferHandle big = Buffer(*device, 128 * 1024, BufferUsage::Uniform | BufferUsage::Storage,
                                    MemoryLocation::CpuToGpu);
    REQUIRE((ubo_layout.IsValid() && ro_layout.IsValid() && big.IsValid()));
    BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = big;  // size 0: to the end, 128 KiB
    CHECK_FALSE(Group(*device, ubo_layout, &entry, 1).IsValid());
    entry.size = 64 * 1024;  // exactly the limit
    const BindGroupHandle at_limit = Group(*device, ubo_layout, &entry, 1);
    CHECK(at_limit.IsValid());
    entry.size = 32;  // half an element
    CHECK_FALSE(Group(*device, ro_layout, &entry, 1).IsValid());
    entry.size = 64;
    const BindGroupHandle one_element = Group(*device, ro_layout, &entry, 1);
    CHECK(one_element.IsValid());

    const BindingDesc many[] = {{0, BindingType::Sampler, ShaderStage::Fragment, 200, 0},
                                {1, BindingType::Sampler, ShaderStage::Fragment, 57, 0}};
    CHECK_FALSE(Layout(*device, many, 2).IsValid());
    const BindingDesc enough[] = {{0, BindingType::Sampler, ShaderStage::Fragment, 200, 0},
                                  {1, BindingType::Sampler, ShaderStage::Fragment, 56, 0}};
    const BindGroupLayoutHandle enough_layout = Layout(*device, enough, 2);
    CHECK(enough_layout.IsValid());

    device->DestroyBindGroup(at_limit);
    device->DestroyBindGroup(one_element);
    device->DestroyBindGroupLayout(ubo_layout);
    device->DestroyBindGroupLayout(ro_layout);
    device->DestroyBindGroupLayout(enough_layout);
    device->DestroyBuffer(big);
    device->Quiesce();
}

TEST_CASE("A DX12 bind group of every binding type has its views together and its samplers in a table",
          "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc bindings[] = {
        {5, BindingType::Sampler, ShaderStage::Fragment, 1, 0},
        {0, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1, 0},
        {1, BindingType::ReadOnlyStorageBuffer, ShaderStage::Vertex, 1, 16},
        {2, BindingType::StorageBuffer, ShaderStage::Fragment, 1, 4},
        {3, BindingType::SampledTexture, ShaderStage::Fragment, 2, 0},
        {4, BindingType::StorageTexture, ShaderStage::Fragment, 1, 0},
    };
    const BindGroupLayoutHandle layout = Layout(*device, bindings, 6);
    REQUIRE(layout.IsValid());
    const BufferHandle ubo = Buffer(*device, 200, BufferUsage::Uniform, MemoryLocation::CpuToGpu);  // rounded up to 256
    const BufferHandle ro = Buffer(*device, 1024, BufferUsage::Storage, MemoryLocation::CpuToGpu);
    const BufferHandle rw = Buffer(*device, 1024, BufferUsage::Storage, MemoryLocation::GpuOnly);
    TextureDesc td;
    td.width = 16;
    td.height = 16;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled | TextureUsage::Storage;
    const TextureHandle tex = device->CreateTexture(td);
    const SamplerHandle smp = device->CreateSampler(Linear());
    REQUIRE((ubo.IsValid() && ro.IsValid() && rw.IsValid() && tex.IsValid() && smp.IsValid()));

    BindGroupEntry entries[7];
    entries[0].binding = 0;
    entries[0].buffer = ubo;
    entries[1].binding = 1;
    entries[1].buffer = ro;
    entries[1].offset = 256;
    entries[1].size = 512;
    entries[2].binding = 2;
    entries[2].buffer = rw;
    entries[3].binding = 3;
    entries[3].texture = tex;
    entries[4].binding = 3;
    entries[4].array_element = 1;
    entries[4].texture = tex;
    entries[5].binding = 4;
    entries[5].texture = tex;
    entries[6] = SamplerEntry(5, smp);
    const BindGroupHandle group = Group(*device, layout, entries, 7);
    REQUIRE(group.IsValid());
    Dx12Device::BindGroupInfo info;
    REQUIRE(device->LookupBindGroup(group, &info));
    CHECK(info.view_count == 6);  // the texture array counts twice
    CHECK(info.sampler_count == 1);
    CHECK(info.samplers.ptr != 0);
    CHECK(info.resources_live);
    CHECK_FALSE(info.transient);
    CHECK(device->Resources().bind_groups == 1);

    // An element left out is an error, as on Vulkan.
    CHECK_FALSE(Group(*device, layout, entries, 6).IsValid());
    BindGroupEntry missing[7];
    for (int i = 0; i < 7; ++i)
    {
        missing[i] = entries[i];
    }
    missing[4].array_element = 0;  // element 1 of binding 3 has no entry now
    CHECK_FALSE(Group(*device, layout, missing, 7).IsValid());

    // Destroying a resource the group was made from shows in the lookup.
    device->DestroyBuffer(ro);
    REQUIRE(device->LookupBindGroup(group, &info));
    CHECK_FALSE(info.resources_live);

    device->DestroyBindGroup(group);
    CHECK_FALSE(device->LookupBindGroup(group, &info));
    device->DestroyBindGroupLayout(layout);
    device->DestroySampler(smp);
    device->DestroyTexture(tex);
    device->DestroyBuffer(ubo);
    device->DestroyBuffer(rw);
    device->Quiesce();
    const ResourceCounts after = device->Resources();
    CHECK(after.bind_groups == 0);
    CHECK(after.bind_group_layouts == 0);
    CHECK(after.samplers == 0);
}

TEST_CASE("DX12 groups with the same samplers share one table, which is freed with the last group", "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const u32 free_at_start = device->FreeSamplerDescriptors();
    CHECK(free_at_start == GpuDescriptorTables::kCapacity);
    const BindingDesc two[] = {{0, BindingType::Sampler, ShaderStage::Fragment, 1, 0},
                               {1, BindingType::Sampler, ShaderStage::Fragment, 1, 0}};
    const BindGroupLayoutHandle layout = Layout(*device, two, 2);
    const SamplerHandle lin = device->CreateSampler(Linear());
    const SamplerHandle near = device->CreateSampler(Nearest());
    const BindGroupEntry ab[] = {SamplerEntry(0, lin), SamplerEntry(1, near)};
    const BindGroupEntry ba[] = {SamplerEntry(0, near), SamplerEntry(1, lin)};
    const BindGroupHandle g1 = Group(*device, layout, ab, 2);
    const BindGroupHandle g2 = Group(*device, layout, ab, 2);
    const BindGroupHandle g3 = Group(*device, layout, ba, 2);
    REQUIRE((g1.IsValid() && g2.IsValid() && g3.IsValid()));
    Dx12Device::BindGroupInfo i1, i2, i3;
    REQUIRE(device->LookupBindGroup(g1, &i1));
    REQUIRE(device->LookupBindGroup(g2, &i2));
    REQUIRE(device->LookupBindGroup(g3, &i3));
    CHECK(i1.samplers.ptr == i2.samplers.ptr);
    CHECK(i1.samplers.ptr != i3.samplers.ptr);  // the order is part of the table
    CHECK(device->FreeSamplerDescriptors() == free_at_start - 4);
    device->DestroyBindGroup(g1);
    device->Quiesce();
    CHECK(device->FreeSamplerDescriptors() == free_at_start - 4);  // g2 still uses the table
    device->DestroyBindGroup(g2);
    device->DestroyBindGroup(g3);
    device->Quiesce();
    CHECK(device->FreeSamplerDescriptors() == free_at_start);
    device->DestroySampler(lin);
    device->DestroySampler(near);
    device->DestroyBindGroupLayout(layout);
    device->Quiesce();
}

TEST_CASE("A DX12 sampler table is not shared with a later sampler that reuses a destroyed sampler's descriptor",
          "[rhi][dx12][gpu]")
{
    // Review finding (Day 166): tables were keyed by the samplers' CPU descriptors, which a new sampler can reuse once
    // the old one is collected, while a group made from the old one still holds the table with the old sampler in it.
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc one[] = {{0, BindingType::Sampler, ShaderStage::Fragment, 1, 0}};
    const BindGroupLayoutHandle layout = Layout(*device, one, 1);
    const SamplerHandle old_sampler = device->CreateSampler(Linear());
    const BindGroupEntry old_entry = SamplerEntry(0, old_sampler);
    const BindGroupHandle old_group = Group(*device, layout, &old_entry, 1);
    REQUIRE(old_group.IsValid());
    device->DestroySampler(old_sampler);
    device->Quiesce();  // the descriptor is free now; the group (and its table) lives on
    const SamplerHandle new_sampler = device->CreateSampler(Nearest());
    const BindGroupEntry new_entry = SamplerEntry(0, new_sampler);
    const BindGroupHandle new_group = Group(*device, layout, &new_entry, 1);
    REQUIRE(new_group.IsValid());
    Dx12Device::BindGroupInfo old_info, new_info;
    REQUIRE(device->LookupBindGroup(old_group, &old_info));
    REQUIRE(device->LookupBindGroup(new_group, &new_info));
    CHECK_FALSE(old_info.resources_live);
    CHECK(old_info.samplers.ptr != new_info.samplers.ptr);
    device->DestroyBindGroup(old_group);
    device->DestroyBindGroup(new_group);
    device->DestroySampler(new_sampler);
    device->DestroyBindGroupLayout(layout);
    device->Quiesce();
}

TEST_CASE("DX12 transient groups end with their frame slot, and the view ring's slot is emptied then",
          "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc ubo_binding[] = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1, 0}};
    const BindGroupLayoutHandle layout = Layout(*device, ubo_binding, 1);
    const BufferHandle ubo = Buffer(*device, 256, BufferUsage::Uniform, MemoryLocation::CpuToGpu);
    BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = ubo;

    device->BeginFrame();
    const u32 slot = static_cast<u32>(device->FrameNumber() % Dx12Device::kFrameSlots);
    const BindGroupHandle transient = Group(*device, layout, &entry, 1, true);
    REQUIRE(transient.IsValid());
    Dx12Device::BindGroupInfo info;
    REQUIRE(device->LookupBindGroup(transient, &info));
    CHECK(info.transient);
    CHECK(device->Resources().bind_groups == 0);  // transient groups are not the application's to destroy
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    REQUIRE(device->ViewRing().Allocate(slot, 8, &cpu, &gpu));
    CHECK(device->ViewRing().Used(slot) == 8);
    device->EndFrame();
    for (u32 i = 1; i < Dx12Device::kFrameSlots; ++i)
    {
        device->BeginFrame();
        CHECK(device->LookupBindGroup(transient, &info));  // still alive while its slot is not reused
        device->EndFrame();
    }
    device->BeginFrame();  // the slot comes round again
    CHECK_FALSE(device->LookupBindGroup(transient, &info));
    CHECK(device->ViewRing().Used(slot) == 0);
    device->EndFrame();

    // A full slot refuses more, and only that slot.
    const u32 per_slot = device->ViewRing().PerSlot();
    CHECK(per_slot == Dx12Device::kViewsPerFrame);
    REQUIRE(device->ViewRing().Allocate(0, per_slot, &cpu, &gpu));
    CHECK_FALSE(device->ViewRing().Allocate(0, 1, &cpu, &gpu));
    CHECK(device->ViewRing().Allocate(1, 1, &cpu, &gpu));
    device->ViewRing().Reset(0);
    device->ViewRing().Reset(1);

    device->DestroyBindGroupLayout(layout);
    device->DestroyBuffer(ubo);
    device->Quiesce();
}

TEST_CASE("DX12 root signatures are cached by layout content and push range, with a table per group and root constants",
          "[rhi][dx12][gpu]")
{
    Dx12DevicePtr device = MakeBindingsDevice();
    REQUIRE(device);
    BindingsSilence silence;
    const BindingDesc views_and_sampler[] = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1, 0},
                                             {1, BindingType::SampledTexture, ShaderStage::Fragment, 1, 0},
                                             {2, BindingType::Sampler, ShaderStage::Fragment, 1, 0}};
    const BindingDesc same_reordered[] = {views_and_sampler[2], views_and_sampler[0], views_and_sampler[1]};
    const BindingDesc samplers_only[] = {{0, BindingType::Sampler, ShaderStage::Fragment, 1, 0}};
    const BindGroupLayoutHandle a = Layout(*device, views_and_sampler, 3);
    const BindGroupLayoutHandle a2 = Layout(*device, same_reordered, 3);
    const BindGroupLayoutHandle b = Layout(*device, samplers_only, 1);
    REQUIRE((a.IsValid() && a2.IsValid() && b.IsValid()));
    const usize start = device->RootSignatureCount();

    const BindGroupLayoutHandle ab[] = {a, b};
    Dx12Device::RootLayout first;
    REQUIRE(device->RootSignatureFor(ab, 2, 16, ShaderStage::Vertex, false, &first));
    CHECK(first.signature != nullptr);
    CHECK(first.layout_count == 2);
    CHECK(first.view_param[0] == 0);
    CHECK(first.sampler_param[0] == 1);
    CHECK(first.view_param[1] == -1);
    CHECK(first.sampler_param[1] == 2);
    CHECK(first.push_param == 3);
    CHECK(first.push_values == 4);
    CHECK(device->RootSignatureCount() == start + 1);

    // An equal layout made separately (in another order) is the same identity: a cache hit.
    const BindGroupLayoutHandle a2b[] = {a2, b};
    Dx12Device::RootLayout again;
    REQUIRE(device->RootSignatureFor(a2b, 2, 16, ShaderStage::Vertex, false, &again));
    CHECK(again.signature == first.signature);
    CHECK(device->RootSignatureCount() == start + 1);

    // Another push range, compute, or no groups at all: new signatures.
    Dx12Device::RootLayout other;
    REQUIRE(device->RootSignatureFor(ab, 2, 0, ShaderStage::None, false, &other));
    CHECK(other.push_param == -1);
    REQUIRE(device->RootSignatureFor(ab, 2, 16, ShaderStage::Vertex, true, &other));
    REQUIRE(device->RootSignatureFor(nullptr, 0, 0, ShaderStage::None, false, &other));
    CHECK(device->RootSignatureCount() == start + 4);

    // A destroyed layout is refused.
    device->DestroyBindGroupLayout(b);
    CHECK_FALSE(device->RootSignatureFor(ab, 2, 16, ShaderStage::Vertex, false, &other));
    device->DestroyBindGroupLayout(a);
    device->DestroyBindGroupLayout(a2);
    device->Quiesce();
}
