#include "rhi/dx12/dx12_backend.h"

#include "cstr/format.h"

#include "rhi/backend_registry.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/rhi.h"

namespace engine_placeholder::rhi
{

namespace
{

DevicePtr CreateDx12BackendDevice(const DeviceCreateInfo& info)
{
    return dx12::Dx12Device::Create(info);
}

bool EnumerateDx12Adapters(const DeviceCreateInfo& info, AdapterList* out)
{
    return dx12::Dx12Device::EnumerateAdapters(info, out);
}

bool Dx12Available(char* reason, usize reason_size)
{
    dx12::ComRef<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), factory.PutVoid())))
    {
        FormatTo(reason, reason_size, "no DXGI 1.6 factory could be created");
        return false;
    }
    for (UINT i = 0;; ++i)
    {
        dx12::ComRef<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, adapter.Put())))
        {
            break;
        }
        // A null output pointer asks whether a device could be made, without making one.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            return true;
        }
    }
    FormatTo(reason, reason_size, "no adapter supports Direct3D 12 at feature level 12_0");
    return false;
}

}  // namespace

bool RegisterDx12Backend()
{
    BackendDescriptor d;
    d.backend = Backend::Dx12;
    d.name = "dx12";
    d.create = &CreateDx12BackendDevice;
    d.enumerate_adapters = &EnumerateDx12Adapters;
    d.available = &Dx12Available;
    d.shader_format = ShaderFormat::Dxil;
    d.auto_rank = 20;  // above Vulkan (10): Auto means Direct3D 12 on Windows since it passed the suites (Phase 13 Day
                       // 172)
    return RegisterBackend(d);
}

}  // namespace engine_placeholder::rhi
