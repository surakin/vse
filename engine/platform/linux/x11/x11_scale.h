#pragma once

//! The X11 display scale: how the desktop's `Xft.dpi` resource becomes the window's content scale.

#include "string_view.h"
#include "types.h"

namespace engine_placeholder::os::x11
{

/// The smallest content scale: below 1 the window would draw smaller than 100%.
inline constexpr f32 kMinContentScale = 1.0f;
/// The largest content scale: anything above is a misconfigured resource.
inline constexpr f32 kMaxContentScale = 4.0f;

/// Reads the `Xft.dpi` resource out of the text of the root window's `RESOURCE_MANAGER` property (the X resource
/// database, one `name:<blanks>value` per line) and returns it over 96 as a content scale.
///
/// This is the value GTK, Qt and Chromium use for X11: the X server's own screen size and dpi (`xdpyinfo`) usually say
/// 96 whatever the desktop is set to. Both `Xft.dpi` and the loose binding `Xft*dpi` are read. As in an X resource
/// database, a later line replaces an earlier one with the same name (an appended `xrdb -merge` wins), and the tight
/// `Xft.dpi` beats the loose `Xft*dpi` whatever the order. A missing, malformed or non-positive final value, and
/// anything below 96 dpi, gives 1; anything above 4 times 96 gives 4.
/// - Param resources: the resource database text; it is only read during the call.
/// - Returns: the scale, from [[os::x11::kMinContentScale]] to [[os::x11::kMaxContentScale]].
/// - Fails: never; a text with no usable value gives 1.
inline f32 ContentScaleFromResources(StringView resources)
{
    // The scale of the last tight (`Xft.dpi`) and the last loose (`Xft*dpi`) line; -1 while there is none.
    f32 tight = -1.0f;
    f32 loose = -1.0f;
    usize pos = 0;
    while (pos < resources.Length())
    {
        usize end = pos;
        while (end < resources.Length() && resources[end] != '\n')
        {
            ++end;
        }
        usize i = pos;
        pos = end + 1;
        while (i < end && (resources[i] == ' ' || resources[i] == '\t'))
        {
            ++i;
        }
        const StringView line = resources.Substring(i, end - i);
        const bool is_tight = line.StartsWith("Xft.dpi");
        if (!is_tight && !line.StartsWith("Xft*dpi"))
        {
            continue;
        }
        usize k = 7;
        while (k < line.Length() && (line[k] == ' ' || line[k] == '\t'))
        {
            ++k;
        }
        if (k >= line.Length() || line[k] != ':')
        {
            continue;  // `Xft.dpix:` and the like are other resources
        }
        ++k;
        while (k < line.Length() && (line[k] == ' ' || line[k] == '\t'))
        {
            ++k;
        }
        f64 value = 0.0;
        bool digits = false;
        while (k < line.Length() && line[k] >= '0' && line[k] <= '9')
        {
            value = value * 10.0 + static_cast<f64>(line[k] - '0');
            digits = true;
            ++k;
        }
        if (k < line.Length() && line[k] == '.')
        {
            ++k;
            f64 place = 0.1;
            while (k < line.Length() && line[k] >= '0' && line[k] <= '9')
            {
                value += static_cast<f64>(line[k] - '0') * place;
                place *= 0.1;
                digits = true;
                ++k;
            }
        }
        while (k < line.Length() && (line[k] == ' ' || line[k] == '\t' || line[k] == '\r'))
        {
            ++k;
        }
        f32 scale = kMinContentScale;  // a line that is not a number replaces an earlier one with the default
        if (digits && k == line.Length() && value > 0.0)
        {
            scale = static_cast<f32>(value / 96.0);
            scale = scale < kMinContentScale ? kMinContentScale : (scale > kMaxContentScale ? kMaxContentScale : scale);
        }
        (is_tight ? tight : loose) = scale;
    }
    return tight >= 0.0f ? tight : (loose >= 0.0f ? loose : kMinContentScale);
}

}  // namespace engine_placeholder::os::x11
