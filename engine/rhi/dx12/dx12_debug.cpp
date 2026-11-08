#include "rhi/dx12/dx12_debug.h"

#include <atomic>
#include <mutex>

#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12 {

namespace {

std::atomic<u64> g_errors{0};
std::atomic<u64> g_warnings{0};
std::atomic<u64> g_live_objects{0};
std::mutex g_capture_mutex;
DebugMessageCaptureFn g_capture = nullptr;
void* g_capture_user = nullptr;

} // namespace

DebugMessageCounts GetDebugMessageCounts() {
    DebugMessageCounts c;
    c.errors = g_errors.load(std::memory_order_relaxed);
    c.warnings = g_warnings.load(std::memory_order_relaxed);
    c.live_objects = g_live_objects.load(std::memory_order_relaxed);
    return c;
}

void ResetDebugMessageCounts() {
    g_errors.store(0, std::memory_order_relaxed);
    g_warnings.store(0, std::memory_order_relaxed);
    g_live_objects.store(0, std::memory_order_relaxed);
}

void SetDebugMessageCapture(DebugMessageCaptureFn fn, void* user) {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    g_capture = fn;
    g_capture_user = user;
}

void NoteLiveObject() { g_live_objects.fetch_add(1, std::memory_order_relaxed); }

void RecordDebugMessage(D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* text) {
    LogVerbosity verbosity = LogVerbosity::Info;
    switch (severity) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
        verbosity = LogVerbosity::Error;
        g_errors.fetch_add(1, std::memory_order_relaxed);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        verbosity = LogVerbosity::Warn;
        g_warnings.fetch_add(1, std::memory_order_relaxed);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
    case D3D12_MESSAGE_SEVERITY_MESSAGE: verbosity = LogVerbosity::Debug; break;
    }
    RhiLog(verbosity, "D3D12 debug layer [%d]: %s", static_cast<int>(id), text != nullptr ? text : "");
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    if (g_capture != nullptr) {
        g_capture(g_capture_user, verbosity, static_cast<u32>(id), text != nullptr ? text : "");
    }
}

} // namespace engine_placeholder::rhi::dx12
