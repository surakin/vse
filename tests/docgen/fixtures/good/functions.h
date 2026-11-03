#pragma once

/// A free function.
int Add(int a, int b);

/// A function with attributes and noexcept.
[[nodiscard]] bool TryThing(const char* name) noexcept;

/// A function with a trailing return type.
auto Trailing(int x) -> long;

/// A constexpr function with a default argument.
constexpr int Square(int x, int scale = 1) { return x * x * scale; }

/// An inline function with a body containing braces.
inline int WithBody(int x) {
    if (x > 0) {
        return x;
    }
    return -x;
}

/// A class with operators and conversions.
class Money {
public:
    /// Adds two amounts.
    Money operator+(const Money& other) const;
    /// Compares two amounts.
    bool operator==(const Money& other) const;
    /// Converts to a double.
    explicit operator double() const;
    /// Subscript.
    int& operator[](int index);
    /// Function call.
    int operator()(int a) const;
};
