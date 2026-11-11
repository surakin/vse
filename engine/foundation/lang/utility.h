#pragma once

#include <type_traits>
#include <utility>

//! Language-support utilities (Phase 16): the engine's spelling of `<utility>`. This header and the other `lang/` headers are the only
//! files that include the standard library's language-support headers.
namespace engine_placeholder {

/// Casts `value` to an rvalue reference (`std::move`).
///
/// - Thread-safety: thread-safe. Pure; no state.
template <class T>
constexpr decltype(auto) Move(T&& value) noexcept {
    return std::move(value);
}

/// Forwards `value` with the value category it was passed with (`std::forward`).
///
/// - Thread-safety: thread-safe. Pure; no state.
template <class T, class U>
constexpr T&& Forward(U&& value) noexcept {
    return static_cast<T&&>(value);
}

/// Replaces `target` with `value` and returns the old value (`std::exchange`).
///
/// - Thread-safety: thread-compatible. Not safe on the same `target` from two threads.
template <class T, class U = T>
constexpr T Exchange(T& target, U&& value) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_assignable_v<T&, U>) {
    T old = std::move(target);
    target = std::forward<U>(value);
    return old;
}

/// Swaps two objects (`std::swap`).
///
/// - Thread-safety: thread-compatible. Not safe on the same objects from two threads.
template <class T>
constexpr void Swap(T& a, T& b) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
    T tmp = std::move(a);
    a = std::move(b);
    b = std::move(tmp);
}

/// Returns `value` as a const reference (`std::as_const`).
template <class T>
constexpr const T& AsConst(T& value) noexcept {
    return value;
}

/// Names a value of type `T` in an unevaluated context only (`std::declval`); it is never defined.
template <class T>
T&& Declval() noexcept;

} // namespace engine_placeholder
