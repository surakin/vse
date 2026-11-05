// renderer_demo_mint: writes the demo's texture files. Run as `renderer_demo_mint <output directory>`; the committed files in
// assets/ are its output, and a test checks that they still are (byte for byte).

#include <cstdio>

#include "demo_assets.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output directory>\n", argv[0]);
        return 2;
    }
    using namespace renderer_demo;
    u32 count = 0;
    const DemoTexture* textures = DemoTextures(&count);
    for (u32 i = 0; i < count; ++i) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/%s", argv[1], textures[i].file);
        const demo::RtexImage image = demo::BuildRtex(textures[i].make(), textures[i].format);
        if (!demo::WriteRtex(path, image)) {
            std::fprintf(stderr, "renderer_demo_mint: could not write %s\n", path);
            return 1;
        }
        std::printf("wrote %s\n", path);
    }
    return 0;
}
