#pragma once

//! A header with a file overview.
//!
//! The overview may have several paragraphs.

/// A base with a documented virtual.
class Shape {
public:
    /// Returns the area.
    virtual double Area() const;
};

/// A derived shape; its override inherits the base summary.
class Circle : public Shape {
public:
    double Area() const override;
};
