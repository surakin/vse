#pragma once

// Byte buffers for the renderer tests: a vector of bytes read back from a buffer or texture, and the comparison of two
// of them.

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "types.h"

namespace renderer_test
{

using ByteVector = vse::Vector<vse::u8>;

/// True when `a` and `b` have the same length and the same bytes.
inline bool SameBytes(const ByteVector& a, const ByteVector& b)
{
    return a.Size() == b.Size() && (a.Size() == 0 || vse::MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

}  // namespace renderer_test
