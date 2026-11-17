// The CoreAudio backend (Phase 26 Day 441). An AUHAL output unit bound to one device pulls frames through a render
// callback on CoreAudio's I/O thread; the callback renders straight into the unit's buffer when the unit takes
// interleaved f32 (the usual case) and through a scratch buffer, de-interleaved at the boundary, when it only takes one
// buffer per channel. The device grants its nominal rate (the mixer adapts, as with WASAPI) and the wanted layout when
// the device has that many output channels. The system's notifications (the default output changed, the device list
// changed, the device died, an I/O cycle was missed) arrive as property listener blocks on a private serial queue, so
// `Stop` and `Close` can drain it: after either returns no listener runs and the error callback is not called. A lost
// device, or a change of the default output when the default was opened, is reported once as `Lost`; the system reopens
// through Day 427's path.
#include "audio/macos/coreaudio_device.h"

#include "audio/pcm_negotiate.h"
#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#include <AudioToolbox/AudioToolbox.h>
#include <Block.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

namespace engine_placeholder::audio
{

namespace
{

AudioObjectPropertyAddress Address(AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    AudioObjectPropertyAddress a;
    a.mSelector = selector;
    a.mScope = scope;
    a.mElement = kAudioObjectPropertyElementMain;
    return a;
}

// Reads one fixed-size property; false when the object does not have it.
template <class T>
bool ReadProperty(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, T* out)
{
    const AudioObjectPropertyAddress a = Address(selector, scope);
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, out) == noErr && size == sizeof(T);
}

// The system's device ids; false when the list cannot be read or the heap is exhausted.
bool DeviceIds(Vector<AudioObjectID>* out)
{
    out->Clear();
    const AudioObjectPropertyAddress a = Address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
    {
        return false;
    }
    const usize count = size / sizeof(AudioObjectID);
    if (count == 0)
    {
        return true;
    }
    if (!out->TryResizeUninitialized(count))
    {
        return false;
    }
    size = static_cast<UInt32>(count * sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, out->Data()) != noErr)
    {
        out->Clear();
        return false;
    }
    out->Resize(size / sizeof(AudioObjectID));  // the list may have shrunk between the two calls
    return true;
}

// The output channels of a device, summed over its output streams; 0 for an input-only device.
u32 OutputChannels(AudioObjectID device)
{
    const AudioObjectPropertyAddress a = Address(kAudioDevicePropertyStreamConfiguration,
                                                 kAudioObjectPropertyScopeOutput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &a, 0, nullptr, &size) != noErr || size < sizeof(AudioBufferList))
    {
        return 0;
    }
    Vector<u8> bytes;
    if (!bytes.TryResizeUninitialized(size))
    {
        return 0;
    }
    AudioBufferList* list = reinterpret_cast<AudioBufferList*>(bytes.Data());
    if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, list) != noErr)
    {
        return 0;
    }
    u32 channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
    {
        channels += list->mBuffers[i].mNumberChannels;
    }
    return channels;
}

// The device's UID as UTF-8 into `out`; false when it has none or it does not fit.
bool DeviceUid(AudioObjectID device, char* out, usize capacity)
{
    CFStringRef uid = nullptr;
    if (!ReadProperty(device, kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, &uid) || uid == nullptr)
    {
        return false;
    }
    const bool ok = CFStringGetCString(uid, out, static_cast<CFIndex>(capacity), kCFStringEncodingUTF8);
    CFRelease(uid);
    return ok;
}

AudioObjectID DefaultOutput()
{
    AudioObjectID device = kAudioObjectUnknown;
    if (!ReadProperty(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
                      kAudioObjectPropertyScopeGlobal, &device))
    {
        return kAudioObjectUnknown;
    }
    return device;
}

u32 NominalRate(AudioObjectID device)
{
    Float64 rate = 0.0;
    if (!ReadProperty(device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, &rate) ||
        rate < 1.0)
    {
        return 0;
    }
    return static_cast<u32>(rate + 0.5);
}

DeviceStatus StatusOf(OSStatus status)
{
    if (status == kAudioHardwareBadDeviceError || status == kAudioHardwareBadObjectError)
    {
        return DeviceStatus::Lost;
    }
    if (status == kAudioDevicePermissionsError)
    {
        return DeviceStatus::PermissionDenied;
    }
    if (status == kAudioDeviceUnsupportedFormatError || status == kAudioUnitErr_FormatNotSupported)
    {
        return DeviceStatus::Unsupported;
    }
    return DeviceStatus::IoError;
}

u64 NowMicros()
{
    return os::MonotonicTimeNanos() / 1000;
}

class CoreAudioDevice final : public AudioDevice
{
public:
    ~CoreAudioDevice() override { Close(); }

    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (unit_ != nullptr)
        {
            return DeviceStatus::AlreadyOpen;
        }
        if (config.buffer_frames < 1 || config.buffer_frames > kMaxDeviceBufferFrames ||
            ChannelCount(config.layout) == 0 ||
            (config.sample_rate != 0 && (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate)))
        {
            return DeviceStatus::InvalidConfig;
        }
        const DeviceStatus status = OpenUnit(config);
        if (status != DeviceStatus::Ok)
        {
            Release();
        }
        return status;
    }

    DeviceStatus Start() override
    {
        if (unit_ == nullptr || render_ == nullptr)
        {
            return DeviceStatus::NotOpen;
        }
        if (running_.Load())
        {
            return DeviceStatus::Busy;
        }
        if (lost_.Load())
        {
            return DeviceStatus::Lost;
        }
        stop_.Store(false);
        const OSStatus status = AudioOutputUnitStart(unit_);
        if (status != noErr)
        {
            stop_.Store(true);
            return StatusOf(status);
        }
        running_.Store(true);
        return DeviceStatus::Ok;
    }

    void Stop() override
    {
        if (!running_.Load())
        {
            return;
        }
        // From here the callback renders silence without calling render, and the listeners do not call the error
        // callback; then wait for a render call already in flight and for the listener queue.
        stop_.Store(true);
        AudioOutputUnitStop(unit_);
        while (in_render_.Load() != 0)
        {
            ThisThread::Yield();
        }
        dispatch_sync(queue_, ^{
                      });
        running_.Store(false);
    }

    DeviceStatus Close() override
    {
        Stop();
        Release();
        return DeviceStatus::Ok;
    }

    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return unit_ != nullptr; }
    bool IsRunning() const override { return running_.Load(); }
    DeviceStatus Pump(u64) override { return DeviceStatus::Unsupported; }
    u64 FramesRendered() const override { return rendered_.Load(); }

    DeviceCounters Counters() const override
    {
        DeviceCounters c;
        c.callbacks = callbacks_.Load();
        c.underruns = underruns_.Load();
        c.slow_callbacks = slow_.Load();
        c.longest_callback_micros = longest_.Load();
        return c;
    }

private:
    // Finds the device, makes and configures the unit, allocates the scratch buffer and starts listening.
    DeviceStatus OpenUnit(const DeviceConfig& config)
    {
        const bool want_default = config.name.Empty() || config.name == StringView("default");
        device_ = want_default ? DefaultOutput() : FindByUid(config.name);
        if (device_ == kAudioObjectUnknown)
        {
            return DeviceStatus::NotFound;
        }
        const u32 device_channels = OutputChannels(device_);
        const u32 rate = NominalRate(device_);
        if (device_channels == 0)
        {
            return DeviceStatus::NotFound;  // an input-only device is not an output
        }
        if (rate < kMinSampleRate || rate > kMaxSampleRate)
        {
            return DeviceStatus::Unsupported;
        }
        // The device's own channel count, as WASAPI grants the endpoint's (the mixer adapts): handing AUHAL a narrower
        // stream would play it on the first channels only (mono from the left speaker). A count with no layout plays
        // stereo (or mono) on its first channels.
        ChannelLayout layout = ChannelLayout::Stereo;
        if (!LayoutOf(device_channels, &layout))
        {
            layout = device_channels >= 2 ? ChannelLayout::Stereo : ChannelLayout::Mono;
        }
        const u32 channels = ChannelCount(layout);

        AudioComponentDescription description = {};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_HALOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (component == nullptr)
        {
            return DeviceStatus::NotFound;
        }
        OSStatus status = AudioComponentInstanceNew(component, &unit_);
        if (status != noErr)
        {
            unit_ = nullptr;
            return StatusOf(status);
        }
        status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                      &device_, sizeof(device_));
        if (status != noErr)
        {
            return StatusOf(status);
        }

        // The buffer size: the wanted frames within the device's range; then read back what the device took.
        AudioValueRange range = {};
        u32 frames = config.buffer_frames;
        if (ReadProperty(device_, kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, &range))
        {
            frames = frames < static_cast<u32>(range.mMinimum) ? static_cast<u32>(range.mMinimum) : frames;
            frames = frames > static_cast<u32>(range.mMaximum) ? static_cast<u32>(range.mMaximum) : frames;
        }
        const AudioObjectPropertyAddress size_address = Address(kAudioDevicePropertyBufferFrameSize);
        UInt32 wanted = frames;
        AudioObjectSetPropertyData(device_, &size_address, 0, nullptr, sizeof(wanted), &wanted);  // best effort
        UInt32 granted = 0;
        if (!ReadProperty(device_, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, &granted) ||
            granted == 0)
        {
            granted = frames;
        }
        frames = granted < kMaxDeviceBufferFrames ? granted : kMaxDeviceBufferFrames;
        UInt32 slice = 0;
        UInt32 slice_size = sizeof(slice);
        if (AudioUnitGetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &slice,
                                 &slice_size) != noErr ||
            slice < granted)
        {
            slice = granted;
            AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &slice,
                                 sizeof(slice));
        }

        // The format the callback writes: f32 at the device's rate, interleaved if the unit takes it.
        AudioStreamBasicDescription format = {};
        format.mSampleRate = rate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = channels;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = 4 * channels;
        format.mBytesPerPacket = 4 * channels;
        interleaved_ = true;
        status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                      sizeof(format));
        if (status != noErr)
        {
            format.mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
            format.mBytesPerFrame = 4;
            format.mBytesPerPacket = 4;
            interleaved_ = false;
            status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                          sizeof(format));
        }
        if (status != noErr)
        {
            return DeviceStatus::Unsupported;
        }
        AURenderCallbackStruct callback;
        callback.inputProc = &CoreAudioDevice::RenderProc;
        callback.inputProcRefCon = this;
        status = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                                      sizeof(callback));
        if (status == noErr)
        {
            status = AudioUnitInitialize(unit_);
        }
        if (status != noErr)
        {
            return StatusOf(status);
        }
        initialized_ = true;
        if (!interleaved_ && !scratch_.TryResizeUninitialized(static_cast<usize>(frames) * channels))
        {
            return DeviceStatus::OutOfMemory;
        }

        channels_ = channels;
        info_.sample_rate = rate;
        info_.layout = layout;
        info_.buffer_frames = frames;
        info_.latency_frames = granted + Latency(device_);
        lost_.Store(false);
        stop_.Store(true);
        rendered_.Store(0);
        callbacks_.Store(0);
        underruns_.Store(0);
        slow_.Store(0);
        longest_.Store(0);
        return Listen(want_default);
    }

    // The device's own latency, its safety offset and the first output stream's latency, in frames.
    static u32 Latency(AudioObjectID device)
    {
        UInt32 latency = 0;
        UInt32 safety = 0;
        ReadProperty(device, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, &latency);
        ReadProperty(device, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, &safety);
        u32 total = latency + safety;
        const AudioObjectPropertyAddress a = Address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput);
        AudioStreamID stream = kAudioObjectUnknown;
        UInt32 size = sizeof(stream);  // only the first stream is read
        if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, &stream) == noErr && size == sizeof(stream))
        {
            UInt32 stream_latency = 0;
            ReadProperty(stream, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, &stream_latency);
            total += stream_latency;
        }
        return total;
    }

    static AudioObjectID FindByUid(StringView name)
    {
        Vector<AudioObjectID> ids;
        if (!DeviceIds(&ids))
        {
            return kAudioObjectUnknown;
        }
        for (usize i = 0; i < ids.Size(); ++i)
        {
            char uid[sizeof(DeviceDescription::name)];
            if (DeviceUid(ids[i], uid, sizeof(uid)) && StringView(uid) == name)
            {
                return ids[i];
            }
        }
        return kAudioObjectUnknown;
    }

    // Registers the listeners on the private queue. Each block only reads properties and sets atomics.
    DeviceStatus Listen(bool follows_default)
    {
        queue_ = dispatch_queue_create("engine.audio.coreaudio", DISPATCH_QUEUE_SERIAL);
        if (queue_ == nullptr)
        {
            return DeviceStatus::OutOfMemory;
        }
        follows_default_ = follows_default;
        system_listener_ = Block_copy(^(UInt32, const AudioObjectPropertyAddress*) {
          if (follows_default_)
          {
              // A failed read (the HAL updating its list) says nothing about the default: only a different device is.
              const AudioObjectID now = DefaultOutput();
              if (now != kAudioObjectUnknown && now != device_)
              {
                  ReportLost();
              }
          }
          else if (!StillListed())
          {
              ReportLost();
          }
        });
        device_listener_ = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress* addresses) {
          for (UInt32 i = 0; i < count; ++i)
          {
              if (addresses[i].mSelector == kAudioDeviceProcessorOverload)
              {
                  underruns_.FetchAdd(1);  // CoreAudio missed an I/O cycle
              }
              else if (addresses[i].mSelector == kAudioDevicePropertyDeviceIsAlive)
              {
                  UInt32 alive = 1;
                  if (!ReadProperty(device_, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                    &alive) ||
                      alive == 0)
                  {
                      ReportLost();
                  }
              }
          }
        });
        if (system_listener_ == nullptr || device_listener_ == nullptr)
        {
            return DeviceStatus::OutOfMemory;
        }
        for (u32 i = 0; i < kSystemSelectors.Size(); ++i)
        {
            const AudioObjectPropertyAddress a = Address(kSystemSelectors[i]);
            if (AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &a, queue_, system_listener_) != noErr)
            {
                return DeviceStatus::IoError;  // without the listener a loss would go unseen
            }
            system_listening_[i] = true;
        }
        for (u32 i = 0; i < kDeviceSelectors.Size(); ++i)
        {
            const AudioObjectPropertyAddress a = Address(kDeviceSelectors[i]);
            if (AudioObjectAddPropertyListenerBlock(device_, &a, queue_, device_listener_) != noErr)
            {
                return DeviceStatus::IoError;
            }
            device_listening_[i] = true;
        }
        return DeviceStatus::Ok;
    }

    // Whether the opened device is still in the system's list (the listener of the device list).
    bool StillListed() const
    {
        Vector<AudioObjectID> ids;
        if (!DeviceIds(&ids))
        {
            return true;  // cannot tell: the device's own IsAlive listener still watches
        }
        for (usize i = 0; i < ids.Size(); ++i)
        {
            if (ids[i] == device_)
            {
                return true;
            }
        }
        return false;
    }

    // Releases everything Open made, in the reverse order; no listener runs after it returns.
    void Release()
    {
        for (u32 i = 0; i < kDeviceSelectors.Size(); ++i)
        {
            if (device_listening_[i])
            {
                const AudioObjectPropertyAddress a = Address(kDeviceSelectors[i]);
                AudioObjectRemovePropertyListenerBlock(device_, &a, queue_, device_listener_);
                device_listening_[i] = false;
            }
        }
        for (u32 i = 0; i < kSystemSelectors.Size(); ++i)
        {
            if (system_listening_[i])
            {
                const AudioObjectPropertyAddress a = Address(kSystemSelectors[i]);
                AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &a, queue_, system_listener_);
                system_listening_[i] = false;
            }
        }
        if (queue_ != nullptr)
        {
            dispatch_sync(queue_, ^{
                          });  // a block queued before the removal has run
            dispatch_release(queue_);
            queue_ = nullptr;
        }
        if (system_listener_ != nullptr)
        {
            Block_release(system_listener_);
            system_listener_ = nullptr;
        }
        if (device_listener_ != nullptr)
        {
            Block_release(device_listener_);
            device_listener_ = nullptr;
        }
        if (unit_ != nullptr)
        {
            if (initialized_)
            {
                AudioUnitUninitialize(unit_);
                initialized_ = false;
            }
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
        device_ = kAudioObjectUnknown;
        scratch_.Reset();
    }

    static OSStatus RenderProc(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*, UInt32,
                               UInt32 frames, AudioBufferList* io)
    {
        return static_cast<CoreAudioDevice*>(self)->Render(flags, frames, io);
    }

    // CoreAudio's I/O thread: renders `frames` frames in calls of at most the granted buffer size.
    OSStatus Render(AudioUnitRenderActionFlags* flags, UInt32 frames, AudioBufferList* io)
    {
        in_render_.FetchAdd(1);
        if (stop_.Load() || lost_.Load() || !BuffersFit(io, frames))
        {
            for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
            {
                MemSet(io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);
            }
            *flags |= kAudioUnitRenderAction_OutputIsSilence;
            in_render_.FetchSub(1);
            return noErr;
        }
        u32 done = 0;
        while (done < frames)
        {
            const u32 left = frames - done;
            const u32 n = left < info_.buffer_frames ? left : info_.buffer_frames;
            f32* target = interleaved_ ? static_cast<f32*>(io->mBuffers[0].mData) + static_cast<usize>(done) * channels_
                                       : scratch_.Data();
            const u64 start = NowMicros();
            render_(user_, target, n);
            const u64 took = NowMicros() - start;
            callbacks_.FetchAdd(1);
            rendered_.FetchAdd(n);
            if (took > static_cast<u64>(n) * 1000000ull / info_.sample_rate)  // longer than the frames it rendered last
            {
                slow_.FetchAdd(1);
            }
            u64 longest = longest_.Load();
            while (took > longest && !longest_.CompareExchangeWeak(longest, took))
            {
            }
            if (!interleaved_)
            {
                // One buffer per channel: de-interleave the scratch into each.
                for (u32 c = 0; c < channels_; ++c)  // BuffersFit checked one buffer per channel
                {
                    f32* out = static_cast<f32*>(io->mBuffers[c].mData) + done;
                    for (u32 i = 0; i < n; ++i)
                    {
                        out[i] = scratch_[static_cast<usize>(i) * channels_ + c];
                    }
                }
            }
            done += n;
        }
        in_render_.FetchSub(1);
        return noErr;
    }

    // Whether the unit's buffers hold `frames` frames in the format set in `Open`; a pull that does not fit is played
    // as silence rather than written past a buffer.
    bool BuffersFit(const AudioBufferList* io, UInt32 frames) const
    {
        const usize floats = interleaved_ ? static_cast<usize>(frames) * channels_ : frames;
        const u32 buffers = interleaved_ ? 1 : channels_;
        if (io->mNumberBuffers != buffers)
        {
            return false;
        }
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
        {
            if (io->mBuffers[b].mData == nullptr || io->mBuffers[b].mDataByteSize < floats * sizeof(f32))
            {
                return false;
            }
        }
        return true;
    }

    // Called once, from the listener queue, when the device went away or the default output changed.
    void ReportLost()
    {
        if (lost_.Exchange(true))
        {
            return;
        }
        // stop_ first: it is false only between Start and Stop, when SetCallbacks may not run (Start's store orders the
        // callbacks before this read; Stop drains this queue after setting it).
        if (!stop_.Load() && error_ != nullptr)
        {
            error_(user_, DeviceStatus::Lost);
        }
    }

    static constexpr Array<AudioObjectPropertySelector, 2> kSystemSelectors = {
        kAudioHardwarePropertyDefaultOutputDevice, kAudioHardwarePropertyDevices};
    static constexpr Array<AudioObjectPropertySelector, 2> kDeviceSelectors = {kAudioDevicePropertyDeviceIsAlive,
                                                                               kAudioDeviceProcessorOverload};

    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    AudioObjectID device_ = kAudioObjectUnknown;
    AudioComponentInstance unit_ = nullptr;
    bool initialized_ = false;
    bool interleaved_ = true;
    bool follows_default_ = false;
    u32 channels_ = 0;
    DeviceInfo info_;
    Vector<f32> scratch_;
    dispatch_queue_t queue_ = nullptr;
    AudioObjectPropertyListenerBlock system_listener_ = nullptr;
    AudioObjectPropertyListenerBlock device_listener_ = nullptr;
    Array<bool, 2> system_listening_ = {false, false};
    Array<bool, 2> device_listening_ = {false, false};
    Atomic<bool> running_{false};
    Atomic<bool> stop_{true};
    Atomic<bool> lost_{false};
    Atomic<u32> in_render_{0};
    Atomic<u64> rendered_{0};
    Atomic<u64> callbacks_{0};
    Atomic<u64> underruns_{0};
    Atomic<u64> slow_{0};
    Atomic<u64> longest_{0};
};

}  // namespace

DeviceStatus EnumerateCoreAudioDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    Vector<AudioObjectID> ids;
    if (!DeviceIds(&ids))
    {
        return DeviceStatus::Ok;  // the system will not list its devices: no outputs
    }
    const AudioObjectID default_device = DefaultOutput();
    for (usize i = 0; i < ids.Size(); ++i)
    {
        const u32 channels = OutputChannels(ids[i]);
        if (channels == 0)
        {
            continue;
        }
        DeviceDescription d;
        if (!DeviceUid(ids[i], d.name, sizeof(d.name)))
        {
            continue;  // no UID, or one too long to name the device by
        }
        d.is_default = ids[i] == default_device;
        d.max_channels = channels;
        const u32 rate = NominalRate(ids[i]);
        if (rate != 0)
        {
            d.min_rate = rate;  // the device plays its nominal rate: that is what `Open` grants
            d.max_rate = rate;
        }
        if (!out->TryReserve(out->Size() + 1))
        {
            return DeviceStatus::OutOfMemory;
        }
        out->Push(d);
    }
    return DeviceStatus::Ok;
}

UniquePtr<AudioDevice> CreateCoreAudioDevice()
{
    return MakeUnique<CoreAudioDevice>();
}

}  // namespace engine_placeholder::audio
