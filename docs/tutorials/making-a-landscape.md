# Tutorial: Making a Landscape

**Goal:** Turn a grid of heights into a terrain that is drawn, that the physics world can stand on and that answers "how high is the ground here?" the same way for both.

**You need:** nothing but the engine's libraries for the sample; to see the result, `render_features_demo --screenshot land.ppm` draws a 2048 x 2048 terrain with a character and bodies on it. The guide: [Terrain](../guide/terrain.md), with [Physics](../guide/physics.md) for the collider.

## The idea

A terrain is one data set read three ways. The data is a height map: a grid of 16-bit samples, a range in metres that the samples span (sample 0 is the lowest height, 65535 the highest) and the distance between samples. The renderer makes a texture of it and a quadtree of tiles over it; the physics module makes a height-field collider from the same samples; your code asks it for heights. Because all three read the same numbers, a ball rolls on exactly the surface you see.

## Steps

1. **Make the samples.** Any function of (x, z) that gives a number from 0 to 65535: noise, a scan, a painting. `render_features_demo` uses value noise from integers, so every machine makes the same hills. The map is 2 to 8192 samples on a side.
2. **Make the height map blob.** `asset::BuildHeightMapBlob` writes the samples with their mip levels and a pyramid of block minima and maxima (the renderer uses the pyramid to bound each tile in height). In a project, `asset_mint mint-heightmap` makes the same blob offline and the resource manager loads it; the blob is checked on load, so a damaged file is refused instead of drawn.
3. **Make the renderer's source.** `asset::MakeTerrainSource` makes the height texture and the tile quadtree from the blob, once. Many terrain objects can share one source, and a new source replaces an old one when the file is reloaded.
4. **Choose what to draw.** Each frame `TerrainTiles::Select` walks the quadtree with the camera and the view frustum and returns the tiles to draw: fine tiles near the camera, coarser ones farther away (a tile of level L covers 2^L tiles across and is drawn with the same 32 x 32 grid). The sample asks for the tiles of a camera low over a 1024-sample map.
5. **Place and dress it.** `RenderScene::AddTerrain` takes a `TerrainObject`: the source, a material and a world matrix (a translation and a turn about y; the terrain's own space starts at its first sample). The material has up to four layers (a tiling in metres, a tint, a roughness) taken from a texture array, a splat map that says which layer where, a rule that gives steep faces their own layer, and a distance beyond which the detail fades into the layer's average colour.
6. **Make it solid.** `physics::HeightField::CreateFromSamples` takes the same samples and range. The field's space is centred on its body, so the body sits half the map from the origin to line up with the drawn terrain. `HeightAtMap(x, z)` answers in the terrain's own coordinates and is the same function as the asset's `HeightAt`; the sample checks that they agree.
7. **Stand on it.** Add the field to a physics world as a static body's collider; bodies, a character and ray casts (`physics::TerrainRayCast` hits only terrain) work as with any shape. A body below the surface is lifted out through it: there are no overhangs, by rule.

```cpp compile standalone run
#include <cstdio>

#include "asset/gpu_loaders.h"
#include "asset/height_map.h"
#include "containers/shared_bytes.h"
#include "containers/vector.h"
#include "physics/height_field.h"
#include "renderer/camera.h"
#include "renderer/terrain_source.h"
#include "rhi/null/null_device.h"

using namespace vse;

int main()
{
    // 1: a hill, in samples (0 to 65535) over a range of 0 to 40 metres.
    const u32 side = 1024;
    Vector<u16> samples;
    for (u32 z = 0; z < side; ++z)
    {
        for (u32 x = 0; x < side; ++x)
        {
            const i32 dx = static_cast<i32>(x) - 512;
            const i32 dz = static_cast<i32>(z) - 512;
            const i32 d = (dx * dx + dz * dz) / 8;
            samples.Push(static_cast<u16>(d > 60000 ? 4000 : 64000 - d));
        }
    }

    // 2: the blob, and a view of it.
    asset::TypeRegistry types;
    asset::HeightMapHeader header;
    header.width = side;
    header.height = side;
    header.range_min = 0.0f;
    header.range_max = 40.0f;
    header.cell_size = 1.0f;
    Vector<byte> blob;
    const ByteSpan bytes(reinterpret_cast<const byte*>(samples.Data()), samples.Size() * 2);
    if (!asset::BuildHeightMapBlob(types, asset::ResourceId::FromValue(5), header, bytes, &blob).Ok())
    {
        return 1;
    }
    asset::HeightMapView view;
    if (!view.Open(CopyShared(blob), types).Ok())
    {
        return 1;
    }

    // 3 and 4: the renderer's source, and the tiles for a camera 30 metres above the south edge looking north.
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::TerrainSourcePtr source;
    if (!asset::MakeTerrainSource(device.Get(), view, nullptr, &source))
    {
        return 1;
    }
    const renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(512.0f, 30.0f, 0.0f), Vec3(512.0f, 10.0f, 400.0f), Vec3(0, 1, 0));
    const Frustum frustum = Frustum::FromViewProjection(camera.ViewProjection());
    Vector<renderer::TerrainTile> tiles;
    source->tiles.Select(camera.position, &frustum, renderer::TerrainSelectSettings(), &tiles);
    u32 finest = 0;
    for (const renderer::TerrainTile& t : tiles)
    {
        finest += t.level == 0 ? 1 : 0;
    }
    std::printf("%u levels, %u tiles chosen, %u of them the finest\n", source->tiles.LevelCount(),
                static_cast<u32>(tiles.Size()), finest);

    // 6: the collider from the same samples, and the two ways of asking for a height.
    physics::Validation v = physics::Validation::Ok;
    const physics::HeightFieldConstPtr field = physics::HeightField::CreateFromSamples(
        Span<const u16>(samples.Data(), samples.Size()), side, side, 0.0f, 40.0f, 1.0f, &v);
    if (!field)
    {
        return 1;
    }
    const f32 from_asset = view.HeightAt(512.0f, 512.0f);
    const f32 from_field = field->HeightAtMap(512.0f, 512.0f);
    std::printf("the top of the hill: %.2f m from the asset, %.2f m from the collider\n", from_asset, from_field);
    return from_asset == from_field ? 0 : 1;
}
```

```output
6 levels, 70 tiles chosen, 26 of them the finest
the top of the hill: 39.06 m from the asset, 39.06 m from the collider
```

## Where to go next

- [Lighting a World with a Sky](lighting-a-world-with-a-sky.md) for what the terrain is lit by.
- [A Forest in One Draw](a-forest-in-one-draw.md): placing things on it with ray casts.
