// audio_probe: Phase 26 Day 425. A command-line tool for listening to the real audio backend by hand.
//
//   audio_probe list [--backend=auto|pipewire|alsa|wasapi|coreaudio|null]    the outputs of that backend
//   audio_probe play [--backend=...] [--device=NAME] [--seconds=N] [--frequency=HZ] [--buffer=FRAMES] [--rate=HZ]
//                                                            plays a quiet sine tone through the device and prints
//                                                            the granted configuration and the counters
//
// The options are this tool's own command line, not engine settings.
#include "build_defs.h"
#include "log/output.h"
#include "audio/device.h"
#include "audio/device_list.h"
#include "containers/vector.h"
#include "cstr/number.h"
#include "lang/scalar.h"
#include "os/command_line.h"
#include "os/console.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::audio;

namespace
{

constexpr os::CommandLineOption kOptions[] = {{"backend", true},   {"device", true}, {"seconds", true},
                                              {"frequency", true}, {"buffer", true}, {"rate", true}};
using Line = os::CommandLine<HeapAllocator>;

int Usage()
{
    out::PrintError("usage: audio_probe list [--backend=auto|pipewire|alsa|wasapi|coreaudio|null]\n"
                    "       audio_probe play [--backend=...] [--device=NAME] [--seconds=N] [--frequency=HZ]\n"
                    "                        [--buffer=FRAMES] [--rate=HZ]\n");
    return 64;
}

bool KindOf(const char* text, DeviceKind* kind)
{
    const StringView name(text);
    // WASAPI and CoreAudio are the platform's output, each named only on its own platform (Day 441 review).
    if (name == StringView("auto") || (ENGINE_PLATFORM_WINDOWS && name == StringView("wasapi")) ||
        (ENGINE_PLATFORM_MACOS && name == StringView("coreaudio")))
    {
        *kind = DeviceKind::Platform;
    }
    else if (name == StringView("pipewire"))
    {
        *kind = DeviceKind::PipeWire;
    }
    else if (name == StringView("alsa"))
    {
        *kind = DeviceKind::Alsa;
    }
    else if (name == StringView("null"))
    {
        *kind = DeviceKind::Null;
    }
    else
    {
        return false;
    }
    return true;
}

u32 NumberOption(const Line& line, const char* name, u32 fallback)
{
    const char* text = line.OptionOr(name, nullptr);
    if (text == nullptr)
    {
        return fallback;
    }
    u32 value = 0;
    for (u32 i = 0; text[i] != 0; ++i)
    {
        if (text[i] < '0' || text[i] > '9' || value > 100000000u)
        {
            return fallback;
        }
        value = value * 10 + static_cast<u32>(text[i] - '0');
    }
    return value;
}

struct Tone
{
    u32 channels = 2;
    f32 step = 0.0f;
    f32 phase = 0.0f;
};

void RenderTone(void* user, f32* out, u32 frames)
{
    Tone* tone = static_cast<Tone*>(user);
    for (u32 n = 0; n < frames; ++n)
    {
        const f32 v = 0.1f * Sin(tone->phase);
        tone->phase += tone->step;
        if (tone->phase > 6.2831853f)
        {
            tone->phase -= 6.2831853f;
        }
        for (u32 c = 0; c < tone->channels; ++c)
        {
            out[static_cast<usize>(n) * tone->channels + c] = v;
        }
    }
}

int List(DeviceKind kind)
{
    Vector<DeviceDescription> devices;
    EnumerateDevices(kind, &devices);
    if (devices.Size() == 0)
    {
        out::Print("no outputs\n");
    }
    for (u32 i = 0; i < devices.Size(); ++i)
    {
        out::Print("%s%s  up to %u channels\n", devices[i].name, devices[i].is_default ? " (default)" : "",
                   devices[i].max_channels);
    }
    return 0;
}

int Play(const Line& line, DeviceKind kind)
{
    UniquePtr<AudioDevice> device = CreateDevice(kind);
    if (device == nullptr)
    {
        return 1;
    }
    DeviceConfig config;
    const char* name = line.OptionOr("device", "");
    config.name = StringView(name);
    config.sample_rate = NumberOption(line, "rate", 48000);
    config.buffer_frames = NumberOption(line, "buffer", 1024);
    const u32 seconds = NumberOption(line, "seconds", 3);
    const u32 frequency = NumberOption(line, "frequency", 440);
    Tone tone;
    device->SetCallbacks(&RenderTone, nullptr, &tone);
    const DeviceStatus opened = device->Open(config);
    if (opened != DeviceStatus::Ok)
    {
        out::PrintError("audio_probe: open failed (status %u)\n", static_cast<u32>(opened));
        return 2;
    }
    const DeviceInfo info = device->Info();
    tone.channels = ChannelCount(info.layout);
    tone.step = 6.2831853f * static_cast<f32>(frequency) / static_cast<f32>(info.sample_rate);
    out::Print("granted: %u Hz, %u channels, %u frames per call, latency %u frames\n", info.sample_rate, tone.channels,
               info.buffer_frames, info.latency_frames);
    if (device->Start() != DeviceStatus::Ok)
    {
        out::PrintError("audio_probe: start failed\n");
        return 3;
    }
    ThisThread::SleepForMilliseconds(static_cast<u64>(seconds) * 1000);
    device->Stop();
    const DeviceCounters counters = device->Counters();
    out::Print("callbacks %llu, underruns %llu, slow %llu, longest %llu us, frames %llu\n",
               static_cast<unsigned long long>(counters.callbacks), static_cast<unsigned long long>(counters.underruns),
               static_cast<unsigned long long>(counters.slow_callbacks),
               static_cast<unsigned long long>(counters.longest_callback_micros),
               static_cast<unsigned long long>(device->FramesRendered()));
    device->Close();
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const Line line(HeapAllocator::Default(), argc, argv, Span<const os::CommandLineOption>(kOptions, 6));
    if (!line.Ok() || line.ArgCount() < 1)
    {
        if (!line.Ok())
        {
            out::PrintError("audio_probe: %s\n", line.Error().Data());
        }
        return Usage();
    }
    DeviceKind kind = DeviceKind::Platform;
    if (!KindOf(line.OptionOr("backend", "auto"), &kind))
    {
        return Usage();
    }
    const StringView command = line.Arg(0);
    if (command == StringView("list"))
    {
        return List(kind);
    }
    if (command == StringView("play"))
    {
        return Play(line, kind);
    }
    return Usage();
}
