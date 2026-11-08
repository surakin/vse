// Dx12Device: creation, the debug layer, capabilities, frames, fences and submission (Phase 13 Day 164).

#include "rhi/dx12/dx12_device.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log/assert.h"
#include "os/os.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12 {

namespace {

std::atomic<u64> g_leaked_user_objects{0};
std::atomic<u64> g_leaked_d3d12_objects{0};

// "1" on, "0" off, anything else (or unset) undecided.
int ParseSwitch(const char* text) {
    if (text == nullptr) return -1;
    if (std::strcmp(text, "1") == 0) return 1;
    if (std::strcmp(text, "0") == 0) return 0;
    return -1;
}

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

// The info queue's callback: every debug-layer message. A live-object report at device destruction is not a warning about the
// application's use of the API: the device naming itself is expected, anything else is a leak.
void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void*) {
    const char* text = description != nullptr ? description : "";
    if (std::strncmp(text, "Live ", 5) == 0) {
        if (std::strstr(text, "ID3D12Device") == nullptr && std::strncmp(text, "Live Object Summary", 19) != 0) {
            NoteLiveObject();
            RhiLog(LogVerbosity::Warn, "D3D12 debug layer: %s", text);
        }
        return;
    }
    RecordDebugMessage(severity, id, text);
}

} // namespace

Dx12Device::LeakCounts Dx12Device::GetLeakCounts() {
    LeakCounts c;
    c.user_objects = g_leaked_user_objects.load(std::memory_order_relaxed);
    c.live_d3d12_objects = g_leaked_d3d12_objects.load(std::memory_order_relaxed);
    return c;
}

void Dx12Device::ResetLeakCounts() {
    g_leaked_user_objects.store(0, std::memory_order_relaxed);
    g_leaked_d3d12_objects.store(0, std::memory_order_relaxed);
}

Dx12DevicePtr Dx12Device::Create(const DeviceCreateInfo& info) {
    // Held with the plain deleter until initialization succeeds: a half-built device must not be waited on.
    UniquePtr<Dx12Device> device = MakeUnique<Dx12Device>();
    if (!device || !device->Initialize(info)) {
        return Dx12DevicePtr();
    }
    return Dx12DevicePtr(device.Release());
}

bool Dx12Device::EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out) {
    *out = AdapterList{};
    ComRef<IDXGIFactory6> factory;
    const HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), factory.PutVoid());
    if (FAILED(hr)) {
        RhiLog(LogVerbosity::Error, "D3D12: cannot create a DXGI factory (%s, 0x%08lx)", HresultName(hr), static_cast<unsigned long>(hr));
        return false;
    }
    AdapterSet set;
    if (!dx12::ListAdapters(factory.Get(), info.window != nullptr, &set)) {
        return false;
    }
    *out = set.list;
    return true;
}

bool Dx12Device::Initialize(const DeviceCreateInfo& info) {
    // Validation: On and Off as asked; Auto follows ENGINE_RHI_VALIDATION, else on in Debug builds when the layer is installed.
    bool want_validation = false;
    switch (info.validation) {
    case ValidationSetting::On: want_validation = true; break;
    case ValidationSetting::Off: want_validation = false; break;
    case ValidationSetting::Auto: {
        const int env = ParseSwitch(std::getenv("ENGINE_RHI_VALIDATION"));
        want_validation = env >= 0 ? env == 1 : kDebugBuild;
        break;
    }
    }
    if (want_validation) {
        // Process-wide: once enabled, every later device of the process has the debug layer (D3D12 cannot turn it off again).
        ComRef<ID3D12Debug> debug;
        if (FAILED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.PutVoid()))) {
            if (info.validation == ValidationSetting::On) {
                RhiLog(LogVerbosity::Error, "D3D12: validation was asked for, but the debug layer (d3d12SDKLayers.dll) is not installed: add the "
                                            "Graphics Tools optional feature (BUILD.md)");
                return false;
            }
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true)) {
                RhiLog(LogVerbosity::Warn, "D3D12: the debug layer is not installed (the Graphics Tools optional feature); running without validation");
            }
        } else {
            debug->EnableDebugLayer();
            validation_ = true;
            const int sync_env = ParseSwitch(std::getenv("ENGINE_RHI_VALIDATION_SYNC"));
            if (sync_env >= 0 ? sync_env == 1 : info.sync_validation) {
                // The closest D3D12 counterpart of Vulkan's synchronization validation: GPU-based validation checks resource states
                // and descriptors as the GPU executes. Slow; for tests and debugging only.
                ComRef<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug->QueryInterface(__uuidof(ID3D12Debug1), debug1.PutVoid()))) {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                    gpu_validation_ = true;
                }
            }
        }
    }

    HRESULT hr = CreateDXGIFactory2(validation_ ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory6), factory_.PutVoid());
    if (FAILED(hr) && validation_) {
        hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), factory_.PutVoid()); // the DXGI debug layer is optional
    }
    if (FAILED(hr)) {
        RhiLog(LogVerbosity::Error, "D3D12: cannot create a DXGI factory (%s, 0x%08lx)", HresultName(hr), static_cast<unsigned long>(hr));
        return false;
    }

    AdapterSet set;
    dx12::ListAdapters(factory_.Get(), info.window != nullptr, &set);
    adapters_ = set.list;
    char reason[256] = {};
    const int pick = PickAdapter(set, info.adapter, reason, sizeof(reason));
    if (pick < 0) {
        RhiLog(LogVerbosity::Error, "CreateDevice: %s", reason);
        return false;
    }
    dxgi_adapter_ = set.adapters[pick].Share();
    adapter_ = set.list.items[pick];

    hr = D3D12CreateDevice(dxgi_adapter_.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), device_.PutVoid());
    if (FAILED(hr)) {
        RhiLog(LogVerbosity::Error, "D3D12: D3D12CreateDevice failed on '%s' (%s, 0x%08lx)", adapter_.name, HresultName(hr), static_cast<unsigned long>(hr));
        return false;
    }
    if (validation_) {
        if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D12InfoQueue1), info_queue_.PutVoid()))) {
            info_queue_->RegisterMessageCallback(&OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &info_callback_cookie_);
        } else {
            RhiLog(LogVerbosity::Warn, "D3D12: ID3D12InfoQueue1 is not available; debug-layer messages are not routed to LogRhi");
        }
        ComRef<ID3D12InfoQueue> queue;
        if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D12InfoQueue), queue.PutVoid()))) {
            // Break into a debugger on errors, but never in a run without one (that would end the process).
            const BOOL attached = IsDebuggerPresent();
            queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, attached);
            queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, attached);
            queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
        }
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (!Check(device_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), queue_.PutVoid()), "CreateCommandQueue") ||
        !Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence_.PutVoid()), "CreateFence")) {
        return false;
    }
    queue_->SetName(L"engine direct queue");

    frames_in_flight_ = info.frames_in_flight < 1 ? 1 : (info.frames_in_flight > 3 ? 3 : info.frames_in_flight);
    window_ = info.window;
    lose_device_at_frame_ = info.faults.lose_device_at_frame;
    lose_device_at_submit_ = info.faults.lose_device_at_submit;
    lose_device_at_present_ = info.faults.lose_device_at_present;
    strikes_left_ = info.faults.strikes_left;
    FillCaps();
    epoch_ = AllocateDeviceSerial(); // usable from here on; the handle pools are seeded from it
    if (!InitializeResources()) {
        return false;
    }
    RhiLog(LogVerbosity::Info, "D3D12: device on '%s' (%s), validation %s%s", adapter_.name, adapter_.id, validation_ ? "on" : "off",
           gpu_validation_ ? " with GPU-based validation" : "");
    return true;
}

void Dx12Device::FillCaps() {
    DeviceCaps c;
    c.max_texture_dimension_2d = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;          // 16384
    c.max_texture_dimension_3d = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;        // 2048
    c.max_texture_array_layers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;      // 2048
    c.max_bind_groups = kMaxBindGroups;                                         // one descriptor table pair per group
    c.max_color_attachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;           // 8
    c.max_push_constant_bytes = kMaxPushConstantBytes;                          // 32 root constants
    c.min_uniform_buffer_offset_alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256: a CBV starts on 256 bytes
    c.min_storage_buffer_offset_alignment = 256;                                // as the default; raw views need only 16
    c.buffer_texture_copy_offset_alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // 512
    c.buffer_texture_copy_row_pitch_alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;  // 256
    c.max_sampler_anisotropy = static_cast<f32>(D3D12_MAX_MAXANISOTROPY);       // 16
    for (u32& n : c.max_compute_work_group_count) n = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION; // 65535
    c.depth_format = Format::D32_Float;
    UINT64 frequency = 0;
    if (queue_ && SUCCEEDED(queue_->GetTimestampFrequency(&frequency)) && frequency != 0) {
        c.timestamp_period_ns = static_cast<f32>(1.0e9 / static_cast<f64>(frequency));
    }
    c.supports_bindless = false;
    c.shader_format = ShaderFormat::Dxil;
    c.object_index_stream = true; // D3D12 shaders have no base instance (shaders/target_common.slang)
    caps_ = c;
}

Dx12Device::~Dx12Device() {
    if (!device_) {
        return; // never initialized
    }
    WaitIdle();
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        CollectRetiredLocked(true);
    }
    const ResourceCounts counts = Resources();
    if (counts.UserObjects() != 0) {
        g_leaked_user_objects.fetch_add(counts.UserObjects(), std::memory_order_relaxed);
        RhiLog(LogVerbosity::Warn, "D3D12: %zu application object(s) were still alive when the device was destroyed", counts.UserObjects());
    }
    ShutdownResources();
    fence_.Reset();
    queue_.Reset();
    ReportLiveObjects();
    if (info_queue_) {
        info_queue_->UnregisterMessageCallback(info_callback_cookie_);
        info_queue_.Reset();
    }
    device_.Reset();
    dxgi_adapter_.Reset();
    factory_.Reset();
}

void Dx12Device::ReportLiveObjects() {
    if (!validation_) {
        return;
    }
    ComRef<ID3D12DebugDevice> debug_device;
    if (FAILED(device_->QueryInterface(__uuidof(ID3D12DebugDevice), debug_device.PutVoid()))) {
        return;
    }
    const u64 before = GetDebugMessageCounts().live_objects;
    // Reported through the info queue's callback (still registered); IGNORE_INTERNAL leaves out the runtime's own objects.
    debug_device->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
    const u64 live = GetDebugMessageCounts().live_objects - before;
    if (live != 0) {
        g_leaked_d3d12_objects.fetch_add(live, std::memory_order_relaxed);
        RhiLog(LogVerbosity::Error, "D3D12: %llu D3D12 object(s) besides the device were alive at its destruction (see the warnings above)",
               static_cast<unsigned long long>(live));
    }
}

void Dx12Device::NotYet(const char* what, int day) {
    RhiLog(LogVerbosity::Error, "D3D12: %s is not implemented yet (Phase 13 Day %d)", what, day);
}

// ---- errors and device loss ----

bool Dx12Device::Check(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) {
        return true;
    }
    if (IsDeviceLostResult(result)) {
        NoteRemoved(what);
    } else {
        RhiLog(LogVerbosity::Error, "D3D12: %s failed (%s, 0x%08lx)", what, HresultName(result), static_cast<unsigned long>(result));
    }
    return false;
}

void Dx12Device::NoteRemoved(const char* where) {
    if (device_lost_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
    RhiLog(LogVerbosity::Error, "D3D12: the device was removed (noticed in %s; reason %s, 0x%08lx); further submissions are ignored", where,
           HresultName(reason), static_cast<unsigned long>(reason));
    ENGINE_ENSURE(false, "the D3D12 device was removed"); // the same report as Vulkan's loss: a trace, once
}

bool Dx12Device::InjectLoss(const char* where, u32 number) {
    if (device_lost_.load(std::memory_order_relaxed)) {
        return true;
    }
    if (strikes_left_ != nullptr) {
        u32 left = strikes_left_->load(std::memory_order_relaxed);
        bool struck = false;
        while (left != 0 && !struck) {
            struck = strikes_left_->compare_exchange_weak(left, left - 1, std::memory_order_relaxed);
        }
        if (!struck) {
            return false;
        }
    }
    if (!device_lost_.exchange(true, std::memory_order_relaxed)) {
        RhiLog(LogVerbosity::Error, "D3D12: the device was lost (injected at %s %u); further submissions are ignored", where, number);
        ENGINE_ENSURE(false, "the D3D12 device was lost");
    }
    return true;
}

// ---- fences ----

u64 Dx12Device::SignalLocked() {
    const u64 value = last_signaled_ + 1;
    if (!Check(queue_->Signal(fence_.Get(), value), "ID3D12CommandQueue::Signal")) {
        return 0;
    }
    last_signaled_ = value;
    last_signaled_published_.store(value, std::memory_order_release);
    return value;
}

bool Dx12Device::WaitForValue(u64 value, u64 timeout_ns) {
    if (value == 0) {
        return true;
    }
    u64 completed = fence_->GetCompletedValue();
    if (completed == ~0ull) { // what a removed device's fences report
        NoteRemoved("a fence wait");
        return false;
    }
    if (completed >= value) {
        return true;
    }
    if (timeout_ns == 0) {
        return false;
    }
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr || !Check(fence_->SetEventOnCompletion(value, event), "ID3D12Fence::SetEventOnCompletion")) {
        if (event != nullptr) CloseHandle(event);
        return false;
    }
    const u64 start = os::MonotonicTimeNanos();
    bool reached = false;
    for (;;) {
        DWORD slice = kWaitSliceMs;
        if (timeout_ns != ~0ull) {
            const u64 elapsed = os::MonotonicTimeNanos() - start;
            if (elapsed >= timeout_ns) {
                break;
            }
            const u64 left_ms = (timeout_ns - elapsed + 999999) / 1000000;
            slice = static_cast<DWORD>(left_ms < kWaitSliceMs ? left_ms : kWaitSliceMs);
        }
        const DWORD r = WaitForSingleObject(event, slice);
        completed = fence_->GetCompletedValue();
        if (completed == ~0ull || FAILED(device_->GetDeviceRemovedReason())) {
            NoteRemoved("a fence wait");
            break;
        }
        if (r == WAIT_OBJECT_0 || completed >= value) {
            reached = true;
            break;
        }
        if (r != WAIT_TIMEOUT) {
            RhiLog(LogVerbosity::Error, "D3D12: waiting for the fence failed (Windows error %lu)", GetLastError());
            break;
        }
    }
    CloseHandle(event);
    return reached;
}

bool Dx12Device::WaitFence(FenceValue value, u64 timeout_ns) {
    if (value == 0) {
        return true;
    }
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }
    return WaitForValue(value, timeout_ns);
}

FenceValue Dx12Device::CompletedFence() const {
    const u64 completed = fence_->GetCompletedValue();
    return completed == ~0ull ? last_signaled_published_.load(std::memory_order_acquire) : completed;
}

// ---- frames ----

void Dx12Device::UpdateCompletedFramesLocked() {
    const u64 completed = CompletedFence();
    while (completed_frames_ < ended_frames_ && frame_fences_[(completed_frames_ + 1) % kTrackedFrames] <= completed) {
        ++completed_frames_;
    }
}

u64 Dx12Device::CompletedFrame() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return completed_frames_;
}

void Dx12Device::CollectRetiredLocked(bool all) {
    const u64 completed = all ? ~0ull : completed_frames_;
    buffers_.CollectRetired(completed, [&](BufferRes& b) {
        ReleaseResourceBytes(b.bytes);
        b.resource.Reset();
    });
    textures_.CollectRetired(completed, [&](TextureRes& t) {
        ReleaseTextureViews(t);
        if (!t.external) ReleaseResourceBytes(t.bytes);
        t.resource.Reset();
    });
}

void Dx12Device::BeginFrame() {
    // Never let the unfinished frames outrun the fence ring, and keep at most frames_in_flight frames on the GPU. The wait happens
    // outside the lock, so other threads keep creating and destroying resources meanwhile.
    for (;;) {
        u64 wait_for = 0;
        {
            std::lock_guard<std::mutex> lock(resource_mutex_);
            UpdateCompletedFramesLocked();
            if (ended_frames_ - completed_frames_ >= kTrackedFrames - 2) {
                wait_for = frame_fences_[(completed_frames_ + 1) % kTrackedFrames];
            }
            const u64 reuse_frame = frame_number_ + 1 > frames_in_flight_ ? frame_number_ + 1 - frames_in_flight_ : 0;
            if (reuse_frame > completed_frames_ && reuse_frame <= ended_frames_) {
                const u64 fence = frame_fences_[reuse_frame % kTrackedFrames];
                wait_for = wait_for > fence ? wait_for : fence;
            }
        }
        if (wait_for == 0 || !WaitForValue(wait_for, ~0ull)) {
            break; // nothing to wait for, or the device is removed (the wait returned at once)
        }
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ENGINE_ASSERT(!frame_open_.load(std::memory_order_relaxed), "BeginFrame: the previous frame was not ended");
    UpdateCompletedFramesLocked();
    ++frame_number_;
    frame_open_.store(true, std::memory_order_release);
    if (lose_device_at_frame_ != 0 && frame_number_ == lose_device_at_frame_) {
        InjectLoss("frame", static_cast<u32>(frame_number_));
    }
    CollectRetiredLocked(false);
}

void Dx12Device::EndFrame() {
    u64 frame = 0;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ENGINE_ASSERT(frame_open_.load(std::memory_order_relaxed), "EndFrame: no frame is open");
        frame_open_.store(false, std::memory_order_release); // later destroys belong to the next frame
        frame = frame_number_;
    }
    // Read the last signal after closing the frame: a resource destroyed in this frame may have been used by any submission made
    // before the close, and this value covers all of them.
    u64 last = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        last = last_signaled_;
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ended_frames_ = frame;
    frame_fences_[frame % kTrackedFrames] = last;
}

u64 Dx12Device::FrameNumber() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return frame_number_;
}

void Dx12Device::WaitIdle() {
    // A removed device's fence reports the largest value, so this returns at once then; an injected loss leaves a healthy device that
    // may still run work, so the wait happens in both cases.
    u64 last = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        last = last_signaled_;
    }
    WaitForValue(last, ~0ull);
    std::lock_guard<std::mutex> lock(resource_mutex_);
    UpdateCompletedFramesLocked();
    CollectRetiredLocked(true);
}

FenceValue Dx12Device::Submit(CommandList* const* lists, usize count) {
    ENGINE_ASSERT(frame_open_.load(std::memory_order_acquire), "Submit: no frame is open (call BeginFrame first)");
    ENGINE_ASSERT(count == 0 || lists != nullptr, "Submit: lists is null but count is not zero");
    std::lock_guard<std::mutex> lock(submit_mutex_);
    if (device_lost_.load(std::memory_order_relaxed)) {
        return 0;
    }
    if (count == 0) {
        return last_signaled_; // nothing new: the fence for everything submitted so far
    }
    if (lose_device_at_submit_ != 0 && submit_count_.fetch_add(1, std::memory_order_relaxed) + 1 == lose_device_at_submit_ &&
        InjectLoss("submit", lose_device_at_submit_)) {
        return 0;
    }
    NotYet("submitting command lists", 168);
    return 0;
}

ResourceCounts Dx12Device::Resources() const {
    ResourceCounts c;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        c.buffers = buffers_.LiveCount();
        c.textures = textures_.LiveCount();
    }
    // As on Vulkan: every raw resource alive (the application's and the backend's own, such as the staging ring), and their memory.
    c.internal_objects = raw_resources_.load(std::memory_order_relaxed);
    c.device_memory_bytes = device_memory_bytes_.load(std::memory_order_relaxed);
    return c;
}

} // namespace engine_placeholder::rhi::dx12
