// Dx12Device: command lists, submission and the GenerateMips pipeline (Phase 13 Day 168).
//
// Each command list has its own allocator; the lists of a frame slot are handed out again once that slot's frame has finished (BeginFrame
// waits for it), so an allocator is never reset while the GPU still reads it. Submit runs every list in its own ExecuteCommandLists:
// D3D12 returns each buffer to COMMON after one, which is the state a list's buffer tracking starts from. Before a list runs, the
// textures it needs in a state they are not in are transitioned by a prologue list, from the states the device has recorded; after it,
// the device records the states it left them in.

#include "log/assert.h"
#include "mipgen.comp.dxil.inc"
#include "rhi/dx12/dx12_command_list.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

namespace engine_placeholder::rhi::dx12 {

// ---- command lists ----

CommandList* Dx12Device::CreateCommandList() {
    ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Acquire), "CreateCommandList: no frame is open (call BeginFrame first)");
    if (device_lost_.Load(MemoryOrder::Relaxed)) {
        return nullptr;
    }
    const u64 frame = FrameNumber();
    LockGuard<Mutex> lock(list_mutex_);
    ListPool& pool = list_pools_[frame % kFrameSlots];
    if (pool.frame != frame) {
        pool.used = 0; // the slot's previous frame has finished: its lists can be recorded again
        pool.frame = frame;
    }
    if (pool.used == pool.lists.Size()) {
        ComRef<ID3D12CommandAllocator> allocator;
        ComRef<ID3D12GraphicsCommandList> list;
        if (!Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), allocator.PutVoid()),
                   "CreateCommandAllocator") ||
            !Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, __uuidof(ID3D12GraphicsCommandList),
                                              list.PutVoid()),
                   "CreateCommandList")) {
            return nullptr;
        }
        list->Close(); // created recording; Begin resets it
        list->SetName(L"engine command list");
        UniquePtr<Dx12CommandList> made = MakeUnique<Dx12CommandList>(this, static_cast<ComRef<ID3D12CommandAllocator>&&>(allocator),
                                                                      static_cast<ComRef<ID3D12GraphicsCommandList>&&>(list));
        if (!made) {
            return nullptr;
        }
        pool.lists.Push(static_cast<UniquePtr<Dx12CommandList>&&>(made));
    }
    Dx12CommandList* list = pool.lists[pool.used].Get();
    if (!list->Recycle(frame)) {
        return nullptr;
    }
    ++pool.used;
    return list;
}

void Dx12Device::ShutdownCommands() {
    LockGuard<Mutex> lock(list_mutex_);
    for (ListPool& pool : list_pools_) {
        pool.lists.Clear();
        pool.used = 0;
    }
    LockGuard<Mutex> mip(mip_mutex_);
    mip_state_.Reset();
    mip_root_.Reset();
}

FenceValue Dx12Device::Submit(CommandList* const* lists, usize count) {
    ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Acquire), "Submit: no frame is open (call BeginFrame first)");
    ENGINE_ASSERT(count == 0 || lists != nullptr, "Submit: lists is null but count is not zero");
    const u64 frame = FrameNumber();
    // Everything below is under the submission lock, so two threads that wrongly submit one list are serialized and the second aborts.
    LockGuard<Mutex> lock(submit_mutex_);
    for (usize i = 0; i < count; ++i) {
        auto* list = static_cast<Dx12CommandList*>(lists[i]);
        ENGINE_ASSERT(list != nullptr && list->Owner() == this, "Submit: a list is null or belongs to another device");
        ENGINE_ASSERT(list->GetPhase() == Dx12CommandList::Phase::Ended, "Submit: a list was not ended, or was already submitted or listed twice");
        ENGINE_ASSERT(list->Frame() == frame, "Submit: a list is from an earlier frame");
        list->SetPhase(Dx12CommandList::Phase::Submitting);
    }
    auto finish = [&]() {
        for (usize i = 0; i < count; ++i) static_cast<Dx12CommandList*>(lists[i])->SetPhase(Dx12CommandList::Phase::Submitted);
    };
    if (device_lost_.Load(MemoryOrder::Relaxed)) {
        finish();
        return 0;
    }
    if (count == 0) {
        return last_signaled_; // nothing new: the fence for everything submitted so far
    }
    if (lose_device_at_submit_ != 0 && submit_count_.FetchAdd(1, MemoryOrder::Relaxed) + 1 == lose_device_at_submit_ &&
        InjectLoss("submit", lose_device_at_submit_)) {
        finish();
        return 0;
    }
    for (usize i = 0; i < count; ++i) {
        if (static_cast<Dx12CommandList*>(lists[i])->Failed()) {
            RhiLog(LogVerbosity::Error, "Submit: list %zu failed while recording and was not submitted (nothing in the batch ran)", i);
            finish();
            return 0;
        }
    }
    Vector<D3D12_RESOURCE_BARRIER> prologue;
    for (usize i = 0; i < count; ++i) {
        auto* list = static_cast<Dx12CommandList*>(lists[i]);
        const Vector<Dx12CommandList::TextureUse>& uses = list->TextureUses();
        prologue.Clear();
        {
            LockGuard<Mutex> resources(resource_mutex_);
            for (usize u = 0; u < uses.Size(); ++u) {
                const TextureRes* t = textures_.Get(uses[u].texture);
                if (t == nullptr || t->state == uses[u].initial) continue; // a texture destroyed since: its frame keeps it, nothing reads it
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = uses[u].resource;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = t->state;
                b.Transition.StateAfter = uses[u].initial;
                prologue.Push(b);
            }
        }
        if (prologue.Size() != 0) {
            usize slot = 0;
            ID3D12GraphicsCommandList* pre = BeginInternalLocked(&slot);
            if (pre == nullptr) {
                finish();
                return 0;
            }
            pre->ResourceBarrier(static_cast<UINT>(prologue.Size()), prologue.Data());
            if (SubmitInternalLocked(slot) == 0) {
                finish();
                return 0;
            }
        }
        ID3D12CommandList* one[] = {list->List()};
        queue_->ExecuteCommandLists(1, one);
        LockGuard<Mutex> resources(resource_mutex_);
        for (usize u = 0; u < uses.Size(); ++u) {
            if (TextureRes* t = textures_.Get(uses[u].texture)) t->state = uses[u].current;
        }
    }
    const u64 value = SignalLocked();
    finish();
    return value;
}

// ---- GenerateMips ----

DXGI_FORMAT Dx12Device::MipStorageFormat(Format format) {
    switch (format) {
    case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return ToDxgi(format);
    }
}

bool Dx12Device::CanGenerateMips(Format format) const {
    return format != Format::Undefined && static_cast<usize>(format) < static_cast<usize>(Format::Count) && mip_support_[static_cast<usize>(format)];
}

bool Dx12Device::MipGenCapable(const TextureDesc& desc) const {
    return Has(desc.usage, TextureUsage::Sampled) && Has(desc.usage, TextureUsage::CopySrc) && Has(desc.usage, TextureUsage::CopyDst) &&
           desc.mip_levels > 1 && desc.dimension == TextureDimension::Tex2D && desc.samples == 1 && CanGenerateMips(desc.format);
}

bool Dx12Device::MipPipelineFor(MipPipeline* out) {
    LockGuard<Mutex> lock(mip_mutex_);
    if (!mip_state_) {
        // shaders/mipgen.comp: t0 the source level, s1 a linear clamp sampler (static here), u2 the written level, b0 space15 four constants.
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 2;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.ShaderRegister = 0;
        params[1].Constants.RegisterSpace = 15;
        params[1].Constants.Num32BitValues = 4;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 1;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 2;
        rd.pParameters = params;
        rd.NumStaticSamplers = 1;
        rd.pStaticSamplers = &sampler;
        ComRef<ID3DBlob> blob, error;
        HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, blob.Put(), error.Put());
        if (FAILED(hr)) {
            RhiLog(LogVerbosity::Error, "D3D12: the mip root signature does not serialize (%s): %s", HresultName(hr),
                   error ? static_cast<const char*>(error->GetBufferPointer()) : "no message");
            return false;
        }
        if (!Check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), mip_root_.PutVoid()),
                   "CreateRootSignature (mips)")) {
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = mip_root_.Get();
        pd.CS = {shaders::kMipgenCompDxil, shaders::kMipgenCompDxilByteCount};
        if (!Check(device_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), mip_state_.PutVoid()), "CreateComputePipelineState (mips)")) {
            mip_root_.Reset();
            return false;
        }
        mip_state_->SetName(L"engine GenerateMips");
    }
    out->root = mip_root_.Get();
    out->state = mip_state_.Get();
    return true;
}

} // namespace engine_placeholder::rhi::dx12
