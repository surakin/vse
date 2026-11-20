#include "settings/engine_settings.h"

#include "cstr/format.h"

#include "log/log.h"

namespace vse::settings
{

namespace
{

const char* const kWindowBackends[] = {"auto", "wayland", "x11", "win32", "cocoa"};  // cocoa: Phase 21 Day 295
const char* const kVerbosities[] = {"error", "warn", "info", "debug", "trace"};

struct LogKeys
{
    // keys[2 * i] is the verbosity key of the i-th registered channel, keys[2 * i + 1] the enabled key.
    char keys[64][2][80] = {};
    char channel_names[64][48] = {};
    u32 count = 0;
};

LogKeys& GetLogKeys()
{
    static LogKeys keys;
    return keys;
}

void LowerCase(const char* in, char* out, usize capacity)
{
    usize i = 0;
    for (; in[i] != '\0' && i + 1 < capacity; ++i)
    {
        out[i] = in[i] >= 'A' && in[i] <= 'Z' ? static_cast<char>(in[i] - 'A' + 'a') : in[i];
    }
    out[i] = '\0';
}

void ApplyChannel(LogChannel* channel, StringView what, const Value& value)
{
    if (what == "verbosity")
    {
        channel->SetVerbosity(static_cast<LogVerbosity>(value.AsEnum()));
    }
    else if (what == "enabled")
    {
        channel->SetEnabled(value.AsBool());
    }
}

void OnLogChange(void*, const ChangeEvent& event)
{
    const LogKeys& keys = GetLogKeys();
    for (usize c = 0; c < event.count; ++c)
    {
        const StringView key = event.registry->KeyOf(event.changes[c].id).View();
        for (u32 i = 0; i < keys.count; ++i)
        {
            for (int k = 0; k < 2; ++k)
            {
                if (key != StringView(keys.keys[i][k]))
                {
                    continue;
                }
                if (LogChannel* channel = LogChannel::Find(StringView(keys.channel_names[i])))
                {
                    ApplyChannel(channel, k == 0 ? StringView("verbosity") : StringView("enabled"),
                                 event.changes[c].new_value);
                }
            }
        }
    }
}

}  // namespace

void RegisterEngineSettings(Registry* registry)
{
    SettingDesc d = MakeEnum("platform.window_backend", kWindowBackends, 5, 0);
    d.env = "ENGINE_WINDOW_BACKEND";
    d.flag = "backend";
    d.label = "Window backend";
    d.description =
        "the display backend: wayland or x11 on Linux, win32 on Windows, cocoa on macOS (auto picks Wayland, then X11, on Linux)";
    d.group = "platform";
    d.flags = kRestartRequired;
    registry->Register(d);

    d = MakeBool("rhi.pipeline_cache", true);
    d.description = "keep the Vulkan pipeline cache on disk";
    d.group = "rhi";
    d.flags = kRestartRequired | kAdvanced;
    registry->Register(d);

    d = MakeText("rhi.pipeline_cache_dir", "");
    d.env = "ENGINE_RHI_CACHE_DIR";
    d.description = "the directory of the pipeline cache file (empty: the user cache directory)";
    d.group = "rhi";
    d.max_text = 255;
    d.flags = kRestartRequired | kAdvanced;
    registry->Register(d);
}

void RegisterLogSettings(Registry* registry)
{
    LogKeys& keys = GetLogKeys();
    // The key strings live in a table that only grows, so every registry that borrows them stays valid however often
    // this runs.
    for (LogChannel* c = LogChannel::First(); c != nullptr; c = c->Next())
    {
        if (StringView(c->Name()).Length() >= sizeof(keys.channel_names[0]))
        {
            continue;  // a name that cannot be kept whole is skipped, not cut
        }
        u32 i = 0;
        while (i < keys.count && StringView(keys.channel_names[i]) != StringView(c->Name()))
        {
            ++i;
        }
        if (i == keys.count)
        {
            if (keys.count >= 64)
            {
                continue;
            }
            char lower[48];
            LowerCase(c->Name(), lower, sizeof(lower));
            FormatTo(keys.keys[i][0], sizeof(keys.keys[i][0]), "log.%s.verbosity", lower);
            FormatTo(keys.keys[i][1], sizeof(keys.keys[i][1]), "log.%s.enabled", lower);
            if (Key::Parse(StringView(keys.keys[i][0]), nullptr) != KeyError::None ||
                Key::Parse(StringView(keys.keys[i][1]), nullptr) != KeyError::None)
            {
                continue;
            }
            FormatTo(keys.channel_names[i], sizeof(keys.channel_names[i]), "%s", c->Name());
            ++keys.count;
        }
        if (registry->Find(StringView(keys.keys[i][0])).IsValid())
        {
            continue;
        }
        SettingDesc v = MakeEnum(keys.keys[i][0], kVerbosities, 5, static_cast<u32>(c->Verbosity()));
        v.group = "log";
        v.description = "how much the channel logs: error, warn, info, debug or trace";
        v.flags = kAdvanced;
        registry->Register(v);
        SettingDesc en = MakeBool(keys.keys[i][1], c->Enabled());
        en.group = "log";
        en.description = "whether the channel logs at all";
        en.flags = kAdvanced;
        registry->Register(en);
    }
}

void AttachLogSettings(Settings* settings)
{
    const LogKeys& keys = GetLogKeys();
    for (u32 i = 0; i < keys.count; ++i)
    {
        LogChannel* channel = LogChannel::Find(StringView(keys.channel_names[i]));
        if (channel == nullptr)
        {
            continue;
        }
        const SettingId v = settings->GetRegistry().Find(StringView(keys.keys[i][0]));
        const SettingId en = settings->GetRegistry().Find(StringView(keys.keys[i][1]));
        // Only what a layer set: a channel configured in code keeps its configuration unless a file, the environment or
        // the command line says otherwise.
        if (v.IsValid() && settings->Provenance(v).layer >= 0)
        {
            ApplyChannel(channel, "verbosity", settings->GetRegistry().Current(v));
        }
        if (en.IsValid() && settings->Provenance(en).layer >= 0)
        {
            ApplyChannel(channel, "enabled", settings->GetRegistry().Current(en));
        }
    }
    settings->AddListener("log", &OnLogChange, nullptr);
}

}  // namespace vse::settings
