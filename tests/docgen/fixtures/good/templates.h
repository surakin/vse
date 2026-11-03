#pragma once

#include <concepts>

/// A class template.
template <typename T, int N = 4>
class Buffer {
public:
    /// Returns the capacity.
    constexpr int Capacity() const { return N; }
    /// Stores a value.
    void Put(const T& value);
};

/// A function template with a requires clause.
template <typename T>
    requires std::integral<T>
T Twice(T value);

/// A variadic function template.
template <typename... Args>
void Log(const char* fmt, Args... args);

/// An alias template.
template <typename T>
using Ptr = T*;

/// A variable template.
template <typename T>
inline constexpr bool is_small = sizeof(T) <= 8;

/// A specialization.
template <>
class Buffer<char, 1> {
public:
    /// Returns the capacity.
    int Capacity() const;
};
