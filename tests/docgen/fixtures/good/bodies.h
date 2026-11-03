#pragma once

/// A class whose members have all kinds of bodies.
class Widget {
public:
    /// Inline getter.
    int Get() const { return value_; }

    /// A method with a lambda and nested braces in its body.
    int Run(int n) {
        auto f = [n](int x) { return x + n; };
        int arr[] = {1, 2, 3};
        struct Local {
            int a;
        } local{4};
        return f(arr[0]) + local.a;
    }

    /// A constructor with a member-initializer list using braces.
    explicit Widget(int v) : value_{v}, other_{v + 1} {}

private:
    int value_ = 0;
    int other_{0};
};

/// A function with a brace-initialized default argument.
void Configure(int options = int{3});
