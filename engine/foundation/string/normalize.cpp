#include "string/normalize.h"

// Hangul syllable arithmetic (UAX #15 / Unicode 3.12), used instead of a
// table lookup for the whole precomposed-syllable block — a shortcut here
// would silently produce wrong output for all Hangul text.
namespace engine_placeholder
{

namespace
{

constexpr u32 kHangulSBase = 0xAC00;
constexpr u32 kHangulLBase = 0x1100;
constexpr u32 kHangulVBase = 0x1161;
constexpr u32 kHangulTBase = 0x11A7;
constexpr u32 kHangulLCount = 19;
constexpr u32 kHangulVCount = 21;
constexpr u32 kHangulTCount = 28;
constexpr u32 kHangulNCount = kHangulVCount * kHangulTCount;  // 588
constexpr u32 kHangulSCount = kHangulLCount * kHangulNCount;  // 11172

bool IsHangulSyllable(u32 codepoint)
{
    return codepoint >= kHangulSBase && codepoint < kHangulSBase + kHangulSCount;
}

// Fills `out` with the syllable's L+V (2) or L+V+T (3) jamo, returns the count.
usize DecomposeHangulSyllable(u32 codepoint, u32 out[3])
{
    u32 s_index = codepoint - kHangulSBase;
    u32 l = kHangulLBase + s_index / kHangulNCount;
    u32 v = kHangulVBase + (s_index % kHangulNCount) / kHangulTCount;
    u32 t_index = s_index % kHangulTCount;
    out[0] = l;
    out[1] = v;
    if (t_index == 0)
    {
        return 2;
    }
    out[2] = kHangulTBase + t_index;
    return 3;
}

// L + V -> LV, or LV + T -> LVT. Returns false if `a`/`b` don't form a
// valid Hangul composition pair.
bool ComposeHangulPair(u32 a, u32 b, u32* out)
{
    bool a_is_l = a >= kHangulLBase && a < kHangulLBase + kHangulLCount;
    bool b_is_v = b >= kHangulVBase && b < kHangulVBase + kHangulVCount;
    if (a_is_l && b_is_v)
    {
        u32 l_index = a - kHangulLBase;
        u32 v_index = b - kHangulVBase;
        *out = kHangulSBase + (l_index * kHangulVCount + v_index) * kHangulTCount;
        return true;
    }
    bool a_is_lv = IsHangulSyllable(a) && (a - kHangulSBase) % kHangulTCount == 0;
    bool b_is_t = b > kHangulTBase && b < kHangulTBase + kHangulTCount;
    if (a_is_lv && b_is_t)
    {
        *out = a + (b - kHangulTBase);
        return true;
    }
    return false;
}

bool TryComposePair(u32 a, u32 b, u32* out)
{
    if (ComposeHangulPair(a, b, out))
    {
        return true;
    }
    return LookupCanonicalComposition(a, b, out);
}

}  // namespace

void Detail_FullyDecomposeAppend(u32 codepoint, Vector<u32, ArenaAllocator>& out)
{
    if (IsHangulSyllable(codepoint))
    {
        u32 jamo[3];
        usize count = DecomposeHangulSyllable(codepoint, jamo);
        // Jamo are dead ends (no further canonical decomposition).
        for (usize i = 0; i < count; ++i)
        {
            out.Push(jamo[i]);
        }
        return;
    }

    u32 decomposed[2];
    u8 length = 0;
    if (LookupCanonicalDecomposition(codepoint, decomposed, &length))
    {
        // Some decompositions are multi-step; decompose the results too.
        for (u8 i = 0; i < length; ++i)
        {
            Detail_FullyDecomposeAppend(decomposed[i], out);
        }
        return;
    }

    out.Push(codepoint);
}

void Detail_CanonicalOrder(Vector<u32, ArenaAllocator>& codepoints)
{
    usize n = codepoints.Size();
    usize i = 0;
    while (i < n)
    {
        if (CombiningClass(codepoints[i]) == 0)
        {
            ++i;
            continue;
        }
        usize run_start = i;
        while (i < n && CombiningClass(codepoints[i]) != 0)
        {
            ++i;
        }
        usize run_end = i;  // [run_start, run_end) is a maximal non-starter run.

        // Stable insertion sort by combining class — runs are tiny (a
        // handful of combining marks at most).
        for (usize a = run_start + 1; a < run_end; ++a)
        {
            u32 value = codepoints[a];
            u8 value_class = CombiningClass(value);
            usize b = a;
            while (b > run_start && CombiningClass(codepoints[b - 1]) > value_class)
            {
                codepoints[b] = codepoints[b - 1];
                --b;
            }
            codepoints[b] = value;
        }
    }
}

void Detail_ComposeInPlace(Vector<u32, ArenaAllocator>& codepoints)
{
    usize n = codepoints.Size();
    if (n == 0)
    {
        return;
    }

    usize starter_index = 0;
    u32 starter = codepoints[0];
    u8 last_class = CombiningClass(starter);
    usize write = 1;

    for (usize read = 1; read < n; ++read)
    {
        u32 ch = codepoints[read];
        u8 ch_class = CombiningClass(ch);

        // Composition is blocked if some intervening, non-composed
        // character had combining class 0 or >= this one (UAX #15's
        // blocking rule). `last_class` tracks that running maximum since
        // the current starter; 0 doubles as "nothing intervened yet".
        u32 composed = 0;
        bool composed_here = false;
        if ((last_class == 0 || last_class < ch_class) && TryComposePair(starter, ch, &composed) &&
            !IsCompositionExcluded(composed))
        {
            starter = composed;
            codepoints[starter_index] = composed;
            composed_here = true;
        }

        if (!composed_here)
        {
            codepoints[write] = ch;
            if (ch_class == 0)
            {
                starter = ch;
                starter_index = write;
            }
            last_class = ch_class;
            ++write;
        }
    }

    while (codepoints.Size() > write)
    {
        codepoints.Pop();
    }
}

}  // namespace engine_placeholder
