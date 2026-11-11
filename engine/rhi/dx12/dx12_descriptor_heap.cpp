#include "rhi/dx12/dx12_descriptor_heap.h"

#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"
#include "sync/atomic.h"

namespace engine_placeholder::rhi::dx12
{

void CpuDescriptorHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    device_ = device;
    type_ = type;
    increment_ = device->GetDescriptorHandleIncrementSize(type);
}

bool CpuDescriptorHeap::Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out)
{
    if (!free_.Empty())
    {
        out->ptr = free_[free_.Size() - 1];
        free_.Pop();
        ++used_;
        return true;
    }
    if (next_in_last_ == kDescriptorsPerHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = kDescriptorsPerHeap;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ComRef<ID3D12DescriptorHeap> heap;
        const HRESULT hr = device_->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.PutVoid());
        if (FAILED(hr))
        {
            RhiLog(LogVerbosity::Error, "D3D12: cannot create a descriptor heap of %u descriptors (%s)",
                   kDescriptorsPerHeap, HresultName(hr));
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

void CpuDescriptorHeap::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    if (handle.ptr == 0)
    {
        return;
    }
    free_.Push(handle.ptr);
    --used_;
}

void CpuDescriptorHeap::Shutdown()
{
    free_.Clear();
    heaps_.Clear();
    next_in_last_ = kDescriptorsPerHeap;
    used_ = 0;
}

// ---- CpuDescriptorBlocks ----

void CpuDescriptorBlocks::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    device_ = device;
    type_ = type;
    increment_ = device->GetDescriptorHandleIncrementSize(type);
}

u32 CpuDescriptorBlocks::ClassOf(u32 count)
{
    u32 c = 0;
    while ((1u << c) < count)
    {
        ++c;
    }
    return c;
}

bool CpuDescriptorBlocks::Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE* out)
{
    if (count == 0 || count > kDescriptorsPerHeap)
    {
        return false;
    }
    const u32 c = ClassOf(count);
    if (!free_[c].Empty())
    {
        out->ptr = free_[c][free_[c].Size() - 1];
        free_[c].Pop();
        return true;
    }
    const u32 size = 1u << c;
    if (heaps_.Empty() || next_in_last_ + size > kDescriptorsPerHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = kDescriptorsPerHeap;
        ComRef<ID3D12DescriptorHeap> heap;
        const HRESULT hr = device_->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.PutVoid());
        if (FAILED(hr))
        {
            RhiLog(LogVerbosity::Error, "D3D12: cannot create a descriptor heap of %u descriptors (%s)",
                   kDescriptorsPerHeap, HresultName(hr));
            return false;
        }
        heaps_.Push(static_cast<ComRef<ID3D12DescriptorHeap>&&>(heap));
        next_in_last_ = 0;
    }
    out->ptr = heaps_[heaps_.Size() - 1]->GetCPUDescriptorHandleForHeapStart().ptr +
               static_cast<usize>(next_in_last_) * increment_;
    next_in_last_ += size;
    return true;
}

void CpuDescriptorBlocks::Free(D3D12_CPU_DESCRIPTOR_HANDLE first, u32 count)
{
    if (first.ptr == 0 || count == 0 || count > kDescriptorsPerHeap)
    {
        return;
    }
    free_[ClassOf(count)].Push(first.ptr);
}

void CpuDescriptorBlocks::Shutdown()
{
    for (Vector<usize>& list : free_)
    {
        list.Clear();
    }
    heaps_.Clear();
    next_in_last_ = kDescriptorsPerHeap;
}

// ---- GpuDescriptorRing ----

bool GpuDescriptorRing::Initialize(ID3D12Device* device, u32 per_slot, u32 slots)
{
    if (slots == 0 || slots > kMaxSlots)
    {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = per_slot * slots;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap_.PutVoid());
    if (FAILED(hr))
    {
        RhiLog(LogVerbosity::Error, "D3D12: cannot create the shader-visible descriptor heap of %u descriptors (%s)",
               desc.NumDescriptors, HresultName(hr));
        return false;
    }
    heap_->SetName(L"engine shader-visible view ring");
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
    increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    per_slot_ = per_slot;
    for (Atomic<u32>& u : used_)
    {
        u.Store(0, MemoryOrder::Relaxed);
    }
    return true;
}

bool GpuDescriptorRing::Allocate(u32 slot, u32 count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                 D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    if (slot >= kMaxSlots || count == 0 || count > per_slot_)
    {
        return false;
    }
    // A claim that overflows the region is not undone: the region is full for the rest of the frame anyway.
    const u32 first = used_[slot].FetchAdd(count, MemoryOrder::Relaxed);
    if (first > per_slot_ - count)
    {
        return false;
    }
    const u64 index = static_cast<u64>(slot) * per_slot_ + first;
    cpu->ptr = cpu_start_.ptr + static_cast<usize>(index) * increment_;
    gpu->ptr = gpu_start_.ptr + index * increment_;
    return true;
}

void GpuDescriptorRing::Reset(u32 slot)
{
    if (slot < kMaxSlots)
    {
        used_[slot].Store(0, MemoryOrder::Relaxed);
    }
}

// ---- GpuDescriptorTables ----

bool GpuDescriptorTables::Initialize(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = kCapacity;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT hr = device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap_.PutVoid());
    if (FAILED(hr))
    {
        RhiLog(LogVerbosity::Error, "D3D12: cannot create the shader-visible sampler heap (%s)", HresultName(hr));
        return false;
    }
    heap_->SetName(L"engine shader-visible sampler tables");
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
    increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    ranges_.Clear();
    ranges_.Push(Range{0, kCapacity});
    return true;
}

bool GpuDescriptorTables::Allocate(u32 count, u32* offset)
{
    for (usize i = 0; i < ranges_.Size(); ++i)
    {
        Range& r = ranges_[i];
        if (r.count >= count)
        {
            *offset = r.offset;
            r.offset += count;
            r.count -= count;
            if (r.count == 0)
            {
                for (usize k = i + 1; k < ranges_.Size(); ++k)
                {
                    ranges_[k - 1] = ranges_[k];
                }
                ranges_.Pop();
            }
            return true;
        }
    }
    return false;
}

void GpuDescriptorTables::Free(u32 offset, u32 count)
{
    if (count == 0)
    {
        return;
    }
    usize at = 0;
    while (at < ranges_.Size() && ranges_[at].offset < offset)
    {
        ++at;
    }
    // Insert at `at`, then merge with the neighbours it touches.
    ranges_.Push(Range{0, 0});
    for (usize k = ranges_.Size() - 1; k > at; --k)
    {
        ranges_[k] = ranges_[k - 1];
    }
    ranges_[at] = Range{offset, count};
    if (at + 1 < ranges_.Size() && ranges_[at].offset + ranges_[at].count == ranges_[at + 1].offset)
    {
        ranges_[at].count += ranges_[at + 1].count;
        for (usize k = at + 2; k < ranges_.Size(); ++k)
        {
            ranges_[k - 1] = ranges_[k];
        }
        ranges_.Pop();
    }
    if (at > 0 && ranges_[at - 1].offset + ranges_[at - 1].count == ranges_[at].offset)
    {
        ranges_[at - 1].count += ranges_[at].count;
        for (usize k = at + 1; k < ranges_.Size(); ++k)
        {
            ranges_[k - 1] = ranges_[k];
        }
        ranges_.Pop();
    }
}

u32 GpuDescriptorTables::FreeCount() const
{
    u32 n = 0;
    for (usize i = 0; i < ranges_.Size(); ++i)
    {
        n += ranges_[i].count;
    }
    return n;
}

}  // namespace engine_placeholder::rhi::dx12
