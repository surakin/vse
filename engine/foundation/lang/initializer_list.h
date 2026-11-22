#pragma once

#include <initializer_list>

//! Brace-initializer lists: the engine's spelling of `<initializer_list>`. The compiler requires the
//! standard type for
//! `{a, b, c}` to convert to a parameter, so `InitializerList<T>` is an alias of it, not a distinct type.
namespace vse
{

/// A view of the elements of a brace-enclosed list (`std::initializer_list<T>`); begin/end/size as the standard type.
template <class T> using InitializerList = std::initializer_list<T>;

}  // namespace vse
