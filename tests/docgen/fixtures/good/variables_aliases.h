#pragma once

#include <cstddef>

/// A namespace-scope constant.
inline constexpr unsigned kMaxThings = 256;

/// A class with static members.
class Config {
public:
    /// A static constexpr member.
    static constexpr int kLimit = 8;
    /// A static data member.
    static int counter;
};

/// A using-alias.
using Size = std::size_t;

/// A typedef.
typedef unsigned long Handle;

using namespace std;
