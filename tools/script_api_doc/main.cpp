// script_api_doc: Phase 27 Day 450. Renders docs/reference/script-api.md from the script binding registry.
//
//   script_api_doc --write [--file PATH]    writes the page
//   script_api_doc --check [--file PATH]    fails when the page on disk is not what the registry renders
//   script_api_doc --print                  prints the page
//
// The default path is docs/reference/script-api.md under the current directory (ctest runs it from the source root).
#include "io/file.h"
#include "io/path.h"
#include "os/command_line.h"
#include "os/console.h"
#include "script/bindings.h"
#include "script_media/audio_bindings.h"
#include "script_media/input_bindings.h"
#include "script_physics/physics_bindings.h"

using namespace vse;

namespace
{

constexpr os::CommandLineOption kOptions[] = {{"write", false}, {"check", false}, {"print", false}, {"file", true}};
using Line = os::CommandLine<HeapAllocator>;

int Usage()
{
    os::PrintError("usage: script_api_doc (--write | --check | --print) [--file PATH]\n");
    return 64;
}

}  // namespace

int main(int argc, char** argv)
{
    const Line line(HeapAllocator::Default(), argc, argv, Span<const os::CommandLineOption>(kOptions, 4));
    if (!line.Ok())
    {
        os::PrintError("script_api_doc: %s\n", line.Error().Data());
        return Usage();
    }
    const bool write = line.HasFlag("write");
    const bool check = line.HasFlag("check");
    const bool print = line.HasFlag("print");
    if ((write ? 1 : 0) + (check ? 1 : 0) + (print ? 1 : 0) != 1)
    {
        return Usage();
    }
    script::BindingRegistry registry;
    if (!script::RegisterStandardBindings(&registry))
    {
        os::PrintError("script_api_doc: the standard bindings do not register\n");
        return 2;
    }
    if (!script_physics::RegisterPhysicsBindings(&registry))
    {
        os::PrintError("script_api_doc: the physics bindings do not register\n");
        return 2;
    }
    if (!script_media::RegisterAudioBindings(&registry) || !script_media::RegisterInputBindings(&registry))
    {
        os::PrintError("script_api_doc: the audio and input bindings do not register\n");
        return 2;
    }
    const String<HeapAllocator> page = registry.RenderMarkdown();
    if (print)
    {
        os::Print("%s", page.CStr());
        return 0;
    }
    io::Path path;
    if (!io::Path::Parse(StringView(line.OptionOr("file", "docs/reference/script-api.md")), io::NativePathStyle(),
                         &path))
    {
        os::PrintError("script_api_doc: bad path\n");
        return 2;
    }
    if (write)
    {
        if (!io::WriteFileAtomic(path, page.CStr(), page.Length(), 0644).Ok())
        {
            os::PrintError("script_api_doc: cannot write the page\n");
            return 2;
        }
        return 0;
    }
    String<HeapAllocator> current(HeapAllocator::Default());
    if (!io::ReadTextFile(path, &current).Ok())
    {
        os::PrintError("script_api_doc: cannot read docs/reference/script-api.md (run script_api_doc --write)\n");
        return 1;
    }
    if (current.View() != page.View())
    {
        os::PrintError("script_api_doc: docs/reference/script-api.md is out of date (run script_api_doc --write)\n");
        return 1;
    }
    return 0;
}
