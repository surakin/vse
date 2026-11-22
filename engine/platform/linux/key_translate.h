#pragma once

#include "platform/input.h"
#include "types.h"

//! The Linux backends' key translation (X11 and XKB keysyms to
//! [[os::Key]], shared by the Wayland and X11 backends. Windows translates scan codes and its layout's characters
//! instead
//! (`platform/windows/win32_keys.h`).

namespace vse::os
{

/// Maps an X11/XKB keysym value to an engine [[os::Key]].
///
/// Upper- and lower-case letters map to the same key. Anything not in the enum, including non-Latin
/// letters, yields `Key::Unknown`.
/// - Param keysym: keysym value; X11 and XKB share the same numbering.
Key KeyFromKeysym(u32 keysym);

}  // namespace vse::os
