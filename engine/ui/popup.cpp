#include "ui/popup.h"

#include "string/string.h"

namespace engine_placeholder::ui
{

namespace
{

f32 Clamp(f32 v, f32 lo, f32 hi)
{
    if (v > hi)
    {
        v = hi;
    }
    if (v < lo)
    {
        v = lo;
    }
    return v;
}

}  // namespace

Rect PlacePopup(Size size, const Rect& anchor, Placement placement, const Rect& bounds)
{
    Rect r;
    r.w = size.w;
    r.h = size.h;
    const bool vertical = placement == Placement::Below || placement == Placement::Above;
    if (vertical)
    {
        const f32 below = anchor.Bottom();
        const f32 above = anchor.y - size.h;
        const bool fits_below = below + size.h <= bounds.Bottom();
        const bool fits_above = above >= bounds.y;
        const bool want_below = placement == Placement::Below;
        const bool use_below = want_below ? (fits_below || !fits_above) : (!fits_above && fits_below);
        r.y = use_below ? below : above;
        r.x = Clamp(anchor.x, bounds.x, bounds.Right() - size.w);
    }
    else
    {
        const f32 right = anchor.Right();
        const f32 left = anchor.x - size.w;
        const bool fits_right = right + size.w <= bounds.Right();
        const bool fits_left = left >= bounds.x;
        const bool want_right = placement == Placement::Right;
        const bool use_right = want_right ? (fits_right || !fits_left) : (!fits_left && fits_right);
        r.x = use_right ? right : left;
        r.y = Clamp(anchor.y, bounds.y, bounds.Bottom() - size.h);
    }
    // Neither side had room: slide it over the anchor so that its far edge is inside the bounds (its last items stay
    // reachable).
    if (r.x + r.w > bounds.Right())
    {
        r.x = bounds.Right() - r.w;
    }
    if (r.y + r.h > bounds.Bottom())
    {
        r.y = bounds.Bottom() - r.h;
    }
    // Larger than the bounds: start inside them.
    if (r.x < bounds.x)
    {
        r.x = bounds.x;
    }
    if (r.y < bounds.y)
    {
        r.y = bounds.y;
    }
    return r;
}

String<HeapAllocator> ChordText(const KeyChord& chord)
{
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    if ((chord.modifiers & os::kModCtrl) != 0)
    {
        sb.Append(StringView("Ctrl+"));
    }
    if ((chord.modifiers & os::kModShift) != 0)
    {
        sb.Append(StringView("Shift+"));
    }
    if ((chord.modifiers & os::kModAlt) != 0)
    {
        sb.Append(StringView("Alt+"));
    }
    const u32 k = static_cast<u32>(chord.key);
    const u32 a = static_cast<u32>(Key::A);
    const u32 f1 = static_cast<u32>(Key::F1);
    const u32 n0 = static_cast<u32>(Key::Num0);
    if (k >= a && k <= static_cast<u32>(Key::Z))
    {
        sb.Append(static_cast<char>('A' + (k - a)));
    }
    else if (k >= f1 && k <= static_cast<u32>(Key::F12))
    {
        sb.Append(StringView("F"));
        sb.AppendInt(static_cast<i64>(k - f1 + 1));
    }
    else if (k >= n0 && k <= static_cast<u32>(Key::Num9))
    {
        sb.Append(static_cast<char>('0' + (k - n0)));
    }
    else
    {
        switch (chord.key)
        {
        case Key::Enter: sb.Append(StringView("Enter")); break;
        case Key::Escape: sb.Append(StringView("Esc")); break;
        case Key::Tab: sb.Append(StringView("Tab")); break;
        case Key::Space: sb.Append(StringView("Space")); break;
        case Key::Delete: sb.Append(StringView("Del")); break;
        case Key::Insert: sb.Append(StringView("Ins")); break;
        case Key::Backspace: sb.Append(StringView("Backspace")); break;
        case Key::Home: sb.Append(StringView("Home")); break;
        case Key::End: sb.Append(StringView("End")); break;
        case Key::PageUp: sb.Append(StringView("PgUp")); break;
        case Key::PageDown: sb.Append(StringView("PgDn")); break;
        case Key::Left: sb.Append(StringView("Left")); break;
        case Key::Right: sb.Append(StringView("Right")); break;
        case Key::Up: sb.Append(StringView("Up")); break;
        case Key::Down: sb.Append(StringView("Down")); break;
        default: sb.Append(StringView("?")); break;
        }
    }
    String<HeapAllocator> out(HeapAllocator::Default(), StringView());
    out.Append(sb.View());
    return out;
}

}  // namespace engine_placeholder::ui
