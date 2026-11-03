#pragma once

/// A base class with every kind of member.
class Base {
public:
    /// Constructs a default base.
    Base();
    /// Destroys the base.
    virtual ~Base();
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;

    /// A pure virtual hook.
    virtual void Hook() = 0;
    /// A virtual method.
    virtual int Compute(int x) const;

    /// A nested public type.
    struct Nested {
        int value; ///< A data member documented on the same line.
    };

protected:
    /// Excluded from the reference because it is protected.
    void Helper();

private:
    int secret_ = 0;
};

/// A derived class.
class Derived final : public Base {
public:
    /// Implements the hook.
    void Hook() override;
    int Compute(int x) const override;
    /// Defaulted constructor.
    Derived() = default;
};

/// A struct: members are public by default.
struct Point {
    float x; ///< X coordinate.
    float y; ///< Y coordinate.
};

/// A union.
union Bits {
    int i;
    float f;
};
