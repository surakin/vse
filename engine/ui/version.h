#pragma once

#include "types.h"

namespace vse::ui
{

/// Version of the layout algorithm's observable behavior; bumped when a change would move a widget in an existing
/// layout (a golden or saved layout then needs a new look).
///
/// - Thread-safety: thread-safe; a constant.
constexpr u32 kUiLayoutVersion = 1;

/// Returns [[ui::kUiLayoutVersion]]; a function so the library has a symbol to link against until the toolkit's first
/// sources land.
///
/// - Thread-safety: thread-safe.
u32 LayoutVersion();

}  // namespace vse::ui
