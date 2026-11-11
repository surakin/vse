#include "cstr/number.h"

#include "build_defs.h"

#if !ENGINE_PLATFORM_WINDOWS

#include <cstdlib>
#include <locale.h>
#if ENGINE_PLATFORM_MACOS || ENGINE_PLATFORM_IOS
#include <xlocale.h>  // strtod_l
#endif

#include "cstr/cstr.h"
#include "lang/limits.h"

// detail::ParseDoubleWithoutFromChars (Phase 26 Day 300): std::from_chars for double is a deleted function in Apple's
// libc++. The grammar is read here, so the end position never depends on strtod's wider one (leading space, '+', hex,
// the locale's decimal point); only the conversion of a normalized copy is left to strtod_l, in the C locale.
namespace engine_placeholder::detail
{

namespace
{

// Significant digits kept for strtod_l. Deciding the correctly rounded double needs at most 767 of them; the rest is
// folded into one sticky nonzero digit, so a number that lies exactly halfway between two doubles is told apart from
// one just above it.
constexpr i64 kKeptDigits = 780;
// Decimal exponents (of 0.ddd) beyond which every nonzero number is out of the range of f64: the largest double is
// about 1.8e308 and the smallest subnormal about 4.9e-324. Clamping the exponent at these bounds keeps the arithmetic
// far from overflow.
constexpr i64 kExponentAbove = 330;
constexpr i64 kExponentBelow = -360;
// The written exponent stops growing here. It must stay far above anything `point` (the digits before the point, or
// minus the leading zeros after it: bounded by the input's length) can cancel, or a long number with a long exponent
// would come out wrong but in range; 10^15 also keeps point + exponent far from overflowing i64.
constexpr i64 kExponentSaturation = 1000000000000000;

bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

char Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// True when [p, last) starts with `word` (lowercase), compared without case.
bool StartsWithNoCase(const char* p, const char* last, const char* word)
{
    for (; *word != '\0'; ++word, ++p)
    {
        if (p == last || Lower(*p) != *word)
        {
            return false;
        }
    }
    return true;
}

locale_t CLocale()
{
    static const locale_t locale = ::newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    return locale;
}

ParseDoubleResult Fail(const char* first)
{
    return {0.0, first, false};
}

}  // namespace

ParseDoubleResult ParseDoubleWithoutFromChars(const char* first, const char* last) noexcept
{
    const char* p = first;
    const bool negative = p != last && *p == '-';
    if (negative)
    {
        ++p;
    }
    const f64 sign = negative ? -1.0 : 1.0;

    // inf, infinity, nan, nan(chars)
    if (StartsWithNoCase(p, last, "inf"))
    {
        p += StartsWithNoCase(p, last, "infinity") ? static_cast<i64>(CStrLength("infinity"))
                                                   : static_cast<i64>(CStrLength("inf"));
        return {sign * Limits<f64>::Infinity(), p, true};
    }
    if (StartsWithNoCase(p, last, "nan"))
    {
        p += static_cast<i64>(CStrLength("nan"));
        if (p != last && *p == '(')
        {
            const char* q = p + 1;
            while (q != last && (IsDigit(*q) || (Lower(*q) >= 'a' && Lower(*q) <= 'z') || *q == '_'))
            {
                ++q;
            }
            if (q != last && *q == ')')
            {
                p = q + 1;
            }
        }
        return {negative ? -Limits<f64>::QuietNaN() : Limits<f64>::QuietNaN(), p, true};
    }

    // Digits, an optional point, more digits: at least one digit in all.
    const char* int_begin = p;
    while (p != last && IsDigit(*p))
    {
        ++p;
    }
    const char* int_end = p;
    const char* frac_begin = p;
    const char* frac_end = p;
    if (p != last && *p == '.')
    {
        frac_begin = p + 1;
        frac_end = frac_begin;
        while (frac_end != last && IsDigit(*frac_end))
        {
            ++frac_end;
        }
        if (int_end != int_begin || frac_end != frac_begin)
        {
            p = frac_end;
        }
    }
    if (int_end == int_begin && frac_end == frac_begin)
    {
        return Fail(first);
    }

    // An exponent counts only when it is complete: e or E, an optional sign, at least one digit.
    i64 exponent = 0;
    if (p != last && (*p == 'e' || *p == 'E'))
    {
        const char* q = p + 1;
        bool exponent_negative = false;
        if (q != last && (*q == '+' || *q == '-'))
        {
            exponent_negative = *q == '-';
            ++q;
        }
        if (q != last && IsDigit(*q))
        {
            while (q != last && IsDigit(*q))
            {
                if (exponent < kExponentSaturation)
                {
                    exponent = exponent * 10 + (*q - '0');
                }
                ++q;
            }
            if (exponent_negative)
            {
                exponent = -exponent;
            }
            p = q;
        }
    }
    const char* const end = p;

    // The value is 0.<significant digits> x 10^decimal_exponent. Leading zeros move the point; trailing zeros change
    // nothing.
    i64 point = int_end - int_begin;  // digits before the point
    char digits[kKeptDigits + 1];
    i64 kept = 0;
    bool started = false;
    bool sticky = false;
    i64 last_nonzero = 0;  // kept digits up to and including the last nonzero one
    for (int part = 0; part < 2; ++part)
    {
        const char* d = part == 0 ? int_begin : frac_begin;
        const char* d_end = part == 0 ? int_end : frac_end;
        for (; d != d_end; ++d)
        {
            if (!started)
            {
                if (*d == '0')
                {
                    --point;
                    continue;
                }
                started = true;
            }
            if (kept < kKeptDigits)
            {
                digits[kept++] = *d;
                if (*d != '0')
                {
                    last_nonzero = kept;
                }
            }
            else if (*d != '0')
            {
                sticky = true;
            }
        }
    }
    if (!started)
    {
        return {sign * 0.0, end, true};
    }
    if (sticky)
    {
        digits[kept++] = '1';  // after all kKeptDigits digits, zeros included: the dropped digits are below the last
                               // kept one
    }
    else
    {
        kept = last_nonzero;
    }

    i64 decimal_exponent = point + exponent;
    if (decimal_exponent > kExponentAbove)
    {
        return {0.0, end, false};
    }
    if (decimal_exponent < kExponentBelow)
    {
        return {0.0, end, false};
    }

    // "-0.<digits>e<exponent>": sign, "0.", the digits, 'e', a sign and at most 4 exponent digits, the terminator.
    char text[1 + 2 + (kKeptDigits + 1) + 1 + 1 + 4 + 1];
    i64 n = 0;
    if (negative)
    {
        text[n++] = '-';
    }
    text[n++] = '0';
    text[n++] = '.';
    for (i64 i = 0; i < kept; ++i)
    {
        text[n++] = digits[i];
    }
    text[n++] = 'e';
    if (decimal_exponent < 0)
    {
        text[n++] = '-';
        decimal_exponent = -decimal_exponent;
    }
    char exponent_digits[4];
    i64 exponent_count = 0;
    do
    {
        exponent_digits[exponent_count++] = static_cast<char>('0' + decimal_exponent % 10);
        decimal_exponent /= 10;
    } while (decimal_exponent != 0);
    while (exponent_count > 0)
    {
        text[n++] = exponent_digits[--exponent_count];
    }
    text[n] = '\0';

    const locale_t locale = CLocale();
    if (locale == static_cast<locale_t>(nullptr))
    {
        return Fail(first);
    }
    char* text_end = nullptr;
    const f64 value = ::strtod_l(text, &text_end, locale);
    if (text_end != text + n)
    {
        return Fail(first);
    }
    // Out of range: overflow to infinity, or a nonzero number that rounds to zero.
    if (value == Limits<f64>::Infinity() || value == -Limits<f64>::Infinity() || value == 0.0)
    {
        return {0.0, end, false};
    }
    return {value, end, true};
}

}  // namespace engine_placeholder::detail

#endif  // !ENGINE_PLATFORM_WINDOWS
