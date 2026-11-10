#pragma once

//! A compile-time list of indices, to expand two parameter packs of equal length side by side.

#include "types.h"

namespace engine_placeholder::ecs::detail {

/// The indices `I...`.
template <usize... I>
struct IndexList {};

/// `MakeIndexList<N>::Type` is `IndexList<0, 1, ..., N - 1>`.
template <usize N, usize... I>
struct MakeIndexList : MakeIndexList<N - 1, N - 1, I...> {};
template <usize... I>
struct MakeIndexList<0, I...> {
    using Type = IndexList<I...>;
};

} // namespace engine_placeholder::ecs::detail
