#pragma once

/// A plain enum.
enum Color { Red, Green = 5, Blue };

/// A scoped enum with an underlying type.
enum class Mode : unsigned char {
    Off,       ///< Disabled.
    On = 1,    ///< Enabled.
    Auto = 0x10 ///< Chosen automatically.
};

/// An enum class without an underlying type.
enum class Flag { A, B };
