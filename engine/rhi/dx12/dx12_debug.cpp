#include "rhi/dx12/dx12_debug.h"

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12
{

namespace
{

Atomic<u64> g_errors{0};
Atomic<u64> g_warnings{0};
Atomic<u64> g_live_objects{0};
Mutex g_capture_mutex;
DebugMessageCaptureFn g_capture = nullptr;
void* g_capture_user = nullptr;

}  // namespace

DebugMessageCounts GetDebugMessageCounts()
{
    DebugMessageCounts c;
    c.errors = g_errors.Load(MemoryOrder::Relaxed);
    c.warnings = g_warnings.Load(MemoryOrder::Relaxed);
    c.live_objects = g_live_objects.Load(MemoryOrder::Relaxed);
    return c;
}

void ResetDebugMessageCounts()
{
    g_errors.Store(0, MemoryOrder::Relaxed);
    g_warnings.Store(0, MemoryOrder::Relaxed);
    g_live_objects.Store(0, MemoryOrder::Relaxed);
}

void SetDebugMessageCapture(DebugMessageCaptureFn fn, void* user)
{
    LockGuard<Mutex> lock(g_capture_mutex);
    g_capture = fn;
    g_capture_user = user;
}

void NoteLiveObject()
{
    g_live_objects.FetchAdd(1, MemoryOrder::Relaxed);
}

void RecordDebugMessage(D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* text)
{
    LogVerbosity verbosity = LogVerbosity::Info;
    switch (severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
        verbosity = LogVerbosity::Error;
        g_errors.FetchAdd(1, MemoryOrder::Relaxed);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        verbosity = LogVerbosity::Warn;
        g_warnings.FetchAdd(1, MemoryOrder::Relaxed);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
    case D3D12_MESSAGE_SEVERITY_MESSAGE: verbosity = LogVerbosity::Debug; break;
    }
    RhiLog(verbosity, "D3D12 debug layer [%d]: %s", static_cast<int>(id), text != nullptr ? text : "");
    LockGuard<Mutex> lock(g_capture_mutex);
    if (g_capture != nullptr)
    {
        g_capture(g_capture_user, verbosity, static_cast<u32>(id), text != nullptr ? text : "");
    }
}

}  // namespace engine_placeholder::rhi::dx12
