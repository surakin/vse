#pragma once

#include <cstddef>
#include <cstdint>

//! Fixed-width type aliases used throughout the engine instead of the raw
//! standard names, so every layer agrees on exact widths. Native `bool` is
//! used as-is.
namespace engine_placeholder {

/// Signed 8-bit integer.
using i8 = std::int8_t;
/// Signed 16-bit integer.
using i16 = std::int16_t;
/// Signed 32-bit integer.
using i32 = std::int32_t;
/// Signed 64-bit integer.
using i64 = std::int64_t;

/// Unsigned 8-bit integer.
using u8 = std::uint8_t;
/// Unsigned 16-bit integer.
using u16 = std::uint16_t;
/// Unsigned 32-bit integer.
using u32 = std::uint32_t;
/// Unsigned 64-bit integer.
using u64 = std::uint64_t;

/// Unsigned integer wide enough to hold any object size or array index (`std::size_t`).
using usize = std::size_t;

/// 32-bit IEEE-754 floating point (`float`).
using f32 = float;
/// 64-bit IEEE-754 floating point (`double`).
using f64 = double;

} // namespace engine_placeholder
