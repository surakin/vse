#pragma once
//! The test extension's table (`engine.test_state`), built only when `ENGINE_C_TEST_EXTENSION` is ON. Internal to the
//! module `engine_c`.
#include "engine_c/engine_test_extension.h"

namespace vse::engine_c
{

/// Returns the static table of the boundary tests' operations and singleton addresses.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
const engine_test_extension* TestStateExtension();

}  // namespace vse::engine_c
