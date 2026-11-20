#pragma once

namespace vse {
namespace demo {

/// A base class with a documented virtual.
class Shape {
public:
    /// Returns the area.
    /// - Returns: the area in square units.
    virtual double Area() const;
    /// Draws the shape.
    virtual void Draw();
};

/// A circle; its overrides inherit the base summaries.
class Circle : public Shape {
public:
    double Area() const override;
    /// Draws the circle with its own words.
    void Draw() override;
};

/// An internal-only helper.
/// - Internal: exposed so tests can reach it.
void HelperForTests();

} // namespace demo
} // namespace vse
