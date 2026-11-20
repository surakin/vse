// renderer_demo_mint: writes the demo's texture files. Run as `renderer_demo_mint <output directory>`; the committed
// files in assets/ are its output, and a test checks that they still are (byte for byte).

#include "os/command_line.h"
#include "log/output.h"
#include "os/console.h"

#include "io/file.h"
#include "io/file_system.h"
#include "demo_assets.h"

int main(int argc, char** argv)
{
    const vse::os::CommandLine<vse::HeapAllocator> line(vse::HeapAllocator::Default(), argc, argv);
    if (line.ArgCount() != 1)
    {
        vse::out::PrintError("usage: %s <output directory>\n", line.ProgramName().Data());
        return 2;
    }
    using namespace renderer_demo;
    u32 count = 0;
    const DemoTexture* textures = DemoTextures(&count);
    for (u32 i = 0; i < count; ++i)
    {
        io::Path directory;
        if (!io::Path::Parse(line.Arg(0), io::NativePathStyle(), &directory))
        {
            vse::out::PrintError("renderer_demo_mint: the output directory is not a usable path\n");
            return 1;
        }
        const io::Path path = directory.Join(StringView(textures[i].file));
        const demo::RtexImage image = demo::BuildRtex(textures[i].make(), textures[i].format);
        if (const io::Status status = demo::WriteRtex(path, image); status.Failed())
        {
            vse::out::PrintError("renderer_demo_mint: could not write %.*s: %s\n",
                                 static_cast<int>(path.View().Length()), path.View().Data(),
                                 io::ErrorName(status.error));
            return 1;
        }
        vse::out::Print("wrote %.*s\n", static_cast<int>(path.View().Length()), path.View().Data());
    }
    return 0;
}
