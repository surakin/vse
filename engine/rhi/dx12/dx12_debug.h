#pragma once

#include "log/log.h"
#include "rhi/dx12/d3d12_api.h"
#include "types.h"

//! The D3D12 debug layer's messages: routed to [[rhi::LogRhi]], counted, and offered to a test's capture hook (Phase 13 Day 164).

namespace engine_placeholder::rhi::dx12 {

/// Running totals of debug-layer messages since process start or the last [[rhi::dx12::ResetDebugMessageCounts]].
///
/// Thread-safety: thread-compatible plain data (a snapshot).
struct DebugMessageCounts {
    /// `CORRUPTION` and `ERROR` messages.
    u64 errors = 0;
    /// `WARNING` messages.
    u64 warnings = 0;
    /// Live-object reports (`ReportLiveDeviceObjects`) of anything but the device itself, at device destruction.
    u64 live_objects = 0;
};

/// Returns the totals.
///
/// Thread-safety: thread-safe.
DebugMessageCounts GetDebugMessageCounts();

/// Sets the totals to zero.
///
/// Thread-safety: thread-safe.
void ResetDebugMessageCounts();

/// A test's hook for every debug-layer message: called with the pointer given to [[rhi::dx12::SetDebugMessageCapture]], the
/// severity as a log verbosity (`Error` for corruption and errors), the `D3D12_MESSAGE_ID` and the text (borrowed for the call).
using DebugMessageCaptureFn = void (*)(void* user, LogVerbosity severity, u32 id, const char* text);

/// Installs a capture hook, or removes it with null.
///
/// Param fn: the hook, or null.
/// Param user: passed back to it.
/// Thread-safety: thread-safe; the hook may be called from any thread the D3D12 runtime reports on.
void SetDebugMessageCapture(DebugMessageCaptureFn fn, void* user);

/// Records one message: counts it, logs it on `LogRhi` and passes it to the capture hook. Used by the device's info-queue callback.
///
/// Param severity: the D3D12 severity.
/// Param id: the message id.
/// Param text: the message.
/// Thread-safety: thread-safe.
void RecordDebugMessage(D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* text);

/// Notes that the device's live-object report named something besides the device: counted in `live_objects`.
///
/// Thread-safety: thread-safe.
void NoteLiveObject();

} // namespace engine_placeholder::rhi::dx12
