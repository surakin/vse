// The Metal backend's bindings (Phase 21 Day 302): the slot map built from a minted library's slot table against a
// pipeline's layouts, its mismatches rejected, the binder skipping unchanged slots and starting over on a new encoder,
// the sampler cache, bind group validation, transient groups, and groups resolved by handle. macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "bindings_test.frag.dxil.inc"
#include "containers/array.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace vse;
using namespace vse::rhi;
using namespace vse::rhi::metal;
using namespace vse::rhi::metal::test;

namespace
{

// The layout bindings_test.frag declares: a uniform buffer, a texture and a sampler in set 0.
BindGroupLayoutHandle FragmentLayout(Device& device, bool with_texture = true,
                                     ShaderStage stages = ShaderStage::Fragment)
{
    Array<BindingDesc, 3> b;
    b[0].binding = 0;
    b[0].type = BindingType::UniformBuffer;
    b[0].stages = stages;
    b[1].binding = 2;
    b[1].type = BindingType::Sampler;
    b[1].stages = stages;
    b[2].binding = 1;
    b[2].type = BindingType::SampledTexture;
    b[2].stages = stages;
    BindGroupLayoutDesc d;
    d.entries = vse::Span<BindingDesc>(b.Data(), with_texture ? 3 : 2);
    return device.CreateBindGroupLayout(d);
}

struct Resources
{
    BufferHandle uniforms;
    TextureHandle texture;
    SamplerHandle sampler;
};

Resources MakeResources(Device& device)
{
    Resources r;
    BufferDesc bd;
    bd.size = 1024;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    r.uniforms = device.CreateBuffer(bd);
    TextureDesc td;
    td.width = 4;
    td.height = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::Sampled;
    r.texture = device.CreateTexture(td);
    r.sampler = device.CreateSampler(SamplerDesc{});
    return r;
}

BindGroupHandle MakeGroup(Device& device, BindGroupLayoutHandle layout, const Resources& r, u64 offset = 0,
                          bool transient = false)
{
    Array<BindGroupEntry, 3> e;
    e[0].binding = 0;
    e[0].buffer = r.uniforms;
    e[0].offset = offset;
    e[0].size = 256;
    e[1].binding = 1;
    e[1].texture = r.texture;
    e[2].binding = 2;
    e[2].sampler = r.sampler;
    BindGroupDesc d;
    d.layout = layout;
    d.entries = vse::Span<BindGroupEntry>(e.Data(), 3);
    d.transient = transient;
    return device.CreateBindGroup(d);
}

const ShaderSlotTable* FragmentSlots()
{
    return shaders::kBindingsTestFragShader.For(ShaderFormat::MetalLib).slots;
}

}  // namespace

TEST_CASE("A minted Metal library carries its slot table into the shader descriptor", "[rhi][metal]")
{
    const ShaderBlob blob = shaders::kBindingsTestFragShader.For(ShaderFormat::MetalLib);
    REQUIRE(blob.slots != nullptr);
    CHECK(blob.slots->count == 3);
    CHECK(blob.slots->push_constant_slot == 1);  // the uniform buffer has buffer(0)
    ShaderDesc d;
    d.SetCode(blob);
    CHECK(d.slots == blob.slots);
    CHECK(shaders::kBindingsTestFragShader.For(ShaderFormat::Spirv).slots == nullptr);
}

TEST_CASE("Metal slots come from the library's table matched against the layouts", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    const BindGroupLayoutHandle layout = FragmentLayout(*device);
    REQUIRE(layout.IsValid());
    MetalStageSlots slots;
    char error[256] = {};
    REQUIRE(MapStageSlots(AsMetal(*device), ShaderStage::Fragment, FragmentSlots(), &layout, 1, &slots, error,
                          sizeof(error)));
    INFO(error);
    REQUIRE(slots.refs.Size() == 3);
    CHECK(slots.push_constant_slot == 1);
    for (usize i = 0; i < slots.refs.Size(); ++i)
    {
        const MetalSlotRef& r = slots.refs[i];
        CHECK(r.set == 0);
        CHECK(r.slot == 0);  // each in its own table: buffer(0), texture(0), sampler(0)
        CHECK(r.type == (r.binding == 0   ? BindingType::UniformBuffer
                         : r.binding == 1 ? BindingType::SampledTexture
                                          : BindingType::Sampler));
    }
    device->DestroyBindGroupLayout(layout);
}

TEST_CASE("A pipeline layout that disagrees with the library's slots is rejected with the reason", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    MetalStageSlots slots;
    char error[256] = {};
    // The layout lacks the texture the shader samples.
    const BindGroupLayoutHandle missing = FragmentLayout(*device, false);
    CHECK_FALSE(MapStageSlots(AsMetal(*device), ShaderStage::Fragment, FragmentSlots(), &missing, 1, &slots, error,
                              sizeof(error)));
    CHECK(CStrFind(error, "set 0 binding 1, which the set's layout lacks") != nullptr);
    // The bindings are not visible to the fragment stage.
    const BindGroupLayoutHandle vertex_only = FragmentLayout(*device, true, ShaderStage::Vertex);
    CHECK_FALSE(MapStageSlots(AsMetal(*device), ShaderStage::Fragment, FragmentSlots(), &vertex_only, 1, &slots, error,
                              sizeof(error)));
    CHECK(CStrFind(error, "not visible to that stage") != nullptr);
    // No layout for the set at all.
    CHECK_FALSE(MapStageSlots(AsMetal(*device), ShaderStage::Fragment, FragmentSlots(), nullptr, 0, &slots, error,
                              sizeof(error)));
    CHECK(CStrFind(error, "no valid layout for set 0") != nullptr);
    // A table whose buffer shares the push-constant block's slot, and one where two buffers share a slot.
    const BindGroupLayoutHandle full = FragmentLayout(*device);
    const ShaderSlot clash[1] = {{0, 0, 1}};
    const ShaderSlotTable on_push{clash, 1, 1};
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, &on_push, &full, 1, &slots, error, sizeof(error)));
    Array<BindingDesc, 2> two;
    two[0].binding = 0;
    two[0].type = BindingType::UniformBuffer;
    two[0].stages = ShaderStage::Fragment;
    two[1].binding = 1;
    two[1].type = BindingType::ReadOnlyStorageBuffer;
    two[1].stages = ShaderStage::Fragment;
    two[1].stride = 16;
    BindGroupLayoutDesc ld;
    ld.entries = vse::Span<BindingDesc>(two.Data(), 2);
    const BindGroupLayoutHandle buffers = device->CreateBindGroupLayout(ld);
    const ShaderSlot same[2] = {{0, 0, 3}, {0, 1, 3}};
    const ShaderSlotTable shared{same, 2, kNoShaderSlot};
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, &shared, &buffers, 1, &slots, error, sizeof(error)));
    CHECK(CStrFind(error, "share the fragment shader's slot 3") != nullptr);
    // The shader's declared type and array size must be the layout's.
    const ShaderSlot as_sampler[1] = {{0, 0, 0, BindingType::Sampler, 1}};
    const ShaderSlotTable wrong_type{as_sampler, 1, kNoShaderSlot};
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, &wrong_type, &full, 1, &slots, error, sizeof(error)));
    CHECK(CStrFind(error, "is a sampler in the fragment shader but a buffer in the layout") != nullptr);
    const ShaderSlot four[1] = {{0, 1, 0, BindingType::SampledTexture, 4}};
    const ShaderSlotTable wrong_count{four, 1, kNoShaderSlot};
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, &wrong_count, &full, 1, &slots, error, sizeof(error)));
    CHECK(CStrFind(error, "has 4 elements in the fragment shader but 1 in the layout") != nullptr);
    // Push constants outside the buffer table.
    const ShaderSlotTable far_push{nullptr, 0, 31};
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, &far_push, &full, 1, &slots, error, sizeof(error)));
    CHECK(CStrFind(error, "outside Metal's table") != nullptr);
    // A module without a table is refused when the stage has something to bind, and maps when it has nothing.
    CHECK_FALSE(
        MapStageSlots(AsMetal(*device), ShaderStage::Fragment, nullptr, &full, 1, &slots, error, sizeof(error)));
    CHECK(CStrFind(error, "has no slot table") != nullptr);
    CHECK(MapStageSlots(AsMetal(*device), ShaderStage::Vertex, nullptr, &full, 1, &slots, error, sizeof(error)));
    CHECK(MapStageSlots(AsMetal(*device), ShaderStage::Fragment, nullptr, nullptr, 0, &slots, error, sizeof(error)));
    CHECK(slots.refs.Empty());
    device->DestroyBindGroupLayout(missing);
    device->DestroyBindGroupLayout(vertex_only);
    device->DestroyBindGroupLayout(full);
    device->DestroyBindGroupLayout(buffers);
}

TEST_CASE("The Metal binder skips slots that already hold the resource, and starts over on a new encoder",
          "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    const BindGroupLayoutHandle layout = FragmentLayout(*device);
    const Resources r = MakeResources(*device);
    const BindGroupHandle a = MakeGroup(*device, layout, r, 0);
    const BindGroupHandle b = MakeGroup(*device, layout, r, 256);  // the same resources, another buffer offset
    REQUIRE(a.IsValid());
    REQUIRE(b.IsValid());
    MetalStageSlots slots;
    char error[256] = {};
    REQUIRE(MapStageSlots(metal, ShaderStage::Fragment, FragmentSlots(), &layout, 1, &slots, error, sizeof(error)));
    // A layout with a sampler at binding 0, and a group of it.
    BindingDesc sampler_binding;
    sampler_binding.binding = 0;
    sampler_binding.type = BindingType::Sampler;
    sampler_binding.stages = ShaderStage::Fragment;
    BindGroupLayoutDesc sld;
    sld.entries = vse::Span<BindingDesc>(&sampler_binding, 1);
    const BindGroupLayoutHandle sampler_layout = device->CreateBindGroupLayout(sld);
    BindGroupEntry sampler_entry;
    sampler_entry.binding = 0;
    sampler_entry.sampler = r.sampler;
    BindGroupDesc sgd;
    sgd.layout = sampler_layout;
    sgd.entries = vse::Span<BindGroupEntry>(&sampler_entry, 1);
    const BindGroupHandle sampler_group = device->CreateBindGroup(sgd);
    REQUIRE(sampler_group.IsValid());
    @autoreleasepool
    {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                      width:4
                                                                                     height:4
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> target = [metal.Mtl() newTextureWithDescriptor:td];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [metal.Queue() commandBuffer];
        MetalBinder binder;
        for (int encoder_round = 0; encoder_round < 2; ++encoder_round)
        {
            id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
            binder.Reset();
            CHECK(binder.BindRender(metal, encoder, nullptr, &slots, 0, a));
            CHECK(binder.IssuedCalls() == 3);  // buffer, texture, sampler
            CHECK(binder.BindRender(metal, encoder, nullptr, &slots, 0, a));
            CHECK(binder.IssuedCalls() == 3);  // nothing changed
            CHECK(binder.BindRender(metal, encoder, nullptr, &slots, 0, b));
            CHECK(binder.IssuedCalls() == 4);  // only the buffer offset changed
            CHECK(binder.BindRender(metal, encoder, &slots, nullptr, 0, a));
            CHECK(binder.IssuedCalls() == 7);  // the vertex stage's slots are its own
            const u32 push[4] = {1, 2, 3, 4};
            binder.PushRender(encoder, nullptr, &slots, push, sizeof(push));  // at buffer(1)
            CHECK(binder.IssuedCalls() == 8);
            // A group whose binding 0 is a sampler, bound where the pipeline has a buffer: refused, nothing set.
            const bool was = LogRhi.Enabled();
            LogRhi.SetEnabled(false);
            CHECK_FALSE(binder.BindRender(metal, encoder, nullptr, &slots, 0, sampler_group));
            LogRhi.SetEnabled(was);
            CHECK(binder.IssuedCalls() == 8);
            [encoder endEncoding];
        }
        [commands commit];
        [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);
    }
    device->DestroyBindGroup(a);
    device->DestroyBindGroup(b);
    device->DestroyBindGroup(sampler_group);
    device->DestroyBindGroupLayout(sampler_layout);
    device->DestroyBindGroupLayout(layout);
    device->DestroyBuffer(r.uniforms);
    device->DestroyTexture(r.texture);
    device->DestroySampler(r.sampler);
    device->WaitIdle();
    const ResourceCounts c = device->Resources();
    CHECK(c.bind_groups == 0);
    CHECK(c.bind_group_layouts == 0);
    CHECK(c.samplers == 0);
}

TEST_CASE("A slot table listed out of binding order maps sorted and binds every resource (container audit Day 12)",
          "[rhi][metal][gpu]")
{
    // The binder finds a group's resource in a stage's refs by a binary search, so MapStageSlots must sort them by
    // (set, binding) whatever order the library's table lists them in.
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    const BindGroupLayoutHandle layout = FragmentLayout(*device);
    const Resources r = MakeResources(*device);
    const BindGroupHandle group = MakeGroup(*device, layout, r, 0);
    REQUIRE(group.IsValid());
    const ShaderSlot reversed[3] = {{0, 2, 0, BindingType::Sampler, 1},
                                    {0, 1, 0, BindingType::SampledTexture, 1},
                                    {0, 0, 0, BindingType::UniformBuffer, 1}};
    const ShaderSlotTable table{reversed, 3, kNoShaderSlot};
    MetalStageSlots slots;
    char error[256] = {};
    REQUIRE(MapStageSlots(metal, ShaderStage::Fragment, &table, &layout, 1, &slots, error, sizeof(error)));
    INFO(error);
    REQUIRE(slots.refs.Size() == 3);
    for (usize i = 0; i < slots.refs.Size(); ++i)
    {
        CHECK(slots.refs[i].binding == i);
    }
    @autoreleasepool
    {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                      width:4
                                                                                     height:4
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> target = [metal.Mtl() newTextureWithDescriptor:td];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [metal.Queue() commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        MetalBinder binder;
        binder.Reset();
        CHECK(binder.BindRender(metal, encoder, nullptr, &slots, 0, group));
        CHECK(binder.IssuedCalls() == 3);  // buffer, texture and sampler all found
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);
    }
    device->DestroyBindGroup(group);
    device->DestroyBindGroupLayout(layout);
    device->DestroyBuffer(r.uniforms);
    device->DestroyTexture(r.texture);
    device->DestroySampler(r.sampler);
    device->WaitIdle();
}

TEST_CASE("Equal Metal sampler descriptions share one sampler until the last is destroyed", "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    SamplerDesc d;
    d.min_filter = FilterMode::Nearest;
    d.address_u = AddressMode::ClampToBorder;
    d.border_color = BorderColor::OpaqueWhite;
    d.max_anisotropy = 16.0f;  // the largest a description may ask for
    const SamplerHandle first = device->CreateSampler(d);
    d.debug_name = "the same, named";
    const SamplerHandle second = device->CreateSampler(d);
    REQUIRE(first.IsValid());
    CHECK(first == second);
    CHECK(device->Resources().samplers == 1);
    SamplerDesc compare;
    compare.compare_enable = true;
    compare.compare_op = CompareOp::LessEqual;
    const SamplerHandle other = device->CreateSampler(compare);
    CHECK(other != first);
    CHECK(device->Resources().samplers == 2);
    device->DestroySampler(first);
    CHECK(AsMetal(*device).SamplerObject(first) != nil);  // one reference is left
    device->DestroySampler(second);
    device->DestroySampler(other);
    device->WaitIdle();
    CHECK(device->Resources().samplers == 0);
}

TEST_CASE("Metal bind groups validate their resources, hold them by handle, and transient ones end with their frame",
          "[rhi][metal][gpu]")
{
    DevicePtr device = CreateDevice(MetalInfo("metal_bindings_test"));
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    const BindGroupLayoutHandle layout = FragmentLayout(*device);
    Resources r = MakeResources(*device);
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    // A uniform binding at an offset that is not a multiple of 256, and one past the buffer.
    CHECK_FALSE(MakeGroup(*device, layout, r, 16).IsValid());
    CHECK_FALSE(MakeGroup(*device, layout, r, 1024).IsValid());
    // A texture without Sampled usage.
    TextureDesc td;
    td.width = 4;
    td.height = 4;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment;
    Resources bad = r;
    bad.texture = device->CreateTexture(td);
    CHECK_FALSE(MakeGroup(*device, layout, bad, 0).IsValid());
    LogRhi.SetEnabled(was);

    const BindGroupHandle group = MakeGroup(*device, layout, r, 0);
    REQUIRE(group.IsValid());
    Vector<MetalGroupResource> resolved;
    REQUIRE(metal.ResolveGroup(group, &resolved));
    REQUIRE(resolved.Size() == 3);
    for (usize i = 0; i < resolved.Size(); ++i)
    {
        CHECK((resolved[i].buffer != nil || resolved[i].texture != nil || resolved[i].sampler != nil));
    }
    // Destroying a resource invalidates the group (it is resolved by handle when bound).
    device->DestroyTexture(r.texture);
    CHECK_FALSE(metal.ResolveGroup(group, &resolved));
    device->DestroyBindGroup(group);
    device->DestroyTexture(bad.texture);

    // A transient group lives through its frame and is gone once the frame's slot comes round again.
    td.usage = TextureUsage::Sampled;
    r.texture = device->CreateTexture(td);
    device->BeginFrame();
    const BindGroupHandle transient = MakeGroup(*device, layout, r, 0, true);
    REQUIRE(transient.IsValid());
    CHECK(device->Resources().bind_groups == 0);  // transient groups are not counted as the application's
    device->EndFrame();
    for (int f = 0; f < 6; ++f)
    {
        device->BeginFrame();
        device->EndFrame();
    }
    CHECK_FALSE(metal.ResolveGroup(transient, &resolved));
    device->DestroyBindGroupLayout(layout);
    device->DestroyBuffer(r.uniforms);
    device->DestroyTexture(r.texture);
    device->DestroySampler(r.sampler);
    device->WaitIdle();
    CHECK(device->Resources().UserObjects() == 0);
}
