#include "ui/text_words.h"

#include "string/utf8_codec.h"

namespace engine_placeholder::ui
{

namespace
{

bool InRange(u32 c, u32 lo, u32 hi)
{
    return c >= lo && c <= hi;
}

bool IsSpaceCodepoint(u32 c)
{
    return c == ' ' || InRange(c, 0x09, 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 || InRange(c, 0x2000, 0x200B) ||
           InRange(c, 0x2028, 0x2029) || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool IsPunctCodepoint(u32 c)
{
    if (c < 0x80)
    {
        return (InRange(c, 0x21, 0x2F) || InRange(c, 0x3A, 0x40) || InRange(c, 0x5B, 0x5E) || c == 0x60 ||
                InRange(c, 0x7B, 0x7E));
    }
    if (InRange(c, 0xA1, 0xBF))
    {
        return c != 0xAA && c != 0xB5 && c != 0xBA;
    }
    return c == 0xD7 || c == 0xF7 || InRange(c, 0x2010, 0x2027) || InRange(c, 0x2030, 0x205E) ||
           InRange(c, 0x3001, 0x3003) || InRange(c, 0x3008, 0x3011) || InRange(c, 0xFF01, 0xFF0F) ||
           InRange(c, 0xFF1A, 0xFF20);
}

u32 StartBefore(StringView text, u32 offset)
{
    u32 o = offset;
    if (o == 0)
    {
        return 0;
    }
    --o;
    while (o > 0 && (static_cast<u8>(text[o]) & 0xC0) == 0x80)
    {
        --o;
    }
    return o;
}

CharClass ClassAtOffset(StringView text, u32 offset)
{
    const DecodedCodepoint d = DecodeUtf8At(text.Data(), text.Length(), offset);
    return ClassOf(d.codepoint);
}

u32 StepForward(StringView text, u32 offset)
{
    const DecodedCodepoint d = DecodeUtf8At(text.Data(), text.Length(), offset);
    return offset + static_cast<u32>(d.byte_length);
}

u32 Clamp(StringView text, u32 offset)
{
    return offset > text.Length() ? static_cast<u32>(text.Length()) : offset;
}

}  // namespace

CharClass ClassOf(u32 codepoint)
{
    if (IsSpaceCodepoint(codepoint))
    {
        return CharClass::Space;
    }
    if (IsPunctCodepoint(codepoint))
    {
        return CharClass::Punct;
    }
    return CharClass::Word;
}

u32 WordStart(StringView text, u32 offset)
{
    u32 o = Clamp(text, offset);
    while (o > 0 && ClassAtOffset(text, StartBefore(text, o)) == CharClass::Space)
    {
        o = StartBefore(text, o);
    }
    if (o == 0)
    {
        return 0;
    }
    const CharClass cls = ClassAtOffset(text, StartBefore(text, o));
    while (o > 0 && ClassAtOffset(text, StartBefore(text, o)) == cls)
    {
        o = StartBefore(text, o);
    }
    return o;
}

u32 WordEnd(StringView text, u32 offset)
{
    const u32 len = static_cast<u32>(text.Length());
    u32 o = Clamp(text, offset);
    while (o < len && ClassAtOffset(text, o) == CharClass::Space)
    {
        o = StepForward(text, o);
    }
    if (o >= len)
    {
        return len;
    }
    const CharClass cls = ClassAtOffset(text, o);
    while (o < len && ClassAtOffset(text, o) == cls)
    {
        o = StepForward(text, o);
    }
    return o;
}

void WordRangeAt(StringView text, u32 offset, u32* begin, u32* end)
{
    const u32 len = static_cast<u32>(text.Length());
    *begin = 0;
    *end = 0;
    if (len == 0)
    {
        return;
    }
    u32 o = Clamp(text, offset);
    const CharClass cls = o < len ? ClassAtOffset(text, o) : ClassAtOffset(text, StartBefore(text, o));
    u32 b = o < len ? o : StartBefore(text, o);
    u32 e = o < len ? StepForward(text, o) : o;
    while (b > 0 && ClassAtOffset(text, StartBefore(text, b)) == cls)
    {
        b = StartBefore(text, b);
    }
    while (e < len && ClassAtOffset(text, e) == cls)
    {
        e = StepForward(text, e);
    }
    *begin = b;
    *end = e;
}

}  // namespace engine_placeholder::ui
