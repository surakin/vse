# Tutorial: Lighting a World with a Sky

**Goal:** Give a scene a sky, from a flat colour to a gradient with a sun to a cube map, and see how the sky also lights what is under it.

**You need:** a scene that draws, for example the one of [From a Scene to a Lit Frame with the Renderer](hello-renderer.md). The program `render_features_demo` shows the result: `render_features_demo --screenshot sky.ppm --sky cube` draws a landscape under a cube map sky and `--sky procedural` draws it under the gradient. The guide: [The renderer](../guide/renderer.md) ("The sky") and [Render settings](../guide/render-settings.md).

## The idea

The sky is what shows where nothing else was drawn, and it is also a light: the lit surfaces take their ambient light from it and reflect it. The sky is a plain value, `renderer::SkySettings`, that you hand to the frame; the frame draws it after the lighting pass, only where the depth buffer still holds the far plane, so it never matters in which order the rest was drawn. Three modes:

- `Color`: one flat colour (the default). Cheap, and right for a menu or a studio.
- `Procedural`: a gradient from a ground colour through a horizon colour to a zenith colour, and a sun disc.
- `Cube`: a cube map you made, six square images of the sky seen from the middle of a box. Use it when the gradient is not enough: clouds, a city, a sunset.

## Steps

1. **Start with a colour.** `SceneFrame::SetEnvironment(exposure, background)` sets the flat sky and the exposure of the tone map. Everything else here is `SetSky`.
2. **Ask the gradient what it looks like.** `renderer::SkyColor(settings, direction)` is the same function as the sky's shader, on the CPU: use it to check a setting, or to colour something to match the sky. The sample below prints the colour straight up, at the horizon, straight down and toward the sun.
3. **Move the sun.** `sun_direction` points toward the sun (any length but zero); `sun_size` is its angular radius in radians, `sun_color` its radiance (a value above 1 is what makes it glow after tone mapping). Black hides it. The directional light that lights the surfaces is a light of your scene: give it the same direction, so the shadows and the sun agree.
4. **Make the cube.** A cube texture is six faces of the same size; face order is +x, -x, +y, -y, +z, -z. `GpuTexture::CreateFromSharedLevels` makes it from bytes that stay alive as the source for a device reset. `render_features_demo` fills its faces by calling `SkyColor` for each texel, which is a good way to start: it gives a cube that matches the gradient, which you then replace with real pictures. `SkySettings::rotation` turns the cube about +Y; `tint` and `exposure` scale it.
5. **Switch at run time.** `SceneFrame::SetSky(settings, sky_reflection)` takes effect the next frame, with no restart: the demo's `C` key flips between the two modes this way. A cube sky whose texture is missing, not a cube, or on a device without cube textures falls back to the gradient.
6. **Let the sky light the scene.** `ambient_intensity` above 0 makes the sky give the lit surfaces their ambient light (it replaces the flat ambient lights; 0 keeps them), and `sky_reflection` says how much of the sky shiny surfaces reflect. A procedural sky makes a small cube of itself for this; a cube sky is used as it is.

```cpp compile standalone run
#include <cstdio>

#include "renderer/sky.h"

using namespace vse;
using namespace vse::renderer;

static void Show(const char* name, const SkySettings& sky, const Vec3& direction)
{
    const Vec3 c = SkyColor(sky, direction);
    std::printf("%-8s %.2f %.2f %.2f\n", name, c.x, c.y, c.z);
}

int main()
{
    SkySettings sky;
    sky.mode = SkyMode::Procedural;
    sky.sun_direction = Vec3(0.0f, 0.2f, 1.0f);  // low, toward +z
    if (!SkySettingsValid(sky))
    {
        return 1;
    }
    Show("up", sky, Vec3(0.0f, 1.0f, 0.0f));
    Show("horizon", sky, Vec3(1.0f, 0.0f, 0.0f));
    Show("down", sky, Vec3(0.0f, -1.0f, 0.0f));
    Show("sun", sky, Vec3(0.0f, 0.2f, 1.0f));

    // A cube sky turned a quarter turn: the world direction +x reads the cube along its own +z.
    sky.rotation = 1.5707964f;
    const Vec3 d = SkyCubeDirection(sky, Vec3(1.0f, 0.0f, 0.0f));
    std::printf("cube     along z: %.2f, sideways: %.2f\n", d.z, d.x * d.x + d.y * d.y);  // the squares hide a -0
    return 0;
}
```

```output
up       0.18 0.35 0.75
horizon  0.65 0.75 0.90
down     0.25 0.24 0.22
sun      8.60 7.91 6.88
cube     along z: 1.00, sideways: 0.00
```

## Where to go next

- [Making a Landscape](making-a-landscape.md): what the sky shines on.
- [A Forest in One Draw](a-forest-in-one-draw.md): the trees that stand in it.
