#pragma once

// The renderer demo's procedural textures. The committed files under assets/ are produced by `renderer_demo_mint` from
// these functions (see README.md); the demo and the tests also call them directly when a file is missing, so a checkout
// without the files still runs. Everything here is deterministic: the same code gives the same bytes.

#include "textures.h"

namespace renderer_demo
{

using namespace vse;

// A name and the pixels of one demo texture.
struct DemoTexture
{
    const char* file;        // the file name under assets/
    rhi::Format format;      // how the file is stored and sampled
    demo::Pixels (*make)();  // builds the level-0 pixels
};

// The demo textures: checker.rtex (sRGB albedo), stripes.rtex (sRGB albedo), normal.rtex (UNORM tangent-space normals).
// Returns the table and sets `count`.
const DemoTexture* DemoTextures(u32* count);

// 256x256 checkerboard, 32-texel cells, warm yellow and deep blue.
demo::Pixels MakeCheckerPixels();
// 256x256 diagonal stripes, red and cream, anti-aliased by construction (a smooth ramp, not a step).
demo::Pixels MakeStripePixels();
// 256x256 tangent-space normal map of a 4x4 grid of round bumps: (x, y, z) mapped to (0.5 x + 0.5, ...), z toward the
// viewer.
demo::Pixels MakeBumpNormalPixels();

}  // namespace renderer_demo
