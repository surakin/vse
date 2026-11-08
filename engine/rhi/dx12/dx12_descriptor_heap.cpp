#include "rhi/dx12/dx12_descriptor_heap.h"

#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12 {

void CpuDescriptorHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    device_ = device;
    type_ = type;
    increment_ = device->GetDescriptorHandleIncrementSize(type);
}

bool CpuDescriptorHeap::Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out) {
    if (!free_.Empty()) {
        out->ptr = free_[free_.Size() - 1];
        free_.Pop();
        ++used_;
        return true;
    }
    if (next_in_last_ == kDescriptorsPerHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = kDescriptorsPerHeap;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ComRef<ID3D12DescriptorHeap> heap;
        const HRESULT hr = device_->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.PutVoid());
        if (FAILED(hr)) {
            RhiLog(LogVerbosity::Error, "D3D12: cannot create a descriptor heap of %u descriptors (%s)", kDescriptorsPerHeap, HresultName(hr));
            return false;
        }
        heaps_.Push(static_cast<ComRef<ID3D12DescriptorHeap>&&>(heap));
        next_in_last_ = 0;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE base = heaps_[heaps_.Size() - 1]->GetCPUDescriptorHandleForHeapStart();
    out->ptr = base.ptr + static_cast<usize>(next_in_last_) * increment_;
    ++next_in_last_;
    ++used_;
    return true;
}

void CpuDescriptorHeap::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (handle.ptr == 0) {
        return;
    }
    free_.Push(handle.ptr);
    --used_;
}

void CpuDescriptorHeap::Shutdown() {
    free_.Clear();
    heaps_.Clear();
    next_in_last_ = kDescriptorsPerHeap;
    used_ = 0;
}

} // namespace engine_placeholder::rhi::dx12
