#include "settings/engine_settings.h"

#include "cstr/format.h"

#include "containers/unique_ptr.h"
#include "io/file_log_device.h"
#include "io/path.h"
#include "log/log.h"
#include "log/log_device.h"

namespace vse::settings
{

namespace
{

const char* const kWindowBackends[] = {"auto", "wayland", "x11", "win32", "cocoa"};  // cocoa: Phase 21 Day 295
const char* const kVerbosities[] = {"error", "warn", "info", "debug", "trace"};
const char* const kConsoleModes[] = {"auto", "plain", "color", "off"};  // the order of ConsoleLogMode

constexpr const char* kKeyConsole = "log.console";
constexpr const char* kKeyFilePath = "log.file.path";
constexpr const char* kKeyFileLevel = "log.file.level";
constexpr const char* kKeyFileMaxBytes = "log.file.max_bytes";
constexpr const char* kKeyFileKeep = "log.file.keep";

// The file log device the settings made, one per process. Its destructor removes it from the registry first, so a line
// logged while the process shuts down never reaches a destroyed device.
struct FileLogState
{
    UniquePtr<io::FileLogDevice> device;
    ~FileLogState() { Drop(); }
    void Drop()
    {
        if (device != nullptr)
        {
            RemoveLogDevice(device.Get());
            device = nullptr;
        }
    }
};

FileLogState& GetFileLogState()
{
    static FileLogState state;
    return state;
}

Value Read(const Registry& registry, const char* key)
{
    return registry.Current(registry.Find(StringView(key)));
}

void ApplyConsole(const Registry& registry)
{
    if (registry.Find(StringView(kKeyConsole)).IsValid())
    {
        SetConsoleLogMode(static_cast<ConsoleLogMode>(Read(registry, kKeyConsole).AsEnum()));
    }
}

// Makes the file device what the four `log.file.*` settings say: none for an empty path, else a fresh device on the
// path (reopening in append mode loses nothing). An unopenable path leaves the process without a file device and says
// so once on the core channel.
void ApplyFile(const Registry& registry, bool attaching)
{
    if (!registry.Find(StringView(kKeyFilePath)).IsValid())
    {
        return;
    }
    FileLogState& state = GetFileLogState();
    const Value path_value = Read(registry, kKeyFilePath);  // the text is borrowed from it
    const StringView text = path_value.AsText();
    if (attaching && text.Length() == 0)
    {
        return;  // nothing asked for: another Settings' log file stays
    }
    state.Drop();
    if (text.Length() == 0)
    {
        return;
    }
    io::Path path;
    if (!io::Path::Parse(text, io::NativePathStyle(), &path))
    {
        Log(LogCore, LogVerbosity::Warn, "log.file.path is not a usable path; no log file");
        return;
    }
    io::FileLogOptions options;
    options.max_bytes = static_cast<u64>(Read(registry, kKeyFileMaxBytes).AsInt());
    options.keep = static_cast<u32>(Read(registry, kKeyFileKeep).AsInt());
    UniquePtr<io::FileLogDevice> device;
    if (!io::FileLogDevice::Open(path, options, &device).Ok())
    {
        Log(LogCore, LogVerbosity::Warn, "log.file.path could not be opened; no log file");
        return;
    }
    if (AddLogDevice(device.Get(), static_cast<LogVerbosity>(Read(registry, kKeyFileLevel).AsEnum())))
    {
        state.device = static_cast<UniquePtr<io::FileLogDevice>&&>(device);
    }
}

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
    bool console_changed = false;
    bool file_changed = false;
    for (usize c = 0; c < event.count; ++c)
    {
        const StringView key = event.registry->KeyOf(event.changes[c].id).View();
        console_changed = console_changed || key == StringView(kKeyConsole);
        file_changed = file_changed || key == StringView(kKeyFilePath) || key == StringView(kKeyFileLevel) ||
                       key == StringView(kKeyFileMaxBytes) || key == StringView(kKeyFileKeep);
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
    if (console_changed)
    {
        ApplyConsole(*event.registry);
    }
    if (file_changed)
    {
        ApplyFile(*event.registry, false);
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
    if (!registry->Find(StringView(kKeyConsole)).IsValid())
    {
        SettingDesc console = MakeEnum(kKeyConsole, kConsoleModes, 4, 0);
        console.group = "log";
        console.description = "the console log device: auto (colour on a colour terminal), plain, color or off";
        console.flags = kAdvanced;
        registry->Register(console);
        SettingDesc path = MakeText(kKeyFilePath, "");
        path.group = "log";
        path.description = "append the log to this file (empty: no log file); its directory must exist";
        path.max_text = 511;
        path.flags = kAdvanced;
        registry->Register(path);
        SettingDesc level = MakeEnum(kKeyFileLevel, kVerbosities, 5, static_cast<u32>(LogVerbosity::Trace));
        level.group = "log";
        level.description = "the least severe level written to the log file";
        level.flags = kAdvanced;
        registry->Register(level);
        SettingDesc max_bytes = MakeInt(kKeyFileMaxBytes, 10 * 1024 * 1024, 0, i64{1} << 40);
        max_bytes.group = "log";
        max_bytes.description = "rotate the log file at this size in bytes (0: never)";
        max_bytes.flags = kAdvanced;
        registry->Register(max_bytes);
        SettingDesc keep = MakeInt(kKeyFileKeep, 3, 0, 99);
        keep.group = "log";
        keep.description = "rotated log files kept (name.1 is the newest)";
        keep.flags = kAdvanced;
        registry->Register(keep);
    }
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
    // The device settings: the console mode only if a layer set it (the code's choice stands otherwise); the file is
    // opened whenever a path is set.
    const SettingId console = settings->GetRegistry().Find(StringView(kKeyConsole));
    if (console.IsValid() && settings->Provenance(console).layer >= 0)
    {
        ApplyConsole(settings->GetRegistry());
    }
    ApplyFile(settings->GetRegistry(), true);
    settings->AddListener("log", &OnLogChange, nullptr);
}

}  // namespace vse::settings
