// The Metal device (Phase 26 Day 316; metal_device.h): creation, the timeline and frames, the capabilities,
// command-buffer errors, device loss. Objective-C++ with ARC: the device, queue, event and listener are strong members
// released by the destructor after the GPU is idle.
#include "rhi/metal/metal_device.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "containers/array.h"
#include "sync/atomic.h"
#include "log/assert.h"
#include "os/console.h"
#include "os/os.h"
#include "rhi/metal/metal_adapters.h"
#include "rhi/metal/metal_command_list.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

namespace
{

// A not-yet-built operation of Days 317-321: logged once per day of the plan in each program, then quiet (a renderer
// asks for every resource, on every reset).
void NotYet(const char* what, u32 day)
{
    static Array<Atomic<bool>, 8> said;  // Days 316-323
    const u32 index = day >= 316 && day < 316 + 8 ? day - 316 : 7;
    if (!said[index].Exchange(true, MemoryOrder::AcqRel))
    {
        RhiLog(LogVerbosity::Error,
               "Metal: %s is not built yet (Phase 26 Day %u; logged once for that day's operations)", what, day);
    }
}

// What a failed command buffer's error code means; the codes are MTLCommandBufferError's.
const char* CommandBufferErrorName(NSInteger code)
{
    switch (code)
    {
    case MTLCommandBufferErrorNone: return "none";
    case MTLCommandBufferErrorInternal: return "internal";
    case MTLCommandBufferErrorTimeout: return "timeout";
    case MTLCommandBufferErrorPageFault: return "page fault";
    case MTLCommandBufferErrorAccessRevoked: return "access revoked";
    case MTLCommandBufferErrorNotPermitted: return "not permitted";
    case MTLCommandBufferErrorOutOfMemory: return "out of memory";
    case MTLCommandBufferErrorInvalidResource: return "invalid resource";
    case MTLCommandBufferErrorMemoryless: return "memoryless";
    case MTLCommandBufferErrorDeviceRemoved: return "device removed";
    case MTLCommandBufferErrorStackOverflow: return "stack overflow";
    default: return "unknown";
    }
}

// Whether the error leaves the device unusable (removed, revoked, or a GPU fault the driver reset after), not just the
// one buffer failed.
bool LosesDevice(NSInteger code)
{
    return code == MTLCommandBufferErrorDeviceRemoved || code == MTLCommandBufferErrorAccessRevoked ||
           code == MTLCommandBufferErrorTimeout || code == MTLCommandBufferErrorPageFault ||
           code == MTLCommandBufferErrorInternal;
}

bool EnvironmentOn(const char* name)
{
    const char* value = os::GetEnvironment(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

}  // namespace

MetalDevicePtr MetalDevice::Create(const DeviceCreateInfo& info)
{
    // Held with the plain deleter until initialization succeeds: a half-built device must not be waited on.
    UniquePtr<MetalDevice> device = MakeUnique<MetalDevice>();
    if (!device || !device->Initialize(info))
    {
        return MetalDevicePtr();
    }
    return MetalDevicePtr(device.Release());
}

// The descriptor is not read: whether an adapter can present does not depend on the window (a device that is not
// headless can draw into any CAMetalLayer).
bool MetalDevice::EnumerateAdapters(const DeviceCreateInfo&, AdapterList* out)
{
    MetalAdapterSet set;
    @autoreleasepool
    {
        if (!ListMetalAdapters(&set))
        {
            *out = AdapterList{};
            RhiLog(LogVerbosity::Error, "Metal: this Mac has no Metal device");
            return false;
        }
    }
    *out = set.list;
    return true;
}

bool MetalDevice::Initialize(const DeviceCreateInfo& info)
{
    if (!shared_)
    {
        RhiLog(LogVerbosity::Error, "CreateDevice: out of memory for the Metal device's shared state");
        return false;
    }
    @autoreleasepool
    {
        MetalAdapterSet set;
        if (!ListMetalAdapters(&set))
        {
            RhiLog(LogVerbosity::Error, "CreateDevice: this Mac has no Metal device");
            return false;
        }
        adapters_ = set.list;
        // The descriptor's adapter, else ENGINE_RHI_GPU, as the other backends pick (the setting renderer.adapter fills
        // the descriptor in programs that have Settings).
        const char* requested = info.adapter != nullptr && info.adapter[0] != '\0'
                                    ? info.adapter
                                    : os::GetEnvironment("ENGINE_RHI_GPU");
        char reason[256] = {};
        const int pick = PickMetalAdapter(set, requested, reason, sizeof(reason));
        if (pick < 0)
        {
            RhiLog(LogVerbosity::Error, "CreateDevice: %s", reason);
            return false;
        }
        device_ = set.devices[static_cast<NSUInteger>(pick)];
        adapter_ = set.list.items[pick];
        // Command lists hold their command buffer from Begin to Submit, and many may be open at once (recording on
        // jobs): the queue allows more than Metal's default 64 outstanding buffers, past which taking one would block.
        queue_ = [device_ newCommandQueueWithMaxCommandBufferCount:kMaxCommandBuffers];
        timeline_ = [device_ newSharedEvent];
        dispatch_queue_t notify = dispatch_queue_create("engine.metal.timeline", DISPATCH_QUEUE_SERIAL);
        listener_ = [[MTLSharedEventListener alloc] initWithDispatchQueue:notify];
        if (queue_ == nil || timeline_ == nil || listener_ == nil)
        {
            RhiLog(LogVerbosity::Error, "CreateDevice: the Metal queue or timeline event could not be created on '%s'",
                   adapter_.name);
            return false;
        }
        queue_.label = @"engine queue";
        timeline_.label = @"engine timeline";
        timeline_.signaledValue = 0;
    }
    // Validation is the process's: Metal reads MTL_DEBUG_LAYER (and MTL_SHADER_VALIDATION) when it starts, so the
    // setting can only report it. `On` without the variable is a warning, not a failure.
    validation_ = EnvironmentOn("MTL_DEBUG_LAYER");
    if (info.validation == ValidationSetting::On && !validation_)
    {
        RhiLog(LogVerbosity::Warn,
               "Metal: validation was asked for, but Metal's is an environment setting read at process start: set "
               "MTL_DEBUG_LAYER=1 (and MTL_SHADER_VALIDATION=1, MTL_DEBUG_LAYER_ERROR_MODE=assert) before starting");
    }
    frames_in_flight_ = info.frames_in_flight < 1 ? 1 : (info.frames_in_flight > 3 ? 3 : info.frames_in_flight);
    lose_device_at_frame_ = info.faults.lose_device_at_frame;
    lose_device_at_submit_ = info.faults.lose_device_at_submit;
    strikes_left_ = info.faults.strikes_left;
    fail_allocations_from_ = info.faults.fail_allocations_from;
    FillCaps();
    epoch_ = AllocateDeviceSerial();  // the handle pools are seeded from it
    if (!InitializeResources())
    {
        return false;
    }
    pipeline_cache_.Open(device_, info, adapter_, epoch_);  // running without a cache is not a failure
    RhiLog(LogVerbosity::Info, "Metal: device on '%s' (%s), validation %s%s, %s memory", adapter_.name, adapter_.id,
           validation_ ? "on" : "off", EnvironmentOn("MTL_SHADER_VALIDATION") ? " with shader validation" : "",
           managed_ ? "managed" : "shared");
    return true;
}

void MetalDevice::FillCaps()
{
    caps_ = DeviceCaps{};
    caps_.max_texture_dimension_2d = 16384;  // the Mac2 family
    caps_.max_texture_dimension_3d = 2048;
    caps_.max_texture_array_layers = 2048;
    caps_.max_color_attachments = 8;
    caps_.max_push_constant_bytes = kMaxPushConstantBytes;  // setBytes: takes up to 4 KiB
    caps_.min_uniform_buffer_offset_alignment = 256;        // constant buffers on macOS
    caps_.min_storage_buffer_offset_alignment = 256;
    caps_.buffer_texture_copy_offset_alignment = 16;  // a multiple of the texel size, which 16 covers for every format
    caps_.buffer_texture_copy_row_pitch_alignment = 1;  // nothing beyond the texel size
    caps_.max_sampler_anisotropy = 16.0f;
    caps_.depth_format = Format::D32_Float;
    caps_.timestamp_period_ns = 0.0f;
    caps_
        .supports_bindless = false;  // argument-buffer tier 1 on this Mac (decision 9): bind groups are direct bindings
    caps_.shader_format = ShaderFormat::MetalLib;
    caps_.object_index_stream = true;  // [[instance_id]] excludes the base instance: the renderer's index stream
    for (usize f = 1; f < static_cast<usize>(Format::Count); ++f)
    {
        // Metal generates mips with a blit (Day 320) for a color format that can be rendered and filtered.
        mip_support_[f] = MtlFormatCanGenerateMips(static_cast<Format>(f), device_);
    }
}

MetalDevice::~MetalDevice()
{
    // DeviceDeleter waited for the GPU; a device that never finished initializing has nothing in flight.
    @autoreleasepool
    {
        if (queue_ != nil)
        {
            WaitIdle();  // collects every retired resource
            const ResourceCounts counts = Resources();
            if (counts.UserObjects() != 0)
            {
                NoteLeakedUserObjects(counts.UserObjects());
                RhiLog(LogVerbosity::Warn,
                       "Metal: %zu application object(s) were still alive when the device was destroyed",
                       counts.UserObjects());
            }
        }
        if (queue_ != nil)
        {
            pipeline_cache_.Save();
        }
        ShutdownResources();
        listener_ = nil;
        timeline_ = nil;
        queue_ = nil;
        device_ = nil;
    }
}

bool MetalDevice::FormatSupport(Format format, TextureUsage usage) const
{
    return MtlFormatSupports(format, usage, device_);
}

bool MetalDevice::CanGenerateMips(Format format) const
{
    return format != Format::Undefined && static_cast<usize>(format) < static_cast<usize>(Format::Count) &&
           mip_support_[static_cast<usize>(format)];
}

// ---- errors and loss ----

void MetalDevice::NoteLost(MetalDeviceShared& shared, const char* where, const char* reason)
{
    if (shared.lost.Exchange(true, MemoryOrder::AcqRel))
    {
        return;
    }
    RhiLog(LogVerbosity::Error, "Metal: the device was lost (noticed in %s: %s); further submissions are ignored",
           where, reason);
    ENGINE_ENSURE(false, "the Metal device was lost");  // the same report as the other backends' loss: a trace, once
}

void MetalDevice::WatchCommandBuffer(id<MTLCommandBuffer> buffer)
{
    // The block holds its own reference to the shared state, never `this`: the handler may run after the device is
    // destroyed (DeviceDeleter waits for the timeline, which Metal signals before it calls the handlers).
    SharedPtr<MetalDeviceShared> shared = shared_;
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
      if (done.status != MTLCommandBufferStatusError)
      {
          return;
      }
      const NSInteger code = done.error != nil ? done.error.code : MTLCommandBufferErrorInternal;
      NSString* label = done.label != nil ? done.label : @"(no label)";
      NSString* text = done.error != nil ? done.error.localizedDescription : @"no description";
      RhiLog(LogVerbosity::Error, "Metal: command buffer '%s' failed (%s, %ld): %s", label.UTF8String,
             CommandBufferErrorName(code), static_cast<long>(code), text.UTF8String);
      if (LosesDevice(code))
      {
          NoteLost(*shared, "a command buffer's completion", CommandBufferErrorName(code));
      }
    }];
}

bool MetalDevice::TakeStrike()
{
    if (strikes_left_ == nullptr)
    {
        return true;
    }
    u32 left = strikes_left_->Load(MemoryOrder::Relaxed);
    while (left != 0)
    {
        if (strikes_left_->CompareExchangeWeak(left, left - 1, MemoryOrder::Relaxed))
        {
            return true;
        }
    }
    return false;
}

bool MetalDevice::InjectLoss(const char* where, u32 number)
{
    if (DeviceLost())
    {
        return true;
    }
    if (!TakeStrike())
    {
        return false;
    }
    char reason[64];
    FormatTo(reason, sizeof(reason), "injected at %s %u", where, number);
    NoteLost(where, reason);
    return true;
}

void MetalDevice::InjectDeviceLoss()
{
    // Metal has no call that removes a device: the loss is recorded, and every later submission is ignored, as for a
    // real one (the real removal notifications are Day 325's).
    InjectLoss("request", 0);
}

// ---- the timeline ----

u64 MetalDevice::SignalAfterLocked(id<MTLCommandBuffer> buffer)
{
    const u64 value = last_signaled_ + 1;
    [buffer encodeSignalEvent:timeline_ value:value];
    last_signaled_ = value;
    last_signaled_published_.Store(value, MemoryOrder::Release);
    return value;
}

u64 MetalDevice::SignalLocked()
{
    if (DeviceLost())
    {
        return 0;
    }
    @autoreleasepool
    {
        id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
        if (buffer == nil)
        {
            NoteLost("a timeline signal", "the queue gave no command buffer");
            return 0;
        }
        buffer.label = @"engine timeline signal";
        WatchCommandBuffer(buffer);
        const u64 value = SignalAfterLocked(buffer);
        [buffer commit];
        return value;
    }
}

bool MetalDevice::WaitForValue(u64 value, u64 timeout_ns)
{
    if (value == 0 || timeline_.signaledValue >= value)
    {
        return true;
    }
    if (timeout_ns == 0 || DeviceLost())
    {
        return false;
    }
    // The listener signals a semaphore when the timeline reaches the value; the wait takes it in slices, so a lost
    // device (whose buffers never finish) ends it within one slice. A late notification signals a semaphore nobody
    // waits on, which the block keeps alive.
    dispatch_semaphore_t reached = dispatch_semaphore_create(0);
    [timeline_ notifyListener:listener_
                      atValue:value
                        block:^(id<MTLSharedEvent>, uint64_t) {
                          dispatch_semaphore_signal(reached);
                        }];
    const u64 start = os::MonotonicTimeNanos();
    for (;;)
    {
        u64 slice_ns = kWaitSliceMs * 1000000ull;
        if (timeout_ns != ~0ull)
        {
            const u64 elapsed = os::MonotonicTimeNanos() - start;
            if (elapsed >= timeout_ns)
            {
                return timeline_.signaledValue >= value;
            }
            slice_ns = timeout_ns - elapsed < slice_ns ? timeout_ns - elapsed : slice_ns;
        }
        if (dispatch_semaphore_wait(reached, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(slice_ns))) == 0 ||
            timeline_.signaledValue >= value)
        {
            return true;
        }
        if (DeviceLost())
        {
            return false;
        }
    }
}

bool MetalDevice::WaitFence(FenceValue value, u64 timeout_ns)
{
    if (value == 0)
    {
        return true;
    }
    return WaitForValue(value, timeout_ns);
}

FenceValue MetalDevice::CompletedFence() const
{
    // A lost device's buffers may never finish: everything submitted counts as done then, as on the other backends.
    if (DeviceLost())
    {
        return last_signaled_published_.Load(MemoryOrder::Acquire);
    }
    return timeline_.signaledValue;
}

// ---- frames ----

void MetalDevice::UpdateCompletedFramesLocked()
{
    const u64 completed = CompletedFence();
    while (completed_frames_ < ended_frames_ && frame_fences_[(completed_frames_ + 1) % kTrackedFrames] <= completed)
    {
        ++completed_frames_;
    }
}

u64 MetalDevice::CompletedFrame() const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const_cast<MetalDevice*>(this)->UpdateCompletedFramesLocked();
    return completed_frames_;
}

void MetalDevice::BeginFrame()
{
    // Keep at most frames_in_flight frames on the GPU and never let the unfinished frames outrun the fence ring; the
    // wait happens outside the lock.
    for (;;)
    {
        u64 wait_for = 0;
        {
            LockGuard<Mutex> lock(resource_mutex_);
            UpdateCompletedFramesLocked();
            if (ended_frames_ - completed_frames_ >= kTrackedFrames - 2)
            {
                wait_for = frame_fences_[(completed_frames_ + 1) % kTrackedFrames];
            }
            const u64 reuse_frame = frame_number_ + 1 > frames_in_flight_ ? frame_number_ + 1 - frames_in_flight_ : 0;
            if (reuse_frame > completed_frames_ && reuse_frame <= ended_frames_)
            {
                const u64 fence = frame_fences_[reuse_frame % kTrackedFrames];
                wait_for = wait_for > fence ? wait_for : fence;
            }
        }
        if (wait_for == 0 || !WaitForValue(wait_for, ~0ull))
        {
            break;  // nothing to wait for, or the device is lost (the wait returned)
        }
    }
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(!frame_open_.Load(MemoryOrder::Relaxed), "BeginFrame: the previous frame was not ended");
    UpdateCompletedFramesLocked();
    ++frame_number_;
    frame_open_.Store(true, MemoryOrder::Release);
    if (lose_device_at_frame_ != 0 && frame_number_ == lose_device_at_frame_)
    {
        InjectLoss("frame", static_cast<u32>(frame_number_));
    }
    RetireFrameGroupsLocked(static_cast<u32>(frame_number_ % kFrameSlots));  // that slot's last frame has finished
    CollectRetiredLocked(false);                                             // what the finished frames destroyed
}

void MetalDevice::EndFrame()
{
    u64 frame = 0;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Relaxed), "EndFrame: no frame is open");
        frame_open_.Store(false, MemoryOrder::Release);  // later destroys belong to the next frame
        frame = frame_number_;
    }
    // Read the last signal after closing the frame: a resource destroyed in this frame may have been used by any
    // submission made before the close, and this value covers all of them.
    u64 last = 0;
    {
        LockGuard<Mutex> lock(submit_mutex_);
        last = last_signaled_;
    }
    LockGuard<Mutex> lock(resource_mutex_);
    ended_frames_ = frame;
    frame_fences_[frame % kTrackedFrames] = last;
}

u64 MetalDevice::FrameNumber() const
{
    LockGuard<Mutex> lock(resource_mutex_);
    return frame_number_;
}

void MetalDevice::WaitIdle()
{
    u64 last = 0;
    {
        LockGuard<Mutex> lock(submit_mutex_);
        last = last_signaled_;
    }
    WaitForValue(last, ~0ull);
    LockGuard<Mutex> lock(resource_mutex_);
    UpdateCompletedFramesLocked();
    CollectRetiredLocked(true);
}

ResourceCounts MetalDevice::Resources() const
{
    ResourceCounts c;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        c.buffers = buffers_.LiveCount();
        c.textures = textures_.LiveCount();
        c.samplers = samplers_.LiveCount();
        c.shaders = shaders_.LiveCount();
        c.pipelines = pipelines_.LiveCount();
        c.bind_group_layouts = bind_group_layouts_.LiveCount();
        bind_groups_.ForEachLive([&](BindGroupHandle, const BindGroupRes& g)
                                 { c.bind_groups += g.transient ? 0 : 1; });  // transient groups end with their frame
    }
    // As on Vulkan and D3D12: every Metal resource alive (the application's and the device's own, such as the staging
    // ring), and their memory.
    c.internal_objects = raw_resources_.Load(MemoryOrder::Relaxed);
    c.device_memory_bytes = device_memory_bytes_.Load(MemoryOrder::Relaxed);
    return c;
}

// ---- Day 321 ----

Swapchain* MetalDevice::CreateSwapchain(const SwapchainDesc&)
{
    NotYet("CreateSwapchain", 321);
    return nullptr;
}
void MetalDevice::DestroySwapchain(Swapchain*) {}
bool MetalDevice::RecreateSurface(const os::Window*)
{
    return true;  // no swapchain before Day 321: nothing to recreate
}

}  // namespace engine_placeholder::rhi::metal
