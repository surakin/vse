// The hardware devices (Phase 26 Days 425-426): the negotiation against recorded capability tables, the conversion at
// the device boundary, and, where a sound device exists, opening it, rendering at its pace, recovering from a stalled
// callback and destroying a running device.
#include <catch2/catch_test_macros.hpp>

#include "audio/device.h"
#include "audio/device_list.h"
#include "audio/pcm_negotiate.h"
#include "build_defs.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::audio;

namespace
{

// Recorded from this project's machines: an analogue HD-audio codec (16-bit only), a USB DAC (everything).
PcmCaps HdaCodec()
{
    PcmCaps caps;
    caps.rate_min = 44100;
    caps.rate_max = 192000;
    caps.channels_min = 2;
    caps.channels_max = 2;
    caps.has_s16 = true;
    caps.has_s32 = true;
    caps.period_min = 32;
    caps.period_max = 8192;
    caps.periods_min = 2;
    caps.periods_max = 32;
    caps.buffer_min = 64;
    caps.buffer_max = 16384;
    return caps;
}

PcmCaps UsbDac()
{
    PcmCaps caps;
    caps.rate_min = 8000;
    caps.rate_max = 384000;
    caps.channels_min = 2;
    caps.channels_max = 8;
    caps.has_f32 = true;
    caps.has_s32 = true;
    caps.has_s16 = true;
    caps.period_min = 48;
    caps.period_max = 4096;
    caps.periods_min = 2;
    caps.periods_max = 8;
    caps.buffer_min = 96;
    caps.buffer_max = 8192;
    return caps;
}

#if ENGINE_PLATFORM_LINUX || ENGINE_PLATFORM_WINDOWS || ENGINE_PLATFORM_MACOS
// The hardware tests' render callback (Linux, Windows from Day 440 and macOS from Day 441).
struct Counting
{
    Atomic<u64> calls{0};
    Atomic<u64> frames{0};
    Atomic<u32> sleep_ms{0};
};

void CountingRender(void* user, f32* out, u32 frames)
{
    Counting* counting = static_cast<Counting*>(user);
    for (u32 i = 0; i < frames * 2; ++i)
    {
        out[i] = 0.0f;
    }
    counting->calls.FetchAdd(1);
    counting->frames.FetchAdd(frames);
    const u32 sleep = counting->sleep_ms.Load();
    if (sleep > 0)
    {
        ThisThread::SleepForMilliseconds(sleep);
    }
}

// Opens the default output with a stereo 1024-frame configuration, or says why the test is skipped.
bool OpenDefault(UniquePtr<AudioDevice>* device, Counting* counting, u32 frames)
{
    *device = CreateDevice(DeviceKind::Platform);
    REQUIRE(*device != nullptr);
    (*device)->SetCallbacks(&CountingRender, nullptr, counting);
    DeviceConfig config;
    config.buffer_frames = frames;
    const DeviceStatus status = (*device)->Open(config);
    if (status != DeviceStatus::Ok)
    {
        WARN("no sound device here (open status " << static_cast<u32>(status) << "): the hardware test is skipped");
        return false;
    }
    return true;
}
#endif

}  // namespace

TEST_CASE("Backend: the negotiation picks the best format and clamps into the card's range", "[audio][backend]")
{
    PcmGrant grant;
    REQUIRE(NegotiatePcm(UsbDac(), 48000, 2, 1024, &grant) == DeviceStatus::Ok);
    CHECK(grant.format == PcmFormat::F32);
    CHECK(grant.rate == 48000);
    CHECK(grant.channels == 2);
    CHECK(grant.period_frames == 1024);
    CHECK(grant.periods == 3);
    REQUIRE(NegotiatePcm(HdaCodec(), 48000, 6, 128, &grant) == DeviceStatus::Ok);
    CHECK(grant.format == PcmFormat::S32);  // no float: the next best
    CHECK(grant.channels == 2);             // the codec has two
    REQUIRE(NegotiatePcm(HdaCodec(), 0, 2, 100000, &grant) == DeviceStatus::Ok);
    CHECK(grant.rate == 48000);                           // 0 asks for the default rate
    CHECK(grant.period_frames * grant.periods <= 16384);  // the period shrank until two or three fit
    CHECK(grant.periods >= 2);
    REQUIRE(NegotiatePcm(UsbDac(), 4000, 2, 16, &grant) == DeviceStatus::Ok);
    CHECK(grant.rate == 8000);
    CHECK(grant.period_frames == 48);
    PcmCaps sixteen = HdaCodec();
    sixteen.has_s32 = false;
    REQUIRE(NegotiatePcm(sixteen, 96000, 2, 256, &grant) == DeviceStatus::Ok);
    CHECK(grant.format == PcmFormat::S16);
}

TEST_CASE("Backend: a table that offers nothing playable is refused", "[audio][backend]")
{
    PcmGrant grant;
    grant.rate = 7;
    PcmCaps none = HdaCodec();
    none.has_s16 = false;
    none.has_s32 = false;
    CHECK(NegotiatePcm(none, 48000, 2, 256, &grant) == DeviceStatus::Unsupported);
    CHECK(NegotiatePcm(PcmCaps(), 48000, 2, 256, &grant) == DeviceStatus::Unsupported);
    PcmCaps tiny = UsbDac();
    tiny.buffer_max = 40;  // smaller than two minimum periods
    CHECK(NegotiatePcm(tiny, 48000, 2, 256, &grant) == DeviceStatus::Unsupported);
    CHECK(grant.rate == 7);  // not written on failure
}

TEST_CASE("Backend: conversion at the device boundary", "[audio][backend]")
{
    const f32 in[5] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f};
    f32 as_float[5] = {};
    ConvertForPcm(PcmFormat::F32, in, 5, as_float);
    for (u32 i = 0; i < 5; ++i)
    {
        CHECK(as_float[i] == in[i]);
    }
    i16 as_short[5] = {};
    ConvertForPcm(PcmFormat::S16, in, 5, as_short);
    CHECK(as_short[0] == 0);
    CHECK(as_short[1] == 32767);
    CHECK(as_short[2] <= -32767);
    CHECK(as_short[3] > 16000);
    CHECK(as_short[3] < 16500);
    CHECK(as_short[4] == 32767);  // clipped
    i32 as_int[5] = {};
    ConvertForPcm(PcmFormat::S32, in, 5, as_int);
    CHECK(as_int[0] == 0);
    CHECK(as_int[1] > 2147483000);
    CHECK(as_int[2] < -2147483000);
    CHECK(PcmSampleBytes(PcmFormat::S16) == 2);
    CHECK(PcmSampleBytes(PcmFormat::F32) == 4);
}

#if ENGINE_PLATFORM_LINUX

TEST_CASE("Backend: the lists have valid names and a default", "[audio][backend][linux]")
{
    for (const DeviceKind kind : {DeviceKind::Platform, DeviceKind::PipeWire, DeviceKind::Alsa})
    {
        Vector<DeviceDescription> list;
        REQUIRE(EnumerateDevices(kind, &list) == DeviceStatus::Ok);
        u32 defaults = 0;
        for (u32 i = 0; i < list.Size(); ++i)
        {
            CHECK(list[i].name[0] != 0);
            CHECK(list[i].max_channels >= 2);
            defaults += list[i].is_default ? 1 : 0;
        }
        CHECK(defaults == (list.Size() == 0 ? 0u : 1u));
    }
}

TEST_CASE("Backend: a name that is not there, and a bad configuration, are reported by value",
          "[audio][backend][linux]")
{
    UniquePtr<AudioDevice> alsa = CreateDevice(DeviceKind::Alsa);
    REQUIRE(alsa != nullptr);
    DeviceConfig config;
    config.name = StringView("hw:99,0");
    CHECK(alsa->Open(config) == DeviceStatus::NotFound);
    config.name = StringView("nonsense");
    CHECK(alsa->Open(config) == DeviceStatus::NotFound);
    config.name = StringView("default");
    config.buffer_frames = 0;
    CHECK(alsa->Open(config) == DeviceStatus::InvalidConfig);
    CHECK(alsa->Start() == DeviceStatus::NotOpen);
    CHECK(!alsa->IsOpen());
    alsa->Stop();
    CHECK(alsa->Close() == DeviceStatus::Ok);
    UniquePtr<AudioDevice> platform = CreateDevice(DeviceKind::Platform);
    config.buffer_frames = 1024;
    config.name = StringView("hw:99,0");
    CHECK(platform->Open(config) == DeviceStatus::NotFound);
    CHECK(platform->Pump(10) == DeviceStatus::Unsupported);
}

#endif

#if ENGINE_PLATFORM_WINDOWS

TEST_CASE("Backend: the WASAPI list has endpoint ids and one default", "[audio][backend][windows]")
{
    Vector<DeviceDescription> list;
    REQUIRE(EnumerateDevices(DeviceKind::Platform, &list) == DeviceStatus::Ok);
    u32 defaults = 0;
    for (u32 i = 0; i < list.Size(); ++i)
    {
        CHECK(list[i].name[0] == '{');  // an endpoint id, {0.0.0.00000000}.{guid}
        CHECK(list[i].max_channels >= 1);
        CHECK(list[i].min_rate == list[i].max_rate);  // shared mode plays the mix rate
        defaults += list[i].is_default ? 1 : 0;
    }
    CHECK(defaults == (list.Size() == 0 ? 0u : 1u));
}

TEST_CASE("Backend: WASAPI reports a missing endpoint and a bad configuration by value", "[audio][backend][windows]")
{
    UniquePtr<AudioDevice> device = CreateDevice(DeviceKind::Platform);
    REQUIRE(device != nullptr);
    DeviceConfig config;
    config.name = StringView("{0.0.0.00000000}.{00000000-0000-0000-0000-000000000000}");
    CHECK(device->Open(config) == DeviceStatus::NotFound);
    CHECK(!device->IsOpen());
    config.name = StringView("default");
    config.buffer_frames = 0;
    CHECK(device->Open(config) == DeviceStatus::InvalidConfig);
    CHECK(device->Start() == DeviceStatus::NotOpen);
    CHECK(device->Pump(10) == DeviceStatus::Unsupported);
    device->Stop();
    CHECK(device->Close() == DeviceStatus::Ok);
    CHECK(device->Close() == DeviceStatus::Ok);
}

#endif

#if ENGINE_PLATFORM_MACOS

TEST_CASE("Backend: the CoreAudio list has device UIDs and one default", "[audio][backend][macos]")
{
    Vector<DeviceDescription> list;
    REQUIRE(EnumerateDevices(DeviceKind::Platform, &list) == DeviceStatus::Ok);
    u32 defaults = 0;
    for (u32 i = 0; i < list.Size(); ++i)
    {
        CHECK(list[i].name[0] != 0);
        CHECK(list[i].max_channels >= 1);
        CHECK(list[i].min_rate == list[i].max_rate);  // the nominal rate is what Open grants
        defaults += list[i].is_default ? 1 : 0;
    }
    CHECK(defaults <= 1u);  // a default that is not an output (or has no UID) is not listed
}

TEST_CASE("Backend: CoreAudio reports a missing device and a bad configuration by value", "[audio][backend][macos]")
{
    UniquePtr<AudioDevice> device = CreateDevice(DeviceKind::Platform);
    REQUIRE(device != nullptr);
    DeviceConfig config;
    config.name = StringView("no-such-device-uid");
    CHECK(device->Open(config) == DeviceStatus::NotFound);
    CHECK(!device->IsOpen());
    config.name = StringView("default");
    config.buffer_frames = 0;
    CHECK(device->Open(config) == DeviceStatus::InvalidConfig);
    CHECK(device->Start() == DeviceStatus::NotOpen);
    CHECK(device->Pump(10) == DeviceStatus::Unsupported);
    device->Stop();
    CHECK(device->Close() == DeviceStatus::Ok);
    CHECK(device->Close() == DeviceStatus::Ok);
}

TEST_CASE("Backend: a CoreAudio device opened by its UID grants its own layout and reopens",
          "[audio][backend][macos][hardware]")
{
    Vector<DeviceDescription> list;
    REQUIRE(EnumerateDevices(DeviceKind::Platform, &list) == DeviceStatus::Ok);
    if (list.Size() == 0)
    {
        WARN("no sound output here: the hardware test is skipped");
        return;
    }
    Counting counting;
    UniquePtr<AudioDevice> device = CreateDevice(DeviceKind::Platform);
    REQUIRE(device != nullptr);
    device->SetCallbacks(&CountingRender, nullptr, &counting);
    DeviceConfig config;
    config.name = StringView(list[0].name);
    ChannelLayout own = ChannelLayout::Stereo;
    if (!LayoutOf(list[0].max_channels, &own))
    {
        own = list[0].max_channels >= 2 ? ChannelLayout::Stereo : ChannelLayout::Mono;
    }
    // The device's own count is granted whatever is asked: fewer channels would play on the first speakers only (a mono
    // request on stereo speakers from the left one), more cannot be played.
    config.layout = ChannelLayout::Mono;
    REQUIRE(device->Open(config) == DeviceStatus::Ok);
    CHECK(device->Info().layout == own);
    CHECK(device->Close() == DeviceStatus::Ok);
    config.layout = ChannelLayout::Surround51;
    REQUIRE(device->Open(config) == DeviceStatus::Ok);
    CHECK(device->Info().layout == own);
    CHECK(device->Info().sample_rate == list[0].max_rate);
    CHECK(device->Open(config) == DeviceStatus::AlreadyOpen);
    CHECK(device->Close() == DeviceStatus::Ok);
    config.layout = ChannelLayout::Stereo;
    REQUIRE(device->Open(config) == DeviceStatus::Ok);  // open again after a close
    REQUIRE(device->Start() == DeviceStatus::Ok);
    CHECK(device->Start() == DeviceStatus::Busy);
    ThisThread::SleepForMilliseconds(200);
    device->Stop();
    const u64 calls = counting.calls.Load();
    CHECK(calls > 0);
    ThisThread::SleepForMilliseconds(50);
    CHECK(counting.calls.Load() == calls);  // no render call after Stop returned
    CHECK(device->Close() == DeviceStatus::Ok);
}

#endif

#if ENGINE_PLATFORM_LINUX || ENGINE_PLATFORM_WINDOWS || ENGINE_PLATFORM_MACOS

TEST_CASE("Backend: a real device is rendered at its own pace", "[audio][backend][hardware]")
{
    UniquePtr<AudioDevice> device;
    Counting counting;
    if (!OpenDefault(&device, &counting, 1024))
    {
        return;
    }
    const DeviceInfo info = device->Info();
    CHECK(info.sample_rate >= 8000);
    CHECK(info.buffer_frames >= 1);
    CHECK(info.latency_frames >= 1);
    REQUIRE(device->Start() == DeviceStatus::Ok);
    CHECK(device->IsRunning());
    ThisThread::SleepForMilliseconds(600);
    device->Stop();
    CHECK(!device->IsRunning());
    const u64 frames = counting.frames.Load();
    // 600 ms of audio, less the time to start and the prefill that runs ahead: within a wide tolerance of real time.
    const u64 expected = static_cast<u64>(info.sample_rate) * 600 / 1000;
    CHECK(frames > expected / 2);
    CHECK(frames < expected * 2 + 4 * info.buffer_frames * 2);
    CHECK(device->Counters().callbacks == counting.calls.Load());
    CHECK(device->Close() == DeviceStatus::Ok);
    CHECK(!device->IsOpen());
}

TEST_CASE("Backend: a stalled callback is counted and the device keeps playing", "[audio][backend][hardware]")
{
    UniquePtr<AudioDevice> device;
    Counting counting;
    if (!OpenDefault(&device, &counting, 256))
    {
        return;
    }
    const DeviceInfo info = device->Info();
    REQUIRE(device->Start() == DeviceStatus::Ok);
    ThisThread::SleepForMilliseconds(100);
    const u64 before = counting.calls.Load();
    counting.sleep_ms.Store(static_cast<u32>(info.buffer_frames * 1000ull / info.sample_rate * 8 + 60));
    ThisThread::SleepForMilliseconds(600);
    counting.sleep_ms.Store(0);
    ThisThread::SleepForMilliseconds(300);
    const u64 after = counting.calls.Load();
    device->Stop();
    const DeviceCounters counters = device->Counters();
    CHECK(counters.slow_callbacks >= 1);
    CHECK(counters.longest_callback_micros >= 10000);
    CHECK(after > before + 3);  // it recovered and went on calling
    device->Close();
}

TEST_CASE("Backend: destroying a running device waits for its callback", "[audio][backend][hardware]")
{
    Counting counting;
    {
        UniquePtr<AudioDevice> device;
        if (!OpenDefault(&device, &counting, 512))
        {
            return;
        }
        REQUIRE(device->Start() == DeviceStatus::Ok);
        ThisThread::SleepForMilliseconds(80);
    }  // destroyed while running
    const u64 calls = counting.calls.Load();
    ThisThread::SleepForMilliseconds(100);
    CHECK(counting.calls.Load() == calls);  // no callback after the destructor returned
}

#endif
