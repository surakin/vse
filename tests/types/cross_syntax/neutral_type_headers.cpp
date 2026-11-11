// The type vocabulary headers, included in one translation unit for the cross-target syntax check (Phase 10 Day 126).
// Nothing here may depend on a compiler extension: on the Windows target there is no __int128, so int128.h must take
// its portable path by itself.
#include "containers/span.h"
#include "int128.h"
#include "types.h"

namespace engine_placeholder
{
static_assert(sizeof(u128) == 16, "u128 is 16 bytes on every target");
inline u128 Probe(u128 a, u128 b)
{
    return (a * b + (a / (b + u128(1u)))) >> 3;
}
inline i128 ProbeSigned(i128 a, i128 b)
{
    return (a * b) / (b + i128(1));
}
inline ByteSpan ProbeSpan(const Vector<byte>& v)
{
    return ByteSpan(v).Subspan(0, v.Size());
}
}  // namespace engine_placeholder
