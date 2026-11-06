#pragma once
// A header may mention ENGINE_VERIFY( and ENGINE_UNREACHABLE( in a comment, and use ENGINE_ASSERT and ENGINE_ENSURE.
inline int Half(int v) { ENGINE_ASSERT(v >= 0, "non-negative"); return v / 2; }
