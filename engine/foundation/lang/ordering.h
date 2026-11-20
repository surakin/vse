#pragma once

#include <compare>

//! Three-way comparison results (Phase 16): the engine's spelling of `<compare>`. A class that defines `operator<=>`
//! returns `Ordering`.
namespace vse
{

/// The result of a three-way comparison of totally ordered values (`std::strong_ordering`): `Ordering::less`, `equal`,
/// `greater`.
using Ordering = std::strong_ordering;

}  // namespace vse
