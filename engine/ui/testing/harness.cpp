#include "ui/testing/harness.h"

#include "lang/scalar.h"

#include "io/path.h"
#include "log/assert.h"

namespace vse::ui
{

Harness::Harness(f32 width, f32 height, f32 scale, StringView font_file) : surface_(width, height, scale)
{
    if (library_.Initialize().Ok())
    {
        font_ok_ = text::Font::OpenFile(library_, io::Path(font_file), text::FontOptions{}, &font_).Ok();
    }
    surface_.SetFontProvider(this);
}

text::Font* Harness::Resolve(u32)
{
    return font_ok_ ? &font_ : nullptr;
}

bool Harness::Advance(f32 dt)
{
    return surface_.Frame(dt, &list_, &atlas_);
}

void Harness::PointerMove(f32 x, f32 y)
{
    Event e;
    e.type = EventType::PointerMove;
    e.position = Point{x, y};
    surface_.Post(e);
    Advance(0);
}

void Harness::PointerDown(f32 x, f32 y, u32 button)
{
    Event e;
    e.type = EventType::PointerDown;
    e.position = Point{x, y};
    e.button = button;
    surface_.Post(e);
    Advance(0);
}

void Harness::PointerUp(f32 x, f32 y, u32 button)
{
    Event e;
    e.type = EventType::PointerUp;
    e.position = Point{x, y};
    e.button = button;
    surface_.Post(e);
    Advance(0);
}

void Harness::Click(f32 x, f32 y)
{
    PointerMove(x, y);
    PointerDown(x, y);
    PointerUp(x, y);
}

void Harness::Wheel(f32 x, f32 y, f32 dx, f32 dy)
{
    Event e;
    e.type = EventType::Wheel;
    e.position = Point{x, y};
    e.wheel_x = dx;
    e.wheel_y = dy;
    surface_.Post(e);
    Advance(0);
}

void Harness::Key(ui::Key key, bool release, u32 modifiers)
{
    Event e;
    e.type = EventType::KeyDown;
    e.key = key;
    e.modifiers = modifiers;
    surface_.Post(e);
    if (release)
    {
        e.type = EventType::KeyUp;
        surface_.Post(e);
    }
    Advance(0);
}

void Harness::TypeText(StringView utf8)
{
    usize i = 0;
    while (i < utf8.Length())
    {
        const u8 lead = static_cast<u8>(utf8[i]);
        usize len = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
        if (i + len > utf8.Length())
        {
            len = utf8.Length() - i;
        }
        Event e;
        e.type = EventType::Text;
        e.text_length = static_cast<u8>(len);
        for (usize k = 0; k < len && k < 4; ++k)
        {
            e.text[k] = utf8[i + k];
        }
        surface_.Post(e);
        i += len;
    }
    Advance(0);
}

Widget* Harness::Find(StringView name)
{
    if (Widget* w = surface_.Root()->FindByName(name))
    {
        return w;
    }
    return surface_.PopupLayer()->FindByName(name);
}

Rect Harness::RectOf(StringView name)
{
    Widget* w = Find(name);
    ENGINE_ASSERT(w != nullptr, "Harness::RectOf: no widget has that name");
    return w->Bounds();
}

void Harness::Render(RasterImage* out, u32 clear_abgr)
{
    const u32 w = static_cast<u32>(Ceil(surface_.Width() * surface_.Scale()));
    const u32 h = static_cast<u32>(Ceil(surface_.Height() * surface_.Scale()));
    RasterizeDrawList(list_, &atlas_, w, h, clear_abgr, out);
}

}  // namespace vse::ui
