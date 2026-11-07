#include "settings_main.h"

#include <cstdio>

#include "io/path.h"
#include "renderer/shared_settings.h"
#include "settings/engine_settings.h"
#include "settings/report.h"

namespace demo {

DemoSettings::DemoSettings() = default;
DemoSettings::~DemoSettings() = default;

bool DemoSettings::Begin(const char* app_name, const char* usage, int argc, char** argv) {
    usage_ = String<HeapAllocator>(HeapAllocator::Default(), StringView(usage));
    program_ = String<HeapAllocator>(HeapAllocator::Default(), StringView(argv[0]));
    ::engine_placeholder::renderer::RegisterSharedSettings(&registry_);
    sset::RegisterEngineSettings(&registry_);
    sset::RegisterLogSettings(&registry_);
    settings_ = MakeUnique<sset::Settings>(registry_);
    ::engine_placeholder::renderer::AddRendererMigrations(&settings_->Migrations());
    sset::LoadOptions options;
    options.app_name = app_name;
    options.argc = argc;
    options.argv = argv;
    const sset::LoadReport report = settings_->Load(options);
    sset::AttachLogSettings(settings_.Get());
    for (usize i = 0; i < report.problems.Size(); ++i) {
        const sset::LayerProblem& p = report.problems[i];
        std::fprintf(stderr, "%s: settings: %s%s%s: %s\n", app_name, p.layer, p.key[0] != '\0' ? ": " : "", p.key, p.message);
    }
    for (usize i = 0; i < report.notes.Size(); ++i) {
        const sset::LayerProblem& n = report.notes[i];
        std::fprintf(stderr, "%s: settings: %s: %s\n", app_name, n.layer, n.message);
    }
    const sset::CommandLineResult& cli = settings_->CommandLine();
    rest_storage_.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(argv[0])));
    for (usize i = 0; i < cli.rest.Size(); ++i) rest_storage_.Push(String<HeapAllocator>(HeapAllocator::Default(), cli.rest[i].View()));
    for (usize i = 0; i < rest_storage_.Size(); ++i) rest_pointers_.Push(const_cast<char*>(rest_storage_[i].CStr()));
    rest_pointers_.Push(nullptr); // argv[argc] is null, as the C runtime leaves it
    // The load report already listed the command-line errors and bad command-line values; the caller exits with its usage code.
    bool bad_value = false;
    for (usize i = 0; i < report.problems.Size(); ++i) bad_value = bad_value || StringView(report.problems[i].layer) == "command line";
    return cli.errors.Empty() && !bad_value;
}

void DemoSettings::SetLegacy(const char* key, const char* text, const char* flag) {
    // An old flag counts as the command line: it outranks files and the environment, is not written to the user's file, and yields to an explicit --set.
    settings_->SetFromCommandLine(StringView(key), StringView(text), StringView(flag));
}

void DemoSettings::FillDeviceCreateInfo(::engine_placeholder::rhi::DeviceCreateInfo* info) {
    const SharedPtr<const sset::SettingsSnapshot> shared = settings_->Snapshot();
    auto get = [&](const char* key) -> const sset::Value& { return shared->Get(registry_.Find(StringView(key))); };
    info->validation = static_cast<::engine_placeholder::rhi::ValidationSetting>(get("renderer.validation").AsEnum());
    info->sync_validation = get("renderer.sync_validation").AsBool();
    info->frames_in_flight = static_cast<u32>(get("renderer.frames_in_flight").AsInt());
    info->pipeline_cache = get("rhi.pipeline_cache").AsBool();
    const StringView adapter = get("renderer.adapter").AsText();
    const usize a = adapter.Length() < sizeof(adapter_) - 1 ? adapter.Length() : sizeof(adapter_) - 1;
    for (usize i = 0; i < a; ++i) adapter_[i] = adapter[i];
    adapter_[a] = '\0';
    info->adapter = a != 0 ? adapter_ : nullptr;
    const StringView dir = get("rhi.pipeline_cache_dir").AsText();
    const usize d = dir.Length() < sizeof(cache_dir_) - 1 ? dir.Length() : sizeof(cache_dir_) - 1;
    for (usize i = 0; i < d; ++i) cache_dir_[i] = dir[i];
    cache_dir_[d] = '\0';
    if (d != 0) info->pipeline_cache_dir = cache_dir_;
}

DemoSettings::Effective DemoSettings::ReadEffective() {
    Effective e;
    const SharedPtr<const sset::SettingsSnapshot> shared = settings_->Snapshot();
    const sset::SettingsSnapshot& snapshot = *shared;
    auto id = [&](const char* key) { return registry_.Find(StringView(key)); };
    const StringView adapter = snapshot.Get(id("renderer.adapter")).AsText();
    const usize n = adapter.Length() < sizeof(e.gpu) - 1 ? adapter.Length() : sizeof(e.gpu) - 1;
    for (usize i = 0; i < n; ++i) e.gpu[i] = adapter[i];
    e.immediate = snapshot.Get(id("renderer.present_mode")).AsEnum() == 2;
    e.validation = snapshot.Get(id("renderer.validation")).AsEnum() == 2;
    static const char* const kBackends[] = {"auto", "wayland", "x11"};
    std::snprintf(e.window_backend, sizeof(e.window_backend), "%s", kBackends[snapshot.Get(id("platform.window_backend")).AsEnum() % 3]);
    return e;
}

int DemoSettings::Finish() {
    const sset::CommandLineResult& cli = settings_->CommandLine();
    if (cli.help) {
        std::fputs(usage_.CStr(), stdout);
        String<HeapAllocator> help(HeapAllocator::Default());
        sset::FormatSettingsHelp(registry_, &help);
        std::fputs(help.CStr(), stdout);
        return 0;
    }
    if (cli.list_settings) {
        String<HeapAllocator> listing(HeapAllocator::Default());
        sset::FormatSettingsListing(*settings_, &listing);
        std::fputs(listing.CStr(), stdout);
        return 0;
    }
    if (cli.save_settings) {
        io::Path path;
        if (cli.save_path.Length() != 0) {
            if (!io::Path::Parse(cli.save_path.View(), io::NativePathStyle(), &path)) {
                std::fprintf(stderr, "%s: --save-settings: the path is not valid\n", program_.CStr());
                return 1;
            }
            const sset::Status s = sset::SaveSettingsTo(*settings_, path);
            if (s.Failed()) {
                std::fprintf(stderr, "%s: --save-settings: could not write %s (%s)\n", program_.CStr(), cli.save_path.CStr(), sset::StoreErrorName(s.error));
                return 1;
            }
        } else {
            const sset::Status s = settings_->Save(sset::SaveOptions{true, false, false});
            if (s.Failed()) {
                std::fprintf(stderr, "%s: --save-settings: could not write the user's settings file (%s)\n", program_.CStr(), sset::StoreErrorName(s.error));
                return 1;
            }
        }
        return 0;
    }
    return -1;
}

}  // namespace demo
