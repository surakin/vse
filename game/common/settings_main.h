#pragma once

// The settings front end of a demo: builds the registry and the Settings of the program from its command line, handles the options every program has
// (--set, --settings, --save-settings, --list-settings, --help) and hands back the arguments that belong to the demo.
//
//     demo::DemoSettings demo_settings;
//     if (!demo_settings.Begin("renderer_demo", usage_text, argc, argv)) return 2;   // a bad --set key etc.: already printed
//     ... parse demo_settings.Rest() with the demo's own flags, and set what they mean with demo_settings.Get().SetText(...)
//     const int code = demo_settings.Finish();                                       // --help, --list-settings, --save-settings
//     if (code >= 0) return code;

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "rhi/rhi.h"
#include "settings/settings.h"
#include "string/string.h"

namespace demo {

using namespace engine_placeholder;
namespace sset = ::engine_placeholder::settings;

class DemoSettings {
public:
    DemoSettings();
    ~DemoSettings();
    DemoSettings(const DemoSettings&) = delete;
    DemoSettings& operator=(const DemoSettings&) = delete;

    // Registers the renderer, engine and log settings, loads every layer (the user's file in the standard place, the environment, the command line) and
    // prints what went wrong while loading to stderr. `usage` is the demo's own usage text for --help. Returns false (after printing why) if the command
    // line has an error of its own, such as an unknown --set key; the caller then exits with the usage code.
    bool Begin(const char* app_name, const char* usage, int argc, char** argv);

    // The settings.
    sset::Settings& Get() { return *settings_; }
    const sset::Registry& GetRegistry() const { return registry_; }

    // The arguments the settings system does not own, as an argc/argv pair for the demo's own parser (argv[0] is the program name).
    int RestCount() const { return rest_pointers_.Empty() ? 0 : static_cast<int>(rest_pointers_.Size()) - 1; } // without the null that ends the array
    char** RestArgs() { return rest_pointers_.Data(); }

    // What the demos' own flags mean as settings: the GPU (an empty string for "the best one"), whether presenting is immediate (--no-vsync), whether the
    // validation layer is on (--validation), and the display backend ("auto", "wayland" or "x11").
    struct Effective {
        char gpu[128] = {};
        bool immediate = false;
        bool validation = false;
        char window_backend[16] = "auto";
    };
    Effective ReadEffective();

    // Gives a setting the value of an old flag of the program (--no-vsync, --validation) as if it had been given on the command line: it outranks
    // files and the environment, is shown with the flag as its source, is not written to the user's file, and yields to an explicit --set.
    void SetLegacy(const char* key, const char* text, const char* flag);

    // Fills the parts of a device description that are settings: the validation layer, the GPU, the pipeline cache, the frames in flight. The
    // strings it points to belong to this object.
    void FillDeviceCreateInfo(::engine_placeholder::rhi::DeviceCreateInfo* info);

    // Handles --help, --list-settings and --save-settings (after the demo has put its own flags into the settings). Returns the exit code if one of them
    // was given, or -1 to carry on.
    int Finish();

private:
    char adapter_[128] = {};
    char cache_dir_[256] = {};
    sset::Registry registry_;
    UniquePtr<sset::Settings> settings_;
    String<HeapAllocator> usage_ = String<HeapAllocator>(HeapAllocator::Default());
    String<HeapAllocator> program_ = String<HeapAllocator>(HeapAllocator::Default());
    Vector<String<HeapAllocator>> rest_storage_;
    Vector<char*> rest_pointers_;
};

}  // namespace demo
