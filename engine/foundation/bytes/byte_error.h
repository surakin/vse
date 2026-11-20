#pragma once

//! The result of a read from a [[bytes::ByteReader]].

#include "types.h"

namespace vse::bytes
{

/// Why a read failed. A failed read leaves the reader where it was.
enum class [[nodiscard]] ByteError : u8
{
    Ok,         ///< The read succeeded.
    Truncated,  ///< The input ends before the value does.
    TooLong,    ///< A length or count is above the limit the caller gave.
    Invalid,    ///< The bytes are not a valid encoding: a bool other than 0 or 1, a varint that is not in its shortest
                ///< form or does not fit 64 bits.
};

/// Returns a stable lower-case name for `error` (`"truncated"`); never null.
///
/// - Ownership: the string is static.
/// - Fails: never.
inline const char* ByteErrorName(ByteError error)
{
    switch (error)
    {
    case ByteError::Ok: return "ok";
    case ByteError::Truncated: return "truncated";
    case ByteError::TooLong: return "too long";
    case ByteError::Invalid: return "invalid";
    }
    return "invalid";
}

}  // namespace vse::bytes
