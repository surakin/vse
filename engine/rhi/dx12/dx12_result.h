#pragma once

#include "rhi/dx12/d3d12_api.h"
#include "types.h"

//! `HRESULT`s of the D3D12 and DXGI calls the backend makes, as names for logs and as the device-removed test.

namespace engine_placeholder::rhi::dx12 {

/// Returns a short name for an `HRESULT` the backend can meet (`S_OK`, `E_OUTOFMEMORY`, `E_INVALIDARG`, `DXGI_ERROR_DEVICE_REMOVED`,
/// `DXGI_ERROR_DEVICE_RESET`, `DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_UNSUPPORTED`, ...).
///
/// - Param result: the code.
/// - Returns: a static name, or `"unknown HRESULT"` for a code not in the table (log it with `%08lx` too).
/// - Fails: never.
/// - Ownership: the string is static.
/// - Thread-safety: thread-safe.
const char* HresultName(HRESULT result);

/// Returns whether a code means the device is gone: removed, reset, hung or a driver internal error.
///
/// - Param result: the code.
/// - Returns: true for the device-loss codes.
/// - Fails: never.
/// - Thread-safety: thread-safe.
bool IsDeviceLostResult(HRESULT result);

} // namespace engine_placeholder::rhi::dx12
