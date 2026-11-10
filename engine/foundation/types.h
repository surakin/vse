#pragma once

#include <cstddef>
#include <cstdint>

//! The engine's type vocabulary, used instead of the raw standard names so every layer agrees on exact widths.
//!
//! | Need | Type |
//! |---|---|
//! | a raw byte, or a buffer of them | `byte` (an alias of `u8`; a buffer is `Vector<byte>`, a view is `ByteSpan`) |
//! | 8, 16, 32, 64-bit integers | `i8`..`i64`, `u8`..`u64` (aliases of the `<cstdint>` types) |
//! | 128-bit integers | `i128`, `u128` (classes in `int128.h`: the same behavior on every compiler, with `__int128` inside where it exists) |
//! | an object size or an index | `usize`; the signed difference of two pointers is `isize` |
//! | floating point | `f32`, `f64` |
//! | truth | native `bool` |
//!
//! Code the project owns does not spell `unsigned char`, `uint8_t` and the like; the `std_usage_check` gate (Phase 16 Day 220, which replaced `std_container_check`) will reject them.
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

/// A raw byte: the unit of buffers, file contents and serialized data. The same type as [[u8]], so a byte buffer can be passed to any API that takes `unsigned char`.
using byte = u8;

/// Unsigned integer wide enough to hold any object size or array index (`std::size_t`).
using usize = std::size_t;
/// Signed integer wide enough to hold the difference of two pointers (`std::ptrdiff_t`).
using isize = std::ptrdiff_t;

/// 32-bit IEEE-754 floating point (`float`).
using f32 = float;
/// 64-bit IEEE-754 floating point (`double`).
using f64 = double;

static_assert(sizeof(i8) == 1 && sizeof(i16) == 2 && sizeof(i32) == 4 && sizeof(i64) == 8, "the signed widths");
static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8, "the unsigned widths");
static_assert(sizeof(byte) == 1 && sizeof(usize) == sizeof(isize), "byte and the size types");
static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "the float widths");
static_assert(static_cast<i8>(-1) < 0 && static_cast<i32>(0xFFFFFFFFu) == -1, "two's complement");

} // namespace engine_placeholder
