#pragma once

//! Line-oriented source text helpers shared by the minters: tokens split on blanks, `#` comments, exact number parsing.

#include "cstr/number.h"
#include "lang/scalar.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset::srctext
{

/// The fields of one source line.
struct LineTokens
{
    StringView tokens[24];
    usize count = 0;
    bool overflow = false;
};

inline bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/// Splits a line at blanks, dropping a `#` comment; more than 24 fields set `overflow`.
inline LineTokens Tokenize(StringView line)
{
    LineTokens out;
    usize i = 0;
    while (i < line.Length())
    {
        while (i < line.Length() && IsBlank(line[i]))
        {
            ++i;
        }
        if (i >= line.Length() || line[i] == '#')
        {
            break;
        }
        const usize start = i;
        while (i < line.Length() && !IsBlank(line[i]) && line[i] != '#')
        {
            ++i;
        }
        if (out.count == 24)
        {
            out.overflow = true;
            break;
        }
        out.tokens[out.count++] = line.Substring(start, i - start);
    }
    return out;
}

/// Reads a finite number whose magnitude fits an `f32`.
inline bool ParseFloat(StringView token, f32* out)
{
    const ParseDoubleResult r = ParseDoubleExact(token.Data(), token.Data() + token.Length());
    if (!r.ok || r.end != token.Data() + token.Length() || !IsFinite(r.value) || Abs(r.value) > 3.0e38)
    {
        return false;
    }
    *out = static_cast<f32>(r.value);
    return true;
}

/// Reads an unsigned decimal integer below 2^32.
inline bool ParseUnsigned(StringView token, u32* out)
{
    if (token.Length() == 0 || token.Length() > 10)
    {
        return false;
    }
    u64 v = 0;
    for (usize i = 0; i < token.Length(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
        v = v * 10 + static_cast<u64>(token[i] - '0');
    }
    if (v > 0xffffffffull)
    {
        return false;
    }
    *out = static_cast<u32>(v);
    return true;
}

}  // namespace engine_placeholder::asset::srctext
